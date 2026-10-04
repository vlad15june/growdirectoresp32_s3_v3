#pragma once

#include <vector>
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"

/**
 * @brief Interface for device model management
 */
class IDeviceModel {
public:
    virtual ~IDeviceModel() = default;
    
    // Initialization
    virtual bool init() = 0;
    
    // Device access
    virtual Device* getDeviceByUid(const char* deviceUid) = 0;
    virtual std::vector<Device*> getAllDevices() = 0;
    virtual int getDeviceCount() const = 0;

    /**
     * All devices, by reference - the accessor for code that runs every control-loop iteration.
     *
     * getAllDevices() returns the vector by value, and the control loop walks the list several
     * times per iteration; those copies were a steady heap churn on iterations in which nothing
     * changed. Real models override this to hand out their own vector. The default forwards to
     * getAllDevices() through a reused buffer, so the many small stand-in models in the tests
     * keep working unchanged - at the cost of the copy they always made.
     *
     * The reference is valid until addDevice/removeDevice/clearDevices/init changes the list.
     * The default is additionally not reentrant: a nested call on the same object reassigns the
     * buffer an outer loop may be iterating. Code that calls allDevices() from inside a walk of
     * allDevices() - the device state machine does - must run against a model that overrides
     * this method, as both production models do.
     */
    virtual const std::vector<Device*>& allDevices() {
        allDevicesFallback = getAllDevices();
        return allDevicesFallback;
    }
    
    // Device operations
    virtual bool addDevice(Device* device) = 0;
    virtual bool removeDevice(const char* deviceUid) = 0;
    virtual void clearDevices() = 0;

protected:
    // Buffer behind the default allDevices() only; models that override it never touch this.
    // Bounded by the device count of the module.
    std::vector<Device*> allDevicesFallback;
};
