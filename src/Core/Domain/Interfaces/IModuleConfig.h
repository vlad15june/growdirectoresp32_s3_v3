#ifndef IMODULECONFIG_H
#define IMODULECONFIG_H

#include "ISettings.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/FileSystemStatusEnum.h"

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
    virtual ModuleModeEnum getCurrentModuleMode() const = 0;
    virtual void setCurrentModuleMode(ModuleModeEnum mode) = 0;
    virtual const char* getModuleUid() const = 0;
    virtual const char* getFirmwareVersion() const = 0;
    virtual int getSensorPinCount() const = 0;
    virtual uint8_t getButtonPin() const = 0;
    virtual uint8_t* getRelayPins() const = 0;
    virtual uint8_t* getSensorPowerPins() const = 0;
    virtual uint8_t* getPumpPins() const = 0;
    virtual uint8_t* getPumpStatusPins() const = 0;
    // Test-mode pin accessors
    virtual uint8_t* getTestSensorPins() const = 0;    // pin_TST_SNS (4 pins)
    virtual uint8_t* getAdcPins() const = 0;           // adcPin (4 pins)
    virtual uint8_t  getDoOrpPin() const = 0;          // DO_ORP
    virtual uint8_t  getEcSdnPin() const = 0;          // EC_SDN
    virtual uint8_t* getDimmerOffPins() const = 0;     // DIM_OFF (2 pins)
    virtual uint8_t* getDimmerVoltagePins() const = 0; // V_DIM (2 pins)
    virtual uint8_t  getPowerGood33Pin() const = 0;    // PG 3.3V
    virtual uint8_t  getPowerGood50Pin() const = 0;    // PG 5.0V
    virtual uint8_t  getPowerGood55Pin() const = 0;    // PG 5.5V
    virtual FileSystemStatusEnum getFileSystemStatus() const = 0;
    virtual void setFileSystemStatus(FileSystemStatusEnum status) = 0;
    virtual ~IModuleConfig() = default;
};

#endif // IMODULECONFIG_H