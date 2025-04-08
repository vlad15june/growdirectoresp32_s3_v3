#ifndef MODULE_CONFIG_H
#define MODULE_CONFIG_H

#include <Arduino.h>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"

// CID pin definitions - these should be adjusted to match your hardware
#define CID_0 12 // Example pin, adjust as needed
#define CID_1 13 // Example pin, adjust as needed
#define CID_2 14 // Example pin, adjust as needed

// SETUP_DEBUG flag
#define SETUP_DEBUG true

// Concrete implementation of IModuleConfig
class ModuleConfig : public IModuleConfig
{
private:
    int moduleType;
    int moduleSubType;
    ILogger *logger;

public:
    ModuleConfig(ILogger *loggerInstance = nullptr)
        : moduleType(MODULE_TYPE_DEVICE), moduleSubType(MODULE_SUBTYPE_RELAY), logger(loggerInstance) {}

    void configureModuleType() override
    {
        pinMode(CID_0, INPUT);
        pinMode(CID_1, INPUT);
        pinMode(CID_2, INPUT);

        int code = digitalRead(CID_0) + digitalRead(CID_1) * 2 + digitalRead(CID_2) * 4;

        if (SETUP_DEBUG && logger)
            logger->printf("SETUP: module type code: %d\n", code);

        if (code == 1)
        {
            moduleType = MODULE_TYPE_DEVICE;
            moduleSubType = MODULE_SUBTYPE_RELAY;
        }

        if (code == 2)
        {
            moduleType = MODULE_TYPE_DEVICE;
            moduleSubType = MODULE_SUBTYPE_SOKET;
        }

        if (code == 3)
        {
            moduleType = MODULE_TYPE_SENSOR;
            moduleSubType = MODULE_SUBTYPE_NONE;
        }

        if (code == 4)
        {
            moduleType = MODULE_TYPE_HYDRO;
            moduleSubType = MODULE_SUBTYPE_NONE;
        }

        if (code == 5)
        {
            moduleType = MODULE_TYPE_ANALOG_INPUT_DEVICE;
            moduleSubType = MODULE_SUBTYPE_DIMMER;
        }

        if (SETUP_DEBUG && logger)
            logger->printf("SETUP: moduleType: %d moduleSubType: %d \n", moduleType, moduleSubType);
    }

    int getModuleType() const override
    {
        return moduleType;
    }

    int getModuleSubType() const override
    {
        return moduleSubType;
    }

    void setLogger(ILogger *loggerInstance)
    {
        logger = loggerInstance;
    }
};

#endif // MODULE_CONFIG_H
