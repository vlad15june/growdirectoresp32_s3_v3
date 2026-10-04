#pragma once

#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorTypeDao.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <stdarg.h>

class SensorTypeRepository : public ISensorTypeRepository {
private:
    ISpiffsSensorTypeDao* sensorTypeDao;
    // The held catalog, as one immutable list every reader can share - see SensorTypeCatalog.h.
    // Never null; empty when the module holds no catalog. Replaced as a whole on every change, so
    // a reader holding a snapshot keeps reading the catalog it took.
    mutable SensorTypeCatalog cache;
    mutable bool isCacheValid;
    // Another task is writing storage; see setStorageWriteInFlight().
    bool storageWriteInFlight;
    ILogger* logger;
    IDebugSystem* debugSystem;

    void log(const char* format, ...) const {
        if (logger != nullptr &&
            (debugSystem == nullptr || debugSystem->isFeatureEnabled(DebugFeatures::REPOSITORY_DEBUG))) {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            logger->println(DebugFeatures::REPOSITORY_DEBUG, buffer);
        }
    }

    /**
     * @brief Let go of the held catalog
     *
     * The list is not emptied - it is let go of, and a fresh empty one takes its place, because a
     * reader may still be holding the old one. Skipped when nothing is held, so that a module whose
     * storage cannot be read does not allocate an empty list on every read attempt.
     */
    void releaseCache() const {
        if (!cache->empty()) {
            cache = std::make_shared<const std::vector<SensorType>>();
        }
    }

    /**
     * @brief One type per uid, keeping the first of a repeated one
     *
     * The catalog is addressed by uid by everything that reads it, so a stored file that carries
     * one uid twice must not make the module hold, count and scan the same type twice.
     */
    static void dropRepeatedUids(std::vector<SensorType>& sensorTypes) {
        for (size_t kept = 0; kept < sensorTypes.size(); kept++) {
            for (size_t i = sensorTypes.size(); i > kept + 1; i--) {
                if (sensorTypes[i - 1].uid == sensorTypes[kept].uid) {
                    sensorTypes.erase(sensorTypes.begin() + (i - 1));
                }
            }
        }
    }

    /**
     * @brief Load all sensor types into cache if not already loaded
     */
    void loadCache() const {
        // Not while another task is writing storage: what is held - nothing, when the cache is not
        // valid - is the answer until the write lands.
        if (!isCacheValid && !storageWriteInFlight) {
            log("SensorTypeRepository: Loading cache from storage");
            releaseCache();
            std::vector<SensorType> sensorTypes;
            if (sensorTypeDao->getAll(sensorTypes) && !sensorTypes.empty()) {
                dropRepeatedUids(sensorTypes);
                const size_t loaded = sensorTypes.size();
                // Taken over, not copied into place: what storage handed back becomes the held
                // catalog itself.
                cache = std::make_shared<const std::vector<SensorType>>(std::move(sensorTypes));
                log("SensorTypeRepository: Loaded %zu sensor types into cache", loaded);
                isCacheValid = true;
            } else {
                log("SensorTypeRepository: Failed to load sensor types from storage");
            }
        }
    }

public:
    ~SensorTypeRepository() noexcept override = default;

    SensorTypeRepository(ISpiffsSensorTypeDao* dao, IDebugSystem* debugSystem = nullptr, ILogger* logger = nullptr)
        : sensorTypeDao(dao), cache(std::make_shared<const std::vector<SensorType>>()),
          isCacheValid(false), storageWriteInFlight(false), logger(logger),
          debugSystem(debugSystem) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
        log("SensorTypeRepository: constructed");
    }

    /**
     * @brief Clear cache and mark as invalid
     */
    void invalidateCache() override {
        releaseCache();
        isCacheValid = false;
    }

    void setStorageWriteInFlight(bool inFlight) override {
        storageWriteInFlight = inFlight;
    }

    SensorTypeCatalog catalogSnapshot() override {
        loadCache();
        return cache;
    }

    Utils::Optional<SensorType> get(const char* uid) override {
        if (!uid) {
            log("SensorTypeRepository: get() called with null uid");
            return Utils::Optional<SensorType>();
        }
        loadCache();
        for (const auto& sensorType : *cache) {
            if (sensorType.uid == uid) {
                log("SensorTypeRepository: get() found uid=%s", uid);
                return Utils::Optional<SensorType>(sensorType);
            }
        }
        log("SensorTypeRepository: get() not found uid=%s", uid);
        return Utils::Optional<SensorType>();
    }

    std::vector<SensorType> getAll() override {
        loadCache();
        std::vector<SensorType> sensorTypes = *cache;
        log("SensorTypeRepository: getAll() returning %zu sensor types", sensorTypes.size());
        return sensorTypes;
    }

    void adoptStoredCatalog(std::vector<SensorType> catalog) override {
        // The catalog that arrives is the one being adopted, and the one being dropped is the one
        // this object holds: let go of first, so that the two are never both whole at once. What
        // arrives is then taken over as it stands - a move of the list, not a copy of the types.
        releaseCache();
        dropRepeatedUids(catalog);
        cache = std::make_shared<const std::vector<SensorType>>(std::move(catalog));
        // Empty is a state, not a failure: loadCache() leaves the cache invalid when storage holds
        // nothing, and a stored catalog with no types in it has to read back the same way.
        isCacheValid = !cache->empty();
        log("SensorTypeRepository: adoptStoredCatalog() holding %zu sensor types", cache->size());
    }

    bool save(const SensorType& entity) override {
        log("SensorTypeRepository: save() uid=%s name=%s", entity.uid.c_str(), entity.name.c_str());
        bool result = sensorTypeDao->save(entity, entity.uid);
        if (result) {
            log("SensorTypeRepository: save() succeeded uid=%s", entity.uid.c_str());
            invalidateCache();
        } else {
            log("SensorTypeRepository: save() failed uid=%s", entity.uid.c_str());
        }
        return result;
    }

    int saveAll(const std::vector<SensorType>& entities) override {
        log("SensorTypeRepository: saveAll() count=%zu", entities.size());
        bool result = sensorTypeDao->saveAll(entities);
        if (result) {
            log("SensorTypeRepository: saveAll() succeeded, saved %zu sensor types", entities.size());
            invalidateCache();
            return entities.size();
        }
        log("SensorTypeRepository: saveAll() failed (%s)", getStorageWriteErrorName(getLastSaveError()));
        return 0;
    }

    // GDV1-3827: pass the storage-level reason on, so the caller can log why nothing was written
    StorageWriteErrorEnum getLastSaveError() const override {
        return sensorTypeDao != nullptr ? sensorTypeDao->getLastSaveError() : STORAGE_WRITE_ERROR_NONE;
    }

    size_t getLastSaveBytes() const override {
        return sensorTypeDao != nullptr ? sensorTypeDao->getLastSaveBytes() : 0;
    }

    bool remove(const char* uid) override {
        if (!uid) {
            log("SensorTypeRepository: remove() called with null uid");
            return false;
        }
        log("SensorTypeRepository: remove() uid=%s", uid);
        bool result = sensorTypeDao->remove(uid);
        if (result) {
            log("SensorTypeRepository: remove() succeeded uid=%s", uid);
            invalidateCache();
        } else {
            log("SensorTypeRepository: remove() failed uid=%s", uid);
        }
        return result;
    }

    int removeAll() override {
        log("SensorTypeRepository: removeAll() called");
        std::vector<SensorType> empty;
        bool result = sensorTypeDao->saveAll(empty);
        if (result) {
            log("SensorTypeRepository: removeAll() succeeded");
            invalidateCache();
        } else {
            log("SensorTypeRepository: removeAll() failed");
        }
        return result ? 0 : -1;
    }

    int count() override {
        loadCache();
        int cnt = cache->size();
        log("SensorTypeRepository: count() = %d", cnt);
        return cnt;
    }

    bool exists(const char* uid) override {
        if (!uid) return false;
        loadCache();
        bool found = false;
        for (const auto& sensorType : *cache) {
            if (sensorType.uid == uid) {
                found = true;
                break;
            }
        }
        log("SensorTypeRepository: exists() uid=%s -> %s", uid, found ? "true" : "false");
        return found;
    }

    std::vector<SensorType> getSensorTypesByProtocol(SensorProtocol protocol) override {
        loadCache();
        std::vector<SensorType> filteredSensorTypes;
        for (const auto& sensorType : *cache) {
            if (sensorType.protocol == protocol) {
                filteredSensorTypes.push_back(sensorType);
            }
        }
        log("SensorTypeRepository: getSensorTypesByProtocol() protocol=%d found %zu types",
            static_cast<int>(protocol), filteredSensorTypes.size());
        return filteredSensorTypes;
    }

    std::vector<SensorType> getAnalogSensorTypes() override {
        return getSensorTypesByProtocol(SensorProtocol::ANALOG_PROTOCOL);
    }

    std::vector<SensorType> getI2CSensorTypes() override {
        return getSensorTypesByProtocol(SensorProtocol::I2C_PROTOCOL);
    }

    std::vector<SensorType> getModbusSensorTypes() override {
        return getSensorTypesByProtocol(SensorProtocol::MODBUS_PROTOCOL);
    }

    std::vector<SensorType> getCalibrableSensorTypes() override {
        std::vector<SensorType> sensorTypes;
        sensorTypeDao->getCalibrableSensorTypes(sensorTypes);
        return sensorTypes;
    }

    std::vector<SensorType> getCanScan() override {
        loadCache();
        std::vector<SensorType> result;
        for (const auto& sensorType : *cache) {
            if (sensorType.canScan) {
                result.push_back(sensorType);
            }
        }
        log("SensorTypeRepository: getCanScan() returning %zu sensor types", result.size());
        return result;
    }

    const SensorType* getSensorTypeByName(const std::string& name) override {
        loadCache();
        for (const auto& sensorType : *cache) {
            if (sensorType.name == name) {
                return &sensorType;
            }
        }
        return nullptr;
    }

    /**
     * @brief Manually refresh cache from storage
     */
    void refreshCache() {
        log("SensorTypeRepository: refreshCache() called");
        invalidateCache();
        loadCache();
        log("SensorTypeRepository: cache refreshed with %zu sensor types", cache->size());
    }

    /**
     * @brief Check if cache is currently valid
     * @return true if cache is valid, false otherwise
     */
    bool isCacheCurrent() const {
        return isCacheValid;
    }

    bool clear() override {
        log("SensorTypeRepository: clear() called");
        bool result = sensorTypeDao->clear();
        if (result) {
            log("SensorTypeRepository: clear() succeeded");
            invalidateCache();
        } else {
            log("SensorTypeRepository: clear() failed");
        }
        return result;
    }
};
