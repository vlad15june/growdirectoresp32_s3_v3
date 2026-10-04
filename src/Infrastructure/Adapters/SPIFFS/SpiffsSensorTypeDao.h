#pragma once

#include "ISpiffsSensorTypeDao.h"
#include "ISpiffsAdapter.h"
#include "ISpiffsFileWriter.h"
#include "SpiffsFileReplacer.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Enums/StorageWriteErrorEnum.h"
#include <string>
#include <memory>
#include <stdarg.h>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <algorithm>

class SpiffsSensorTypeDao : public ISpiffsSensorTypeDao {
protected:
    ISpiffsAdapter* spiffsAdapter;
    IJsonConverter* jsonConverter;
    IDebugSystem* debugSystem;
    ILogger* logger;
    SpiffsFileReplacer fileReplacer;
    std::string filePath;
    StorageWriteErrorEnum lastSaveError = STORAGE_WRITE_ERROR_NONE;
    size_t lastSaveBytes = 0;

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

    /**
     * @brief Stream the JSON array of entities into an open writer (GDV1-3827)
     *
     * One entity is serialized at a time, so the peak heap request is the largest single sensor type
     * (~4 KB) instead of one contiguous block for the whole catalog (~28 KB for 14 types). A failure
     * is recorded in lastSaveError and reported to the caller instead of degrading into "[]".
     */
    bool writeEntitiesToWriter(ISpiffsFileWriter& writer, const std::vector<SensorType>& entities) {
        if (!writer.write("[")) {
            lastSaveError = STORAGE_WRITE_ERROR_WRITE;
            return false;
        }

        for (size_t i = 0; i < entities.size(); i++) {
            std::string entityJson;
            try {
                entityJson = entityToJson(entities[i]);
            } catch (...) {
                log("SpiffsSensorTypeDao: ERROR - Exception serializing sensor type %s", entities[i].uid.c_str());
                lastSaveError = STORAGE_WRITE_ERROR_SERIALIZATION;
                return false;
            }

            if (entityJson.empty() || entityJson == "{}") {
                log("SpiffsSensorTypeDao: ERROR - Empty JSON for sensor type %s, aborting write",
                    entities[i].uid.c_str());
                lastSaveError = STORAGE_WRITE_ERROR_SERIALIZATION;
                return false;
            }

            if (i > 0 && !writer.write(",")) {
                lastSaveError = STORAGE_WRITE_ERROR_WRITE;
                return false;
            }
            if (!writer.write(entityJson.c_str())) {
                lastSaveError = STORAGE_WRITE_ERROR_WRITE;
                return false;
            }
        }

        if (!writer.write("]")) {
            lastSaveError = STORAGE_WRITE_ERROR_WRITE;
            return false;
        }

        return true;
    }

    bool jsonToEntities(const std::string& jsonStr, std::vector<SensorType>& sensorTypes) {
        // Unchanged for the same reason as SpiffsRuleDao: true has always meant "the file holds
        // at least one sensor type", and getAll() below passes that on as its own result.
        size_t skipped = 0;
        const bool read = jsonConverter->sensorTypesFromJson(jsonStr, sensorTypes, skipped) == JsonListParse::Ok;

        // A stored model that cannot be read is left out, and the catalog is that much shorter for
        // the rest of the module's life. The result says "the file was read"; only this says what
        // it cost.
        if (skipped > 0) {
            log("SpiffsSensorTypeDao: WARNING - %d of %d stored sensor types could not be read and were skipped",
                static_cast<int>(skipped), static_cast<int>(skipped + sensorTypes.size()));
        }

        return read;
    }

    /**
     * @brief Whether this payload is a whole catalog rather than the beginning of one (GDV1-3827)
     *
     * The streaming write ends with "]", the same end-of-stream marker the writer relies on, so a
     * payload that does not end with it never reached its end and is not a catalog. Beyond that the
     * content is judged exactly the way a normal read judges the stored file: it has to parse into at
     * least one sensor type.
     */
    bool isCompleteCatalog(const std::string& content, std::vector<SensorType>& entities) {
        size_t lastChar = content.find_last_not_of(" \t\r\n");
        if (lastChar == std::string::npos || content[lastChar] != ']') {
            return false;
        }
        return jsonToEntities(content, entities);
    }

    /**
     * @brief Take the catalog from a leftover companion file when the catalog file is gone (GDV1-3827)
     *
     * Replacing the stored catalog is a sequence of renames (SpiffsFileReplacer), so a power cut in
     * the middle of it leaves the complete catalog under `<file>.tmp` (the new one) or `<file>.bak`
     * (the previous one) with `<file>` itself missing. This is the only thing that heals that state:
     * the newer candidate wins, it gets its proper name back, and the older one is dropped.
     */
    bool recoverFromCompanionFile(std::vector<SensorType>& entities) {
        const std::string candidates[] = {
            fileReplacer.tempPathFor(filePath),   // the catalog that was being published
            fileReplacer.backupPathFor(filePath)  // the catalog it was replacing
        };

        for (const std::string& candidate : candidates) {
            std::string content = spiffsAdapter->readSPIFFSstr(candidate.c_str());
            if (content.empty()) {
                continue;
            }

            entities.clear();
            if (!isCompleteCatalog(content, entities)) {
                log("SpiffsSensorTypeDao: %s is not a complete catalog, ignoring it", candidate.c_str());
                entities.clear();
                continue;
            }

            log("SpiffsSensorTypeDao: Recovered %d sensor types from %s",
                static_cast<int>(entities.size()), candidate.c_str());
            if (fileReplacer.promote(candidate, filePath)) {
                // Whatever is left of the interrupted replacement describes an older state.
                fileReplacer.removeCompanions(filePath);
            }
            return true;
        }

        return false;
    }

    /**
     * @brief Work out where the catalog lives; every entry point calls it on first use.
     *
     * Not called from the constructor: this DAO is a member of the dependency graph, which is
     * built before the file system is mounted, so anything it asks of storage there fails.
     *
     * No directory is made: SPIFFS is flat, mkdir() on it always answers ENOTSUP, and the base
     * path is simply the first part of the file's name.
     */
    void initialize() {
        log("SpiffsSensorTypeDao: Initializing");
        std::string basePath = getBasePath();
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
        : spiffsAdapter(spiffsAdapter), jsonConverter(jsonConverter), debugSystem(debugSystem), logger(logger),
          fileReplacer(spiffsAdapter, logger, debugSystem) {
        if (!spiffsAdapter || !jsonConverter) {
            throw std::invalid_argument("Adapters cannot be null");
        }
    }

    bool clear() override {
        log("SpiffsSensorTypeDao: Clearing all data (removing file)");
        if (filePath.empty()) initialize();
        spiffsAdapter->removeFile(filePath.c_str());
        // An empty catalog has to stay empty: a leftover companion file would otherwise be adopted
        // as the catalog by the recovery in getAll() (GDV1-3827).
        fileReplacer.removeCompanions(filePath);
        log("SpiffsSensorTypeDao: Clear operation completed");
        return true;
    }

    /**
     * @brief Set debug system for logging
     */
    void setDebugSystem(IDebugSystem *debugSystemInstance) {
        debugSystem = debugSystemInstance;
        fileReplacer.setDebugSystem(debugSystemInstance);
    }

    /**
     * @brief Set logger for output
     */
    void setLogger(ILogger *loggerInstance) {
        logger = loggerInstance;
        fileReplacer.setLogger(loggerInstance);
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

    /**
     * @brief Write the whole catalog to storage, one sensor type at a time (GDV1-3827)
     *
     * The file is opened once and the JSON is streamed into it, so nothing bigger than a single
     * sensor type has to fit into one heap block. The stream goes to a temporary file that replaces
     * the stored catalog only when everything was written, therefore a failure at any point leaves
     * the previous catalog intact. Why the write failed is kept in getLastSaveError() so the caller
     * can log it — a refusal is no longer indistinguishable from an empty catalog.
     */
    bool saveAll(const std::vector<SensorType>& entities) override {
        log("SpiffsSensorTypeDao: Saving %d sensor types", entities.size());
#ifdef ESP_PLATFORM
        log("SpiffsSensorTypeDao: Free heap before save: %d", ESP.getFreeHeap());
#endif

        lastSaveError = STORAGE_WRITE_ERROR_NONE;
        lastSaveBytes = 0;

        if (filePath.empty()) {
            log("SpiffsSensorTypeDao: FilePath empty, initializing...");
            initialize();
        }

        log("SpiffsSensorTypeDao: Starting streaming write to: %s", filePath.c_str());
        std::unique_ptr<ISpiffsFileWriter> writer = spiffsAdapter->openForRewrite(filePath.c_str());
        if (!writer) {
            log("SpiffsSensorTypeDao: ERROR - Could not open %s for writing", filePath.c_str());
            lastSaveError = STORAGE_WRITE_ERROR_OPEN;
            return false;
        }

        bool streamed = false;
        try {
            streamed = writeEntitiesToWriter(*writer, entities);
        } catch (...) {
            log("SpiffsSensorTypeDao: ERROR - Exception during streaming write!");
            lastSaveError = STORAGE_WRITE_ERROR_SERIALIZATION;
            streamed = false;
        }

        lastSaveBytes = writer->bytesWritten();

        if (!streamed) {
            log("SpiffsSensorTypeDao: ERROR - Write of %d sensor types failed after %d bytes (%s), %s kept",
                static_cast<int>(entities.size()), static_cast<int>(lastSaveBytes),
                getStorageWriteErrorName(lastSaveError), filePath.c_str());
            writer->abort();
            return false;
        }

        if (!writer->commit()) {
            log("SpiffsSensorTypeDao: ERROR - File write failed for %s", filePath.c_str());
            lastSaveError = STORAGE_WRITE_ERROR_COMMIT;
            return false;
        }

#ifdef ESP_PLATFORM
        log("SpiffsSensorTypeDao: Free heap after file write: %d", ESP.getFreeHeap());
#endif
        log("SpiffsSensorTypeDao: File write complete, %d bytes for %d sensor types",
            static_cast<int>(lastSaveBytes), static_cast<int>(entities.size()));
        return true;
    }

    StorageWriteErrorEnum getLastSaveError() const override {
        return lastSaveError;
    }

    size_t getLastSaveBytes() const override {
        return lastSaveBytes;
    }

    bool getAll(std::vector<SensorType>& entities) override {
        log("SpiffsSensorTypeDao: Getting all sensor types");
        if (filePath.empty()) initialize();

        std::string content = spiffsAdapter->readSPIFFSstr(filePath.c_str());
        if (content.length() == 0) {
            log("SpiffsSensorTypeDao: No content found in file");
            return recoverFromCompanionFile(entities);
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
