#pragma once

#include "Repository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorDao.h"
#include <string>
#include <vector>

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
     * @param debugSystem Debug system for logging (optional)
     * @param logger Logger for output (optional)
     */
    SensorRepository(ISpiffsSensorDao* dao, IDebugSystem *debugSystem = nullptr, ILogger *logger = nullptr) 
        : Repository<SensorEntity>(dao, debugSystem, logger), sensorDao(dao) {
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

        std::vector<SensorEntity> allSensors = Repository<SensorEntity>::getAll();
        std::vector<SensorEntity> sensors;

        for (const auto& sensor : allSensors) {
            if (sensor.protocol == protocol) {
                sensors.push_back(sensor);
            }
        }

        return sensors;
    }

    std::vector<SensorEntity> getSensorsByType(const char* sensorType) override {
        if (!sensorType) return {};

        std::vector<SensorEntity> allSensors = Repository<SensorEntity>::getAll();
        std::vector<SensorEntity> sensors;

        for (const auto& sensor : allSensors) {
            if (sensor.sensorType == sensorType) {
                sensors.push_back(sensor);
            }
        }

        return sensors;
    }

    std::vector<SensorEntity> getSensorsByModel(const char* model) override {
        if (!model) return {};

        std::vector<SensorEntity> allSensors = Repository<SensorEntity>::getAll();
        std::vector<SensorEntity> sensors;

        for (const auto& sensor : allSensors) {
            if (sensor.model == model) {
                sensors.push_back(sensor);
            }
        }

        return sensors;
    }

    std::vector<SensorEntity> getActiveSensors() override {
        std::vector<SensorEntity> allSensors = Repository<SensorEntity>::getAll();
        std::vector<SensorEntity> sensors;

        for (const auto& sensor : allSensors) {
            if (sensor.isActive) {
                sensors.push_back(sensor);
            }
        }

        return sensors;
    }

    std::vector<SensorEntity> getSensorsWithErrors() override {
        std::vector<SensorEntity> allSensors = Repository<SensorEntity>::getAll();
        std::vector<SensorEntity> sensors;

        for (const auto& sensor : allSensors) {
            if (sensor.isError) {
                sensors.push_back(sensor);
            }
        }

        return sensors;
    }

    std::vector<SensorEntity> getModbusSensors() override {
        return getSensorsByProtocol("ModBus");
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

        auto sensorOpt = Repository<SensorEntity>::get(uid);
        if (!sensorOpt.has_value()) {
            return false;
        }

        SensorEntity sensor = sensorOpt.value();
        
        if (sensor.isActive == isActive && sensor.isError == isError) {
            return true;
        }
        
        sensor.isActive = isActive;
        sensor.isError = isError;
        
        return Repository<SensorEntity>::save(sensor);
    }

    bool createOrUpdateSensor(const SensorEntity& sensor) override {
        return Repository<SensorEntity>::save(sensor);
    }

    bool updateSensorStatusEnum(const char* uid, SensorStatusEnum status) override {
        if (!uid) return false;

        auto sensorOpt = Repository<SensorEntity>::get(uid);
        if (!sensorOpt.has_value()) {
            return false;
        }

        SensorEntity sensor = sensorOpt.value();
        
        if (sensor.status == status) {
            return true;
        }
        
        sensor.status = status;
        
        return Repository<SensorEntity>::save(sensor);
    }

    std::vector<SensorEntity> getSensorsByStatus(SensorStatusEnum status) override {
        std::vector<SensorEntity> allSensors = Repository<SensorEntity>::getAll();
        std::vector<SensorEntity> filteredSensors;

        for (const auto& sensor : allSensors) {
            if (sensor.status == status) {
                filteredSensors.push_back(sensor);
            }
        }

        return filteredSensors;
    }
};
