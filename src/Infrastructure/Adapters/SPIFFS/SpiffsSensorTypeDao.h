#pragma once

#include "ISpiffsSensorTypeDao.h"
#include "ISpiffsAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include <functional>
#include <algorithm>

class SpiffsSensorTypeDao : public ISpiffsSensorTypeDao {
protected:
    ISpiffsAdapter* spiffsAdapter;
    IJsonConverter* jsonConverter;
    std::string filePath;

    std::string getFileName() const {
        return "sensorTypes.json";
    }

    std::string getBasePath() const {
        return "/data";
    }

    std::string entityToJson(const SensorType& sensorType) {
        return jsonConverter->sensorTypeToJson(sensorType);
    }

    bool jsonToEntity(const std::string& jsonStr, SensorType& sensorType) {
        return jsonConverter->sensorTypeFromJson(jsonStr, sensorType);
    }

    std::string entitiesToJson(const std::vector<SensorType>& sensorTypes) {
        return jsonConverter->sensorTypesToJson(sensorTypes);
    }

    bool jsonToEntities(const std::string& jsonStr, std::vector<SensorType>& sensorTypes) {
        return jsonConverter->sensorTypesFromJson(jsonStr, sensorTypes);
    }

    void initialize() {
        std::string basePath = getBasePath();
        if (!spiffsAdapter->directoryExists(basePath.c_str())) {
            spiffsAdapter->createDirectory(basePath.c_str());
        }
        filePath = basePath + "/" + getFileName();
    }

    bool filterSensorTypes(const std::function<bool(const SensorType&)>& predicate, std::vector<SensorType>& sensorTypes) {
        std::vector<SensorType> allTypes;
        if (!getAll(allTypes)) {
            return false;
        }

        sensorTypes.clear();
        for (const auto& type : allTypes) {
            if (predicate(type)) {
                sensorTypes.push_back(type);
            }
        }

        return true;
    }

public:
    SpiffsSensorTypeDao(ISpiffsAdapter* spiffsAdapter, IJsonConverter* jsonConverter)
        : spiffsAdapter(spiffsAdapter), jsonConverter(jsonConverter) {
        if (!spiffsAdapter || !jsonConverter) {
            throw std::invalid_argument("Adapters cannot be null");
        }
        initialize();
    }

    // ISpiffsDao implementation using id instead of uid
    bool save(const SensorType& entity, const std::string& id) override {
        if (filePath.empty()) initialize();
        std::vector<SensorType> entities;
        getAll(entities);
        
        int entityId = std::stoi(id);
        auto it = std::find_if(entities.begin(), entities.end(), [entityId](const SensorType& e) { return e.id == entityId; });
        if (it != entities.end()) {
            *it = entity;
        } else {
            entities.push_back(entity);
        }
        return saveAll(entities);
    }

    bool getById(const std::string& id, SensorType& entity) override {
        if (filePath.empty()) initialize();
        std::vector<SensorType> entities;
        if (!getAll(entities)) return false;
        
        int entityId = std::stoi(id);
        auto it = std::find_if(entities.begin(), entities.end(), [entityId](const SensorType& e) { return e.id == entityId; });
        if (it == entities.end()) return false;
        entity = *it;
        return true;
    }

    bool remove(const std::string& id) override {
        if (filePath.empty()) initialize();
        std::vector<SensorType> entities;
        if (!getAll(entities)) return false;
        
        int entityId = std::stoi(id);
        auto it = std::find_if(entities.begin(), entities.end(), [entityId](const SensorType& e) { return e.id == entityId; });
        if (it == entities.end()) return false;
        entities.erase(it);
        return saveAll(entities);
    }

    bool saveAll(const std::vector<SensorType>& entities) override {
        if (filePath.empty()) initialize();
        std::string jsonStr = entitiesToJson(entities);
        spiffsAdapter->RewriteToFile(filePath.c_str(), jsonStr.c_str());
        return true;
    }

    bool getAll(std::vector<SensorType>& entities) override {
        if (filePath.empty()) initialize();
        String content = spiffsAdapter->readSPIFFSstr(filePath.c_str());
        if (content.length() == 0) return false;
        return jsonToEntities(content.c_str(), entities);
    }

    bool getSensorTypesByProtocol(SensorProtocol protocol, std::vector<SensorType>& sensorTypes) override {
        return filterSensorTypes([protocol](const SensorType& type) { return type.protocol == protocol; }, sensorTypes);
    }

    bool getCalibrableSensorTypes(std::vector<SensorType>& sensorTypes) override {
        return filterSensorTypes([](const SensorType& type) { return type.canCalibrate; }, sensorTypes);
    }

    bool getSensorTypeByName(const std::string& name, SensorType& sensorType) override {
        std::vector<SensorType> allTypes;
        if (!getAll(allTypes)) {
            return false;
        }

        for (const auto& type : allTypes) {
            if (type.name == name) {
                sensorType = type;
                return true;
            }
        }

        return false;
    }
};
