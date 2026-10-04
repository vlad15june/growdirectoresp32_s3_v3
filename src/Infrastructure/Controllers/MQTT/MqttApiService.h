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
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/RawIoResultDto.h"
#include <cstdarg>
#include <cstdio>

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
    static constexpr const char* DEVICE_COMMAND_TOPIC = "hardware/devices_command/post/%s";
    static constexpr const char* COMMAND_RESULT_TOPIC = "hardware/command_result/%s";
    static constexpr const char* RAW_IO_RESULT_TOPIC = "hardware/raw_io_result/%s";
    static constexpr const char* RULE_STATE_TOPIC = "hardware/rules/state/%s";
    static constexpr const char* RULE_STATE_BY_UID_TOPIC = "hardware/rules/state/%s/%s";
    static constexpr const char* RULE_SENSOR_OFFLINE_TOPIC = "hardware/event/rule_sensor_offline/%s";
    
    // Longest topic is "hardware/rules/state/<uid>/<rule-uid>"; 128 bytes has always been the
    // size here and stays it.
    static const size_t TOPIC_BUFFER_SIZE = 128;
    
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
    
    // Format a topic with the module UID into a buffer owned by the caller.
    //
    // The buffer is the caller's, not a member of this class: a member buffer with a returned
    // pointer into it would be safe only while a single task publishes. Between formatTopic()
    // and publish() the caller serialises a JSON document, and a second task formatting its own
    // topic in that window would silently redirect the first one's payload to the wrong topic.
    // Two tasks do publish through this object, on two cores - the control loop and the outbox
    // network task - so the topic lives on the caller's stack for the whole
    // format-serialise-publish sequence and cannot be shared with anyone.
    bool formatTopic(char* out, size_t outSize, const char* topicFormat) {
        if (_moduleConfig == nullptr) {
            log("MQTT: formatTopic - ModuleConfig not available");
            return false;
        }
        
        const char* moduleUid = _moduleConfig->getModuleUid();
        if (moduleUid == nullptr || strlen(moduleUid) == 0) {
            log("MQTT: formatTopic - Module UID not available");
            return false;
        }
        
        snprintf(out, outSize, topicFormat, moduleUid);
        return true;
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
        
        char topic[TOPIC_BUFFER_SIZE];
        if (!formatTopic(topic, sizeof(topic), MODULE_STRUCTURE_TOPIC)) {
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
        
        char topic[TOPIC_BUFFER_SIZE];
        if (!formatTopic(topic, sizeof(topic), DIAGNOSTIC_TOPIC)) {
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
        
        char topic[TOPIC_BUFFER_SIZE];
        if (!formatTopic(topic, sizeof(topic), UPDATE_TOPIC)) {
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
    
    bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& commandUpdate) override {
        if (_mqttManager == nullptr || !_mqttManager->isConnected()) {
            log("MQTT: sendDeviceCommandExecutionResult - MQTT Manager not available or not connected");
            return false;
        }
        
        if (_jsonConverter == nullptr) {
            log("MQTT: sendDeviceCommandExecutionResult - JSON converter not available");
            return false;
        }
        
        char topic[TOPIC_BUFFER_SIZE];
        if (!formatTopic(topic, sizeof(topic), DEVICE_COMMAND_TOPIC)) {
            return false;
        }
        
        std::string commandJson = _jsonConverter->deviceCommandUpdateToJson(commandUpdate);
        log("MQTT: Publishing device command result to topic: %s", topic);
        
        if (!_mqttManager->publish(topic, commandJson.c_str())) {
            log("MQTT: Failed to publish device command result");
            return false;
        }
        
        return true;
    }
    
    bool sendCommandResult(const CommandResultDto& commandResult) override {
        if (_mqttManager == nullptr || !_mqttManager->isConnected()) {
            log("MQTT: sendCommandResult - MQTT Manager not available or not connected");
            return false;
        }
        
        if (_jsonConverter == nullptr) {
            log("MQTT: sendCommandResult - JSON converter not available");
            return false;
        }
        
        char topic[TOPIC_BUFFER_SIZE];
        if (!formatTopic(topic, sizeof(topic), COMMAND_RESULT_TOPIC)) {
            return false;
        }
        
        std::string commandResultJson = _jsonConverter->commandResultToJson(commandResult);
        log("MQTT: Publishing command result to topic: %s", topic);
        
        if (!_mqttManager->publish(topic, commandResultJson.c_str())) {
            log("MQTT: Failed to publish command result");
            return false;
        }

        return true;
    }

    bool sendRawIoResult(const RawIoResultDto& result) override {
        if (_mqttManager == nullptr || !_mqttManager->isConnected()) {
            log("MQTT: sendRawIoResult - MQTT Manager not available or not connected");
            return false;
        }

        if (_jsonConverter == nullptr) {
            log("MQTT: sendRawIoResult - JSON converter not available");
            return false;
        }

        char topic[TOPIC_BUFFER_SIZE];
        if (!formatTopic(topic, sizeof(topic), RAW_IO_RESULT_TOPIC)) {
            return false;
        }

        std::string rawIoResultJson = _jsonConverter->rawIoResultToJson(result);
        log("MQTT: Publishing raw IO result to topic: %s", topic);

        if (!_mqttManager->publish(topic, rawIoResultJson.c_str())) {
            log("MQTT: Failed to publish raw IO result");
            return false;
        }

        return true;
    }

    bool sendRuleStateCommand(const char* msg) override {
        if (_mqttManager == nullptr || !_mqttManager->isConnected()) {
            log("MQTT: sendRuleStateCommand - MQTT Manager not available or not connected");
            return false;
        }
        
        char topic[TOPIC_BUFFER_SIZE];
        if (!formatTopic(topic, sizeof(topic), RULE_STATE_TOPIC)) {
            return false;
        }
        
        log("MQTT: Publishing rule state command to topic: %s", topic);

        if (!_mqttManager->publish(topic, msg)) {
            log("MQTT: Failed to publish rule state command");
            return false;
        }

        return true;
    }

    bool sendByUidRuleStateCommand(const char* uid, const char* msg) override {
        if (_mqttManager == nullptr || !_mqttManager->isConnected()) {
            log("MQTT: sendByUidRuleStateCommand - MQTT Manager not available or not connected");
            return false;
        }

        if (_moduleConfig == nullptr) {
            log("MQTT: sendByUidRuleStateCommand - ModuleConfig not available");
            return false;
        }

        const char* moduleUid = _moduleConfig->getModuleUid();
        if (moduleUid == nullptr || strlen(moduleUid) == 0) {
            log("MQTT: sendByUidRuleStateCommand - Module UID not available");
            return false;
        }

        char topic[TOPIC_BUFFER_SIZE];
        snprintf(topic, sizeof(topic), RULE_STATE_BY_UID_TOPIC, moduleUid, uid);

        log("MQTT: Publishing rule state command to topic: %s", topic);

        if (!_mqttManager->publish(topic, msg)) {
            log("MQTT: Failed to publish by-uid rule state command");
            return false;
        }

        return true;
    }

    bool sendRuleSensorOfflineEvent(const char* msg) override {
        if (_mqttManager == nullptr || !_mqttManager->isConnected()) {
            log("MQTT: sendRuleSensorOfflineEvent - MQTT Manager not available or not connected");
            return false;
        }

        char topic[TOPIC_BUFFER_SIZE];
        if (!formatTopic(topic, sizeof(topic), RULE_SENSOR_OFFLINE_TOPIC)) {
            return false;
        }

        log("MQTT: Publishing rule_sensor_offline event to topic: %s", topic);

        if (!_mqttManager->publish(topic, msg)) {
            log("MQTT: Failed to publish rule_sensor_offline event");
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
