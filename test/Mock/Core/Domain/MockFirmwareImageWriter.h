#pragma once

#include "Core/Domain/Interfaces/Adapters/IFirmwareImageWriter.h"
#include "Sim/HostMd5.h"

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * @brief Records what the download session asks of the flash slot, answers what the test says.
 *
 * The stand-in for the Update singleton on the host: it keeps counters, not an image. A test
 * that needs the flash to refuse sets the corresponding result field.
 *
 * The one thing it does for real is the checksum, because that is the behaviour the OTA
 * scenarios are about. It follows the device mechanism it stands in for, the Arduino Update
 * library: begin() clears any previous expectation and restarts the digest, setExpectedMd5()
 * refuses a string that is not 32 characters long, the digest is taken over everything write()
 * accepted since begin() (and so does not depend on when the expectation was armed), and end()
 * compares the two case-sensitively - the library's own digest is lowercase hex - and refuses
 * the image with UPDATE_ERROR_MD5 when they differ. An image only becomes bootable when end()
 * returns true, which is where the library sets the boot partition.
 */
class MockFirmwareImageWriter : public IFirmwareImageWriter {
public:
    /** The Arduino Update library's UPDATE_ERROR_MD5, the code a refused digest reports. */
    static const int ERROR_MD5 = 7;

    // Scripted answers
    bool beginResult = true;
    bool endResult = true;
    int lastErrorValue = 0;
    uint32_t freeImageSpaceValue = 3342336;
    /** From this byte count on, write() reports 0 written - a failing flash. */
    size_t acceptAtMostBytes = SIZE_MAX;
    /** Whether setExpectedMd5() is allowed to succeed at all - the writer's own refusal. */
    bool acceptExpectedMd5 = true;

    // Recorded calls
    int beginCalls = 0;
    size_t lastBeginSize = 0;
    size_t bytesWritten = 0;
    int writeCalls = 0;
    int endCalls = 0;
    int abortCalls = 0;
    int setExpectedMd5Calls = 0;
    std::string lastExpectedMd5;
    /** How many bytes had been written when the expectation was armed - must be zero. */
    size_t bytesWrittenWhenMd5Armed = SIZE_MAX;
    /** How many times begin() had run when the expectation was armed - must be one. */
    int beginCallsWhenMd5Armed = 0;
    /** Whether end() ever accepted an image, i.e. whether a slot was made bootable. */
    bool imageAccepted = false;
    /** Whether end() refused an image because its digest did not match the expectation. */
    bool checksumRefused = false;

    bool begin(size_t imageSize) override
    {
        beginCalls++;
        lastBeginSize = imageSize;
        _running = beginResult;
        _expectedMd5.clear();
        _digest.reset();
        return beginResult;
    }

    bool setExpectedMd5(const char* md5) override
    {
        setExpectedMd5Calls++;
        lastExpectedMd5 = (md5 != nullptr) ? md5 : "";
        bytesWrittenWhenMd5Armed = bytesWritten;
        beginCallsWhenMd5Armed = beginCalls;
        if (!acceptExpectedMd5 || lastExpectedMd5.size() != 32)
        {
            return false;
        }
        _expectedMd5 = lastExpectedMd5;
        return true;
    }

    size_t write(const uint8_t* data, size_t len) override
    {
        writeCalls++;
        size_t accepted = len;
        if (bytesWritten + len > acceptAtMostBytes)
        {
            accepted = (acceptAtMostBytes > bytesWritten) ? (acceptAtMostBytes - bytesWritten) : 0;
        }
        if (data != nullptr && accepted > 0)
        {
            _digest.add(data, accepted);
        }
        bytesWritten += accepted;
        return accepted;
    }

    bool end() override
    {
        endCalls++;
        _running = false;
        if (!endResult)
        {
            return false;
        }
        if (!_expectedMd5.empty() && _digest.hexDigest() != _expectedMd5)
        {
            checksumRefused = true;
            lastErrorValue = ERROR_MD5;
            return false;
        }
        imageAccepted = true;
        return true;
    }

    void abort() override
    {
        abortCalls++;
        _running = false;
    }

    bool isRunning() override { return _running; }

    int lastError() override { return lastErrorValue; }

    uint32_t freeImageSpace() override { return freeImageSpaceValue; }

    /** @brief The digest of everything write() accepted, as the device would have computed it. */
    std::string writtenImageMd5() const { return _digest.hexDigest(); }

private:
    bool _running = false;
    std::string _expectedMd5;
    hostsim::HostMd5 _digest;
};
