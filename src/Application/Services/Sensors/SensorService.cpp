#include "SensorService.h"
#include <cstring>
#include <algorithm>

SensorService::SensorService(IModuleConfig* moduleConfig, 
                            ModuleStructure* moduleStructure,
                            ISensorRepository* sensorRepository,
                            ISensorReader* sensorReader,
                            INTPTimeAdapter* timeAdapter,
                            IArduinoAdapter* arduinoAdapter,
                            IHybridAPIService* hybridApiService,
                            ILogger* logger,
                            IDebugSystem* debugSystem) 
    : moduleConfig(moduleConfig), 
      moduleStructure(moduleStructure),
      sensorRepository(sensorRepository),
      sensorReader(sensorReader),
      timeAdapter(timeAdapter),
      arduinoAdapter(arduinoAdapter),
      hybridApiService(hybridApiService),
      logger(logger),
      debugSystem(debugSystem),
      lastSensorReadTime(0),
      lastSensorUpdateTime(0) {
}

SensorService::~SensorService() {
}

bool SensorService::init() {
    log("SensorService: Initializing");
    
    if (!sensorRepository) {
        log("SensorService: Sensor repository is null");
        return false;
    }
    
    if (!sensorReader) {
        log("SensorService: Sensor reader is null");
        return false;
    }
    
    lastSensorReadTime = arduinoAdapter->millis();
    lastSensorUpdateTime = arduinoAdapter->millis();
    
    log("SensorService: Initialization successful");
    return true;
}

void SensorService::loop() {
    unsigned long currentTime = arduinoAdapter->millis();
    
    if (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        log("SensorService: Reading all sensors");
        readAllSensors();
        lastSensorReadTime = currentTime;
    }
    
    if (hybridApiService && (currentTime - lastSensorUpdateTime >= SENSOR_UPDATE_INTERVAL)) {
        log("SensorService: Sending sensor update to server");
        UpdateDto updateDto = createSensorUpdateDto();
        
        hybridApiService->sendModuleUpdateData(updateDto);
        lastSensorUpdateTime = currentTime;
    }
}

bool SensorService::readAllSensors() {
    if (!sensorRepository) {
        return false;
    }
    
    bool success = true;
    
    if (!readAnalogSensors()) {
        success = false;
    }
    
    if (!readI2CSensors()) {
        success = false;
    }
    
    if (!readModbusSensors()) {
        success = false;
    }
    
    return success;
}

bool SensorService::readSensor(const char* uid) {
    if (!uid || !sensorRepository || !sensorReader) {
        return false;
    }
    
    auto sensorOpt = sensorRepository->get(uid);
    if (!sensorOpt.has_value()) {
        log("SensorService: Sensor with UID %s not found", uid);
        return false;
    }
    
    SensorEntity sensor = sensorOpt.value();
    if (!sensor.isActive) {
        log("SensorService: Sensor %s is not active", uid);
        return false;
    }
    
    uint32_t currentTime = arduinoAdapter->millis();
    
    if (sensor.isMultiChannel()) {
        std::map<std::string, float> channelValues;
        if (sensorReader->readMultiChannelSensor(sensor, channelValues)) {
            for (const auto& pair : channelValues) {
                sensorRepository->updateChannelValue(uid, pair.first.c_str(), pair.second);
            }
            sensorRepository->updateSensorStatus(uid, true, false);
            log("SensorService: Read multi-channel sensor %s", uid);
            return true;
        } else {
            sensorRepository->updateSensorStatus(uid, sensor.isActive, true);
            log("SensorService: Failed to read multi-channel sensor %s", uid);
            return false;
        }
    } else {
        float value;
        if (sensorReader->readSensor(sensor, value)) {
            sensorRepository->updateSensorValue(uid, value, currentTime);
            sensorRepository->updateSensorStatus(uid, true, false);
            log("SensorService: Read sensor %s, value: %.2f", uid, value);
            return true;
        } else {
            sensorRepository->updateSensorStatus(uid, sensor.isActive, true);
            log("SensorService: Failed to read sensor %s", uid);
            return false;
        }
    }
}

bool SensorService::readAnalogSensors() {
    auto analogSensors = sensorRepository->getSensorsByProtocol("ANALOG");
    bool allSuccess = true;
    
    for (const auto& sensor : analogSensors) {
        if (!sensor.isActive) continue;
        
        if (!readSensor(sensor.uid.c_str())) {
            allSuccess = false;
        }
    }
    
    return allSuccess;
}

bool SensorService::readI2CSensors() {
    auto i2cSensors = sensorRepository->getI2CSensors();
    bool allSuccess = true;
    
    for (const auto& sensor : i2cSensors) {
        if (!sensor.isActive) continue;
        
        if (!readSensor(sensor.uid.c_str())) {
            allSuccess = false;
        }
    }
    
    return allSuccess;
}

bool SensorService::readModbusSensors() {
    auto modbusSensors = sensorRepository->getModbusSensors();
    bool allSuccess = true;
    
    for (const auto& sensor : modbusSensors) {
        if (!sensor.isActive) continue;
        
        if (!readSensor(sensor.uid.c_str())) {
            allSuccess = false;
        }
    }
    
    return allSuccess;
}

std::string SensorService::generateSensorUid(const SensorEntity& sensor, const std::string& channelName) const {
    std::string moduleUid = moduleConfig->getModuleUid();
    std::string baseUid = sensor.uid;
    
    if (sensor.protocol == "Analog") {
        return baseUid + (channelName.empty() ? "" : "." + channelName);
    }
    else if (sensor.protocol == "I2C") {
        std::string sensorNumber = "1";
        return baseUid + "." + sensorNumber + (channelName.empty() ? "" : "." + channelName);
    }
    else if (sensor.protocol == "ModBus") {
        std::string sensorAddress = std::to_string(sensor.modbusDeviceAddress);
        std::string sensorNumber = "1";
        return baseUid + "." + sensorAddress + "." + sensorNumber + (channelName.empty() ? "" : "." + channelName);
    }
    
    return baseUid;
}

UpdateDto SensorService::createSensorUpdateDto() {
    UpdateDto dto;
    
    dto.growdirectorUid = moduleConfig->getModuleUid();
    dto.time = timeAdapter->getFormattedTime();
    dto.millis = arduinoAdapter->millis();
    
    ISettings* settings = moduleConfig->getSettings();
    if (settings) {
        char accountUidBuffer[256];
        settings->getAccountUid(accountUidBuffer);
        dto.accountUid = accountUidBuffer;
        
        char userEmailBuffer[256];
        settings->getUser(userEmailBuffer);
        dto.email = userEmailBuffer;
    }
    
    auto sensors = sensorRepository->getActiveSensors();
    for (const auto& sensor : sensors) {
        if (sensor.isMultiChannel()) {
            for (const auto& channelValue : sensor.channelValues) {
                SensorDto sensorDto;
                sensorDto.sensorUid = generateSensorUid(sensor, channelValue.first);
                sensorDto.sensorType = sensor.sensorType;
                sensorDto.model = sensor.model;
                sensorDto.protocol = sensor.protocol;
                sensorDto.createdAt = sensor.createdAt;
                sensorDto.isActive = sensor.isActive;
                sensorDto.isError = sensor.isError;
                sensorDto.currentValue = channelValue.second;
                sensorDto.lastReadTime = sensor.lastReadTime;
                
                dto.sensors.push_back(sensorDto);
            }
        } else {
            SensorDto sensorDto;
            sensorDto.sensorUid = generateSensorUid(sensor);
            sensorDto.sensorType = sensor.sensorType;
            sensorDto.model = sensor.model;
            sensorDto.protocol = sensor.protocol;
            sensorDto.createdAt = sensor.createdAt;
            sensorDto.isActive = sensor.isActive;
            sensorDto.isError = sensor.isError;
            sensorDto.currentValue = sensor.currentValue;
            sensorDto.lastReadTime = sensor.lastReadTime;
            
            dto.sensors.push_back(sensorDto);
        }
    }
    
    return dto;
}
