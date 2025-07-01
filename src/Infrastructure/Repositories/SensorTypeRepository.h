#pragma once

#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorTypeDao.h"
#include <string>
#include <vector>

class SensorTypeRepository : public ISensorTypeRepository {
private:
    ISpiffsSensorTypeDao* sensorTypeDao;

public:
    ~SensorTypeRepository() noexcept override = default;

    SensorTypeRepository(ISpiffsSensorTypeDao* dao) 
        : sensorTypeDao(dao) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
    }

    Utils::Optional<SensorType> get(const char* id) override {
        if (!id) return Utils::Optional<SensorType>();
        
        SensorType sensorType;
        if (sensorTypeDao->getById(id, sensorType)) {
            return Utils::Optional<SensorType>(sensorType);
        }
        return Utils::Optional<SensorType>();
    }

    std::vector<SensorType> getAll() override {
        std::vector<SensorType> sensorTypes;
        sensorTypeDao->getAll(sensorTypes);
        return sensorTypes;
    }

    bool save(const SensorType& entity) override {
        return sensorTypeDao->save(entity, std::to_string(entity.id));
    }

    int saveAll(const std::vector<SensorType>& entities) override {
        return sensorTypeDao->saveAll(entities) ? entities.size() : 0;
    }

    bool remove(const char* id) override {
        if (!id) return false;
        return sensorTypeDao->remove(id);
    }

    int removeAll() override {
        std::vector<SensorType> empty;
        return sensorTypeDao->saveAll(empty) ? 0 : -1;
    }

    int count() override {
        std::vector<SensorType> sensorTypes = getAll();
        return sensorTypes.size();
    }

    bool exists(const char* id) override {
        if (!id) return false;
        
        SensorType sensorType;
        return sensorTypeDao->getById(id, sensorType);
    }

    std::vector<SensorType> getSensorTypesByProtocol(SensorProtocol protocol) override {
        std::vector<SensorType> sensorTypes;
        sensorTypeDao->getSensorTypesByProtocol(protocol, sensorTypes);
        return sensorTypes;
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
        std::vector<SensorType> sensorTypes;
        sensorTypeDao->getCalibrableSensorTypes(sensorTypes);
        return sensorTypes;
    }

    SensorType* getSensorTypeByName(const std::string& name) override {
        static SensorType foundSensorType;
        if (sensorTypeDao->getSensorTypeByName(name, foundSensorType)) {
            return &foundSensorType;
        }
        return nullptr;
    }
};
