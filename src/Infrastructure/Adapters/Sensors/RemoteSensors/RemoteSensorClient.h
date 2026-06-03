#ifndef REMOTE_SENSOR_RECEIVER_H
#define REMOTE_SENSOR_RECEIVER_H

#include "Core/Domain/Interfaces/Adapters/Sensors/IRemoteSensorReceiver.h"
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"
#include "Core/Domain/Interfaces/Udp/IUdpController.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Core/Domain/Interfaces/Mqtt/IMessageProcessor.h"
#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"

#include <set>
#include <string>
#include <vector>
#include <utility>

class RemoteSensorReceiver : public IRemoteSensorReceiver {
private:
    static const size_t MAX_SUBSCRIPTIONS = 50;
    static const uint32_t SUBSCRIPTION_BOOT_DELAY_MS = 10000;

    class InboundUpdateTopicHandler : public BaseTopicHandler {
    private:
        RemoteSensorReceiver* _receiver;

    public:
        InboundUpdateTopicHandler(RemoteSensorReceiver* receiver, ILogger* logger, IDebugSystem* debugSystem)
            : BaseTopicHandler("hardware/update/#", logger, debugSystem),
              _receiver(receiver) {}

        void handleMessage(const char* topic, const uint8_t* payload, unsigned int length) override {
            if (!_receiver) {
                return;
            }
            std::string payloadStr(reinterpret_cast<const char*>(payload), length);
            _receiver->onMqttMessage(topic, payloadStr.c_str());
        }
    };

    IUdpManager* _udpManager;
    IMqttManager* _mqttManager;
    IMessageProcessor* _messageProcessor;
    ISensorValueModel* _sensorValueModel;
    IRuleRepository* _ruleRepository;
    ISensorRepository* _sensorRepository;
    IJsonConverter* _jsonConverter;
    IArduinoAdapter* _arduinoAdapter;
    ILogger* _logger;
    IDebugSystem* _debugSystem;

    InboundUpdateTopicHandler _mqttHandler;
    std::set<std::string> _activeSubscriptions;
    bool _initialized;

    void logEvent(const char* action, const char* details) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG)) {
            _logger->logJson(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG, action, details);
        }
    }

    std::string ownerModuleUidOf(const std::string& sensorUid) const {
        return sensorUid.substr(0, 17);
    }

    std::string topicFor(const std::string& ownerUid) const {
        return "hardware/update/" + ownerUid;
    }

    bool isLocalSensor(const std::string& sensorUid, const std::vector<SensorEntity>& localSensors) const {
        for (const auto& sensor : localSensors) {
            if (sensor.uid.empty()) {
                continue;
            }
            if (sensorUid == sensor.uid) {
                return true;
            }
            if (sensorUid.size() > sensor.uid.size() &&
                sensorUid.compare(0, sensor.uid.size(), sensor.uid) == 0 &&
                sensorUid[sensor.uid.size()] == '.') {
                return true;
            }
        }
        return false;
    }

    bool looksLikeJsonObject(const std::string& payload) const {
        size_t start = payload.find_first_not_of(" \t\r\n");
        if (start == std::string::npos || payload[start] != '{') {
            return false;
        }
        return payload.find('}', start) != std::string::npos;
    }

    void writeSensorsFromPayload(const std::string& payload) {
        std::vector<std::pair<std::string, float>> pairs;
        if (!_jsonConverter || !_jsonConverter->sensorValuesFromUpdateJson(payload, pairs)) {
            const char* reason = looksLikeJsonObject(payload) ? "no_sensors_array" : "parse_error";
            char details[80];
            snprintf(details, sizeof(details), "{\"reason\":\"%s\",\"len\":%d}", reason, static_cast<int>(payload.size()));
            logEvent("drop", details);
            return;
        }

        uint32_t now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        for (const auto& pair : pairs) {
            if (_sensorValueModel) {
                _sensorValueModel->setSensorValue(pair.first, pair.second);
                _sensorValueModel->setLastReadTime(pair.first, now);
            }
            char details[192];
            snprintf(details, sizeof(details), "{\"sensorUid\":\"%s\",\"value\":%.2f,\"lastReadTime\":%u}",
                     pair.first.c_str(), pair.second, now);
            logEvent("write", details);
        }
    }

    void onUdpPacket(const UdpPacketData& packet) {
        char details[128];
        snprintf(details, sizeof(details), "{\"len\":%d,\"from\":\"%s\"}",
                 static_cast<int>(packet.length), packet.remoteIP ? packet.remoteIP : "");
        logEvent("udp_packet", details);

        std::string payload(packet.data, packet.length);
        writeSensorsFromPayload(payload);
    }

    void onMqttMessage(const char* topic, const char* payload) {
        std::string payloadStr(payload ? payload : "");
        char details[160];
        snprintf(details, sizeof(details), "{\"len\":%d,\"topic\":\"%s\"}",
                 static_cast<int>(payloadStr.size()), topic ? topic : "");
        logEvent("mqtt_message", details);

        writeSensorsFromPayload(payloadStr);
    }

public:
    RemoteSensorReceiver(
        IUdpManager* udpManager,
        IMqttManager* mqttManager,
        IMessageProcessor* messageProcessor,
        ISensorValueModel* sensorValueModel,
        IRuleRepository* ruleRepository,
        ISensorRepository* sensorRepository,
        IJsonConverter* jsonConverter,
        IArduinoAdapter* arduinoAdapter,
        ILogger* logger,
        IDebugSystem* debugSystem)
        : _udpManager(udpManager),
          _mqttManager(mqttManager),
          _messageProcessor(messageProcessor),
          _sensorValueModel(sensorValueModel),
          _ruleRepository(ruleRepository),
          _sensorRepository(sensorRepository),
          _jsonConverter(jsonConverter),
          _arduinoAdapter(arduinoAdapter),
          _logger(logger),
          _debugSystem(debugSystem),
          _mqttHandler(this, logger, debugSystem),
          _initialized(false) {}

    ~RemoteSensorReceiver() override = default;

    bool init() override {
        if (_initialized) {
            return true;
        }

        bool udpReady = false;
        if (_udpManager) {
            IUdpController* controller = _udpManager->getController();
            if (controller) {
                controller->setPacketCallback([this](const UdpPacketData& packet) {
                    onUdpPacket(packet);
                });
                udpReady = true;
            }
        }

        bool handlerReady = false;
        if (_messageProcessor) {
            _messageProcessor->addTopicHandler(&_mqttHandler);
            handlerReady = true;
        }

        char details[64];
        snprintf(details, sizeof(details), "{\"udp\":%s,\"handler\":%s}",
                 udpReady ? "true" : "false", handlerReady ? "true" : "false");
        logEvent("init", details);

        _initialized = true;
        onRuleSetChanged();
        return true;
    }

    void onRuleSetChanged() override {
        uint32_t now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        if (now < SUBSCRIPTION_BOOT_DELAY_MS) {
            char details[64];
            snprintf(details, sizeof(details), "{\"reason\":\"boot_delay\",\"millis\":%u}", now);
            logEvent("reconcile_skip", details);
            return;
        }

        std::vector<Rule> rules;
        if (_ruleRepository) {
            rules = _ruleRepository->getAll();
        }

        std::vector<SensorEntity> localSensors;
        if (_sensorRepository) {
            localSensors = _sensorRepository->getAll();
        }

        std::set<std::string> desired;
        std::set<std::string> remoteUids;

        for (const auto& rule : rules) {
            std::vector<std::string> ruleSensorUids;
            if (!rule.sensorUid.empty()) {
                ruleSensorUids.push_back(rule.sensorUid);
            }
            if (rule.ruleType == RuleType::AVERAGE_SENSOR && !rule.conditions.empty()) {
                for (const auto& uid : rule.conditions[0].sensorUids) {
                    if (!uid.empty()) {
                        ruleSensorUids.push_back(uid);
                    }
                }
            }

            for (const auto& uid : ruleSensorUids) {
                if (isLocalSensor(uid, localSensors)) {
                    continue;
                }
                remoteUids.insert(uid);
                desired.insert(topicFor(ownerModuleUidOf(uid)));
            }
        }

        std::vector<std::string> toUnsubscribe;
        for (const auto& topic : _activeSubscriptions) {
            if (desired.find(topic) == desired.end()) {
                toUnsubscribe.push_back(topic);
            }
        }

        std::vector<std::string> toSubscribe;
        for (const auto& topic : desired) {
            if (_activeSubscriptions.find(topic) == _activeSubscriptions.end()) {
                toSubscribe.push_back(topic);
            }
        }

        for (const auto& topic : toUnsubscribe) {
            bool ok = _mqttManager && _mqttManager->unsubscribe(topic.c_str());
            if (ok) {
                _activeSubscriptions.erase(topic);
                char details[160];
                snprintf(details, sizeof(details), "{\"topic\":\"%s\",\"ok\":true,\"active\":%d}",
                         topic.c_str(), static_cast<int>(_activeSubscriptions.size()));
                logEvent("unsubscribe", details);
            } else {
                char details[128];
                snprintf(details, sizeof(details), "{\"topic\":\"%s\",\"ok\":false}", topic.c_str());
                logEvent("unsubscribe_fail", details);
            }
        }

        for (const auto& topic : toSubscribe) {
            if (_activeSubscriptions.size() >= MAX_SUBSCRIPTIONS) {
                char details[128];
                snprintf(details, sizeof(details), "{\"topic\":\"%s\",\"cap\":%d}",
                         topic.c_str(), static_cast<int>(MAX_SUBSCRIPTIONS));
                logEvent("subscribe_capped", details);
                continue;
            }
            bool ok = _mqttManager && _mqttManager->subscribe(topic.c_str());
            if (ok) {
                _activeSubscriptions.insert(topic);
                char details[160];
                snprintf(details, sizeof(details), "{\"topic\":\"%s\",\"ok\":true,\"active\":%d}",
                         topic.c_str(), static_cast<int>(_activeSubscriptions.size()));
                logEvent("subscribe", details);
            } else {
                char details[128];
                snprintf(details, sizeof(details), "{\"topic\":\"%s\",\"ok\":false}", topic.c_str());
                logEvent("subscribe_fail", details);
            }
        }

        std::string summary = "{\"rules\":" + std::to_string(rules.size()) +
                              ",\"remote\":" + jsonArrayOf(remoteUids) +
                              ",\"toSub\":" + std::to_string(toSubscribe.size()) +
                              ",\"toUnsub\":" + std::to_string(toUnsubscribe.size()) +
                              ",\"active\":" + std::to_string(_activeSubscriptions.size()) + "}";
        logEvent("reconcile", summary.c_str());
    }

private:
    static std::string jsonArrayOf(const std::set<std::string>& items) {
        std::string result = "[";
        bool first = true;
        for (const auto& item : items) {
            if (!first) {
                result += ",";
            }
            result += "\"" + item + "\"";
            first = false;
        }
        result += "]";
        return result;
    }
};

#endif // REMOTE_SENSOR_RECEIVER_H
