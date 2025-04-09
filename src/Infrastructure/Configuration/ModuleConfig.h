#ifndef MODULE_CONFIG_H
#define MODULE_CONFIG_H

#include <Arduino.h>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

// CID pin definitions - these should be adjusted to match your hardware
#define CID_0 5
#define CID_1 6
#define CID_2 7

// Concrete implementation of IModuleConfig
class ModuleConfig : public IModuleConfig
{
private:
    int moduleType;
    int moduleSubType;
    ILogger *logger;
    IDebugSystem *debugSystem;

public:
    ModuleConfig(ILogger *loggerInstance = nullptr, IDebugSystem *debugSystemInstance = nullptr)
        : moduleType(MODULE_TYPE_DEVICE), moduleSubType(MODULE_SUBTYPE_RELAY), logger(loggerInstance), debugSystem(debugSystemInstance) {}

    void configureModuleType() override
    {
        pinMode(CID_0, INPUT);
        pinMode(CID_1, INPUT);
        pinMode(CID_2, INPUT);

        int code = digitalRead(CID_0) + digitalRead(CID_1) * 2 + digitalRead(CID_2) * 4;

        if (debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG) && logger)
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

        if (debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG) && logger)
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
