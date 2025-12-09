#ifndef CLEAR_MODULE_DATA_SERVICE_H
#define CLEAR_MODULE_DATA_SERVICE_H

#include "Application/Interfaces/IClearModuleDataService.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsRuleDao.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorDao.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorTypeDao.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
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
        IDebugSystem* debugSystem = nullptr
    ) : ruleDao(ruleDao),
        sensorDao(sensorDao),
        sensorTypeDao(sensorTypeDao),
        logger(logger), 
        debugSystem(debugSystem) {}
    
    ~ClearModuleDataService() override = default;
    
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
        
        log("Module data clearing completed. Success: %s", success ? "true" : "false");
        return success;
    }
};

#endif // CLEAR_MODULE_DATA_SERVICE_H
