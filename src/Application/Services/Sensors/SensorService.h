#pragma once

#include <vector>
#include <memory>
#include <ctime>
#include <cstdarg>
#include "ISensorService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Application/DTOs/UpdateDto.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class SensorService : public ISensorService {
private:
    IModuleConfig* moduleConfig;
    ModuleStructure* moduleStructure;
    ISensorRepository* sensorRepository;
    ISensorReader* sensorReader;
    INTPTimeAdapter* timeAdapter;
    IArduinoAdapter* arduinoAdapter;
    IHybridAPIService* hybridApiService;
    ILogger* logger;
    IDebugSystem* debugSystem;
    
    const unsigned long SENSOR_READ_INTERVAL = 5000;
    const unsigned long SENSOR_UPDATE_INTERVAL = 30000;
    unsigned long lastSensorReadTime = 0;
    unsigned long lastSensorUpdateTime = 0;
    
    std::string generateSensorUid(const SensorEntity& sensor, const std::string& channelName = "") const;
    UpdateDto createSensorUpdateDto();
    bool readAnalogSensors();
    bool readI2CSensors();
    bool readModbusSensors();
    
    void log(const char* format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::RULE_SENSOR_VALUE_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
    
            logger->println(DebugFeatures::RULE_SENSOR_VALUE_DEBUG, buffer);
        }
    }
    
public:
    SensorService(IModuleConfig* moduleConfig, 
                  ModuleStructure* moduleStructure,
                  ISensorRepository* sensorRepository,
                  ISensorReader* sensorReader,
                  INTPTimeAdapter* timeAdapter,
                  IArduinoAdapter* arduinoAdapter,
                  IHybridAPIService* hybridApiService = nullptr,
                  ILogger* logger = nullptr,
                  IDebugSystem* debugSystem = nullptr);

    ~SensorService();
    
    bool init() override;
    void loop() override;
    bool readAllSensors() override;
    bool readSensor(const char* uid) override;
};
