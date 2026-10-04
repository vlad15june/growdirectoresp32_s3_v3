#ifndef CLEAR_MODULE_DATA_SERVICE_H
#define CLEAR_MODULE_DATA_SERVICE_H

#include "Core/Domain/Interfaces/IClearModuleDataService.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsAdapter.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsRuleDao.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorDao.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorTypeDao.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include <stdarg.h>
#include <vector>

/**
 * @brief Service for clearing all module data from SPIFFS storage
 */
class ClearModuleDataService : public IClearModuleDataService {
private:
    ISpiffsRuleDao* ruleDao;
    ISpiffsSensorDao* sensorDao;
    ISpiffsSensorTypeDao* sensorTypeDao;
    ILogger* logger;
    IDebugSystem* debugSystem;
    ISettings* settings;
    ISpiffsAdapter* spiffsAdapter;

    void log(const char *format, ...) {
        if (logger != nullptr && debugSystem != nullptr && 
            debugSystem->isFeatureEnabled(DebugFeatures::SPIFFS_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println(DebugFeatures::SPIFFS_DEBUG, buffer);
        }
    }

public:
    ClearModuleDataService(
        ISpiffsRuleDao* ruleDao,
        ISpiffsSensorDao* sensorDao,
        ISpiffsSensorTypeDao* sensorTypeDao,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        ISettings* settings = nullptr,
        ISpiffsAdapter* spiffsAdapter = nullptr
    ) : ruleDao(ruleDao),
        sensorDao(sensorDao),
        sensorTypeDao(sensorTypeDao),
        logger(logger), 
        debugSystem(debugSystem),
        settings(settings),
        spiffsAdapter(spiffsAdapter) {}
    
    ~ClearModuleDataService() override = default;

    /**
     * @brief The boot half of a factory reset: drop the stored files the flag asked for
     *
     * clearAllData() marks storage for deletion and the module restarts; the files themselves go
     * here, on the next boot, before anything reads them.
     *
     * The catalog and the sensor registry are dropped through their DAOs rather than by file name,
     * because each of them is stored as up to three files: a replacement that was interrupted
     * leaves the file complete under `.tmp` or `.bak`, and load-time recovery takes it from there
     * and gives it its name back. A reset that removes only the name would be undone by the first
     * read after it.
     *
     * The rule file goes here as well. clearAllData() empties it before the restart, but behind
     * the rule repository's back, and the control loop runs on until the restart: a rule written
     * through the repository in that time stores the rules the repository still holds.
     */
    bool clearStoredFiles() {
        bool success = true;

        if (sensorTypeDao != nullptr) {
            if (sensorTypeDao->clear()) {
                log("Removed the stored sensor type catalog");
            } else {
                log("Failed to remove the stored sensor type catalog");
                success = false;
            }
        } else {
            log("WARNING: sensorTypeDao is null");
            success = false;
        }

        if (sensorDao != nullptr) {
            sensorDao->clear();
            log("Removed the stored sensors");
        } else {
            log("WARNING: sensorDao is null");
            success = false;
        }

        if (ruleDao != nullptr) {
            ruleDao->clear();
            log("Removed the stored rules");
        } else {
            log("WARNING: ruleDao is null");
            success = false;
        }

        if (spiffsAdapter != nullptr) {
            spiffsAdapter->removeFile("/sensors/sensors.json");
            spiffsAdapter->removeFile("/wifiNetworks.json");
            log("Removed the legacy sensors file and the WiFi networks");
        } else {
            log("WARNING: spiffsAdapter is null");
            success = false;
        }

        return success;
    }
    
    bool clearAllData() override {
        log("Clearing module data files...");
        
        bool success = true;
        
        // Clear sensor types data by saving empty vector
        if (sensorTypeDao != nullptr) {
            std::vector<SensorType> emptySensorTypes;
            if (sensorTypeDao->saveAll(emptySensorTypes)) {
                log("Cleared sensor types data");
            } else {
                log("Failed to clear sensor types data");
                success = false;
            }
        } else {
            log("WARNING: sensorTypeDao is null");
            success = false;
        }
        
        // Clear sensors data by saving empty vector
        if (sensorDao != nullptr) {
            std::vector<SensorEntity> emptySensors;
            if (sensorDao->saveAll(emptySensors)) {
                log("Cleared sensors data");
            } else {
                log("Failed to clear sensors data");
                success = false;
            }
        } else {
            log("WARNING: sensorDao is null");
            success = false;
        }
        
        // Clear rules data by saving empty vector
        if (ruleDao != nullptr) {
            std::vector<Rule> emptyRules;
            if (ruleDao->saveAll(emptyRules)) {
                log("Cleared rules data");
            } else {
                log("Failed to clear rules data");
                success = false;
            }
        } else {
            log("WARNING: ruleDao is null");
            success = false;
        }
        
        if (settings != nullptr) {
            settings->setWasInitialSyncCompleted(0);
            log("Cleared wasInitialSyncCompleted flag");

            settings->setNoScanSensors(1);
            log("Set noScanSensors flag: first boot after factory reset will skip scan, second boot will scan");
        } else {
            log("WARNING: settings is null, cannot clear settings flags");
        }
        
        // Reset module mode to USER_CAN_CONFIGURE (factory reset state)
        if (settings != nullptr) {
            settings->setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
            log("Reset module mode to USER_CAN_CONFIGURE (42)");

            settings->setNeedClearSpiffs(NEED_CLEAR_SPIFFS_MARKER);
            log("Set needClearSpiffs flag: SPIFFS files will be deleted on next boot");
        } else {
            log("WARNING: settings is null, cannot reset module mode");
        }
        
        log("Module data clearing completed. Success: %s", success ? "true" : "false");
        return success;
    }
};

#endif // CLEAR_MODULE_DATA_SERVICE_H
