#pragma once

#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IFirmwareImageWriter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"

#include <cstdarg>
#include <cstdio>
#include <cstdint>

// Firmware update status constants
#define FIRMWARE_UPDATE_STATE_DONE 3
#define FIRMWARE_UPDATE_STATE_ERROR 4
#define FIRMWARE_UPDATE_STATE_HTTP_ERROR 5

// HTTP budget of the OTA download, ms - both for opening the connection and for waiting for
// data. Same 30 s every other HTTP call of the firmware uses (RestApiService.h); left alone,
// HTTPClient keeps its own default of 5 s on both (HTTPCLIENT_DEFAULT_TCP_TIMEOUT), which is
// less than a loaded link needs either to finish the TLS handshake (the session then dies
// with -1) or to deliver the first bytes of a 1.5 MB binary (-11), in both cases with the
// server answering normally.
#define FIRMWARE_UPDATE_HTTP_TIMEOUT_MS 30000

// The whole download - opening the connection and streaming the image - must finish inside this
// budget, ms. Sized from the worst legitimate case: the image is ~1.6 MB (~13 Mbit; the current
// build is 1.57 MB), and on a slow but alive link of 32 kbit/s it takes ~7 minutes - doubling
// that for the TLS handshake, HTTP overhead and retransmissions gives 15. A transfer that cannot
// finish in 15 minutes is not a slow network, it is a dead one; without this ceiling a stream
// that froze with the socket open was polled forever, and the command network task - with every
// command queued behind the OTA - was hostage to the peer coming back. The budget is checked
// once per pass of the stream loop, so a session overruns it by at most one HTTP timeout.
#define FIRMWARE_DOWNLOAD_TOTAL_TIMEOUT_MS 900000

// One read of the download stream, bytes. Matches the flash sector the image is written in
// (SPI_FLASH_SEC_SIZE), so a full chunk is one erase-and-write; the buffer is a member of the
// session object, not the downloading task's stack - that task runs the TLS handshake on 8 KB.
#define FIRMWARE_DOWNLOAD_CHUNK_BYTES 4096

// How long to sleep when the socket has nothing to read, ms. Short enough that a live stream
// is drained at line rate, long enough that a stalled one does not busy-spin the task.
#define FIRMWARE_DOWNLOAD_STALL_POLL_MS 100

// Length of the image checksum the update command may carry: an MD5 digest as hex characters.
#define FIRMWARE_IMAGE_MD5_CHARS 32

class FirmwareRemoteUpdate : public IFirmwareRemoteUpdate {
private:
    IHttpClientAdapter* _httpClient;
    IFirmwareImageWriter* _imageWriter;
    IArduinoAdapter* _arduino;
    IWifiAdapter* _wifi;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IOtaUpdateLog* _otaLog;

    char _lastErrorMessage[128];
    uint8_t _lastUpdateStatus;
    int _lastHttpCode = 0;
    // The digest the finished image must have, 32 lowercase hex characters, or empty when the
    // command did not carry one.
    char _expectedMd5[FIRMWARE_IMAGE_MD5_CHARS + 1];
    uint8_t _chunk[FIRMWARE_DOWNLOAD_CHUNK_BYTES];

    /**
     * @brief Copy a checksum from the command into `out`, lowercased, if it is an MD5 digest.
     *
     * The update mechanism compares the digest it computed over the written bytes - lowercase
     * hex - with this string character by character, so a digest that arrived in upper case
     * would reject a perfectly good image. Anything that is not exactly 32 hex characters is
     * not a digest at all and is refused by the caller: falling back to an unverified install
     * would quietly reinstate the very path the checksum is there to close.
     *
     * @param raw The value as it came in the command
     * @param out Buffer of FIRMWARE_IMAGE_MD5_CHARS + 1 bytes
     * @return true if `raw` was a digest and `out` holds it, false otherwise
     */
    static bool normalizeMd5(const char* raw, char* out) {
        size_t length = 0;
        while (raw[length] != '\0') {
            if (length == FIRMWARE_IMAGE_MD5_CHARS) {
                return false;
            }
            const char c = raw[length];
            if (c >= '0' && c <= '9') {
                out[length] = c;
            } else if (c >= 'a' && c <= 'f') {
                out[length] = c;
            } else if (c >= 'A' && c <= 'F') {
                out[length] = (char)(c - 'A' + 'a');
            } else {
                return false;
            }
            length++;
        }

        if (length != FIRMWARE_IMAGE_MD5_CHARS) {
            return false;
        }
        out[FIRMWARE_IMAGE_MD5_CHARS] = '\0';
        return true;
    }

    void otaWrite(const char* format, ...) {
        if (_otaLog == nullptr) {
            return;
        }
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _otaLog->write(buffer);
    }

    // Single place where a failure of the session is recorded: the reason is kept for the
    // caller (it ends up in the command result the backend stores) and written to the OTA
    // log as the `ERROR: <reason>` line it has always been.
    void otaError(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vsnprintf(_lastErrorMessage, sizeof(_lastErrorMessage), format, args);
        va_end(args);

        otaWrite("ERROR: %s", _lastErrorMessage);
    }

    void log(const char* format, ...) {
        if (_logger != nullptr && _debugSystem != nullptr &&
            _debugSystem->isFeatureEnabled(DebugFeatures::UPDATE_FIRMWARE_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::UPDATE_FIRMWARE_DEBUG, buffer);
        }
    }

    // Recorded, not persisted: this method runs on the command network task, and the settings
    // sector (EEPROM.write + commit) is written from the control loop. The caller reads
    // getLastUpdateStatus() and persists it in its finish step, on the loop - the settings keep
    // a single writer.
    void putUpdateStatus(uint8_t status) {
        _lastUpdateStatus = status;
    }

    // Every failure of an open session leaves through here, so no path can forget to release
    // the half-written slot or the connection, and every one records the same three things:
    // the reason, the persisted-status value, and the command result.
    bool failSession(uint8_t status, const char* reason) {
        log("FIRMWARE_UPD: %s", reason);
        _imageWriter->abort();
        _httpClient->end();
        putUpdateStatus(status);
        setFirmwareUpdateCommandResultStatus("Error", reason);
        return false;
    }

    void setFirmwareUpdateCommandResultStatus(const char* status, const char* message) {
        log("FIRMWARE_UPD: Status: %s, Message: %s", status, message);
        // TODO: Implement command result status setting if needed
    }

    /**
     * @brief Move the image from the socket to the flash slot, chunk by chunk.
     *
     * Replaces Update.writeStream(), which owns its caller until the transfer ends however long
     * that takes. Here every wait is explicit: a chunk is read only when the socket has bytes,
     * a dry socket is polled, a closed one ends the session with a reason.
     *
     * The watchdog is fed once per chunk written - progress vouches for this task's liveness
     * as long as the stream moves, and a stalled stream stops vouching by itself.
     *
     * @param sessionStartMs When the whole session started, before the connection was opened:
     *        the download budget is one ceiling over connect and stream together.
     */
    bool streamImageToFlash(int contentLength, unsigned long sessionStartMs) {
        size_t written = 0;

        while (written < (size_t)contentLength) {
            if (_arduino->millis() - sessionStartMs >= FIRMWARE_DOWNLOAD_TOTAL_TIMEOUT_MS) {
                otaError("download timed out after %u ms: %u/%d bytes",
                         (unsigned)FIRMWARE_DOWNLOAD_TOTAL_TIMEOUT_MS,
                         (unsigned)written, contentLength);
                return failSession(FIRMWARE_UPDATE_STATE_ERROR, _lastErrorMessage);
            }

            int availableNow = _httpClient->available();

            if (availableNow <= 0) {
                if (!_httpClient->connected()) {
                    otaError("connection lost at %u/%d bytes", (unsigned)written, contentLength);
                    return failSession(FIRMWARE_UPDATE_STATE_ERROR, _lastErrorMessage);
                }
                _arduino->delay(FIRMWARE_DOWNLOAD_STALL_POLL_MS);
                continue;
            }

            size_t toRead = (size_t)availableNow;
            if (toRead > sizeof(_chunk)) {
                toRead = sizeof(_chunk);
            }
            if (toRead > (size_t)contentLength - written) {
                toRead = (size_t)contentLength - written;
            }

            size_t got = _httpClient->readBytes(_chunk, toRead);
            if (got == 0) {
                _arduino->delay(FIRMWARE_DOWNLOAD_STALL_POLL_MS);
                continue;
            }

            size_t wrote = _imageWriter->write(_chunk, got);
            if (wrote != got) {
                otaError("flash write failed at %u/%d bytes err=%d",
                         (unsigned)(written + wrote), contentLength, _imageWriter->lastError());
                return failSession(FIRMWARE_UPDATE_STATE_ERROR, _lastErrorMessage);
            }

            written += wrote;
            _arduino->feedTaskWatchdog();
            if (_otaLog) {
                _otaLog->writeProgress(written, (size_t)contentLength);
            }
        }

        return true;
    }

public:
    FirmwareRemoteUpdate(IHttpClientAdapter* httpClient,
                         IFirmwareImageWriter* imageWriter,
                         IArduinoAdapter* arduino,
                         IWifiAdapter* wifi,
                         ILogger* logger = nullptr,
                         IDebugSystem* debugSystem = nullptr,
                         IOtaUpdateLog* otaLog = nullptr)
        : _httpClient(httpClient),
          _imageWriter(imageWriter),
          _arduino(arduino),
          _wifi(wifi),
          _logger(logger),
          _debugSystem(debugSystem),
          _otaLog(otaLog) {
        _lastErrorMessage[0] = '\0';
        _lastUpdateStatus = 0;
        _expectedMd5[0] = '\0';
    }

    bool updateFirmwareFromURL(const char* url, const char* expectedMd5) override {
        _lastErrorMessage[0] = '\0';
        _lastUpdateStatus = 0;
        _lastHttpCode = 0;
        _expectedMd5[0] = '\0';

        if (_otaLog) {
            _otaLog->startSession(url);
        }
        otaWrite("DOWNLOAD START");

        // The checksum is optional: a command without one installs the image exactly as it
        // always did. A command with one that is not a digest is refused here, before a byte is
        // downloaded - there is no third mode in which a checksum is present and ignored.
        if (expectedMd5 != nullptr && expectedMd5[0] != '\0' &&
            !normalizeMd5(expectedMd5, _expectedMd5)) {
            _expectedMd5[0] = '\0';
            log("FIRMWARE_UPD: image checksum is not an MD5 digest");
            otaError("image checksum is not an MD5 digest: %.40s", expectedMd5);
            return false;
        }

        if (_wifi == nullptr || !_wifi->isConnected()) {
            log("FIRMWARE_UPD: WiFi not connected");
            otaError("WiFi not connected at download time");
            return false;
        }

        log("UPDATE_FIRMWARE_DEBUG: url=%s", url);

        // The download budget starts here, before the connection is opened: connect time and
        // stream time are one operation to the caller waiting on the cmd-net queue.
        const unsigned long sessionStartMs = _arduino->millis();

        // Must be set before begin(): the client keeps the values until it is told otherwise,
        // and a fresh client starts from the library default of 5 s on both budgets. Both are
        // needed - opening the connection and waiting for data fail separately, with -1 and
        // -11 respectively, and this module has been seen to lose sessions to each of them.
        _httpClient->setConnectTimeout(FIRMWARE_UPDATE_HTTP_TIMEOUT_MS);
        _httpClient->setTimeout(FIRMWARE_UPDATE_HTTP_TIMEOUT_MS);
        otaWrite("HTTP timeout=%u ms connect=%u ms",
                 (unsigned)FIRMWARE_UPDATE_HTTP_TIMEOUT_MS,
                 (unsigned)FIRMWARE_UPDATE_HTTP_TIMEOUT_MS);

        bool isBegin = _httpClient->begin(url);
        log("UPDATE_FIRMWARE_DEBUG: isBegin=%s", isBegin ? "true" : "false");

        if (!isBegin) {
            log("FIRMWARE_UPD: Failed to begin HTTP connection");
            otaError("HTTP begin failed");
            return false;
        }

        int httpCode = _httpClient->GET();
        _lastHttpCode = httpCode;
        log("UPDATE_FIRMWARE_DEBUG: updateFirmwareFromURL get firmware httpCode=%d", httpCode);
        otaWrite("HTTP begin=%d GET code=%d", isBegin ? 1 : 0, httpCode);

        if (httpCode != 200) {
            char reason[64];
            snprintf(reason, sizeof(reason), "HTTP error: %d", httpCode);
            log("FIRMWARE_UPD: %s", reason);
            otaError("HTTP code=%d", httpCode);

            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_HTTP_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", reason);
            return false;
        }

        int contentLength = _httpClient->getSize();
        log("FIRMWARE_UPD: Content length: %d bytes", contentLength);
        otaWrite("contentLength=%d freeSketch=%u", contentLength,
                 (unsigned)_imageWriter->freeImageSpace());

        if (contentLength <= 0) {
            otaError("invalid contentLength=%d", contentLength);
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", "Invalid content length");
            return false;
        }

        bool canBegin = _imageWriter->begin((size_t)contentLength);
        otaWrite("Update.begin canBegin=%d", canBegin ? 1 : 0);
        if (!canBegin) {
            otaError("not enough space, contentLength=%d", contentLength);
            _httpClient->end();
            putUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);
            setFirmwareUpdateCommandResultStatus("Error", "Not enough space to begin OTA");
            return false;
        }

        // After begin(), which clears any previous expectation, and before the first byte is
        // written: nothing in the session then runs on a slot that is not yet verifying.
        if (_expectedMd5[0] != '\0') {
            if (!_imageWriter->setExpectedMd5(_expectedMd5)) {
                otaError("image writer refused checksum md5=%s", _expectedMd5);
                return failSession(FIRMWARE_UPDATE_STATE_ERROR, _lastErrorMessage);
            }
            otaWrite("CHECKSUM md5=%s", _expectedMd5);
        }

        if (!streamImageToFlash(contentLength, sessionStartMs)) {
            return false;
        }

        if (!_imageWriter->end()) {
            const int endError = _imageWriter->lastError();
            if (_expectedMd5[0] != '\0') {
                // With a checksum armed the finalize step is also the verification step: the
                // image stays in the inactive slot, unbootable, and the running firmware carries
                // on. The writer's code says which of the two refused it.
                otaError("image rejected: checksum md5=%s not confirmed, err=%d",
                         _expectedMd5, endError);
            } else {
                otaError("Update.end failed err=%d", endError);
            }
            return failSession(FIRMWARE_UPDATE_STATE_ERROR, _lastErrorMessage);
        }

        log("FIRMWARE_UPD: Update successfully completed. Rebooting...");
        otaWrite("UPDATE OK -> reboot to new firmware");
        _httpClient->end();
        putUpdateStatus(FIRMWARE_UPDATE_STATE_DONE);

        return true;
    }

    bool isUpdateInProgress() override {
        return _imageWriter != nullptr && _imageWriter->isRunning();
    }

    const char* getLastErrorMessage() const override {
        return _lastErrorMessage;
    }

    uint8_t getLastUpdateStatus() const override {
        return _lastUpdateStatus;
    }

    int getLastHttpCode() const override {
        return _lastHttpCode;
    }
};
