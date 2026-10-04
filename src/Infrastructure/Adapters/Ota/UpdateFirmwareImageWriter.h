#pragma once

#include "Core/Domain/Interfaces/Adapters/IFirmwareImageWriter.h"

#include <Update.h>
#include <Esp.h>

/**
 * @brief IFirmwareImageWriter over the Arduino Update singleton.
 *
 * A thin adapter by design: every method is one call into Update, so that the download loop in
 * FirmwareRemoteUpdate - the part with decisions in it - stays out of this device-only file and
 * inside the class the host tests execute.
 */
class UpdateFirmwareImageWriter : public IFirmwareImageWriter {
public:
    bool begin(size_t imageSize) override {
        return Update.begin(imageSize);
    }

    // Update.begin() resets the expected digest, so this can only be armed after it; Update
    // refuses anything that is not 32 characters long and compares case-sensitively against
    // its own lowercase hex, which is what the caller normalizes to.
    bool setExpectedMd5(const char* md5) override {
        return Update.setMD5(md5);
    }

    size_t write(const uint8_t* data, size_t len) override {
        // Update.write takes a non-const pointer but does not modify the buffer.
        return Update.write(const_cast<uint8_t*>(data), len);
    }

    bool end() override {
        return Update.end() && Update.isFinished();
    }

    void abort() override {
        Update.abort();
    }

    bool isRunning() override {
        return Update.isRunning();
    }

    int lastError() override {
        return static_cast<int>(Update.getError());
    }

    uint32_t freeImageSpace() override {
        return ESP.getFreeSketchSpace();
    }
};
