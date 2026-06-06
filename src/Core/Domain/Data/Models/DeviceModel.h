#pragma once

#include <vector>
#include <cstring>
#include "IDeviceModel.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"

/**
 * @brief Implementation of the device model
 */
class DeviceModel : public IDeviceModel {
private:
    std::vector<Device*> devices;
    IModuleConfig* moduleConfig;
    ModuleStructure* moduleStructure;

public:
    /**
     * @brief Constructor
     * @param moduleConfig Module configuration
     * @param moduleStructure Module structure
     */
    DeviceModel(IModuleConfig* moduleConfig, ModuleStructure* moduleStructure) 
        : moduleConfig(moduleConfig), moduleStructure(moduleStructure) {
        devices.reserve(moduleConfig->getDeviceCount());
    }
    
    /**
     * @brief Destructor
     */
    ~DeviceModel() override {
        clearDevices();
    }
    
    /**
     * @brief Initialize the device model
     * @return True if initialization was successful, false otherwise
     */
    bool init() override {
        clearDevices();
        
        int moduleType = moduleConfig->getModuleType();
        int moduleSubType = moduleConfig->getModuleSubType();
        
        if (moduleType == MODULE_TYPE_DEVICE) {
            auto relays = moduleStructure->getAllRelays();
            for (auto relay : relays) {
                DeviceDigitalOut* device = new DeviceDigitalOut();
                device->deviceUid = relay->uid.c_str();
                device->state = DEVICE_STATE_OFF;
                device->isError = false;
                device->deviceType = DEVICE_TYPE_DIGITAL;
                addDevice(device);
            }
        }
        else if (moduleType == MODULE_TYPE_HYDRO) {
            auto pumps = moduleStructure->getAllPumps();
            for (auto pump : pumps) {
                DeviceDigitalOut* device = new DeviceDigitalOut();
                device->deviceUid = pump->uid.c_str();
                device->state = DEVICE_STATE_OFF;
                device->isError = false;
                device->deviceType = DEVICE_TYPE_PUMP;
                addDevice(device);
            }
        }
        else if (moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE) {
            auto relays = moduleStructure->getAllRelays();
            for (auto relay : relays) {
                DeviceAnalogOut* device = new DeviceAnalogOut();
                device->deviceUid = relay->uid.c_str();
                device->state = DEVICE_STATE_OFF;
                device->isError = false;
                device->deviceType = DEVICE_TYPE_ANALOG;
                addDevice(device);
            }
        }
        
        return true;
    }
    
    /**
     * @brief Get a device by its UID
     * @param deviceUid Device UID to search for
     * @return Pointer to the device if found, nullptr otherwise
     */
    Device* getDeviceByUid(const char* deviceUid) override {
        for (auto device : devices) {
            if (strcmp(device->deviceUid, deviceUid) == 0) {
                return device;
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Get all devices
     * @return Vector of device pointers
     */
    std::vector<Device*> getAllDevices() override {
        return devices;
    }
    
    /**
     * @brief Get the number of devices
     * @return Number of devices
     */
    int getDeviceCount() const override {
        return devices.size();
    }
    
    /**
     * @brief Add a device
     * @param device Device to add
     * @return True if the device was added, false otherwise
     */
    bool addDevice(Device* device) override {
        if (device == nullptr) return false;
        
        // Check if device already exists
        if (getDeviceByUid(device->deviceUid) != nullptr) {
            return false;
        }
        
        devices.push_back(device);
        return true;
    }
    
    /**
     * @brief Remove a device by its UID
     * @param deviceUid Device UID to remove
     * @return True if the device was removed, false otherwise
     */
    bool removeDevice(const char* deviceUid) override {
        auto it = std::find_if(devices.begin(), devices.end(), 
            [deviceUid](Device* device) {
                return strcmp(device->deviceUid, deviceUid) == 0;
            });
        
        if (it != devices.end()) {
            delete *it;
            devices.erase(it);
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Clear all devices
     */
    void clearDevices() override {
        for (auto device : devices) {
            delete device;
        }
        devices.clear();
    }
};
