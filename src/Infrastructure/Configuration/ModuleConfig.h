#ifndef MODULE_CONFIG_H
#define MODULE_CONFIG_H

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"

#define CID_0 5
#define CID_1 6
#define CID_2 7

class ModuleConfig : public IModuleConfig
{
private:
    int moduleType;
    int moduleSubType;
    ILogger *logger;
    IDebugSystem *debugSystem;
    ISettings *settings;
    INTPTimeAdapter *timeAdapter;

    void log(const char *format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println(buffer);
        }
    }

public:
    ModuleConfig(ILogger *loggerInstance = nullptr, 
                IDebugSystem *debugSystemInstance = nullptr, 
                ISettings *settingsInstance = nullptr,
                INTPTimeAdapter *timeAdapterInstance = nullptr)
        : moduleType(MODULE_TYPE_DEVICE), moduleSubType(MODULE_SUBTYPE_RELAY),
          logger(loggerInstance), debugSystem(debugSystemInstance), 
          settings(settingsInstance), timeAdapter(timeAdapterInstance) {}

    void init() override
    {
        configureModuleType();

        if (settings != nullptr)
        {
            // Initialize settings based on the module type and subtype
            settings->begin();
            log("SETUP: Settings initialized for module type: %d, subtype: %d\n", moduleType, moduleSubType);
        }
        else
        {
            log("SETUP: Warning - Settings instance is null, skipping initialization\n");
        }

        // Initialize time adapter if available
        if (timeAdapter != nullptr)
        {
            // Setup NTP time services
            timeAdapter->setup();
            // Restore time from storage
            timeAdapter->restoreTimeFromStorage();
            log("SETUP: Time adapter initialized\n");
        }
        else
        {
            log("SETUP: Warning - Time adapter instance is null, skipping initialization\n");
        }
    }

    void configureModuleType() override
    {
        pinMode(CID_0, INPUT);
        pinMode(CID_1, INPUT);
        pinMode(CID_2, INPUT);

        int code = digitalRead(CID_0) + digitalRead(CID_1) * 2 + digitalRead(CID_2) * 4;

        log("SETUP: module type code: %d\n", code);

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

        log("SETUP: moduleType: %d moduleSubType: %d \n", moduleType, moduleSubType);
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

    void setSettings(ISettings *settingsInstance)
    {
        settings = settingsInstance;
    }

    ISettings *getSettings() override
    {
        return settings;
    }

    void setTimeAdapter(INTPTimeAdapter *timeAdapterInstance)
    {
        timeAdapter = timeAdapterInstance;
    }

    INTPTimeAdapter *getTimeAdapter()
    {
        return timeAdapter;
    }
};

#endif // MODULE_CONFIG_H
