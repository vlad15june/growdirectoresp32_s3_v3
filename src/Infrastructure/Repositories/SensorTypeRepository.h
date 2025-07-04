#pragma once

#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorTypeDao.h"
#include <string>
#include <vector>
#include <unordered_map>

class SensorTypeRepository : public ISensorTypeRepository {
private:
    ISpiffsSensorTypeDao* sensorTypeDao;
    mutable std::unordered_map<std::string, SensorType> cache;
    mutable bool isCacheValid;

    /**
     * @brief Load all sensor types into cache if not already loaded
     */
    void loadCache() const {
        if (!isCacheValid) {
            cache.clear();
            std::vector<SensorType> sensorTypes;
            if (sensorTypeDao->getAll(sensorTypes)) {
                for (const auto& sensorType : sensorTypes) {
                    cache[std::to_string(sensorType.id)] = sensorType;
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
    ~SensorTypeRepository() noexcept override = default;

    SensorTypeRepository(ISpiffsSensorTypeDao* dao) 
        : sensorTypeDao(dao), isCacheValid(false) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
    }

    Utils::Optional<SensorType> get(const char* id) override {
        if (!id) return Utils::Optional<SensorType>();
        
        loadCache();
        auto it = cache.find(id);
        if (it != cache.end()) {
            return Utils::Optional<SensorType>(it->second);
        }
        return Utils::Optional<SensorType>();
    }

    std::vector<SensorType> getAll() override {
        loadCache();
        std::vector<SensorType> sensorTypes;
        sensorTypes.reserve(cache.size());
        for (const auto& pair : cache) {
            sensorTypes.push_back(pair.second);
        }
        return sensorTypes;
    }

    bool save(const SensorType& entity) override {
        bool result = sensorTypeDao->save(entity, std::to_string(entity.id));
        if (result) {
            invalidateCache();
        }
        return result;
    }

    int saveAll(const std::vector<SensorType>& entities) override {
        bool result = sensorTypeDao->saveAll(entities);
        if (result) {
            invalidateCache();
            return entities.size();
        }
        return 0;
    }

    bool remove(const char* id) override {
        if (!id) return false;
        bool result = sensorTypeDao->remove(id);
        if (result) {
            invalidateCache();
        }
        return result;
    }

    int removeAll() override {
        std::vector<SensorType> empty;
        bool result = sensorTypeDao->saveAll(empty);
        if (result) {
            invalidateCache();
        }
        return result ? 0 : -1;
    }

    int count() override {
        loadCache();
        return cache.size();
    }

    bool exists(const char* id) override {
        if (!id) return false;
        loadCache();
        return cache.find(id) != cache.end();
    }

    std::vector<SensorType> getSensorTypesByProtocol(SensorProtocol protocol) override {
        loadCache();
        std::vector<SensorType> filteredSensorTypes;
        for (const auto& pair : cache) {
            if (pair.second.protocol == protocol) {
                filteredSensorTypes.push_back(pair.second);
            }
        }
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
