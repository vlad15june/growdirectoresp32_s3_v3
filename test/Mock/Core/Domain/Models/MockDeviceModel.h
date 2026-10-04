#ifndef MOCK_DEVICE_MODEL_H
#define MOCK_DEVICE_MODEL_H

#include "Core/Domain/Data/Models/IDeviceModel.h"
#include <vector>

class MockDeviceModel : public IDeviceModel {
private:
    std::vector<Device*> _devices;

public:
    MockDeviceModel() = default;

    bool init() override {
        return true;
    }

    Device* getDeviceByUid(const char* deviceUid) override {
        for (auto device : _devices) {
            if (device && strcmp(device->deviceUid, deviceUid) == 0) {
                return device;
            }
        }
        return nullptr;
    }

    std::vector<Device*> getAllDevices() override {
        return _devices;
    }

    const std::vector<Device*>& allDevices() override {
        return _devices;
    }

    int getDeviceCount() const override {
        return static_cast<int>(_devices.size());
    }

    bool addDevice(Device* device) override {
        if (device == nullptr) {
            return false;
        }
        _devices.push_back(device);
        return true;
    }

    bool removeDevice(const char* deviceUid) override {
        for (auto it = _devices.begin(); it != _devices.end(); ++it) {
            if (*it && strcmp((*it)->deviceUid, deviceUid) == 0) {
                _devices.erase(it);
                return true;
            }
        }
        return false;
    }

    void clearDevices() override {
        _devices.clear();
    }

    void reset() {
        _devices.clear();
    }
};

#endif // MOCK_DEVICE_MODEL_H
