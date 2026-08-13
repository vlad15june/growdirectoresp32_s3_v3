#ifndef MOCK_FIRMWARE_REMOTE_UPDATE_H
#define MOCK_FIRMWARE_REMOTE_UPDATE_H

#include <string>

#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"

class MockFirmwareRemoteUpdate : public IFirmwareRemoteUpdate {
private:
    bool _updateInProgress = false;
    bool _updateFromURLCalled = false;
    bool _updateFromURLResult = true;
    const char* _lastUrl = nullptr;
    std::string _lastErrorMessage;

public:
    bool updateFirmwareFromURL(const char* url) override {
        _updateFromURLCalled = true;
        _lastUrl = url;
        return _updateFromURLResult;
    }

    bool isUpdateInProgress() override {
        return _updateInProgress;
    }

    const char* getLastErrorMessage() const override {
        return _lastErrorMessage.c_str();
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

    bool wasUpdateFromURLCalled() const {
        return _updateFromURLCalled;
    }

    const char* getLastUrl() const {
        return _lastUrl;
    }

    void reset() {
        _updateInProgress = false;
        _updateFromURLCalled = false;
        _updateFromURLResult = true;
        _lastUrl = nullptr;
        _lastErrorMessage.clear();
    }
};

#endif // MOCK_FIRMWARE_REMOTE_UPDATE_H
