#pragma once

#include "Repository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorDao.h"
#include <string>
#include <vector>
#include <algorithm>

/**
 * @brief Implementation of sensor repository using SPIFFS storage
 */
class SensorRepository : public Repository<SensorEntity>, public ISensorRepository {

private:
    ISpiffsSensorDao* sensorDao;

public:
    ~SensorRepository() noexcept override = default;

    /**
     * @brief Constructor for SensorRepository
     * @param dao Data access object for SensorEntity entities
     */
    SensorRepository(ISpiffsSensorDao* dao) 
        : Repository<SensorEntity>(dao), sensorDao(dao) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
    }

    // IRepository<SensorEntity> implementations 
    Utils::Optional<SensorEntity> get(const char* uid) override {
        return Repository<SensorEntity>::get(uid);
    }

    std::vector<SensorEntity> getAll() override {
        return Repository<SensorEntity>::getAll();
    }

    bool save(const SensorEntity& entity) override {
        return Repository<SensorEntity>::save(entity);
    }

    int saveAll(const std::vector<SensorEntity>& entities) override {
        return Repository<SensorEntity>::saveAll(entities);
    }

    bool remove(const char* uid) override {
        return Repository<SensorEntity>::remove(uid);
    }

    int removeAll() override {
        return Repository<SensorEntity>::removeAll();
    }

    int count() override {
        return Repository<SensorEntity>::count();
    }

    bool exists(const char* uid) override {
        return Repository<SensorEntity>::exists(uid);
    }

    // ISensorRepository implementations
    std::vector<SensorEntity> getSensorsByProtocol(const char* protocol) override {
        if (!protocol) return {};

        std::vector<SensorEntity> sensors;
        sensorDao->getSensorsByProtocol(protocol, sensors);
        return sensors;
    }

    std::vector<SensorEntity> getSensorsByType(const char* sensorType) override {
        if (!sensorType) return {};

        std::vector<SensorEntity> sensors;
        sensorDao->getSensorsByType(sensorType, sensors);
        return sensors;
    }

    std::vector<SensorEntity> getSensorsByModel(const char* model) override {
        if (!model) return {};

        std::vector<SensorEntity> sensors;
        sensorDao->getSensorsByModel(model, sensors);
        return sensors;
    }

    std::vector<SensorEntity> getActiveSensors() override {
        std::vector<SensorEntity> sensors;
        sensorDao->getActiveSensors(sensors);
        return sensors;
    }

    std::vector<SensorEntity> getSensorsWithErrors() override {
        std::vector<SensorEntity> sensors;
        sensorDao->getSensorsWithErrors(sensors);
        return sensors;
    }

    std::vector<SensorEntity> getModbusSensors() override {
        return getSensorsByProtocol("RS485");
    }

    std::vector<SensorEntity> getI2CSensors() override {
        return getSensorsByProtocol("I2C");
    }

    std::vector<SensorEntity> getAtlasSensors() override {
        std::vector<SensorEntity> allSensors = Repository<SensorEntity>::getAll();
        std::vector<SensorEntity> atlasSensors;

        for (const auto& sensor : allSensors) {
            if (sensor.isAtlasSensor()) {
                atlasSensors.push_back(sensor);
            }
        }

        return atlasSensors;
    }

    std::vector<SensorEntity> getMultiChannelSensors() override {
        std::vector<SensorEntity> allSensors = Repository<SensorEntity>::getAll();
        std::vector<SensorEntity> multiChannelSensors;

        for (const auto& sensor : allSensors) {
            if (sensor.isMultiChannel()) {
                multiChannelSensors.push_back(sensor);
            }
        }

        return multiChannelSensors;
    }

    bool updateSensorStatus(const char* uid, bool isActive, bool isError) override {
        if (!uid) return false;

        SensorEntity sensor;
        if (!sensorDao->getSensor(uid, sensor)) {
            return false;
        }

        sensor.isActive = isActive;
        sensor.isError = isError;
        
        return Repository<SensorEntity>::save(sensor);
    }

    bool updateSensorValue(const char* uid, float value, uint32_t readTime) override {
        if (!uid) return false;

        SensorEntity sensor;
        if (!sensorDao->getSensor(uid, sensor)) {
            return false;
        }

        sensor.currentValue = value;
        sensor.lastReadTime = readTime;
        
        return Repository<SensorEntity>::save(sensor);
    }

    bool updateChannelValue(const char* uid, const char* channelName, float value) override {
        if (!uid || !channelName) return false;

        SensorEntity sensor;
        if (!sensorDao->getSensor(uid, sensor)) {
            return false;
        }

        sensor.setChannelValue(channelName, value);
        
        return Repository<SensorEntity>::save(sensor);
    }
};
