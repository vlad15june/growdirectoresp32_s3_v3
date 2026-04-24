#pragma once

#include "ISpiffsSensorTypeDao.h"
#include "ISpiffsAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include <string>
#include <stdarg.h>
#include <cstdio>
#include <functional>
#include <stdexcept>

class SpiffsSensorTypeDao : public ISpiffsSensorTypeDao {
protected:
    ISpiffsAdapter* spiffsAdapter;
    IJsonConverter* jsonConverter;
    IDebugSystem* debugSystem;
    ILogger* logger;
    std::string filePath;

    /**
     * @brief Log method for DAO operations
     */
    void log(const char *format, ...) const {
        if (logger != nullptr && 
            (debugSystem == nullptr || debugSystem->isFeatureEnabled(DebugFeatures::SPIFFS_DEBUG))) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            logger->println(DebugFeatures::SPIFFS_DEBUG, buffer);
        }
    }

    std::string getFileName() const {
        return "sensorTypes.json";
    }

    std::string getBasePath() const {
        return "/data";
    }

    std::string entityToJson(const SensorType& sensorType) {
        return jsonConverter->sensorTypeToJson(sensorType);
    }

    bool jsonToEntity(const std::string& jsonStr, SensorType& sensorType) {
        return jsonConverter->sensorTypeFromJson(jsonStr, sensorType);
    }

    std::string entitiesToJson(const std::vector<SensorType>& sensorTypes) {
        log("SpiffsSensorTypeDao: entitiesToJson called with %d sensor types", sensorTypes.size());
#ifdef ESP_PLATFORM
        log("SpiffsSensorTypeDao: Free heap before JSON conversion: %d", ESP.getFreeHeap());
#endif
        
        try {
            std::string result = jsonConverter->sensorTypesToJson(sensorTypes);
            log("SpiffsSensorTypeDao: JSON conversion successful, result length: %d", result.length());
#ifdef ESP_PLATFORM
            log("SpiffsSensorTypeDao: Free heap after JSON conversion: %d", ESP.getFreeHeap());
#endif
            return result;
        } catch (...) {
            log("SpiffsSensorTypeDao: ERROR - Exception in jsonConverter->sensorTypesToJson!");
            return "[]";
        }
    }

    bool jsonToEntities(const std::string& jsonStr, std::vector<SensorType>& sensorTypes) {
        return jsonConverter->sensorTypesFromJson(jsonStr, sensorTypes);
    }

    void initialize() {
        log("SpiffsSensorTypeDao: Initializing");
        std::string basePath = getBasePath();
        if (!spiffsAdapter->directoryExists(basePath.c_str())) {
            log("SpiffsSensorTypeDao: Creating directory: %s", basePath.c_str());
            spiffsAdapter->createDirectory(basePath.c_str());
        }
        filePath = basePath + "/" + getFileName();
        log("SpiffsSensorTypeDao: File path set to: %s", filePath.c_str());
    }

    bool filterSensorTypes(const std::function<bool(const SensorType&)>& predicate, std::vector<SensorType>& sensorTypes) {
        std::vector<SensorType> allTypes;
        if (!getAll(allTypes)) {
            return false;
        }

        sensorTypes.clear();
        for (const auto& type : allTypes) {
            if (predicate(type)) {
                sensorTypes.push_back(type);
            }
        }

        return true;
    }

public:
    SpiffsSensorTypeDao(ISpiffsAdapter* spiffsAdapter, IJsonConverter* jsonConverter, IDebugSystem* debugSystem = nullptr, ILogger* logger = nullptr)
        : spiffsAdapter(spiffsAdapter), jsonConverter(jsonConverter), debugSystem(debugSystem), logger(logger) {
        if (!spiffsAdapter || !jsonConverter) {
            throw std::invalid_argument("Adapters cannot be null");
        }
        initialize();
    }

    bool clear() override {
        log("SpiffsSensorTypeDao: Clearing all data (removing file)");
        if (filePath.empty()) initialize();
        spiffsAdapter->RemoveFile(filePath.c_str());
        log("SpiffsSensorTypeDao: Clear operation completed");
        return true;
    }

    /**
     * @brief Set debug system for logging
     */
    void setDebugSystem(IDebugSystem *debugSystemInstance) {
        debugSystem = debugSystemInstance;
    }

    /**
     * @brief Set logger for output
     */
    void setLogger(ILogger *loggerInstance) {
        logger = loggerInstance;
    }

    // ISpiffsDao implementation using uid
    bool save(const SensorType& entity, const std::string& uid) override {
        log("SpiffsSensorTypeDao: Saving sensor type with UID: %s", uid.c_str());
        log("SpiffsSensorTypeDao: Entity name: %s, displayName: %s", entity.name.c_str(), entity.displayName.c_str());
        log("SpiffsSensorTypeDao: Entity has %d sub-sensor types", entity.sensorTypes.size());
#ifdef ESP_PLATFORM
        log("SpiffsSensorTypeDao: Free heap at start of save: %d", ESP.getFreeHeap());
#endif
        
        if (filePath.empty()) {
            log("SpiffsSensorTypeDao: FilePath empty, initializing...");
            initialize();
        }
        
        log("SpiffsSensorTypeDao: Getting all existing entities...");
        std::vector<SensorType> entities;
        getAll(entities);
        log("SpiffsSensorTypeDao: Found %d existing entities", entities.size());
        
        auto it = std::find_if(entities.begin(), entities.end(), [&uid](const SensorType& e) { return e.uid == uid; });
        if (it != entities.end()) {
            log("SpiffsSensorTypeDao: Updating existing sensor type");
            *it = entity;
        } else {
            log("SpiffsSensorTypeDao: Adding new sensor type");
            entities.push_back(entity);
        }
        
        bool result = saveAll(entities);
        log("SpiffsSensorTypeDao: Save operation %s", result ? "successful" : "failed");
        return result;
    }

    bool getById(const std::string& uid, SensorType& entity) override {
        log("SpiffsSensorTypeDao: Getting sensor type by UID: %s", uid.c_str());
        if (filePath.empty()) initialize();
        
        std::vector<SensorType> entities;
        if (!getAll(entities)) {
            log("SpiffsSensorTypeDao: Failed to get all sensor types");
            return false;
        }
        
        auto it = std::find_if(entities.begin(), entities.end(), [&uid](const SensorType& e) { return e.uid == uid; });
        if (it == entities.end()) {
            log("SpiffsSensorTypeDao: Sensor type not found");
            return false;
        }
        
        entity = *it;
        log("SpiffsSensorTypeDao: Sensor type found and retrieved");
        return true;
    }

    bool remove(const std::string& uid) override {
        log("SpiffsSensorTypeDao: Removing sensor type with UID: %s", uid.c_str());
        if (filePath.empty()) initialize();
        
        std::vector<SensorType> entities;
        if (!getAll(entities)) {
            log("SpiffsSensorTypeDao: Failed to get all sensor types for removal");
            return false;
        }
        
        auto it = std::find_if(entities.begin(), entities.end(), [&uid](const SensorType& e) { return e.uid == uid; });
        if (it == entities.end()) {
            log("SpiffsSensorTypeDao: Sensor type not found for removal");
            return false;
        }
        
        entities.erase(it);
        bool result = saveAll(entities);
        log("SpiffsSensorTypeDao: Remove operation %s", result ? "successful" : "failed");
        return result;
    }

    bool saveAll(const std::vector<SensorType>& entities) override {
        log("SpiffsSensorTypeDao: Saving %d sensor types", entities.size());
#ifdef ESP_PLATFORM
        log("SpiffsSensorTypeDao: Free heap before save: %d", ESP.getFreeHeap());
#endif
        
        if (filePath.empty()) {
            log("SpiffsSensorTypeDao: FilePath empty, initializing...");
            initialize();
        }
        
        log("SpiffsSensorTypeDao: Starting JSON serialization...");
        for (size_t i = 0; i < entities.size(); i++) {
            log("SpiffsSensorTypeDao: Processing entity %d - UID: %s, Name: %s", 
                i, entities[i].uid.c_str(), entities[i].name.c_str());
            log("SpiffsSensorTypeDao: Entity %d has %d sub-sensor types, %d modbus configs", 
                i, entities[i].sensorTypes.size(), entities[i].modbusRegisterConfig.size());
        }
        
        std::string jsonStr;
        try {
            log("SpiffsSensorTypeDao: Calling entitiesToJson...");
            jsonStr = entitiesToJson(entities);
            log("SpiffsSensorTypeDao: JSON serialization complete, length: %d", jsonStr.length());
#ifdef ESP_PLATFORM
            log("SpiffsSensorTypeDao: Free heap after JSON: %d", ESP.getFreeHeap());
#endif
        } catch (...) {
            log("SpiffsSensorTypeDao: ERROR - Exception in JSON serialization!");
            return false;
        }

        // Guard: if entities are non-empty but serialization produced "[]" or empty,
        // something went wrong — abort to avoid overwriting valid data with an empty file.
        if (!entities.empty() && (jsonStr.empty() || jsonStr == "[]")) {
            log("SpiffsSensorTypeDao: ERROR - JSON serialization produced empty result for %d entities, aborting write",
                static_cast<int>(entities.size()));
            return false;
        }
        
        log("SpiffsSensorTypeDao: Starting file write to: %s", filePath.c_str());
        try {
            bool writeOk = spiffsAdapter->RewriteToFile(filePath.c_str(), jsonStr.c_str());
#ifdef ESP_PLATFORM
            log("SpiffsSensorTypeDao: Free heap after file write: %d", ESP.getFreeHeap());
#endif
            if (!writeOk) {
                log("SpiffsSensorTypeDao: ERROR - File write failed for %s", filePath.c_str());
                return false;
            }
            log("SpiffsSensorTypeDao: File write complete");
        } catch (...) {
            log("SpiffsSensorTypeDao: ERROR - Exception in file write!");
            return false;
        }
        
        return true;
    }

    bool getAll(std::vector<SensorType>& entities) override {
        log("SpiffsSensorTypeDao: Getting all sensor types");
        if (filePath.empty()) initialize();
        
        std::string content = spiffsAdapter->readSPIFFSstr(filePath.c_str());
        if (content.length() == 0) {
            log("SpiffsSensorTypeDao: No content found in file");
            return false;
        }
        
        log("SpiffsSensorTypeDao: File content read, length: %d", content.length());
        bool result = jsonToEntities(content, entities);
        log("SpiffsSensorTypeDao: JSON deserialization %s, sensor types count: %d", result ? "successful" : "failed", entities.size());
        return result;
    }

    bool getSensorTypesByProtocol(SensorProtocol protocol, std::vector<SensorType>& sensorTypes) override {
        log("SpiffsSensorTypeDao: Getting sensor types by protocol: %d", static_cast<int>(protocol));
        bool result = filterSensorTypes([protocol](const SensorType& type) { return type.protocol == protocol; }, sensorTypes);
        log("SpiffsSensorTypeDao: Found %d sensor types for protocol", sensorTypes.size());
        return result;
    }

    bool getCalibrableSensorTypes(std::vector<SensorType>& sensorTypes) override {
        return filterSensorTypes([](const SensorType& type) { 
            return type.hasCalibratableSensorTypes(); 
        }, sensorTypes);
    }

    bool getSensorTypeByName(const std::string& name, SensorType& sensorType) override {
        std::vector<SensorType> allTypes;
        if (!getAll(allTypes)) {
            return false;
        }

        for (const auto& type : allTypes) {
            if (type.name == name) {
                sensorType = type;
                return true;
            }
        }

        return false;
    }
};
