#pragma once

#include "ISpiffsDao.h"
#include "ISpiffsAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include <string>
#include <vector>
#include <stdarg.h>

template <typename T>
class SpiffsDao : public ISpiffsDao<T> {
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

    // Methods to get path and file name
    virtual std::string getBasePath() const { return "/data"; }
    virtual std::string getFileName() const = 0;

    // Protected methods for JSON transformations
    virtual std::string entityToJson(const T& entity) = 0;
    virtual bool jsonToEntity(const std::string& jsonStr, T& entity) = 0;
    virtual std::string entitiesToJson(const std::vector<T>& entities) = 0;
    virtual bool jsonToEntities(const std::string& jsonStr, std::vector<T>& entities) = 0;

    void initialize() {
        log("SpiffsDao: Initializing for file: %s", getFileName().c_str());
        std::string basePath = getBasePath();
        if (!spiffsAdapter->directoryExists(basePath.c_str())) {
            log("SpiffsDao: Creating directory: %s", basePath.c_str());
            spiffsAdapter->createDirectory(basePath.c_str());
        }
        filePath = basePath + "/" + getFileName();
        log("SpiffsDao: File path set to: %s", filePath.c_str());
    }

public:
    SpiffsDao(ISpiffsAdapter* spiffsAdapter, IJsonConverter* jsonConverter, IDebugSystem* debugSystem = nullptr, ILogger* logger = nullptr)
        : spiffsAdapter(spiffsAdapter), jsonConverter(jsonConverter), debugSystem(debugSystem), logger(logger) {
        if (!spiffsAdapter || !jsonConverter) {
            throw std::invalid_argument("Adapters cannot be null");
        }
    }

    virtual ~SpiffsDao() = default;

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

    bool save(const T& entity, const std::string& uid) override {
        log("SpiffsDao: Saving entity with UID: %s", uid.c_str());
        if (filePath.empty()) initialize();
        
        std::vector<T> entities;
        getAll(entities);
        
        auto it = std::find_if(entities.begin(), entities.end(), [&uid](const T& e) { return e.uid == uid; });
        if (it != entities.end()) {
            log("SpiffsDao: Updating existing entity");
            *it = entity;
        } else {
            log("SpiffsDao: Adding new entity");
            entities.push_back(entity);
        }
        
        bool result = saveAll(entities);
        log("SpiffsDao: Save operation %s", result ? "successful" : "failed");
        return result;
    }

    bool getById(const std::string& uid, T& entity) override {
        log("SpiffsDao: Getting entity by UID: %s", uid.c_str());
        if (filePath.empty()) initialize();
        
        std::vector<T> entities;
        if (!getAll(entities)) {
            log("SpiffsDao: Failed to get all entities");
            return false;
        }
        
        auto it = std::find_if(entities.begin(), entities.end(), [&uid](const T& e) { return e.uid == uid; });
        if (it == entities.end()) {
            log("SpiffsDao: Entity not found");
            return false;
        }
        
        entity = *it;
        log("SpiffsDao: Entity found and retrieved");
        return true;
    }

    bool remove(const std::string& uid) override {
        log("SpiffsDao: Removing entity with UID: %s", uid.c_str());
        if (filePath.empty()) initialize();
        
        std::vector<T> entities;
        if (!getAll(entities)) {
            log("SpiffsDao: Failed to get all entities for removal");
            return false;
        }
        
        auto it = std::find_if(entities.begin(), entities.end(), [&uid](const T& e) { return e.uid == uid; });
        if (it == entities.end()) {
            log("SpiffsDao: Entity not found for removal");
            return false;
        }
        
        entities.erase(it);
        bool result = saveAll(entities);
        log("SpiffsDao: Remove operation %s", result ? "successful" : "failed");
        return result;
    }

    bool saveAll(const std::vector<T>& entities) override {
        log("SpiffsDao: Saving %d entities", entities.size());
        if (filePath.empty()) initialize();
        
        std::string jsonStr = entitiesToJson(entities);
        log("SpiffsDao: JSON serialization complete, length: %d", jsonStr.length());
        
        spiffsAdapter->RewriteToFile(filePath.c_str(), jsonStr.c_str());
        log("SpiffsDao: File write complete");
        return true;
    }

    bool getAll(std::vector<T>& entities) override {
        log("SpiffsDao: Getting all entities");
        if (filePath.empty()) initialize();
        
        std::string content = spiffsAdapter->readSPIFFSstr(filePath.c_str());
        if (content.length() == 0) {
            log("SpiffsDao: No content found in file");
            return false;
        }
        
        log("SpiffsDao: File content read, length: %d", content.length());
        bool result = jsonToEntities(content, entities);
        log("SpiffsDao: JSON deserialization %s, entities count: %d", result ? "successful" : "failed", entities.size());
        return result;
    }

    bool clear() override {
        log("SpiffsDao: Clearing all data (removing file)");
        if (filePath.empty()) initialize();
        
        // Just remove the file, no need to check if it exists first
        spiffsAdapter->RemoveFile(filePath.c_str());
        log("SpiffsDao: Clear operation completed");
        return true;
    }
};

// Force explicit template instantiation for Rule
#include "Core/Domain/Data/Entities/Rule.h"
template class SpiffsDao<Rule>;

// Force explicit template instantiation for SensorEntity
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
template class SpiffsDao<SensorEntity>;