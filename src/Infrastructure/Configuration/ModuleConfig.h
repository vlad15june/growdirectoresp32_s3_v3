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

// LED pin definitions
#define LED_SKT_SIZE 2
#define LED_RELAY_SIZE 4
#define LED_HYDRO_SIZE 3
#define LED_DIMMER_SIZE 2
#define LED_SENSOR_SIZE 1

class ModuleConfig : public IModuleConfig
{
private:
    int moduleType;
    int moduleSubType;
    int deviceCount;
    ILogger *logger;
    IDebugSystem *debugSystem;
    ISettings *settings;
    INTPTimeAdapter *timeAdapter;

    // LED arrays
    int* redLeds;
    int* greenLeds;
    int ledsCount;
    
    // LED pin definitions
    static int LED_R_SKT[2];
    static int LED_G_SKT[2];
    static int LED_G_RELAY[4];
    static int LED_R_RELAY[4];
    static uint8_t LED_R_HYDRO[3];
    static uint8_t LED_G_HYDRO[3];
    static int LED_R_DIMMER[2];
    static int LED_G_DIMMER[2];
    static int LED_R_Sensor;
    static int LED_G_Sensor;

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
    
    void initializeLeds()
    {
        // Free any previously allocated LED arrays
        if (redLeds != nullptr) {
            delete[] redLeds;
            redLeds = nullptr;
        }
        if (greenLeds != nullptr) {
            delete[] greenLeds;
            greenLeds = nullptr;
        }
        
        // Set LED arrays based on module type and subtype
        if (moduleType == MODULE_TYPE_DEVICE) {
            if (moduleSubType == MODULE_SUBTYPE_RELAY) {
                redLeds = new int[ledsCount];
                greenLeds = new int[ledsCount];
                
                for (int i = 0; i < ledsCount; i++) {
                    redLeds[i] = LED_R_RELAY[i];
                    greenLeds[i] = LED_G_RELAY[i];
                }
            } 
            else if (moduleSubType == MODULE_SUBTYPE_SOKET) {
                redLeds = new int[ledsCount];
                greenLeds = new int[ledsCount];
                
                for (int i = 0; i < ledsCount; i++) {
                    redLeds[i] = LED_R_SKT[i];
                    greenLeds[i] = LED_G_SKT[i];
                }
            }
        } 
        else if (moduleType == MODULE_TYPE_SENSOR) {
            ledsCount = 1;
            redLeds = new int[1];
            greenLeds = new int[1];
            redLeds[0] = LED_R_Sensor;
            greenLeds[0] = LED_G_Sensor;
        } 
        else if (moduleType == MODULE_TYPE_HYDRO) {
            redLeds = new int[ledsCount];
            greenLeds = new int[ledsCount];
            
            for (int i = 0; i < ledsCount; i++) {
                redLeds[i] = LED_R_HYDRO[i];
                greenLeds[i] = LED_G_HYDRO[i];
            }
        } 
        else if (moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE && moduleSubType == MODULE_SUBTYPE_DIMMER) {
            redLeds = new int[ledsCount];
            greenLeds = new int[ledsCount];
            
            for (int i = 0; i < ledsCount; i++) {
                redLeds[i] = LED_R_DIMMER[i];
                greenLeds[i] = LED_G_DIMMER[i];
            }
        }
        
        log("SETUP: LED arrays initialized - LEDs count: %d\n", ledsCount);
    }

public:
    ModuleConfig(ILogger *loggerInstance = nullptr, 
                IDebugSystem *debugSystemInstance = nullptr, 
                ISettings *settingsInstance = nullptr,
                INTPTimeAdapter *timeAdapterInstance = nullptr)
        : moduleType(MODULE_TYPE_DEVICE), moduleSubType(MODULE_SUBTYPE_RELAY), deviceCount(0),
          logger(loggerInstance), debugSystem(debugSystemInstance), 
          settings(settingsInstance), timeAdapter(timeAdapterInstance) {}
          
    ~ModuleConfig() {
        if (redLeds != nullptr) {
            delete[] redLeds;
        }
        if (greenLeds != nullptr) {
            delete[] greenLeds;
        }
    }

    void init() override
    {
        configureModuleType();
        
        // Initialize LED arrays based on configured module type
        initializeLeds();

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
            deviceCount = 4;
            ledsCount = LED_RELAY_SIZE;
        }

        if (code == 2)
        {
            moduleType = MODULE_TYPE_DEVICE;
            moduleSubType = MODULE_SUBTYPE_SOKET;
            deviceCount = 2;
            ledsCount = LED_SKT_SIZE;
        }

        if (code == 3)
        {
            moduleType = MODULE_TYPE_SENSOR;
            moduleSubType = MODULE_SUBTYPE_NONE;
            deviceCount = 0;
            ledsCount = LED_SENSOR_SIZE;
        }

        if (code == 4)
        {
            moduleType = MODULE_TYPE_HYDRO;
            moduleSubType = MODULE_SUBTYPE_NONE;
            deviceCount = 3;
            ledsCount = LED_HYDRO_SIZE;
        }

        if (code == 5)
        {
            moduleType = MODULE_TYPE_ANALOG_INPUT_DEVICE;
            moduleSubType = MODULE_SUBTYPE_DIMMER;
            deviceCount = 2;
            ledsCount = LED_DIMMER_SIZE;
        }

        log("SETUP: moduleType: %d moduleSubType: %d deviceCount: %d ledsCount: %d\n", 
            moduleType, moduleSubType, deviceCount, ledsCount);
    }

    int getModuleType() const override
    {
        return moduleType;
    }

    int getModuleSubType() const override
    {
        return moduleSubType;
    }

    int getDeviceCount() const override
    {
        return deviceCount;
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

    int* getRedLeds() const override {
        return redLeds;
    }
    
    int* getGreenLeds() const override {
        return greenLeds;
    }
    
    int getLedsCount() const override {
        return ledsCount;
    }
};

// Define static members
int ModuleConfig::LED_R_SKT[2] = {38, 37};
int ModuleConfig::LED_G_SKT[2] = {40, 39};

int ModuleConfig::LED_G_RELAY[4] = {15, 14, 13, 12};
int ModuleConfig::LED_R_RELAY[4] = {35, 36, 37, 38};

uint8_t ModuleConfig::LED_R_HYDRO[3] = {16, 8, 34};
uint8_t ModuleConfig::LED_G_HYDRO[3] = {13, 14, 12};

int ModuleConfig::LED_R_DIMMER[2] = {42, 41};
int ModuleConfig::LED_G_DIMMER[2] = {40, 39};

int ModuleConfig::LED_R_Sensor = 12;
int ModuleConfig::LED_G_Sensor = 13;

#endif // MODULE_CONFIG_H
