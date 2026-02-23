#ifndef MOCK_SPIFFS_SENSOR_TYPE_DAO_H
#define MOCK_SPIFFS_SENSOR_TYPE_DAO_H

#include <string>
#include <vector>
#include <algorithm>
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorTypeDao.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"

class MockSpiffsSensorTypeDao : public ISpiffsSensorTypeDao {
private:
    std::vector<SensorType> storage;
    bool shouldFail = false;

public:
    void setShouldFail(bool fail) { shouldFail = fail; }
    
    void clearStorage() { storage.clear(); }
    
    void addToStorage(const SensorType& sensorType) {
        // Remove if exists, then add
        auto it = std::find_if(storage.begin(), storage.end(), 
            [&sensorType](const SensorType& s) { return s.uid == sensorType.uid; });
        if (it != storage.end()) {
            storage.erase(it);
        }
        storage.push_back(sensorType);
    }

    bool save(const SensorType& entity, const std::string& uid) override {
        if (shouldFail) return false;
        
        auto it = std::find_if(storage.begin(), storage.end(), 
            [&uid](const SensorType& s) { return s.uid == uid; });
        if (it != storage.end()) {
            *it = entity;
        } else {
            storage.push_back(entity);
        }
        return true;
    }

    bool getById(const std::string& uid, SensorType& entity) override {
        if (shouldFail) return false;
        
        auto it = std::find_if(storage.begin(), storage.end(), 
            [&uid](const SensorType& s) { return s.uid == uid; });
        if (it != storage.end()) {
            entity = *it;
            return true;
        }
        return false;
    }

    bool remove(const std::string& uid) override {
        if (shouldFail) return false;
        
        auto it = std::find_if(storage.begin(), storage.end(), 
            [&uid](const SensorType& s) { return s.uid == uid; });
        if (it != storage.end()) {
            storage.erase(it);
            return true;
        }
        return false;
    }

    bool saveAll(const std::vector<SensorType>& entities) override {
        if (shouldFail) return false;
        storage = entities;
        return true;
    }

    bool getAll(std::vector<SensorType>& entities) override {
        if (shouldFail) return false;
        entities = storage;
        return true;
    }

    bool getSensorTypesByProtocol(SensorProtocol protocol, std::vector<SensorType>& sensorTypes) override {
        if (shouldFail) return false;
        
        sensorTypes.clear();
        for (const auto& sensorType : storage) {
            if (sensorType.protocol == protocol) {
                sensorTypes.push_back(sensorType);
            }
        }
        return true;
    }

    bool getCalibrableSensorTypes(std::vector<SensorType>& sensorTypes) override {
        if (shouldFail) return false;
        
        sensorTypes.clear();
        for (const auto& sensorType : storage) {
            if (sensorType.hasCalibratableSensorTypes()) {
                sensorTypes.push_back(sensorType);
            }
        }
        return true;
    }

    bool getSensorTypeByName(const std::string& name, SensorType& sensorType) override {
        if (shouldFail) return false;
        
        for (const auto& type : storage) {
            if (type.name == name) {
                sensorType = type;
                return true;
            }
        }
        return false;
    }

    bool clear() override {
        if (shouldFail) return false;
        storage.clear();
        return true;
    }
};

#endif // MOCK_SPIFFS_SENSOR_TYPE_DAO_H