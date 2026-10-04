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
        return "/data";
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
        // The result keeps its meaning - "the file holds at least one sensor" - which getAll()
        // passes straight on as its own. Only the counts below say what reading it cost.
        size_t skipped = 0;
        size_t refused = 0;
        const bool read = jsonConverter->sensorsFromJson(jsonStr, sensors, skipped, refused);

        // A stored sensor that cannot be read is left out, and the module runs one sensor short
        // until the registry is rewritten. A sensor past the module's capacity is one the registry
        // has no slot for. Both are silent otherwise: the module simply comes up with fewer
        // sensors than it stored.
        if (skipped > 0) {
            log("SpiffsSensorDao: WARNING - %d of %d stored sensors could not be read and were skipped",
                static_cast<int>(skipped), static_cast<int>(skipped + refused + sensors.size()));
        }
        if (refused > 0) {
            log("SpiffsSensorDao: WARNING - %d stored sensors past the capacity of %d were refused",
                static_cast<int>(refused), static_cast<int>(SensorLimits::MAX_SENSORS_PER_MODULE));
        }

        return read;
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
     * @param debugSystem Debug system for logging (optional)
     * @param logger Logger for output (optional)
     */
    SpiffsSensorDao(ISpiffsAdapter* spiffsAdapter, IJsonConverter* jsonConverter, IDebugSystem* debugSystem = nullptr, ILogger* logger = nullptr)
        : SpiffsDao<SensorEntity>(spiffsAdapter, jsonConverter, debugSystem, logger) {
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

    StoredRead readAll(std::vector<SensorEntity>& entities) override {
        return SpiffsDao<SensorEntity>::readAll(entities);
    }

    bool clear() override {
        return SpiffsDao<SensorEntity>::clear();
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
