#ifndef MOCK_DEVICE_SERVICE_H
#define MOCK_DEVICE_SERVICE_H

#include "Application/Services/Devices/IDeviceService.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Application/DTOs/UpdateDto.h"
#include <map>
#include <vector>
#include <string>

class MockDeviceService : public IDeviceService {
private:
    std::vector<Device*> devices;
    std::map<std::string, bool> deviceStates; // Maps deviceUid to on/off state
    UpdateDto updateDto;
    
    // Tracking flags for method calls
    bool initCalled = false;
    bool getDeviceByUidCalled = false;
    bool getAllDevicesCalled = false;
    bool updateDeviceStateCalled = false;
    bool setDeviceValueCalled = false;
    bool createUpdateDtoCalled = false;
    bool turnOnDeviceCalled = false;
    bool turnOffDeviceCalled = false;
    bool isDeviceOnCalled = false;
    bool processDevicesCalled = false;
    bool updateDeviceSensorValuesCalled = false;
    
    // Call counts
    int initCallCount = 0;
    int getDeviceByUidCallCount = 0;
    int getAllDevicesCallCount = 0;
    int updateDeviceStateCallCount = 0;
    int setDeviceValueCallCount = 0;
    int createUpdateDtoCallCount = 0;
    int turnOnDeviceCallCount = 0;
    int turnOffDeviceCallCount = 0;
    int isDeviceOnCallCount = 0;
    int processDevicesCallCount = 0;
    int updateDeviceSensorValuesCallCount = 0;
    
    // Return values
    bool initSuccess = true;
    bool updateDeviceStateSuccess = true;
    bool setDeviceValueSuccess = true;
    bool turnOnDeviceSuccess = true;
    bool turnOffDeviceSuccess = true;

public:
    MockDeviceService() = default;
    
    ~MockDeviceService() override {
        for (auto device : devices) {
            delete device;
        }
        devices.clear();
    }
    
    bool init() override {
        initCalled = true;
        initCallCount++;
        return initSuccess;
    }
    
    Device* getDeviceByUid(const char* deviceUid) override {
        getDeviceByUidCalled = true;
        getDeviceByUidCallCount++;
        
        // Simple implementation for testing
        for (auto device : devices) {
            if (strcmp(device->deviceUid, deviceUid) == 0) {
                return device;
            }
        }
        return nullptr;
    }
    
    std::vector<Device*> getAllDevices() override {
        getAllDevicesCalled = true;
        getAllDevicesCallCount++;
        return devices;
    }
    
    bool updateDeviceState(const char* deviceUid, DeviceStateEnum state) override {
        updateDeviceStateCalled = true;
        updateDeviceStateCallCount++;
        
        if (deviceUid) {
            deviceStates[deviceUid] = (state == DEVICE_STATE_ON);
            
            // Update actual device if it exists
            Device* device = getDeviceByUid(deviceUid);
            if (device) {
                device->state = state;
            }
        }
        
        return updateDeviceStateSuccess;
    }
    
    bool setDeviceValue(const char* deviceUid, float value) override {
        setDeviceValueCalled = true;
        setDeviceValueCallCount++;
        return setDeviceValueSuccess;
    }
    
    UpdateDto createUpdateDto() override {
        createUpdateDtoCalled = true;
        createUpdateDtoCallCount++;
        return updateDto;
    }
    
    bool turnOnDevice(const char* deviceUid) override {
        turnOnDeviceCalled = true;
        turnOnDeviceCallCount++;
        
        if (deviceUid) {
            deviceStates[deviceUid] = true;
        }
        
        return turnOnDeviceSuccess;
    }
    
    bool turnOffDevice(const char* deviceUid) override {
        turnOffDeviceCalled = true;
        turnOffDeviceCallCount++;
        
        if (deviceUid) {
            deviceStates[deviceUid] = false;
        }
        
        return turnOffDeviceSuccess;
    }
    
    bool isDeviceOn(const char* deviceUid) override {
        isDeviceOnCalled = true;
        isDeviceOnCallCount++;
        
        if (deviceUid) {
            auto it = deviceStates.find(deviceUid);
            if (it != deviceStates.end()) {
                return it->second;
            }
        }
        
        return false;
    }
    
    void processDevices() override {
        processDevicesCalled = true;
        processDevicesCallCount++;
    }
    
    void updateDeviceSensorValues() override {
        updateDeviceSensorValuesCalled = true;
        updateDeviceSensorValuesCallCount++;
    }
    
    // Helper methods for tests
    void addDevice(Device* device) {
        if (device) {
            devices.push_back(device);
            if (device->deviceUid) {
                deviceStates[device->deviceUid] = (device->state == DEVICE_STATE_ON);
            }
        }
    }
    
    void setInitSuccess(bool success) {
        initSuccess = success;
    }
    
    void setUpdateDeviceStateSuccess(bool success) {
        updateDeviceStateSuccess = success;
    }
    
    void setTurnOnDeviceSuccess(bool success) {
        turnOnDeviceSuccess = success;
    }
    
    void setTurnOffDeviceSuccess(bool success) {
        turnOffDeviceSuccess = success;
    }
    
    void setUpdateDto(const UpdateDto& dto) {
        updateDto = dto;
    }
    
    bool wasInitCalled() const { return initCalled; }
    bool wasGetDeviceByUidCalled() const { return getDeviceByUidCalled; }
    bool wasGetAllDevicesCalled() const { return getAllDevicesCalled; }
    bool wasUpdateDeviceStateCalled() const { return updateDeviceStateCalled; }
    bool wasSetDeviceValueCalled() const { return setDeviceValueCalled; }
    bool wasCreateUpdateDtoCalled() const { return createUpdateDtoCalled; }
    bool wasTurnOnDeviceCalled() const { return turnOnDeviceCalled; }
    bool wasTurnOffDeviceCalled() const { return turnOffDeviceCalled; }
    bool wasIsDeviceOnCalled() const { return isDeviceOnCalled; }
    bool wasProcessDevicesCalled() const { return processDevicesCalled; }
    bool wasUpdateDeviceSensorValuesCalled() const { return updateDeviceSensorValuesCalled; }
    
    int getInitCallCount() const { return initCallCount; }
    int getGetDeviceByUidCallCount() const { return getDeviceByUidCallCount; }
    int getGetAllDevicesCallCount() const { return getAllDevicesCallCount; }
    int getUpdateDeviceStateCallCount() const { return updateDeviceStateCallCount; }
    int getSetDeviceValueCallCount() const { return setDeviceValueCallCount; }
    int getCreateUpdateDtoCallCount() const { return createUpdateDtoCallCount; }
    int getTurnOnDeviceCallCount() const { return turnOnDeviceCallCount; }
    int getTurnOffDeviceCallCount() const { return turnOffDeviceCallCount; }
    int getIsDeviceOnCallCount() const { return isDeviceOnCallCount; }
    int getProcessDevicesCallCount() const { return processDevicesCallCount; }
    int getUpdateDeviceSensorValuesCallCount() const { return updateDeviceSensorValuesCallCount; }
    
    void reset() {
        // Clear tracking flags
        initCalled = false;
        getDeviceByUidCalled = false;
        getAllDevicesCalled = false;
        updateDeviceStateCalled = false;
        setDeviceValueCalled = false;
        createUpdateDtoCalled = false;
        turnOnDeviceCalled = false;
        turnOffDeviceCalled = false;
        isDeviceOnCalled = false;
        processDevicesCalled = false;
        updateDeviceSensorValuesCalled = false;
        
        // Reset call counts
        initCallCount = 0;
        getDeviceByUidCallCount = 0;
        getAllDevicesCallCount = 0;
        updateDeviceStateCallCount = 0;
        setDeviceValueCallCount = 0;
        createUpdateDtoCallCount = 0;
        turnOnDeviceCallCount = 0;
        turnOffDeviceCallCount = 0;
        isDeviceOnCallCount = 0;
        processDevicesCallCount = 0;
        updateDeviceSensorValuesCallCount = 0;
        
        // Reset return values
        initSuccess = true;
        updateDeviceStateSuccess = true;
        setDeviceValueSuccess = true;
        turnOnDeviceSuccess = true;
        turnOffDeviceSuccess = true;
        
        // Clear devices
        for (auto device : devices) {
            delete device;
        }
        devices.clear();
        deviceStates.clear();
    }
};

#endif // MOCK_DEVICE_SERVICE_H
