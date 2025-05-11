#pragma once

#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include <ArduinoJson.h>

class ArduinoJsonConverter : public IJsonConverter {
public:
    bool moduleStructureFromJson(const std::string& jsonStr, ModuleStructure& moduleStructure) override {
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error) return false;

        moduleStructure.growdirectorUid = doc["GrowdirectorUid"] | "";
        moduleStructure.firmware = doc["firmware"] | "";
        moduleStructure.email = doc["email"] | "";
        moduleStructure.accountUid = doc["accountUid"] | "";
        moduleStructure.latitude = doc["lat"] | 0.0;
        moduleStructure.longitude = doc["lng"] | 0.0;

        moduleStructure.nodes.clear();
        if (doc.containsKey("nodes")) {
            for (JsonObject nodeObj : doc["nodes"].as<JsonArray>()) {
                Node node;
                node.type = nodeObj["type"] | "";
                node.subtype = nodeObj["subtype"] | "";
                node.bootType = nodeObj["bootType"] | "";
                node.fileSystem = nodeObj["fileSystem"] | "";
                node.uid = nodeObj["uid"] | "";
                node.firmware = nodeObj["firmware"] | "";
                node.ssid = nodeObj["ssid"] | "";
                node.ip = nodeObj["ip"] | "";

                node.sensors.clear();
                if (nodeObj.containsKey("sensors")) {
                    for (JsonObject sensorObj : nodeObj["sensors"].as<JsonArray>()) {
                        Sensor sensor;
                        sensor.uid = sensorObj["uid"] | "";
                        sensor.type = sensorObj["type"] | "";
                        sensor.pin = sensorObj["pin"] | 0;
                        node.sensors.push_back(sensor);
                    }
                }

                node.relays.clear();
                if (nodeObj.containsKey("relays")) {
                    for (JsonObject relayObj : nodeObj["relays"].as<JsonArray>()) {
                        Relay relay;
                        relay.uid = relayObj["uid"] | "";
                        relay.pin = relayObj["pin"] | 0;
                        node.relays.push_back(relay);
                    }
                }

                node.pumps.clear();
                if (nodeObj.containsKey("pumps")) {
                    for (JsonObject pumpObj : nodeObj["pumps"].as<JsonArray>()) {
                        Pump pump;
                        pump.uid = pumpObj["uid"] | "";
                        pump.pin = pumpObj["pin"] | 0;
                        node.pumps.push_back(pump);
                    }
                }

                moduleStructure.nodes.push_back(node);
            }
        }
        return true;
    }

    std::string moduleStructureToJson(const ModuleStructure& moduleStructure) override {
        DynamicJsonDocument doc(4096);
        doc["GrowdirectorUid"] = moduleStructure.growdirectorUid;
        doc["firmware"] = moduleStructure.firmware;
        doc["email"] = moduleStructure.email;
        doc["accountUid"] = moduleStructure.accountUid;
        doc["lat"] = moduleStructure.latitude;
        doc["lng"] = moduleStructure.longitude;

        JsonArray nodesArr = doc.createNestedArray("nodes");
        for (const auto& node : moduleStructure.nodes) {
            JsonObject nodeObj = nodesArr.createNestedObject();
            nodeObj["type"] = node.type;
            nodeObj["subtype"] = node.subtype;
            nodeObj["bootType"] = node.bootType;
            nodeObj["fileSystem"] = node.fileSystem;
            nodeObj["uid"] = node.uid;
            nodeObj["firmware"] = node.firmware;
            nodeObj["ssid"] = node.ssid;
            nodeObj["ip"] = node.ip;

            if (!node.sensors.empty()) {
                JsonArray sensorsArr = nodeObj.createNestedArray("sensors");
                for (const auto& sensor : node.sensors) {
                    JsonObject sensorObj = sensorsArr.createNestedObject();
                    sensorObj["uid"] = sensor.uid;
                    sensorObj["type"] = sensor.type;
                    sensorObj["pin"] = sensor.pin;
                }
            }

            if (!node.relays.empty()) {
                JsonArray relaysArr = nodeObj.createNestedArray("relays");
                for (const auto& relay : node.relays) {
                    JsonObject relayObj = relaysArr.createNestedObject();
                    relayObj["uid"] = relay.uid;
                    relayObj["pin"] = relay.pin;
                }
            }

            if (!node.pumps.empty()) {
                JsonArray pumpsArr = nodeObj.createNestedArray("pumps");
                for (const auto& pump : node.pumps) {
                    JsonObject pumpObj = pumpsArr.createNestedObject();
                    pumpObj["uid"] = pump.uid;
                    pumpObj["pin"] = pump.pin;
                }
            }
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    std::string diagnosticToJson(const DiagnosticDto& diagnosticData) override {
        DynamicJsonDocument doc(2048);
        
        doc["rssi"] = diagnosticData.rssi;
        doc["uid"] = diagnosticData.uid;
        doc["email"] = diagnosticData.email;
        
        if (!diagnosticData.accountUid.empty()) {
            doc["accountUid"] = diagnosticData.accountUid;
        }
        
        doc["timestamp"] = diagnosticData.timestamp;
        doc["uptimeMs"] = diagnosticData.uptimeMs;
        doc["networkFails"] = diagnosticData.networkFails;
        doc["apiSendFails"] = diagnosticData.apiSendFails;
        doc["apiStatus"] = diagnosticData.apiStatus;
        doc["mqttStatus"] = diagnosticData.mqttStatus;
        doc["mqttFails"] = diagnosticData.mqttFails;
        doc["apiSendFailRatio"] = diagnosticData.apiSendFailRatio;
        doc["firmware"] = diagnosticData.firmware;
        doc["freeHeap"] = diagnosticData.freeHeap;
        doc["minFreeHeap"] = diagnosticData.minFreeHeap;
        doc["heapSize"] = diagnosticData.heapSize;
        doc["maxAllocHeap"] = diagnosticData.maxAllocHeap;
        doc["failsInHour"] = diagnosticData.failsInHour;
        
        std::string output;
        serializeJson(doc, output);
        return output;
    }
};
