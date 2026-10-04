#ifndef MOCK_SENSOR_REPOSITORY_H
#define MOCK_SENSOR_REPOSITORY_H

#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"

class MockSensorRepository : public ISensorRepository {
public:
    std::vector<SensorEntity> sensors;
    // Filled only by tests that drive the sensor read loop; empty by default, as before
    std::vector<SensorEntity> modbusSensors;

    // Call tracking for updateSensorStatus
    int updateSensorStatusCallCount = 0;
    std::string lastStatusUid;
    bool lastStatusIsActive = false;
    bool lastStatusIsError = false;

    int saveCallCount = 0;
    SensorEntity lastSavedSensor;
    int createOrUpdateSensorCallCount = 0;
    SensorEntity lastCreatedOrUpdatedSensor;
    int removeCallCount = 0;
    std::string lastRemovedUid;

    Utils::Optional<SensorEntity> get(const char* uid) override {
        for (const auto& sensor : sensors) {
            if (sensor.uid == uid) {
                return Utils::Optional<SensorEntity>(sensor);
            }
        }
        return Utils::Optional<SensorEntity>();
    }

    std::vector<SensorEntity> getAll() override { return sensors; }
    bool save(const SensorEntity& sensor) override {
        saveCallCount++;
        lastSavedSensor = sensor;
        return true;
    }
    int saveAll(const std::vector<SensorEntity>&) override { return 0; }
    // Actually erases, the way the real repository does: a caller that removes an entity and then
    // reads the registry back has to see it gone, or "was it deleted" cannot be asked at all.
    bool remove(const char* uid) override {
        removeCallCount++;
        lastRemovedUid = uid ? uid : "";
        if (uid == nullptr) {
            return false;
        }
        bool removed = false;
        for (size_t i = 0; i < sensors.size(); i++) {
            if (sensors[i].uid == uid) {
                sensors.erase(sensors.begin() + i);
                removed = true;
                break;
            }
        }
        for (size_t i = 0; i < modbusSensors.size(); i++) {
            if (modbusSensors[i].uid == uid) {
                modbusSensors.erase(modbusSensors.begin() + i);
                break;
            }
        }
        return removed;
    }
    int removeAll() override { return 0; }
    int count() override { return static_cast<int>(sensors.size()); }
    bool exists(const char* uid) override {
        for (const auto& sensor : sensors) {
            if (sensor.uid == uid) {
                return true;
            }
        }
        return false;
    }
    bool clear() override { return true; }

    std::vector<SensorEntity> getSensorsByProtocol(const char*) override { return {}; }
    std::vector<SensorEntity> getSensorsByType(const char*) override { return {}; }
    std::vector<SensorEntity> getSensorsByModel(const char*) override { return {}; }
    const std::vector<SensorEntity>& getActiveSensors() override { return sensors; }
    std::vector<SensorEntity> getSensorsWithErrors() override { return {}; }
    std::vector<SensorEntity> getModbusSensors() override { return modbusSensors; }
    std::vector<SensorEntity> getI2CSensors() override { return {}; }
    std::vector<SensorEntity> getAtlasSensors() override { return {}; }
    std::vector<SensorEntity> getMultiChannelSensors() override { return {}; }
    bool updateSensorStatus(const char* uid, bool isActive, bool isError) override {
        updateSensorStatusCallCount++;
        lastStatusUid = uid ? uid : "";
        lastStatusIsActive = isActive;
        lastStatusIsError = isError;

        // Actually store it, the way the real repository does. Without this the flag a caller
        // writes is invisible to the caller that reads the entity back, and behaviour that turns
        // on the sensor's error state - port recovery, above all - cannot be exercised at all.
        if (uid) {
            for (auto& sensor : sensors) {
                if (sensor.uid == uid) { sensor.isActive = isActive; sensor.isError = isError; }
            }
            for (auto& sensor : modbusSensors) {
                if (sensor.uid == uid) { sensor.isActive = isActive; sensor.isError = isError; }
            }
        }
        return true;
    }
    // true: createOrUpdateSensor() stores nothing and answers false, as the real repository does
    // when the flash refuses the write.
    bool refuseCreateOrUpdate = false;

    // Stores as well as counts, for the same reason remove() erases: a caller that saves an entity
    // and then plans its next step from the registry has to read back what it just wrote.
    bool createOrUpdateSensor(const SensorEntity& sensor) override {
        createOrUpdateSensorCallCount++;
        lastCreatedOrUpdatedSensor = sensor;
        if (refuseCreateOrUpdate) {
            return false;
        }
        for (auto& stored : sensors) {
            if (stored.uid == sensor.uid) {
                stored = sensor;
                return true;
            }
        }
        sensors.push_back(sensor);
        return true;
    }
    bool updateSensorStatusEnum(const char*, SensorStatusEnum) override { return true; }
    std::vector<SensorEntity> getSensorsByStatus(SensorStatusEnum) override { return {}; }
};

#endif // MOCK_SENSOR_REPOSITORY_H
