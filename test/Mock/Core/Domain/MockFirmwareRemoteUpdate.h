#ifndef MOCK_FIRMWARE_REMOTE_UPDATE_H
#define MOCK_FIRMWARE_REMOTE_UPDATE_H

#include <string>

#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"

class MockFirmwareRemoteUpdate : public IFirmwareRemoteUpdate {
private:
    bool _updateInProgress = false;
    bool _updateFromURLCalled = false;
    bool _updateFromURLResult = true;
    std::string _lastUrl;
    std::string _lastExpectedMd5;
    std::string _lastErrorMessage;
    uint8_t _lastUpdateStatus = 0;
    int _lastHttpCode = 0;
    int _updateFromURLCallCount = 0;

public:
    bool updateFirmwareFromURL(const char* url, const char* expectedMd5) override {
        _updateFromURLCalled = true;
        _updateFromURLCallCount++;
        _lastUrl = (url != nullptr) ? url : "";
        _lastExpectedMd5 = (expectedMd5 != nullptr) ? expectedMd5 : "";
        return _updateFromURLResult;
    }

    bool isUpdateInProgress() override {
        return _updateInProgress;
    }

    const char* getLastErrorMessage() const override {
        return _lastErrorMessage.c_str();
    }

    uint8_t getLastUpdateStatus() const override {
        return _lastUpdateStatus;
    }

    // The FIRMWARE_UPDATE_STATE_* value the real updater records for the caller to persist.
    void setLastUpdateStatus(uint8_t status) {
        _lastUpdateStatus = status;
    }

    int getLastHttpCode() const override {
        return _lastHttpCode;
    }

    void setUpdateInProgress(bool inProgress) {
        _updateInProgress = inProgress;
    }

    // What updateFirmwareFromURL() answers; the real one fails on a dead link or a short write.
    void setUpdateFromURLResult(bool result) {
        _updateFromURLResult = result;
    }

    void setLastErrorMessage(const std::string& message) {
        _lastErrorMessage = message;
    }

    void setLastHttpCode(int code) {
        _lastHttpCode = code;
    }

    bool wasUpdateFromURLCalled() const {
        return _updateFromURLCalled;
    }

    int getUpdateFromURLCallCount() const {
        return _updateFromURLCallCount;
    }

    const char* getLastUrl() const {
        return _lastUrl.c_str();
    }

    // The image checksum the caller passed on, empty when the command carried none.
    const std::string& getLastExpectedMd5() const {
        return _lastExpectedMd5;
    }

    void reset() {
        _updateInProgress = false;
        _updateFromURLCalled = false;
        _updateFromURLResult = true;
        _lastUrl.clear();
        _lastExpectedMd5.clear();
        _lastErrorMessage.clear();
        _lastUpdateStatus = 0;
        _lastHttpCode = 0;
        _updateFromURLCallCount = 0;
    }
};

#endif // MOCK_FIRMWARE_REMOTE_UPDATE_H
