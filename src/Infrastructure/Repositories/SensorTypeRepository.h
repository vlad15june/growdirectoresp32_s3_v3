#pragma once

#include "Repository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorTypeDao.h"
#include <string>
#include <vector>
#include <algorithm>

class SensorTypeRepository : public Repository<SensorType>, public ISensorTypeRepository {
private:
    ISpiffsSensorTypeDao* sensorTypeDao;

public:
    ~SensorTypeRepository() noexcept override = default;

    SensorTypeRepository(ISpiffsSensorTypeDao* dao) 
        : Repository<SensorType>(dao), sensorTypeDao(dao) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
    }

    Utils::Optional<SensorType> get(const char* uid) override {
        return Repository<SensorType>::get(uid);
    }

    std::vector<SensorType> getAll() override {
        return Repository<SensorType>::getAll();
    }

    bool save(const SensorType& entity) override {
        return Repository<SensorType>::save(entity);
    }

    int saveAll(const std::vector<SensorType>& entities) override {
        return Repository<SensorType>::saveAll(entities);
    }

    bool remove(const char* uid) override {
        return Repository<SensorType>::remove(uid);
    }

    int removeAll() override {
        return Repository<SensorType>::removeAll();
    }

    int count() override {
        return Repository<SensorType>::count();
    }

    bool exists(const char* uid) override {
        return Repository<SensorType>::exists(uid);
    }

    std::vector<SensorType> getSensorTypesByProtocol(SensorProtocol protocol) override {
        std::vector<SensorType> allTypes = Repository<SensorType>::getAll();
        std::vector<SensorType> filteredTypes;

        for (const auto& type : allTypes) {
            if (type.protocol == protocol) {
                filteredTypes.push_back(type);
            }
        }

        return filteredTypes;
    }

    std::vector<SensorType> getAnalogSensorTypes() override {
        return getSensorTypesByProtocol(SensorProtocol::ANALOG_PROTOCOL);
    }

    std::vector<SensorType> getI2CSensorTypes() override {
        return getSensorTypesByProtocol(SensorProtocol::I2C_PROTOCOL);
    }

    std::vector<SensorType> getModbusSensorTypes() override {
        return getSensorTypesByProtocol(SensorProtocol::MODBUS_PROTOCOL);
    }

    std::vector<SensorType> getCalibrableSensorTypes() override {
        std::vector<SensorType> allTypes = Repository<SensorType>::getAll();
        std::vector<SensorType> calibrableTypes;

        for (const auto& type : allTypes) {
            if (type.canCalibrate) {
                calibrableTypes.push_back(type);
            }
        }

        return calibrableTypes;
    }

    SensorType* getSensorTypeByName(const std::string& name) override {
        std::vector<SensorType> allTypes = Repository<SensorType>::getAll();
        
        for (auto& type : allTypes) {
            if (type.name == name) {
                return &type;
            }
        }
        
        return nullptr;
    }
};
