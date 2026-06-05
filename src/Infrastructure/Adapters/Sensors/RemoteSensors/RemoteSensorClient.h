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
#include <cstdarg>

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

    void log(const char* format, ...) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG);
            if (feature) {
                _logger->println(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG, buffer, feature->getLogChannel());
            } else {
                _logger->println(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG, buffer);
            }
        }
    }

    void logSerial(const char* format, ...) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger->println(DebugFeatures::REMOTE_SENSOR_RECEIVE_DEBUG, buffer, MqttLoggerMode::SerialOnly);
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
            logSerial("drop reason=%s len=%d", reason, static_cast<int>(payload.size()));
            return;
        }

        uint32_t now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        for (const auto& pair : pairs) {
            if (_sensorValueModel) {
                _sensorValueModel->setSensorValue(pair.first, pair.second);
                _sensorValueModel->setLastReadTime(pair.first, now);
            }
            logSerial("write sensorUid=%s value=%.2f lastReadTime=%u", pair.first.c_str(), pair.second, now);
        }
    }

    void onUdpPacket(const UdpPacketData& packet) {
        logSerial("udp_packet len=%d from=%s",
            static_cast<int>(packet.length), packet.remoteIP ? packet.remoteIP : "");

        std::string payload(packet.data, packet.length);
        writeSensorsFromPayload(payload);
    }

    void onMqttMessage(const char* topic, const char* payload) {
        std::string payloadStr(payload ? payload : "");
        logSerial("mqtt_message len=%d topic=%s",
            static_cast<int>(payloadStr.size()), topic ? topic : "");

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

        log("init udp=%d handler=%d", udpReady ? 1 : 0, handlerReady ? 1 : 0);

        _initialized = true;
        onRuleSetChanged();
        return true;
    }

    void onRuleSetChanged() override {
        uint32_t now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        if (now < SUBSCRIPTION_BOOT_DELAY_MS) {
            log("reconcile_skip boot_delay millis=%u", now);
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
                log("unsubscribe topic=%s ok=1 active=%d", topic.c_str(), static_cast<int>(_activeSubscriptions.size()));
            } else {
                log("unsubscribe_fail topic=%s ok=0", topic.c_str());
            }
        }

        for (const auto& topic : toSubscribe) {
            if (_activeSubscriptions.size() >= MAX_SUBSCRIPTIONS) {
                log("subscribe_capped topic=%s cap=%d", topic.c_str(), static_cast<int>(MAX_SUBSCRIPTIONS));
                continue;
            }
            bool ok = _mqttManager && _mqttManager->subscribe(topic.c_str());
            if (ok) {
                _activeSubscriptions.insert(topic);
                log("subscribe topic=%s ok=1 active=%d", topic.c_str(), static_cast<int>(_activeSubscriptions.size()));
            } else {
                log("subscribe_fail topic=%s ok=0", topic.c_str());
            }
        }

        std::string remoteList = commaList(remoteUids);
        log("reconcile rules=%d remote=[%s] toSub=%d toUnsub=%d active=%d",
            static_cast<int>(rules.size()), remoteList.c_str(),
            static_cast<int>(toSubscribe.size()), static_cast<int>(toUnsubscribe.size()),
            static_cast<int>(_activeSubscriptions.size()));
    }

private:
    static std::string commaList(const std::set<std::string>& items) {
        std::string result;
        bool first = true;
        for (const auto& item : items) {
            if (!first) {
                result += ",";
            }
            result += item;
            first = false;
        }
        return result;
    }
};

#endif // REMOTE_SENSOR_RECEIVER_H
