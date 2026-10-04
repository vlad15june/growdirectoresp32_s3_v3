#ifndef MOCK_SPIFFS_SENSOR_DAO_H
#define MOCK_SPIFFS_SENSOR_DAO_H

#include <string>
#include <vector>
#include <algorithm>
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorDao.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"

/**
 * @brief In-memory stand-in for the SPIFFS sensor DAO.
 *
 * Backs the real SensorRepository in tests that need the repository's caching behaviour
 * (invalidation, generations) without a filesystem underneath. Same shape as MockSpiffsRuleDao.
 */
class MockSpiffsSensorDao : public ISpiffsSensorDao {
private:
    std::vector<SensorEntity> storage;
    bool shouldFail = false;

public:
    void setShouldFail(bool fail) { shouldFail = fail; }
    void clearStorage() { storage.clear(); }
    const std::vector<SensorEntity>& getStorage() const { return storage; }

    // --- ISpiffsDao<SensorEntity> ------------------------------------------------------------

    bool save(const SensorEntity& entity, const std::string& uid) override {
        if (shouldFail) return false;
        auto it = std::find_if(storage.begin(), storage.end(),
            [&uid](const SensorEntity& s) { return s.uid == uid; });
        if (it != storage.end()) *it = entity; else storage.push_back(entity);
        return true;
    }

    bool getById(const std::string& uid, SensorEntity& entity) override {
        auto it = std::find_if(storage.begin(), storage.end(),
            [&uid](const SensorEntity& s) { return s.uid == uid; });
        if (it == storage.end()) return false;
        entity = *it;
        return true;
    }

    bool remove(const std::string& uid) override {
        auto it = std::find_if(storage.begin(), storage.end(),
            [&uid](const SensorEntity& s) { return s.uid == uid; });
        if (it == storage.end()) return false;
        storage.erase(it);
        return true;
    }

    bool saveAll(const std::vector<SensorEntity>& entities) override {
        if (shouldFail) return false;
        storage = entities;
        return true;
    }

    bool getAll(std::vector<SensorEntity>& entities) override {
        if (shouldFail) return false;
        entities = storage;
        return true;
    }

    bool clear() override {
        if (shouldFail) return false;
        storage.clear();
        return true;
    }

    // --- ISpiffsSensorDao --------------------------------------------------------------------

    bool saveSensor(const SensorEntity& sensor) override { return save(sensor, sensor.uid); }

    bool getSensor(const std::string& uid, SensorEntity& sensor) override { return getById(uid, sensor); }

    bool deleteSensor(const std::string& uid) override { return remove(uid); }

    bool getSensors(std::vector<SensorEntity>& sensors) override { return getAll(sensors); }

    bool saveSensors(const std::vector<SensorEntity>& sensors) override { return saveAll(sensors); }

    bool getSensorsByProtocol(const std::string& protocol, std::vector<SensorEntity>& sensors) override {
        if (shouldFail) return false;
        for (const auto& sensor : storage) {
            if (sensor.protocol == protocol) sensors.push_back(sensor);
        }
        return true;
    }

    bool getSensorsByType(const std::string& sensorType, std::vector<SensorEntity>& sensors) override {
        if (shouldFail) return false;
        for (const auto& sensor : storage) {
            if (sensor.sensorType == sensorType) sensors.push_back(sensor);
        }
        return true;
    }

    bool getSensorsByModel(const std::string& model, std::vector<SensorEntity>& sensors) override {
        if (shouldFail) return false;
        for (const auto& sensor : storage) {
            if (sensor.model == model) sensors.push_back(sensor);
        }
        return true;
    }

    bool getActiveSensors(std::vector<SensorEntity>& sensors) override {
        if (shouldFail) return false;
        for (const auto& sensor : storage) {
            if (sensor.isActive) sensors.push_back(sensor);
        }
        return true;
    }

    bool getSensorsWithErrors(std::vector<SensorEntity>& sensors) override {
        if (shouldFail) return false;
        for (const auto& sensor : storage) {
            if (sensor.isError) sensors.push_back(sensor);
        }
        return true;
    }
};

#endif // MOCK_SPIFFS_SENSOR_DAO_H
