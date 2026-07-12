#pragma once

#include <vector>
#include <cstdarg>
#include "ISensorService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Application/DTOs/UpdateDto.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Core/Domain/UseCases/Sensors/SensorRecoveryUseCase.h"

class SensorService : public ISensorService {
private:
    IModuleConfig* moduleConfig;
    ModuleStructure* moduleStructure;
    ISensorRepository* sensorRepository;
    ISensorReader* sensorReader;
    ISensorValueModel* sensorValueModel;
    INTPTimeAdapter* timeAdapter;
    IArduinoAdapter* arduinoAdapter;
    IHybridAPIService* hybridApiService;
    ILogger* logger;
    IDebugSystem* debugSystem;
    IUdpManager* udpManager;
    IJsonConverter* jsonConverter;
    IMqttManager* mqttManager;
    
    const unsigned long SENSOR_READ_INTERVAL = 15000;
    const unsigned long SENSOR_UPDATE_INTERVAL = 15000;
    const unsigned long UDP_SEND_INTERVAL = 15000;
    unsigned long lastSensorReadTime = 0;
    unsigned long lastSensorUpdateTime = 0;
    unsigned long lastUdpSendTime = 0;

    SensorRecoveryUseCase _recovery;
    
    std::string generateSensorUid(const SensorEntity& sensor, const std::string& channelName = "") const;
    UpdateDto createSensorUpdateDto();
    bool readAnalogSensors();
    bool readI2CSensors();
    bool readModbusSensors();
    void sendUpdateViaUdp(const UpdateDto& updateDto);
    
    void log(const char* format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SENSOR_DATA_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
    
            IDebugFeature* feature = debugSystem->getFeature(DebugFeatures::SENSOR_DATA_DEBUG);
            if (feature)
            {
                logger->println(DebugFeatures::SENSOR_DATA_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                logger->println(DebugFeatures::SENSOR_DATA_DEBUG, buffer);
            }
        }
    }

    void logVerbose(const char* format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SENSOR_VERBOSE_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* feature = debugSystem->getFeature(DebugFeatures::SENSOR_VERBOSE_DEBUG);
            if (feature)
            {
                logger->println(DebugFeatures::SENSOR_VERBOSE_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                logger->println(DebugFeatures::SENSOR_VERBOSE_DEBUG, buffer);
            }
        }
    }

public:
    SensorService(IModuleConfig* moduleConfig, 
                  ModuleStructure* moduleStructure,
                  ISensorRepository* sensorRepository,
                  ISensorReader* sensorReader,
                  ISensorValueModel* sensorValueModel,
                  INTPTimeAdapter* timeAdapter,
                  IArduinoAdapter* arduinoAdapter,
                  IHybridAPIService* hybridApiService = nullptr,
                  ILogger* logger = nullptr,
                  IDebugSystem* debugSystem = nullptr,
                  IUdpManager* udpManager = nullptr,
                  IJsonConverter* jsonConverter = nullptr,
                  IMqttManager* mqttManager = nullptr);

    ~SensorService();
    
    bool init() override;
    void loop() override;
    bool readAllSensors() override;
    bool readSensor(SensorEntity sensor) override;
};
