#pragma once

#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsAdapter.h"
#include "Infrastructure/Logging/MqttLogger.h"
#include "Infrastructure/Logging/OtaProgressThrottle.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include <cstdio>
#include <string>

#define OTA_LOG_FILE "/ota.log"
#define OTA_LOG_SIZE 20000
#define OTA_PROGRESS_PCT_STEP 10
#define OTA_PROGRESS_MS_STEP 3000
#define OTA_MAX_UPLOAD_LINES 1000
#define OTA_LOG_TOPIC "ota"

class OtaUpdateLog : public IOtaUpdateLog {
private:
    ISpiffsAdapter* _spiffs;
    MqttLogger* _mqttLogger;
    INTPTimeAdapter* _timeAdapter;
    IArduinoAdapter* _arduino;
    IWifiAdapter* _wifi;
    OtaProgressThrottle _throttle;

    unsigned long nowMs() {
        return _arduino ? _arduino->millis() : 0;
    }

    void serialDebug(const char* line) {
        if (_arduino == nullptr) {
            return;
        }
        char dbg[360];
        snprintf(dbg, sizeof(dbg), "OTA_LOG: %s", line);
        _arduino->serialPrintln(dbg);
    }

    void appendLine(const char* msg) {
        if (_spiffs == nullptr) {
            return;
        }

        if (_spiffs->getFileSize(OTA_LOG_FILE) > OTA_LOG_SIZE) {
            _spiffs->rewriteToFile(OTA_LOG_FILE, "");
        }

        const char* timeStr = _timeAdapter ? _timeAdapter->getFormattedTime() : "";
        char line[320];
        snprintf(line, sizeof(line), "%s up=%lu heap=%u rssi=%d %s",
                 timeStr, nowMs(),
                 (unsigned)(_arduino ? _arduino->getFreeHeap() : 0),
                 (_wifi ? _wifi->getRSSI() : 0), msg);

        _spiffs->appendToFile(OTA_LOG_FILE, line);
        serialDebug(line);
    }

public:
    OtaUpdateLog(ISpiffsAdapter* spiffs = nullptr, MqttLogger* mqttLogger = nullptr,
                 INTPTimeAdapter* timeAdapter = nullptr, IArduinoAdapter* arduino = nullptr,
                 IWifiAdapter* wifi = nullptr)
        : _spiffs(spiffs), _mqttLogger(mqttLogger), _timeAdapter(timeAdapter),
          _arduino(arduino), _wifi(wifi) {}

    void startSession(const char* url) override {
        if (_spiffs == nullptr) {
            return;
        }

        char header[320];
        snprintf(header, sizeof(header), "===== OTA SESSION mac=%s url=%s =====",
                 _wifi ? _wifi->getMACAddress() : "", url ? url : "");
        // Appended, not rewritten. Since GDV1-3834 a failed attempt is followed by a restart
        // within a second, while the log reaches MQTT once every 5 s and only from
        // CONFIGURE_DONE - so rewriting here threw away the session that had just failed,
        // which is the one worth reading. BootEventLog keeps its lines across boots the same
        // way; the file is still emptied by the flush and by the OTA_LOG_SIZE ceiling.
        appendLine(header);

        _throttle.reset();
        write("OTA REQUESTED");
    }

    void write(const char* msg) override {
        appendLine(msg);
    }

    void writeProgress(size_t cur, size_t total) override {
        int pct = 0;
        if (_throttle.shouldEmit(cur, total, nowMs(), OTA_PROGRESS_PCT_STEP, OTA_PROGRESS_MS_STEP, pct)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "DL %d%% %u/%u", pct, (unsigned)cur, (unsigned)total);
            write(msg);
        }
    }

    bool hasPending() override {
        if (_spiffs == nullptr) {
            return false;
        }
        return _spiffs->getFileSize(OTA_LOG_FILE) > 0;
    }

    bool flushToMqttIfAny() override {
        if (_spiffs == nullptr || _mqttLogger == nullptr) {
            return false;
        }
        if (!_mqttLogger->isConnected()) {
            return false;
        }

        std::string content = _spiffs->readSPIFFSstr(OTA_LOG_FILE);
        if (content.empty()) {
            return true;
        }

        size_t start = 0;
        int published = 0;
        while (start < content.size() && published < OTA_MAX_UPLOAD_LINES) {
            size_t end = content.find('\n', start);
            std::string line = (end == std::string::npos)
                ? content.substr(start)
                : content.substr(start, end - start);

            if (!line.empty()) {
                if (!_mqttLogger->publishToTopic(OTA_LOG_TOPIC, line.c_str())) {
                    return false;
                }
                published++;
            }

            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }

        _spiffs->rewriteToFile(OTA_LOG_FILE, "");
        return true;
    }
};
