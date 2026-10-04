#pragma once

#include "Core/Domain/Interfaces/Repositories/IRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsDao.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include <cstdint>
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
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
public:
    /**
     * @brief How long a cache a failed read left is kept before the next access reads storage
     *        again, in ms.
     *
     * The stored sensors are read on the bus once a sweep, fifteen seconds
     * (SensorAcquisitionService::SWEEP_INTERVAL_MS, SensorService::SENSOR_READ_INTERVAL), and a
     * reading - the input of every sensor-driven rule - moves no more often. Read again on that
     * step, a registry that did not come back costs the module at most one sweep of readings, and
     * a rule file that did not come back leaves a relay in the state its rule left it for at most
     * one step longer than the rule would have. The price is one read of the file per step for as
     * long as storage does not answer, and nothing while it does.
     */
    static const unsigned long FAILED_READ_RETRY_MS = 15000;

private:
    ISpiffsDao<T>* dao;
    mutable std::unordered_map<std::string, T> cache;
    // The cached entities in the order storage holds them. A write of one entity is built out of
    // the cache, and this is what lays it out the way the file already is: an entity that is
    // updated keeps its place and a new one goes last. The pointers are into the map, whose
    // elements stay where they are for as long as they are in it; both are filled and emptied
    // together.
    mutable std::vector<const T*> storageOrder;
    mutable bool isCacheValid;
    // The cache holds what a read that did not bring back what is stored left in it: nothing.
    // Readers get that until the next load - the first access FAILED_READ_RETRY_MS after the read
    // (retryAFailedReadWhenDue()), or a write, which loads first; a write is not built from it
    // (cacheMayBeWritten()).
    mutable bool cacheFromFailedRead = false;
    // How many entities this repository last read from storage or stored there, or -1 before it
    // has done either. A store this repository has put entities into does not come back holding
    // nothing, whatever the read of it says.
    mutable long storedCount = -1;
    // What tells when a cache a failed read left is let go of (FAILED_READ_RETRY_MS). Without it
    // such a cache stays until a write reads storage again (cacheMayBeWritten()).
    IArduinoAdapter* clock;
    // When the last read that did not bring back what is stored was made, and how many such reads
    // came one after another.
    mutable unsigned long failedReadAtMs = 0;
    mutable uint32_t failedReadsInARow = 0;
    // Bumped on every invalidation. Derived views (filtered lists kept by subclasses) compare
    // their recorded generation against this to know whether their copy is still current,
    // without touching the entities themselves.
    mutable uint32_t cacheGeneration;

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
     * @brief Let go of a cache a failed read left once FAILED_READ_RETRY_MS has passed since that
     *        read, so that the access asking reads storage again.
     *
     * Asked on every load and every generation(), and the readers on the control loop ask one or
     * the other all the time - the active sensors on every iteration, the rule revision on every
     * evaluation tick - so the stored entities come back within an iteration or a tick of the step,
     * whether or not anything is written in between. The generation moves with the release, and the
     * views derived from the cache are taken again.
     */
    void retryAFailedReadWhenDue() const {
        if (!isCacheValid || !cacheFromFailedRead || clock == nullptr) {
            return;
        }
        if (clock->millis() - failedReadAtMs < FAILED_READ_RETRY_MS) {
            return;
        }
        invalidateCache();
    }

    /**
     * @brief Keep count of the reads that did not bring back what is stored, and say so.
     *
     * One line per such read and one when storage answers again: the file each read was of is on
     * the DAO's line just before.
     */
    void noteTheReadOutcome() const {
        if (cacheFromFailedRead) {
            ++failedReadsInARow;
            failedReadAtMs = (clock != nullptr) ? clock->millis() : 0;
            if (clock != nullptr) {
                log("Repository: Stored entities did not come back whole (%u in a row), read again in %lu ms",
                    static_cast<unsigned>(failedReadsInARow), FAILED_READ_RETRY_MS);
            } else {
                log("Repository: Stored entities did not come back whole (%u in a row), read again on a write",
                    static_cast<unsigned>(failedReadsInARow));
            }
        } else if (failedReadsInARow > 0) {
            log("Repository: Stored entities came back after %u failed read(s)",
                static_cast<unsigned>(failedReadsInARow));
            failedReadsInARow = 0;
        }
    }

    /**
     * @brief Load all entities into cache if not already loaded
     */
    void loadCache() const {
        retryAFailedReadWhenDue();
        if (!isCacheValid) {
            #ifdef DEBUG_LOGGING_CODE_ON
            log("Repository: Loading cache from storage");
            #endif
            cache.clear();
            storageOrder.clear();
            std::vector<T> entities;
            StoredRead outcome = dao->readAll(entities);
            if (outcome != StoredRead::Read && storedCount > 0) {
                outcome = StoredRead::Unreadable;
            }
            cacheFromFailedRead = (outcome == StoredRead::Unreadable);
            noteTheReadOutcome();
            if (outcome == StoredRead::Read) {
                storageOrder.reserve(entities.size());
                for (const auto& entity : entities) {
                    auto placed = cache.emplace(entity.uid, entity);
                    if (placed.second) {
                        storageOrder.push_back(&placed.first->second);
                    } else {
                        placed.first->second = entity;
                    }
                }
                storedCount = static_cast<long>(cache.size());
            } else if (outcome == StoredRead::Nothing) {
                storedCount = 0;
            }
            isCacheValid = true;
        }
    }

    /**
     * @brief Whether a write may be built from the cache.
     *
     * Not when the cache was filled by a read that did not bring back what is stored: a write of
     * one entity built from it would replace everything stored with that entity. Such a cache is
     * loaded again first - the read that failed may have been one the file system could not serve
     * at that moment - and the write is refused only when the second read fails as well.
     */
    bool cacheMayBeWritten() const {
        loadCache();
        if (cacheFromFailedRead) {
            invalidateCache();
            loadCache();
        }
        if (cacheFromFailedRead) {
            log("Repository: Stored entities did not come back whole, not writing over them");
            return false;
        }
        return true;
    }

    /**
     * @brief The entities this repository holds with `entity` put in: what saving it stores.
     */
    std::vector<T> heldEntitiesWith(const T& entity) const {
        loadCache();
        std::vector<T> entities;
        entities.reserve(storageOrder.size() + 1);
        bool placed = false;
        for (const T* held : storageOrder) {
            if (held->uid == entity.uid) {
                entities.push_back(entity);
                placed = true;
            } else {
                entities.push_back(*held);
            }
        }
        if (!placed) {
            entities.push_back(entity);
        }
        return entities;
    }

    /**
     * @brief The entities this repository holds without the one under `uid`: what removing it stores.
     */
    std::vector<T> heldEntitiesWithout(const std::string& uid) const {
        loadCache();
        std::vector<T> entities;
        entities.reserve(storageOrder.size());
        for (const T* held : storageOrder) {
            if (held->uid != uid) {
                entities.push_back(*held);
            }
        }
        return entities;
    }

    /**
     * @brief Clear cache and mark as invalid
     *
     * Every change made through this repository funnels through here (save, saveAll, remove,
     * removeAll, clear, refreshCache), which is what makes the generation counter a complete
     * change signal for derived views - complete for repository writes only. A write straight
     * into the DAO behind the repository's back moves neither the cache nor the generation;
     * refreshCache() is the way to pick such a change up, and without it the next save() or
     * remove() stores what this repository holds over it.
     */
    void invalidateCache() const {
        storageOrder.clear();
        cache.clear();
        isCacheValid = false;
        ++cacheGeneration;
    }

    /**
     * @brief All entities, loaded, by reference - for subclasses building filtered views
     *        without copying the registry.
     */
    const std::unordered_map<std::string, T>& loadedEntities() const {
        loadCache();
        return cache;
    }

public:
    /**
     * @brief Construct repository with SpiffsDao implementation
     * @param dao Data access object for entity type T
     * @param debugSystem Debug system for logging (optional)
     * @param logger Logger for output (optional)
     * @param clock What a failed read is retried by (optional; see FAILED_READ_RETRY_MS)
     */
    Repository(ISpiffsDao<T>* dao, IDebugSystem *debugSystem = nullptr, ILogger *logger = nullptr,
               IArduinoAdapter* clock = nullptr)
        : dao(dao), isCacheValid(false), clock(clock), cacheGeneration(0), debugSystem(debugSystem),
          logger(logger) {
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
     *
     * The file is rewritten whole, and what goes into it is what this repository holds with the
     * entity put in - not the file read back first. A read that fails, or finds the file cut
     * short, yields fewer entities than the module holds, and a write built from it would store
     * that loss.
     *
     * The cache is dropped once the write has succeeded and is read back from the file on the next
     * access, because the stored form is what a restart reads and not every value survives it as
     * it was handed in (a rule's ERROR state is stored as Unsynchronized). A write that failed
     * leaves the cache as it was.
     *
     * @param entity Entity to save
     * @return true if operation was successful, false otherwise
     */
    bool save(const T& entity) override {
        log("Repository: Saving entity with UID: %s", entity.uid.c_str());
        if (!cacheMayBeWritten()) {
            return false;
        }
        std::vector<T> entities = heldEntitiesWith(entity);
        bool result = dao->saveAll(entities);
        if (result) {
            log("Repository: Entity saved successfully, invalidating cache");
            storedCount = static_cast<long>(entities.size());
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
            storedCount = static_cast<long>(entities.size());
            invalidateCache();
            return entities.size();
        }
        return 0;
    }

    /**
     * @brief Delete entity by unique identifier
     *
     * Written the way save() writes: what this repository holds, without the entity.
     *
     * @param uid Unique identifier of the entity to delete
     * @return true if operation was successful, false otherwise (also when no such entity is held)
     */
    bool remove(const char* uid) override {
        if (!uid) return false;
        log("Repository: Removing entity with UID: %s", uid);
        if (!cacheMayBeWritten()) {
            return false;
        }
        if (cache.find(uid) == cache.end()) {
            log("Repository: Entity not found for removal");
            return false;
        }
        std::vector<T> entities = heldEntitiesWithout(uid);
        bool result = dao->saveAll(entities);
        if (result) {
            log("Repository: Entity removed successfully, invalidating cache");
            storedCount = static_cast<long>(entities.size());
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
        if (dao->saveAll(std::vector<T>())) {  // Save empty vector to clear all entities
            storedCount = 0;
        }
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
            storedCount = 0;
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
        // A refresh is how a change made behind this repository's back is picked up, an emptied
        // store included: what storage says is taken as it is.
        storedCount = -1;
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

    /**
     * @brief Whether what this repository holds is what a read that did not bring back what is
     *        stored left - nothing - rather than the stored entities.
     *
     * Loads first, and a failed read that is due to be retried is retried here, so the answer is
     * as current as the retry step allows.
     */
    bool storedEntitiesUnread() const {
        loadCache();
        return cacheFromFailedRead;
    }

    /**
     * @brief Generation of the stored data: changes exactly when the stored entities may have.
     *
     * A caller that recorded the generation along with data derived from the registry can tell
     * with one integer comparison whether that data is still current - the cheap question the
     * control loop asks every iteration. A cache a failed read left is let go of here once its
     * retry is due, which moves the generation: storage may hold something again.
     */
    uint32_t generation() const {
        retryAFailedReadWhenDue();
        return cacheGeneration;
    }
};
