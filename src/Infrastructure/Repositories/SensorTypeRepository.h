#pragma once

#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorTypeDao.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <stdarg.h>

class SensorTypeRepository : public ISensorTypeRepository {
private:
    ISpiffsSensorTypeDao* sensorTypeDao;
    mutable std::unordered_map<std::string, SensorType> cache;
    mutable bool isCacheValid;
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
     * @brief Load all sensor types into cache if not already loaded
     */
    void loadCache() const {
        if (!isCacheValid) {
            log("SensorTypeRepository: Loading cache from storage");
            cache.clear();
            std::vector<SensorType> sensorTypes;
            if (sensorTypeDao->getAll(sensorTypes) && !sensorTypes.empty()) {
                for (const auto& sensorType : sensorTypes) {
                    cache[sensorType.uid] = sensorType;
                }
                log("SensorTypeRepository: Loaded %zu sensor types into cache", sensorTypes.size());
                isCacheValid = true;
            } else {
                log("SensorTypeRepository: Failed to load sensor types from storage");
            }
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
    ~SensorTypeRepository() noexcept override = default;

    SensorTypeRepository(ISpiffsSensorTypeDao* dao, IDebugSystem* debugSystem = nullptr, ILogger* logger = nullptr)
        : sensorTypeDao(dao), isCacheValid(false), logger(logger), debugSystem(debugSystem) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
        log("SensorTypeRepository: constructed");
    }

    Utils::Optional<SensorType> get(const char* uid) override {
        if (!uid) {
            log("SensorTypeRepository: get() called with null uid");
            return Utils::Optional<SensorType>();
        }
        loadCache();
        auto it = cache.find(uid);
        if (it != cache.end()) {
            log("SensorTypeRepository: get() found uid=%s", uid);
            return Utils::Optional<SensorType>(it->second);
        }
        log("SensorTypeRepository: get() not found uid=%s", uid);
        return Utils::Optional<SensorType>();
    }

    std::vector<SensorType> getAll() override {
        loadCache();
        std::vector<SensorType> sensorTypes;
        sensorTypes.reserve(cache.size());
        for (const auto& pair : cache) {
            sensorTypes.push_back(pair.second);
        }
        log("SensorTypeRepository: getAll() returning %zu sensor types", sensorTypes.size());
        return sensorTypes;
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
        int cnt = cache.size();
        log("SensorTypeRepository: count() = %d", cnt);
        return cnt;
    }

    bool exists(const char* uid) override {
        if (!uid) return false;
        loadCache();
        bool found = cache.find(uid) != cache.end();
        log("SensorTypeRepository: exists() uid=%s -> %s", uid, found ? "true" : "false");
        return found;
    }

    std::vector<SensorType> getSensorTypesByProtocol(SensorProtocol protocol) override {
        loadCache();
        std::vector<SensorType> filteredSensorTypes;
        for (const auto& pair : cache) {
            if (pair.second.protocol == protocol) {
                filteredSensorTypes.push_back(pair.second);
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
        for (const auto& pair : cache) {
            if (pair.second.canScan) {
                result.push_back(pair.second);
            }
        }
        log("SensorTypeRepository: getCanScan() returning %zu sensor types", result.size());
        return result;
    }

    SensorType* getSensorTypeByName(const std::string& name) override {
        loadCache();
        for (auto& pair : cache) {
            if (pair.second.name == name) {
                return &pair.second;
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
        log("SensorTypeRepository: cache refreshed with %zu sensor types", cache.size());
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
