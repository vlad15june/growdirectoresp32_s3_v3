#pragma once

#include "Core/Domain/Interfaces/Repositories/IRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsDao.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <stdarg.h>

/**
 * @brief Generic repository implementation using SpiffsDao for data access
 * @tparam T Entity type
 */
template <typename T>
class Repository : public IRepository<T> {
private:
    ISpiffsDao<T>* dao;
    mutable std::unordered_map<std::string, T> cache;
    mutable bool isCacheValid;

protected:
    IDebugSystem *debugSystem;
    ILogger *logger;

    /**
     * @brief Log method for repository operations
     */
    void log(const char *format, ...) const {
        if (logger != nullptr && 
            (debugSystem == nullptr || debugSystem->isFeatureEnabled(DebugFeatures::REPOSITORY_DEBUG))) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            logger->println(DebugFeatures::REPOSITORY_DEBUG, buffer);
        }
    }

    /**
     * @brief Load all entities into cache if not already loaded
     */
    void loadCache() const {
        if (!isCacheValid) {
            #ifdef DEBUG_LOGGING_CODE_ON
            log("Repository: Loading cache from storage");
            #endif
            cache.clear();
            std::vector<T> entities;
            if (dao->getAll(entities)) {
                for (const auto& entity : entities) {
                    cache[entity.uid] = entity;
                }
            }
            isCacheValid = true;
        }
    }

    /**
     * @brief Clear cache and mark as invalid
     */
    void invalidateCache() const {
        cache.clear();
        isCacheValid = false;
    }

public:
    /**
     * @brief Construct repository with SpiffsDao implementation
     * @param dao Data access object for entity type T
     * @param debugSystem Debug system for logging (optional)
     * @param logger Logger for output (optional)
     */
    Repository(ISpiffsDao<T>* dao, IDebugSystem *debugSystem = nullptr, ILogger *logger = nullptr) 
        : dao(dao), isCacheValid(false), debugSystem(debugSystem), logger(logger) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
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

    /**
     * @brief Get entity by unique identifier
     * @param uid Unique identifier of the entity
     * @return Entity if found, nullopt otherwise
     */
    Utils::Optional<T> get(const char* uid) override {
        if (!uid) return Utils::nullopt<T>();
        
        log("Repository: Getting entity with UID: %s", uid);
        loadCache();
        auto it = cache.find(uid);
        if (it != cache.end()) {
            log("Repository: Entity found in cache");
            return Utils::Optional<T>(it->second);
        }
        log("Repository: Entity not found");
        return Utils::nullopt<T>();
    }

    /**
     * @brief Get all entities
     * @return Vector of all entities
     */
    std::vector<T> getAll() override {
        loadCache();
        std::vector<T> entities;
        entities.reserve(cache.size());
        for (const auto& pair : cache) {
            entities.push_back(pair.second);
        }
        return entities;
    }

    /**
     * @brief Save entity (create or update)
     * @param entity Entity to save
     * @return true if operation was successful, false otherwise
     */
    bool save(const T& entity) override {
        log("Repository: Saving entity with UID: %s", entity.uid.c_str());
        bool result = dao->save(entity, entity.uid);
        if (result) {
            log("Repository: Entity saved successfully, invalidating cache");
            invalidateCache();
        } else {
            log("Repository: Failed to save entity");
        }
        return result;
    }

    /**
     * @brief Save multiple entities (create or update)
     * @param entities Vector of entities to save
     * @return Number of successfully saved entities
     */
    int saveAll(const std::vector<T>& entities) override {
        bool result = dao->saveAll(entities);
        if (result) {
            invalidateCache();
            return entities.size();
        }
        return 0;
    }

    /**
     * @brief Delete entity by unique identifier
     * @param uid Unique identifier of the entity to delete
     * @return true if operation was successful, false otherwise
     */
    bool remove(const char* uid) override {
        log("Repository: Removing entity with UID: %s", uid);
        bool result = dao->remove(uid);
        if (result) {
            log("Repository: Entity removed successfully, invalidating cache");
            invalidateCache();
        } else {
            log("Repository: Failed to remove entity");
        }
        return result;
    }

    /**
     * @brief Delete all entities
     * @return Number of deleted entities
     */
    int removeAll() override {
        int count = this->count();
        dao->saveAll(std::vector<T>());  // Save empty vector to clear all entities
        invalidateCache();
        return count;
    }

    /**
     * @brief Count total number of entities
     * @return Number of entities
     */
    int count() override {
        loadCache();
        int entityCount = cache.size();
        log("Repository: Entity count: %d", entityCount);
        return entityCount;
    }

    /**
     * @brief Check if entity with given UID exists
     * @param uid Unique identifier to check
     * @return true if entity exists, false otherwise
     */
    bool exists(const char* uid) override {
        if (!uid) return false;
        loadCache();
        return cache.find(uid) != cache.end();
    }

    /**
     * @brief Clear all data from storage (remove file)
     * @return true if operation was successful, false otherwise
     */
    bool clear() override {
        log("Repository: Clearing all data from storage");
        bool result = dao->clear();
        if (result) {
            log("Repository: Data cleared successfully, invalidating cache");
            invalidateCache();
        } else {
            log("Repository: Failed to clear data");
        }
        return result;
    }

    /**
     * @brief Get the underlying DAO object
     * @return Pointer to the DAO object
     */
    ISpiffsDao<T>* getDao() {
        return dao;
    }

    /**
     * @brief Manually refresh cache from storage
     */
    void refreshCache() {
        log("Repository: Manually refreshing cache");
        invalidateCache();
        loadCache();
    }

    /**
     * @brief Check if cache is currently valid
     * @return true if cache is valid, false otherwise
     */
    bool isCacheCurrent() const {
        return isCacheValid;
    }
};
