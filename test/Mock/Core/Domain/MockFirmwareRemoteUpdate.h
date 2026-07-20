#ifndef MOCK_FIRMWARE_REMOTE_UPDATE_H
#define MOCK_FIRMWARE_REMOTE_UPDATE_H

#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"

class MockFirmwareRemoteUpdate : public IFirmwareRemoteUpdate {
private:
    bool _updateInProgress = false;
    bool _updateFromURLCalled = false;
    const char* _lastUrl = nullptr;

public:
    bool updateFirmwareFromURL(const char* url) override {
        _updateFromURLCalled = true;
        _lastUrl = url;
        return true;
    }

    bool isUpdateInProgress() override {
        return _updateInProgress;
    }

    void setUpdateInProgress(bool inProgress) {
        _updateInProgress = inProgress;
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
        _lastUrl = nullptr;
    }
};

#endif // MOCK_FIRMWARE_REMOTE_UPDATE_H
