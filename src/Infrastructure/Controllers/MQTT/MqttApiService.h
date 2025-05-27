#ifndef MQTT_API_SERVICE_H
#define MQTT_API_SERVICE_H

#include "Core/Domain/Interfaces/API/IMqttApiService.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/UpdateDto.h"

class MqttApiService : public IMqttApiService {
private:
    IMqttManager* _mqttManager;
    IModuleConfig* _moduleConfig;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IJsonConverter* _jsonConverter;
    
    // Topic format strings
    static constexpr const char* MODULE_STRUCTURE_TOPIC = "hardware/structure/%s";
    static constexpr const char* DIAGNOSTIC_TOPIC = "hardware/diagnostic/%s";
    static constexpr const char* UPDATE_TOPIC = "hardware/update/%s";
    
    char _topicBuffer[128];
    
    void log(const char* format, ...) {
        if (_logger != nullptr && _debugSystem != nullptr &&
            (_debugSystem->isFeatureEnabled(DebugFeatures::MQTT_DEBUG)))
        {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::MQTT_DEBUG, buffer);
        }
    }
    
    // Format a topic with the module UID
    const char* formatTopic(const char* topicFormat) {
        if (_moduleConfig == nullptr) {
            log("MQTT: formatTopic - ModuleConfig not available");
            return nullptr;
        }
        
        const char* moduleUid = _moduleConfig->getModuleUid();
        if (moduleUid == nullptr || strlen(moduleUid) == 0) {
            log("MQTT: formatTopic - Module UID not available");
            return nullptr;
        }
        
        snprintf(_topicBuffer, sizeof(_topicBuffer), topicFormat, moduleUid);
        return _topicBuffer;
    }

public:
    MqttApiService(
        IMqttManager* mqttManager,
        IModuleConfig* moduleConfig,
        IJsonConverter* jsonConverter,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr
    ) : _mqttManager(mqttManager),
        _moduleConfig(moduleConfig),
        _jsonConverter(jsonConverter),
        _logger(logger),
        _debugSystem(debugSystem) {
    }
    
    ~MqttApiService() override = default;
    
    bool sendToServerModuleStructure(const ModuleStructure& moduleStructure) override {
        if (_mqttManager == nullptr || !_mqttManager->isConnected()) {
            log("MQTT: sendToServerModuleStructure - MQTT Manager not available or not connected");
            return false;
        }
        
        if (_jsonConverter == nullptr) {
            log("MQTT: sendToServerModuleStructure - JSON converter not available");
            return false;
        }
        
        const char* topic = formatTopic(MODULE_STRUCTURE_TOPIC);
        if (topic == nullptr) {
            return false;
        }
        
        std::string structureJson = _jsonConverter->moduleStructureToJson(moduleStructure);
        log("MQTT: Publishing module structure to topic: %s", topic);
        
        if (!_mqttManager->publish(topic, structureJson.c_str())) {
            log("MQTT: Failed to publish module structure");
            return false;
        }
        return true;
    }
    
    bool sendDiagnosticData(const DiagnosticDto& diagnosticData) override {
        if (_mqttManager == nullptr || !_mqttManager->isConnected()) {
            log("MQTT: sendDiagnosticData - MQTT Manager not available or not connected");
            return false;
        }
        
        if (_jsonConverter == nullptr) {
            log("MQTT: sendDiagnosticData - JSON converter not available");
            return false;
        }
        
        const char* topic = formatTopic(DIAGNOSTIC_TOPIC);
        if (topic == nullptr) {
            return false;
        }
        
        std::string diagnosticJson = _jsonConverter->diagnosticToJson(diagnosticData);
        log("MQTT: Publishing diagnostic data to topic: %s", topic);
        
        if (!_mqttManager->publish(topic, diagnosticJson.c_str())) {
            log("MQTT: Failed to publish diagnostic data");
            return false;
        }
        
        return true;
    }
    
    bool sendModuleUpdateData(const UpdateDto& updateData) override {
        if (_mqttManager == nullptr || !_mqttManager->isConnected()) {
            log("MQTT: sendModuleUpdateData - MQTT Manager not available or not connected");
            return false;
        }
        
        if (_jsonConverter == nullptr) {
            log("MQTT: sendModuleUpdateData - JSON converter not available");
            return false;
        }
        
        const char* topic = formatTopic(UPDATE_TOPIC);
        if (topic == nullptr) {
            return false;
        }
        
        std::string updateJson = _jsonConverter->updateToJson(updateData);
        log("MQTT: Publishing update data to topic: %s", topic);
        
        if (!_mqttManager->publish(topic, updateJson.c_str())) {
            log("MQTT: Failed to publish update data");
            return false;
        }
        
        return true;
    }
    
    bool isModuleOnline() override {
        // Check if MQTT connection is active
        if (_mqttManager == nullptr) {
            log("MQTT: isModuleOnline - MQTT Manager not available");
            return false;
        }
        
        bool isConnected = _mqttManager->isConnected();
        log("MQTT: isModuleOnline - Connection status: %s", isConnected ? "online" : "offline");
        
        return isConnected;
    }
};

#endif // MQTT_API_SERVICE_H
