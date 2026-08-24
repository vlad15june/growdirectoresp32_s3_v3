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
    bool remove(const char*) override { return true; }
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
    std::vector<SensorEntity> getActiveSensors() override { return sensors; }
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
        return true;
    }
    bool createOrUpdateSensor(const SensorEntity& sensor) override {
        createOrUpdateSensorCallCount++;
        lastCreatedOrUpdatedSensor = sensor;
        return true;
    }
    bool updateSensorStatusEnum(const char*, SensorStatusEnum) override { return true; }
    std::vector<SensorEntity> getSensorsByStatus(SensorStatusEnum) override { return {}; }
};

#endif // MOCK_SENSOR_REPOSITORY_H
