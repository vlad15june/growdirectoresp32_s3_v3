#pragma once

#include "SpiffsDao.h"
#include "ISpiffsSensorTypeDao.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include <functional>

class SpiffsSensorTypeDao : public SpiffsDao<SensorType>, public ISpiffsSensorTypeDao {
protected:
    std::string getFileName() const override {
        return "sensorTypes.json";
    }

    std::string getBasePath() const override {
        return "/sensorTypes";
    }

    std::string entityToJson(const SensorType& sensorType) override {
        return jsonConverter->sensorTypeToJson(sensorType);
    }

    bool jsonToEntity(const std::string& jsonStr, SensorType& sensorType) override {
        return jsonConverter->sensorTypeFromJson(jsonStr, sensorType);
    }

    std::string entitiesToJson(const std::vector<SensorType>& sensorTypes) override {
        return jsonConverter->sensorTypesToJson(sensorTypes);
    }

    bool jsonToEntities(const std::string& jsonStr, std::vector<SensorType>& sensorTypes) override {
        return jsonConverter->sensorTypesFromJson(jsonStr, sensorTypes);
    }

    bool filterSensorTypes(const std::function<bool(const SensorType&)>& predicate, std::vector<SensorType>& sensorTypes) {
        std::vector<SensorType> allTypes;
        if (!SpiffsDao<SensorType>::getAll(allTypes)) {
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
        : SpiffsDao<SensorType>(spiffsAdapter, jsonConverter) {
        initialize();
    }

    bool save(const SensorType& entity, const std::string& uid) override {
        return SpiffsDao<SensorType>::save(entity, uid);
    }

    bool getById(const std::string& uid, SensorType& entity) override {
        return SpiffsDao<SensorType>::getById(uid, entity);
    }

    bool remove(const std::string& uid) override {
        return SpiffsDao<SensorType>::remove(uid);
    }

    bool saveAll(const std::vector<SensorType>& entities) override {
        return SpiffsDao<SensorType>::saveAll(entities);
    }

    bool getAll(std::vector<SensorType>& entities) override {
        return SpiffsDao<SensorType>::getAll(entities);
    }

    bool getSensorTypesByProtocol(SensorProtocol protocol, std::vector<SensorType>& sensorTypes) override {
        return filterSensorTypes([protocol](const SensorType& type) { return type.protocol == protocol; }, sensorTypes);
    }

    bool getCalibrableSensorTypes(std::vector<SensorType>& sensorTypes) override {
        return filterSensorTypes([](const SensorType& type) { return type.canCalibrate; }, sensorTypes);
    }

    bool getSensorTypeByName(const std::string& name, SensorType& sensorType) override {
        std::vector<SensorType> allTypes;
        if (!SpiffsDao<SensorType>::getAll(allTypes)) {
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
