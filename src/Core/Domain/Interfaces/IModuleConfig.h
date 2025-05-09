#ifndef IMODULECONFIG_H
#define IMODULECONFIG_H

#include "ISettings.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"

// Module type constants
#define MODULE_TYPE_SENSOR 1
#define MODULE_TYPE_DEVICE 2
#define MODULE_TYPE_HYDRO 3
#define MODULE_TYPE_ANALOG_INPUT_DEVICE 4

// Module subtype constants
#define MODULE_SUBTYPE_NONE 0
#define MODULE_SUBTYPE_SOKET 1
#define MODULE_SUBTYPE_RELAY 2
#define MODULE_SUBTYPE_DIMMER 3

class IModuleConfig
{
public:
    virtual void configureModuleType() = 0;
    virtual void init() = 0;
    virtual int getModuleType() const = 0;
    virtual int getModuleSubType() const = 0;
    virtual int getDeviceCount() const = 0;
    virtual ISettings* getSettings() = 0;
    virtual uint8_t* getRedLeds() const = 0;
    virtual uint8_t* getGreenLeds() const = 0;
    virtual uint8_t getLedsCount() const = 0;
    virtual ModuleModeEnum getRequestedModuleMode() const = 0;
    virtual void setRequestedModuleMode(ModuleModeEnum mode) = 0;
    virtual const char* getModuleUid() const = 0;
    virtual const char* getFirmwareVersion() const = 0;
    virtual int getSensorPinCount() const = 0;  // New method for sensor pin count
    virtual ~IModuleConfig() = default;
};

#endif // IMODULECONFIG_H