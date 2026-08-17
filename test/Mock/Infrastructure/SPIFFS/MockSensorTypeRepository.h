#ifndef MOCK_SENSOR_TYPE_REPOSITORY_H
#define MOCK_SENSOR_TYPE_REPOSITORY_H

#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Enums/StorageWriteErrorEnum.h"
#include "Core/Utils/Optional.h"
#include <vector>
#include <algorithm>
#include <string>

class MockSensorTypeRepository : public ISensorTypeRepository {
private:
    std::vector<SensorType> sensorTypes;
    bool shouldReturnEmpty;
    // GDV1-3827: let a test drive a refused bulk write and the reason reported for it
    bool saveAllShouldFail = false;
    StorageWriteErrorEnum lastSaveError = STORAGE_WRITE_ERROR_NONE;
    size_t lastSaveBytes = 0;

public:
    // Call tracking
    bool getCalled;
    bool getAllCalled;
    bool saveCalled;
    bool saveAllCalled;
    bool removeCalled;
    bool removeAllCalled;
    bool countCalled;
    bool existsCalled;
    bool clearCalled;
    int saveCallCount;
    std::string lastGetUid;
    std::string lastSavedUid;
    std::string lastRemoveUid;

    MockSensorTypeRepository() {
        reset();
    }

    void reset() {
        sensorTypes.clear();
        shouldReturnEmpty = false;
        saveAllShouldFail = false;
        lastSaveError = STORAGE_WRITE_ERROR_NONE;
        lastSaveBytes = 0;
        getCalled = false;
        getAllCalled = false;
        saveCalled = false;
        saveAllCalled = false;
        removeCalled = false;
        removeAllCalled = false;
        countCalled = false;
        existsCalled = false;
        clearCalled = false;
        saveCallCount = 0;
        lastGetUid = "";
        lastSavedUid = "";
        lastRemoveUid = "";
    }

    // Test helper methods
    void addSensorType(const SensorType& sensorType) {
        sensorTypes.push_back(sensorType);
    }

    void setShouldReturnEmpty(bool value) {
        shouldReturnEmpty = value;
    }

    void setSaveAllShouldFail(bool value) {
        saveAllShouldFail = value;
    }

    std::vector<SensorType> getSensorTypes() const {
        return sensorTypes;
    }

    // IRepository<SensorType> implementation
    Utils::Optional<SensorType> get(const char* uid) override {
        getCalled = true;
        lastGetUid = uid ? uid : "";
        
        if (shouldReturnEmpty) {
            return Utils::Optional<SensorType>();
        }

        for (const auto& sensorType : sensorTypes) {
            if (sensorType.uid == uid) {
                return Utils::Optional<SensorType>(sensorType);
            }
        }
        return Utils::Optional<SensorType>();
    }

    std::vector<SensorType> getAll() override {
        getAllCalled = true;
        if (shouldReturnEmpty) {
            return std::vector<SensorType>();
        }
        return sensorTypes;
    }

    bool save(const SensorType& sensorType) override {
        saveCalled = true;
        saveCallCount++;
        lastSavedUid = sensorType.uid;

        // Remove existing with same uid
        auto it = std::remove_if(sensorTypes.begin(), sensorTypes.end(),
            [&sensorType](const SensorType& st) {
                return st.uid == sensorType.uid;
            });
        sensorTypes.erase(it, sensorTypes.end());

        // Add new
        sensorTypes.push_back(sensorType);
        return true;
    }

    int saveAll(const std::vector<SensorType>& entities) override {
        saveAllCalled = true;
        if (saveAllShouldFail) {
            lastSaveError = STORAGE_WRITE_ERROR_SERIALIZATION;
            lastSaveBytes = 0;
            return 0;
        }
        // The real saveAll rewrites /data/sensorTypes.json in full and does not merge.
        sensorTypes.clear();
        int count = 0;
        for (const auto& entity : entities) {
            if (save(entity)) {
                count++;
            }
        }
        lastSaveError = STORAGE_WRITE_ERROR_NONE;
        lastSaveBytes = entities.size() * 100; // stand-in for the streamed byte count
        return count;
    }

    StorageWriteErrorEnum getLastSaveError() const override { return lastSaveError; }

    size_t getLastSaveBytes() const override { return lastSaveBytes; }

    bool remove(const char* uid) override {
        removeCalled = true;
        lastRemoveUid = uid ? uid : "";

        auto it = std::remove_if(sensorTypes.begin(), sensorTypes.end(),
            [uid](const SensorType& st) {
                return st.uid == uid;
            });
        
        bool found = it != sensorTypes.end();
        sensorTypes.erase(it, sensorTypes.end());
        return found;
    }

    int removeAll() override {
        removeAllCalled = true;
        int count = sensorTypes.size();
        sensorTypes.clear();
        return count;
    }

    int count() override {
        countCalled = true;
        return sensorTypes.size();
    }

    bool exists(const char* uid) override {
        existsCalled = true;
        for (const auto& sensorType : sensorTypes) {
            if (sensorType.uid == uid) {
                return true;
            }
        }
        return false;
    }

    // ISensorTypeRepository specific methods
    std::vector<SensorType> getSensorTypesByProtocol(SensorProtocol protocol) override {
        std::vector<SensorType> result;
        for (const auto& sensorType : sensorTypes) {
            if (sensorType.protocol == protocol) {
                result.push_back(sensorType);
            }
        }
        return result;
    }

    std::vector<SensorType> getAnalogSensorTypes() override {
        std::vector<SensorType> result;
        for (const auto& sensorType : sensorTypes) {
            for (const auto& subType : sensorType.sensorTypes) {
                if (subType.isAnalog) {
                    result.push_back(sensorType);
                    break;
                }
            }
        }
        return result;
    }

    std::vector<SensorType> getI2CSensorTypes() override {
        return getSensorTypesByProtocol(SensorProtocol::I2C_PROTOCOL);
    }

    std::vector<SensorType> getModbusSensorTypes() override {
        return getSensorTypesByProtocol(SensorProtocol::MODBUS_PROTOCOL);
    }

    std::vector<SensorType> getCalibrableSensorTypes() override {
        std::vector<SensorType> result;
        for (const auto& sensorType : sensorTypes) {
            for (const auto& subType : sensorType.sensorTypes) {
                if (subType.canCalibrate) {
                    result.push_back(sensorType);
                    break;
                }
            }
        }
        return result;
    }

    std::vector<SensorType> getCanScan() override {
        std::vector<SensorType> result;
        for (const auto& sensorType : sensorTypes) {
            if (sensorType.canScan) {
                result.push_back(sensorType);
            }
        }
        return result;
    }

    SensorType* getSensorTypeByName(const std::string& name) override {
        for (auto& sensorType : sensorTypes) {
            if (sensorType.name == name) {
                return &sensorType;
            }
        }
        return nullptr;
    }

    bool clear() override {
        clearCalled = true;
        sensorTypes.clear();
        return true;
    }
};

#endif // MOCK_SENSOR_TYPE_REPOSITORY_H
