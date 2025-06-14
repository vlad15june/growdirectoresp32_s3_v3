#pragma once

#include "SpiffsDao.h"
#include "ISpiffsSensorDao.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include <functional>

/**
 * @brief Implementation of Sensor-specific DAO using SPIFFS storage
 */
class SpiffsSensorDao : public SpiffsDao<SensorEntity>, public ISpiffsSensorDao {
protected:
    // Implementation of abstract methods from SpiffsDao
    std::string getFileName() const override {
        return "sensors.json";
    }

    std::string getBasePath() const override {
        return "/sensors";
    }

    // JSON conversion methods
    std::string entityToJson(const SensorEntity& sensor) override {
        return jsonConverter->sensorToJson(sensor);
    }

    bool jsonToEntity(const std::string& jsonStr, SensorEntity& sensor) override {
        return jsonConverter->sensorFromJson(jsonStr, sensor);
    }

    std::string entitiesToJson(const std::vector<SensorEntity>& sensors) override {
        return jsonConverter->sensorsToJson(sensors);
    }

    bool jsonToEntities(const std::string& jsonStr, std::vector<SensorEntity>& sensors) override {
        return jsonConverter->sensorsFromJson(jsonStr, sensors);
    }

    bool filterSensors(const std::function<bool(const SensorEntity&)>& predicate, std::vector<SensorEntity>& sensors) {
        std::vector<SensorEntity> allSensors;
        if (!SpiffsDao<SensorEntity>::getAll(allSensors)) {
            return false;
        }

        sensors.clear();
        for (const auto& sensor : allSensors) {
            if (predicate(sensor)) {
                sensors.push_back(sensor);
            }
        }

        return true;
    }

public:
    /**
     * @brief Constructor for SpiffsSensorDao
     * @param spiffsAdapter SPIFFS adapter for file operations
     * @param jsonConverter JSON converter for serialization/deserialization
     */
    SpiffsSensorDao(ISpiffsAdapter* spiffsAdapter, IJsonConverter* jsonConverter)
        : SpiffsDao<SensorEntity>(spiffsAdapter, jsonConverter) {
        initialize(); // Initialize after object creation
    }

    // Implementation of ISpiffsDao methods - needed to resolve ambiguity
    bool save(const SensorEntity& entity, const std::string& uid) override {
        return SpiffsDao<SensorEntity>::save(entity, uid);
    }

    bool getById(const std::string& uid, SensorEntity& entity) override {
        return SpiffsDao<SensorEntity>::getById(uid, entity);
    }

    bool remove(const std::string& uid) override {
        return SpiffsDao<SensorEntity>::remove(uid);
    }

    bool saveAll(const std::vector<SensorEntity>& entities) override {
        return SpiffsDao<SensorEntity>::saveAll(entities);
    }

    bool getAll(std::vector<SensorEntity>& entities) override {
        return SpiffsDao<SensorEntity>::getAll(entities);
    }

    // Implementation of ISpiffsSensorDao methods
    bool saveSensor(const SensorEntity& sensor) override {
        if (sensor.uid.empty()) return false;
        return SpiffsDao<SensorEntity>::save(sensor, sensor.uid);
    }

    bool getSensor(const std::string& uid, SensorEntity& sensor) override {
        return SpiffsDao<SensorEntity>::getById(uid, sensor);
    }

    bool deleteSensor(const std::string& uid) override {
        return SpiffsDao<SensorEntity>::remove(uid);
    }

    bool getSensors(std::vector<SensorEntity>& sensors) override {
        return SpiffsDao<SensorEntity>::getAll(sensors);
    }

    bool saveSensors(const std::vector<SensorEntity>& sensors) override {
        return SpiffsDao<SensorEntity>::saveAll(sensors);
    }

    bool getSensorsByProtocol(const std::string& protocol, std::vector<SensorEntity>& sensors) override {
        return filterSensors([&protocol](const SensorEntity& sensor) { return sensor.protocol == protocol; }, sensors);
    }

    bool getSensorsByType(const std::string& sensorType, std::vector<SensorEntity>& sensors) override {
        return filterSensors([&sensorType](const SensorEntity& sensor) { return sensor.sensorType == sensorType; }, sensors);
    }

    bool getSensorsByModel(const std::string& model, std::vector<SensorEntity>& sensors) override {
        return filterSensors([&model](const SensorEntity& sensor) { return sensor.model == model; }, sensors);
    }

    bool getActiveSensors(std::vector<SensorEntity>& sensors) override {
        return filterSensors([](const SensorEntity& sensor) { return sensor.isActive; }, sensors);
    }

    bool getSensorsWithErrors(std::vector<SensorEntity>& sensors) override {
        return filterSensors([](const SensorEntity& sensor) { return sensor.isError; }, sensors);
    }
};
