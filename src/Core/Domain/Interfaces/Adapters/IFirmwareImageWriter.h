#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief The flash side of an OTA session: where the downloaded image bytes go.
 *
 * Abstracts the Arduino Update singleton the way IHttpClientAdapter abstracts HTTPClient, and
 * for the same reason: the download session (FirmwareRemoteUpdate) owns the loop that moves
 * bytes from the socket to the flash, and that loop has behaviour worth proving on the host -
 * budgets, abort paths, watchdog feeding. The flash itself is hardware and is substituted;
 * the loop is logic and is not.
 */
class IFirmwareImageWriter {
public:
    virtual ~IFirmwareImageWriter() = default;

    /**
     * @brief Open the inactive app slot for an image of the given size
     *
     * @param imageSize Expected image size in bytes
     * @return true if the slot can take the image, false otherwise (e.g. not enough space)
     */
    virtual bool begin(size_t imageSize) = 0;

    /**
     * @brief Arm the checksum the finished image must have
     *
     * Must be called after begin(), which clears any previous expectation; the digest itself is
     * taken over everything write() accepts since begin(). Callers arm it before the first
     * write, so that no part of a session ever runs with the expectation missing. With an
     * expectation armed, end() only accepts an image whose digest matches it.
     *
     * @param md5 Expected MD5 digest, exactly 32 lowercase hex characters
     * @return true if the expectation was accepted, false if the string is not a digest the
     *         implementation can use
     */
    virtual bool setExpectedMd5(const char* md5) = 0;

    /**
     * @brief Append a chunk of image bytes to the slot
     *
     * @param data Chunk bytes
     * @param len Chunk length in bytes
     * @return Number of bytes actually written; anything short of len means the write failed
     */
    virtual size_t write(const uint8_t* data, size_t len) = 0;

    /**
     * @brief Finalize the image: all bytes are in, verify and mark the slot bootable
     *
     * @return true if the slot now holds a complete, accepted image
     */
    virtual bool end() = 0;

    /**
     * @brief Discard a partial image and release the slot
     */
    virtual void abort() = 0;

    /**
     * @brief Whether a session opened by begin() is still in progress
     */
    virtual bool isRunning() = 0;

    /**
     * @brief The implementation's error code for the last failure, for log lines
     */
    virtual int lastError() = 0;

    /**
     * @brief How many bytes the inactive slot can take, for the pre-download log line
     */
    virtual uint32_t freeImageSpace() = 0;
};
