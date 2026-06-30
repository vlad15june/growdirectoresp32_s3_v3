#pragma once

#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Data/Entities/WifiNetworkEntry.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include "Application/DTOs/RuleStateUpdateDto.h"
#include "Core/Domain/Enums/SensorStatusEnum.h"
#include <ArduinoJson.h>

class ArduinoJsonConverter : public IJsonConverter
{
private:
    // Helper function to parse ISO 8601 time string to time_t
    time_t parseISOTimeToTimeT(const std::string& isoTimeStr) {
        if (isoTimeStr.length() < 19) return 0; // Invalid format
        
        // Extract time part: "HH:MM:SS" from "YYYY-MM-DDTHH:MM:SS+00:00"
        size_t timeStart = isoTimeStr.find('T');
        if (timeStart == std::string::npos) return 0;
        
        timeStart++; // Skip 'T'
        if (timeStart + 8 > isoTimeStr.length()) return 0;
        
        try {
            int hours = std::stoi(isoTimeStr.substr(timeStart, 2));
            int minutes = std::stoi(isoTimeStr.substr(timeStart + 3, 2));
            int seconds = std::stoi(isoTimeStr.substr(timeStart + 6, 2));
            
            // Convert to seconds since midnight
            return static_cast<time_t>(hours * 3600 + minutes * 60 + seconds);
        } catch (const std::exception&) {
            return 0;
        }
    }

    // Helper function to convert time_t (seconds since midnight) to ISO time string
    std::string timeToISOString(time_t secondsSinceMidnight) {
        if (secondsSinceMidnight <= 0) return "00:00:00";
        
        int hours = secondsSinceMidnight / 3600;
        int minutes = (secondsSinceMidnight % 3600) / 60;
        int seconds = secondsSinceMidnight % 60;
        
        // Format as HH:MM:SS - simplified without full date/timezone
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hours, minutes, seconds);
        
        // For schedule rules, we typically use today's date with the time
        return "2025-01-01T" + std::string(timeStr) + "+00:00";
    }

    // Helper function to convert internal pin numbering to external JSON format
    int convertPinToJson(int internalPin) {
        // Mapping: internal -> JSON
        // 0 -> 4, 1 -> 5, 2 -> 6, 3 -> 1
        switch (internalPin) {
            case 0: return 4;
            case 1: return 3;
            case 2: return 2;
            case 3: return 1;
            default: return internalPin; // Return unchanged if not in mapping
        }
    }

    // Helper function to convert external JSON pin numbering to internal format
    int convertPinFromJson(int jsonPin) {
        // Mapping: JSON -> internal
        // 4 -> 0, 5 -> 1, 6 -> 2, 1 -> 3
        switch (jsonPin) {
            case 4: return 0;
            case 3: return 1;
            case 2: return 2;
            case 1: return 3;
            default: return jsonPin; // Return unchanged if not in mapping
        }
    }

public:
    bool moduleStructureFromJson(const std::string &jsonStr, ModuleStructure &moduleStructure) override
    {
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        moduleStructure.growdirectorUid = doc["GrowdirectorUid"] | "";
        moduleStructure.firmware = doc["firmware"] | "";
        moduleStructure.email = doc["email"] | "";
        moduleStructure.accountUid = doc["accountUid"] | "";
        moduleStructure.latitude = doc["lat"] | 0.0;
        moduleStructure.longitude = doc["lng"] | 0.0;

        moduleStructure.nodes.clear();
        if (doc.containsKey("nodes"))
        {
            for (JsonObject nodeObj : doc["nodes"].as<JsonArray>())
            {
                Node node;
                node.type = nodeObj["type"] | "";
                node.subtype = nodeObj["subtype"] | "";
                node.bootType = nodeObj["bootType"] | "";
                node.fileSystem = nodeObj["fileSystem"] | "";
                node.psram = nodeObj["psram"] | "";
                node.uid = nodeObj["uid"] | "";
                node.firmware = nodeObj["firmware"] | "";
                node.ssid = nodeObj["ssid"] | "";
                node.ip = nodeObj["ip"] | "";
                node.chip = nodeObj["chip"] | "S3";
                node.board = nodeObj["board"] | 0;

                node.sensors.clear();
                if (nodeObj.containsKey("sensors"))
                {
                    for (JsonObject sensorObj : nodeObj["sensors"].as<JsonArray>())
                    {
                        Sensor sensor;
                        sensor.uid = sensorObj["uid"] | "";
                        sensor.type = sensorObj["type"] | "";
                        sensor.pin = convertPinFromJson(sensorObj["pin"] | 0);
                        sensor.group = sensorObj["group"] | 0;
                        sensor.modelUid = sensorObj["modelUid"] | "";
                        node.sensors.push_back(sensor);
                    }
                }

                node.relays.clear();
                if (nodeObj.containsKey("relays"))
                {
                    for (JsonObject relayObj : nodeObj["relays"].as<JsonArray>())
                    {
                        Relay relay;
                        relay.uid = relayObj["uid"] | "";
                        relay.pin = relayObj["pin"] | 0;
                        node.relays.push_back(relay);
                    }
                }

                node.pumps.clear();
                if (nodeObj.containsKey("pumps"))
                {
                    for (JsonObject pumpObj : nodeObj["pumps"].as<JsonArray>())
                    {
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

    std::string moduleStructureToJson(const ModuleStructure &moduleStructure) override
    {
        DynamicJsonDocument doc(4096);
        doc["GrowdirectorUid"] = moduleStructure.growdirectorUid;
        doc["firmware"] = moduleStructure.firmware;
        doc["email"] = moduleStructure.email;
        doc["accountUid"] = moduleStructure.accountUid;
        doc["lat"] = moduleStructure.latitude;
        doc["lng"] = moduleStructure.longitude;

        JsonArray nodesArr = doc.createNestedArray("nodes");
        for (const auto &node : moduleStructure.nodes)
        {
            JsonObject nodeObj = nodesArr.createNestedObject();
            nodeObj["type"] = node.type;
            nodeObj["subtype"] = node.subtype;
            nodeObj["bootType"] = node.bootType;
            nodeObj["fileSystem"] = node.fileSystem;
            nodeObj["psram"] = node.psram;
            nodeObj["uid"] = node.uid;
            nodeObj["firmware"] = node.firmware;
            nodeObj["ssid"] = node.ssid;
            nodeObj["ip"] = node.ip;
            nodeObj["chip"] = node.chip;
            nodeObj["board"] = node.board;

            if (!node.sensors.empty())
            {
                JsonArray sensorsArr = nodeObj.createNestedArray("sensors");
                for (const auto &sensor : node.sensors)
                {
                    JsonObject sensorObj = sensorsArr.createNestedObject();
                    sensorObj["uid"] = sensor.uid;
                    sensorObj["type"] = sensor.type;
                    sensorObj["pin"] = convertPinToJson(sensor.pin);
                    sensorObj["group"] = sensor.group;
                    sensorObj["modelUid"] = sensor.modelUid;
                }
            }

            if (!node.relays.empty())
            {
                JsonArray relaysArr = nodeObj.createNestedArray("relays");
                for (const auto &relay : node.relays)
                {
                    JsonObject relayObj = relaysArr.createNestedObject();
                    relayObj["uid"] = relay.uid;
                    relayObj["pin"] = relay.pin;
                }
            }

            if (!node.pumps.empty())
            {
                JsonArray pumpsArr = nodeObj.createNestedArray("pumps");
                for (const auto &pump : node.pumps)
                {
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

    std::string diagnosticToJson(const DiagnosticDto &diagnosticData) override
    {
        DynamicJsonDocument doc(2048);

        doc["rssi"] = diagnosticData.rssi;
        doc["uid"] = diagnosticData.uid;
        doc["email"] = diagnosticData.email;

        if (!diagnosticData.accountUid.empty())
        {
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
        doc["psramSize"] = diagnosticData.psramSize;
        doc["psramFree"] = diagnosticData.psramFree;
        doc["failsInHour"] = diagnosticData.failsInHour;

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool ruleFromJson(const std::string &jsonStr, Rule &rule) override
    {
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        // Common properties
        rule.uid = doc["uid"] | "";
        rule.version = doc["version"] | 0;
        rule.workflowUid = doc["workflowUid"] | "";
        rule.workflowVersion = doc["workflowVersion"] | 0;
        rule.workflowDisplayOrder = doc["workflowDisplayOrder"] | 0;
        rule.workflowPriority = doc["workflowPriority"] | 0;
        rule.dependsOnRuleUid = doc["dependsOnRuleUid"] | "";
        rule.deviceUid = doc["deviceUid"] | "";

        // Parse ruleType
        std::string ruleTypeStr = doc["ruleType"] | "";
        if (ruleTypeStr == "step")
            rule.ruleType = RuleType::STEP;
        else if (ruleTypeStr == "sensor")
            rule.ruleType = RuleType::SENSOR;
        else if (ruleTypeStr == "schedule")
            rule.ruleType = RuleType::SCHEDULE;
        else if (ruleTypeStr == "loop")
            rule.ruleType = RuleType::LOOP;
        else if (ruleTypeStr == "average_sensor")
            rule.ruleType = RuleType::AVERAGE_SENSOR;
        else if (ruleTypeStr == "ph")
            rule.ruleType = RuleType::PH;
        else if (ruleTypeStr == "ec")
            rule.ruleType = RuleType::EC;
        else if (ruleTypeStr == "dimmer_auto")
            rule.ruleType = RuleType::DIMMER_AUTO;
        else if (ruleTypeStr == "dimmer_manual")
            rule.ruleType = RuleType::DIMMER_MANUAL;
        else
            rule.ruleType = RuleType::UNKNOWN;

        // Parse mode
        std::string modeStr = doc["mode"] | "";
        rule.mode = RuleMode::OFF;
        if (modeStr == "On")
            rule.mode = RuleMode::ON;
        else if (modeStr == "WaitingForUserConfirm")
            rule.mode = RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM;

        if (doc.containsKey("modeReason") && !doc["modeReason"].isNull())
        {
            rule.modeReason = doc["modeReason"].as<std::string>();
        }
        else
        {
            rule.modeReason = "";
        }

        if (doc.containsKey("errorType") && !doc["errorType"].isNull())
        {
            rule.errorType = doc["errorType"].as<std::string>();
        }
        else
        {
            rule.errorType = "";
        }

        rule.protectiveShutdown = doc["protectiveShutdown"] | false;

        // Parse state
        std::string stateStr = doc["state"] | "";
        if (stateStr == "Synchronized")
            rule.state = RuleState::SYNCHRONIZED;
        else if (stateStr == "Synchronizing")
            rule.state = RuleState::SYNCHRONIZING;
        else if (stateStr == "Deleting")
            rule.state = RuleState::DELETING;
        else
            rule.state = RuleState::UNSYNCHRONIZED;

        // Parse arrays
        rule.daysOfWeek.clear();
        if (doc.containsKey("daysOfWeek"))
        {
            for (JsonVariant v : doc["daysOfWeek"].as<JsonArray>())
            {
                rule.daysOfWeek.push_back(v.as<int>());
            }
        }

        rule.daysOfMonth.clear();
        if (doc.containsKey("daysOfMonth"))
        {
            for (JsonVariant v : doc["daysOfMonth"].as<JsonArray>())
            {
                rule.daysOfMonth.push_back(v.as<int>());
            }
        }

        // Type-specific properties
        if (!rule.isScheduleRule())
        {
            rule.sensorUid = doc["sensorUid"] | "";

            // Parse direction
            std::string directionStr = doc["direction"] | "";
            if (directionStr == "up")
                rule.direction = RuleDirection::UP;
            else if (directionStr == "down")
                rule.direction = RuleDirection::DOWN;

            // Parse conditions
            rule.conditions.clear();
            if (doc.containsKey("conditions"))
            {
                for (JsonObject condObj : doc["conditions"].as<JsonArray>())
                {
                    RuleCondition condition;

                    // Parse sensorUids
                    condition.sensorUids.clear();
                    if (condObj.containsKey("sensorUids"))
                    {
                        for (JsonVariant v : condObj["sensorUids"].as<JsonArray>())
                        {
                            condition.sensorUids.push_back(v.as<std::string>());
                        }
                    }

                    if (rule.isStepRule())
                    {
                        condition.valueTarget = condObj["valueTarget"] | 0.0;
                    }
                    else
                    {
                        condition.valueLow = condObj["valueLow"] | 0.0;
                        condition.valueHigh = condObj["valueHigh"] | 0.0;
                    }

                    // Parse direction
                    std::string condDirStr = condObj["direction"] | "";
                    if (condDirStr == "up")
                        condition.direction = RuleDirection::UP;
                    else if (condDirStr == "down")
                        condition.direction = RuleDirection::DOWN;

                    rule.conditions.push_back(condition);
                }
            }

            rule.conditionOr = doc["conditionOr"] | false;

            if (doc.containsKey("delayOn") && !doc["delayOn"].isNull())
            {
                rule.delayOn = doc["delayOn"].as<int>();
            }
            else
            {
                rule.delayOn = 0;
            }

            if (doc.containsKey("delayOff") && !doc["delayOff"].isNull())
            {
                rule.delayOff = doc["delayOff"].as<int>();
            }
            else
            {
                rule.delayOff = 0;
            }
        }

        // Step rule specific properties
        if (rule.isStepRule())
        {
            rule.valueTarget = doc["valueTarget"] | 0.0;

            if (doc.containsKey("cyclesToSetPoint") && !doc["cyclesToSetPoint"].isNull())
            {
                rule.cyclesToSetPoint = doc["cyclesToSetPoint"].as<int>();
            }

            rule.workingTime = doc["workingTime"] | 0;
            rule.waitingTime = doc["waitingTime"] | 0;
        }

        // Loop / ph / ec working-cycle properties
        if (rule.ruleType == RuleType::LOOP ||
            rule.ruleType == RuleType::PH ||
            rule.ruleType == RuleType::EC)
        {
            rule.workingTime = doc["workingTime"] | 0;
            rule.waitingTime = doc["waitingTime"] | 0;

            if (doc.containsKey("cyclesToSetPoint") && !doc["cyclesToSetPoint"].isNull())
            {
                rule.cyclesToSetPoint = doc["cyclesToSetPoint"].as<int>();
            }
        }

        // Average sensor / ph / ec threshold properties
        if (rule.ruleType == RuleType::AVERAGE_SENSOR ||
            rule.ruleType == RuleType::PH ||
            rule.ruleType == RuleType::EC)
        {
            rule.valueLow = doc["valueLow"] | 0.0;
            rule.valueHigh = doc["valueHigh"] | 0.0;
        }

        // Sensor rule specific properties
        if (rule.isSensorRule())
        {
            rule.valueLow = doc["valueLow"] | 0.0;
            rule.valueHigh = doc["valueHigh"] | 0.0;
            rule.timeToSetPoint = doc["timeToSetPoint"] | 0;
            rule.isAdvanced = doc["isAdvanced"] | false;

            // TODO: These should handle time conversion properly
            if (doc.containsKey("startDayTime"))
            {
                rule.startDayTime = 0; // Should convert ISO time string
            }

            if (doc.containsKey("endDayTime"))
            {
                rule.endDayTime = 0; // Should convert ISO time string
            }

            // Parse advanced rule values
            rule.advancedRuleValues.clear();
            if (doc.containsKey("advancedRuleValues"))
            {
                for (JsonObject valObj : doc["advancedRuleValues"].as<JsonArray>())
                {
                    AdvancedRuleValue val;
                    // TODO: Parse time properly
                    val.time = 0;
                    val.value = valObj["value"] | 0.0;
                    rule.advancedRuleValues.push_back(val);
                }
            }
        }

        // Schedule rule specific properties
        if (rule.isScheduleRule())
        {
            rule.schedule.clear();
            if (doc.containsKey("schedule"))
            {
                for (JsonObject schedObj : doc["schedule"].as<JsonArray>())
                {
                    ScheduleItem item;
                    std::string startAtStr = schedObj["startAt"] | "";
                    item.startAt = parseISOTimeToTimeT(startAtStr);
                    item.duration = schedObj["duration"] | 0;
                    rule.schedule.push_back(item);
                }
            }
        }

        return true;
    }

    std::string ruleToJson(const Rule &rule) override
    {
        DynamicJsonDocument doc(4096);

        // Common properties
        doc["uid"] = rule.uid;
        doc["version"] = rule.version;
        doc["workflowUid"] = rule.workflowUid;
        doc["workflowVersion"] = rule.workflowVersion;
        doc["workflowDisplayOrder"] = rule.workflowDisplayOrder;
        doc["workflowPriority"] = rule.workflowPriority;
        doc["dependsOnRuleUid"] = rule.dependsOnRuleUid;
        doc["deviceUid"] = rule.deviceUid;

        // RuleType
        if (rule.ruleType == RuleType::STEP)
            doc["ruleType"] = "step";
        else if (rule.ruleType == RuleType::SENSOR)
            doc["ruleType"] = "sensor";
        else if (rule.ruleType == RuleType::SCHEDULE)
            doc["ruleType"] = "schedule";
        else if (rule.ruleType == RuleType::LOOP)
            doc["ruleType"] = "loop";
        else if (rule.ruleType == RuleType::AVERAGE_SENSOR)
            doc["ruleType"] = "average_sensor";
        else if (rule.ruleType == RuleType::PH)
            doc["ruleType"] = "ph";
        else if (rule.ruleType == RuleType::EC)
            doc["ruleType"] = "ec";
        else if (rule.ruleType == RuleType::DIMMER_AUTO)
            doc["ruleType"] = "dimmer_auto";
        else if (rule.ruleType == RuleType::DIMMER_MANUAL)
            doc["ruleType"] = "dimmer_manual";

        // Mode
        if (rule.mode == RuleMode::ON)
            doc["mode"] = "On";
        else if (rule.mode == RuleMode::OFF)
            doc["mode"] = "Off";
        else
            doc["mode"] = "Auto";

        if (!rule.modeReason.empty())
        {
            doc["modeReason"] = rule.modeReason;
        }
        else
        {
            doc["modeReason"] = nullptr;
        }

        if (!rule.errorType.empty())
        {
            doc["errorType"] = rule.errorType;
        }
        else
        {
            doc["errorType"] = nullptr;
        }

        doc["protectiveShutdown"] = rule.protectiveShutdown;

        // State
        if (rule.state == RuleState::SYNCHRONIZED)
            doc["state"] = "Synchronized";
        else if (rule.state == RuleState::SYNCHRONIZING)
            doc["state"] = "Synchronizing";
        else if (rule.state == RuleState::DELETING)
            doc["state"] = "Deleting";
        else
            doc["state"] = "Unsynchronized";

        // Arrays
        JsonArray daysOfWeekArr = doc.createNestedArray("daysOfWeek");
        for (int day : rule.daysOfWeek)
        {
            daysOfWeekArr.add(day);
        }

        JsonArray daysOfMonthArr = doc.createNestedArray("daysOfMonth");
        for (int day : rule.daysOfMonth)
        {
            daysOfMonthArr.add(day);
        }

        // Type-specific properties
        if (!rule.isScheduleRule())
        {
            doc["sensorUid"] = rule.sensorUid;

            // Direction
            if (rule.direction == RuleDirection::UP)
                doc["direction"] = "up";
            else
                doc["direction"] = "down";

            // Conditions
            JsonArray conditionsArr = doc.createNestedArray("conditions");
            for (const RuleCondition &cond : rule.conditions)
            {
                JsonObject condObj = conditionsArr.createNestedObject();

                // SensorUids array
                JsonArray sensorUidsArr = condObj.createNestedArray("sensorUids");
                for (const std::string &uid : cond.sensorUids)
                {
                    sensorUidsArr.add(uid);
                }

                if (rule.isStepRule())
                {
                    condObj["valueTarget"] = cond.valueTarget;
                }
                else
                {
                    condObj["valueLow"] = cond.valueLow;
                    condObj["valueHigh"] = cond.valueHigh;
                }

                // Direction
                if (cond.direction == RuleDirection::UP)
                    condObj["direction"] = "up";
                else
                    condObj["direction"] = "down";
            }

            doc["conditionOr"] = rule.conditionOr;

            if (rule.delayOn > 0)
            {
                doc["delayOn"] = rule.delayOn;
            }
            else
            {
                doc["delayOn"] = nullptr;
            }

            if (rule.delayOff > 0)
            {
                doc["delayOff"] = rule.delayOff;
            }
            else
            {
                doc["delayOff"] = nullptr;
            }
        }

        // Step rule specific properties
        if (rule.isStepRule())
        {
            doc["valueTarget"] = rule.valueTarget;

            if (rule.cyclesToSetPoint > 0)
            {
                doc["cyclesToSetPoint"] = rule.cyclesToSetPoint;
            }
            else
            {
                doc["cyclesToSetPoint"] = nullptr;
            }

            doc["workingTime"] = rule.workingTime;
            doc["waitingTime"] = rule.waitingTime;
        }

        // Loop / ph / ec working-cycle properties
        if (rule.ruleType == RuleType::LOOP ||
            rule.ruleType == RuleType::PH ||
            rule.ruleType == RuleType::EC)
        {
            doc["workingTime"] = rule.workingTime;
            doc["waitingTime"] = rule.waitingTime;

            if (rule.cyclesToSetPoint > 0)
            {
                doc["cyclesToSetPoint"] = rule.cyclesToSetPoint;
            }
            else
            {
                doc["cyclesToSetPoint"] = nullptr;
            }
        }

        // Average sensor / ph / ec threshold properties
        if (rule.ruleType == RuleType::AVERAGE_SENSOR ||
            rule.ruleType == RuleType::PH ||
            rule.ruleType == RuleType::EC)
        {
            doc["valueLow"] = rule.valueLow;
            doc["valueHigh"] = rule.valueHigh;
        }

        // Sensor rule specific properties
        if (rule.isSensorRule())
        {
            doc["valueLow"] = rule.valueLow;
            doc["valueHigh"] = rule.valueHigh;
            doc["timeToSetPoint"] = rule.timeToSetPoint;
            doc["isAdvanced"] = rule.isAdvanced;

            // TODO: Format time properly to ISO standard
            doc["startDayTime"] = "";
            doc["endDayTime"] = "";

            // Advanced rule values
            JsonArray advancedValuesArr = doc.createNestedArray("advancedRuleValues");
            for (const AdvancedRuleValue &val : rule.advancedRuleValues)
            {
                JsonObject valObj = advancedValuesArr.createNestedObject();
                // TODO: Format time properly
                valObj["time"] = "";
                valObj["value"] = val.value;
            }
        }

        // Schedule rule specific properties
        if (rule.isScheduleRule())
        {
            JsonArray scheduleArr = doc.createNestedArray("schedule");
            for (const ScheduleItem &item : rule.schedule)
            {
                JsonObject itemObj = scheduleArr.createNestedObject();
                itemObj["startAt"] = timeToISOString(item.startAt);
                itemObj["duration"] = item.duration;
            }
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool rulesFromJson(const std::string &jsonStr, std::vector<Rule> &rules) override
    {
        DynamicJsonDocument doc(16384); // Larger capacity for array
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        rules.clear();
        for (JsonObject ruleObj : doc.as<JsonArray>())
        {
            Rule rule;

            // Copy to a separate document for individual processing
            DynamicJsonDocument ruleDoc(4096);
            JsonObject destObj = ruleDoc.to<JsonObject>();

            // Copy all properties from the array element
            for (JsonPair kv : ruleObj)
            {
                destObj[kv.key()] = kv.value();
            }

            // Serialize and then parse this single rule
            std::string ruleJson;
            serializeJson(ruleDoc, ruleJson);

            if (ruleFromJson(ruleJson, rule))
            {
                rules.push_back(rule);
            }
        }

        return !rules.empty();
    }

    std::string rulesToJson(const std::vector<Rule> &rules) override
    {
        DynamicJsonDocument doc(16384); // Larger capacity for array
        JsonArray rulesArr = doc.to<JsonArray>();

        for (const Rule &rule : rules)
        {
            rulesArr.add(serialized(ruleToJson(rule)));
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    std::string updateToJson(const UpdateDto &updateData) override
    {
        DynamicJsonDocument doc(8192);

        doc["GrowdirectorUid"] = updateData.growdirectorUid;
        doc["email"] = updateData.email;

        if (!updateData.accountUid.empty())
        {
            doc["accountUid"] = updateData.accountUid;
        }

        doc["time"] = updateData.time;
        doc["millis"] = updateData.millis;

        JsonArray devicesArr = doc.createNestedArray("devices");
        for (const auto &device : updateData.devices)
        {
            JsonObject deviceObj = devicesArr.createNestedObject();
            deviceObj["uid"] = device.uid;

            if (!device.switchedAt.empty())
            {
                deviceObj["switchedAt"] = device.switchedAt;
            }

            deviceObj["state"] = device.state;
            deviceObj["state_hw"] = device.state_hw;
            deviceObj["isAuto"] = device.isAuto;
            deviceObj["isManual"] = device.isManual;

            if (!device.ruleUid.empty())
            {
                deviceObj["ruleUid"] = device.ruleUid;
                deviceObj["ruleType"] = device.ruleType;
            }

            if (device.isAuto)
            {
                deviceObj["isRuleTurnOnDevice"] = device.isRuleTurnOnDevice;

                if (!device.isRuleTurnOnDevice && device.state != "delay_on")
                {
                    deviceObj["ruleSensorStopAt"] = device.ruleSensorStopAt;
                    deviceObj["ruleSensorValueStop"] = device.ruleSensorValueStop;
                    deviceObj["ruleStopReason"] = device.ruleStopReason;
                }

                if (!device.ruleSensorStartAt.empty())
                {
                    deviceObj["ruleSensorStartAt"] = device.ruleSensorStartAt;
                }

                deviceObj["ruleVersion"] = device.ruleVersion;

                if (!device.workflowUid.empty())
                {
                    deviceObj["workflowUid"] = device.workflowUid;
                    deviceObj["workflowVersion"] = device.workflowVersion;
                }

                if (device.ruleType == "ph" || device.ruleType == "ec" ||
                    device.ruleType == "sensor" || device.ruleType == "step")
                {
                    deviceObj["sensorUid"] = device.sensorUid;
                    deviceObj["sensorValueLow"] = device.sensorValueLow;
                    deviceObj["sensorValueHigh"] = device.sensorValueHigh;
                    deviceObj["sensorValue"] = device.sensorValue;
                    deviceObj["ruleSensorValueStart"] = device.ruleSensorValueStart;

                    if (device.ruleType == "sensor" && device.timeToSetPoint > 0)
                    {
                        deviceObj["timeToSetPoint"] = device.timeToSetPoint;
                    }

                    if ((device.ruleType == "ph" || device.ruleType == "ec" ||
                         device.ruleType == "step") &&
                        device.cyclesToSetPoint > 0)
                    {
                        deviceObj["cyclesToSetPoint"] = device.cyclesToSetPoint;
                        deviceObj["currentCycle"] = device.currentCycle;
                    }
                }

                if (device.ruleType == "schedule")
                {
                    deviceObj["ruleScheduleStartAt"] = device.ruleScheduleStartAt;
                    deviceObj["ruleScheduleDuration"] = device.ruleScheduleDuration;
                    deviceObj["secondsToSwitchBack"] = device.secondsToSwitchBack;
                    deviceObj["secondsEnabledFor"] = device.secondsEnabledFor;
                }

                if (device.ruleType == "ph" || device.ruleType == "ec" ||
                    device.ruleType == "step" || device.ruleType == "loop")
                {
                    deviceObj["secondsToSwitchBack"] = device.secondsToSwitchBack;
                    deviceObj["secondsDisabledFor"] = device.secondsDisabledFor;
                    deviceObj["secondsEnabledFor"] = device.secondsEnabledFor;
                }
            }

            if (device.isManual)
            {
                deviceObj["commandId"] = device.commandId;
                deviceObj["secondsToSwitchBack"] = device.secondsToSwitchBack;
                deviceObj["secondsEnabledFor"] = device.secondsEnabledFor;
            }
        }

        JsonArray sensorsArr = doc.createNestedArray("sensors");
        for (const auto &sensor : updateData.sensors)
        {
            JsonObject sensorObj = sensorsArr.createNestedObject();
            sensorObj["uid"] = sensor.uid;
            sensorObj["value"] = sensor.value;
            sensorObj["state"] = sensor.state;
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool commandFromJson(const std::string &jsonStr, CommandDto &commandDto) override
    {
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        commandDto.growDirectorUid = doc["growDirectorUid"] | "";
        commandDto.type = doc["type"] | "";
        commandDto.createdAt = doc["createdAt"] | "";
        commandDto.commandUid = doc["commandUid"] | "";

        // DeviceCommandNotification fields
        if (commandDto.isDeviceCommand())
        {
            commandDto.deviceUid = doc["deviceUid"] | "";
            commandDto.commandId = doc["commandId"] | 0;
            commandDto.requestedState = doc["requestedState"] | "";
        }

        // RuleCommandNotification fields
        if (commandDto.isRuleCommand())
        {
            commandDto.ruleUid = doc["ruleUid"] | "";
            commandDto.rulesPayload = doc["rulesPayload"] | "";
        }

        // ModuleCommandNotification fields
        if (commandDto.isModuleCommand())
        {
            commandDto.command = doc["command"] | "";
            commandDto.version = doc["version"] | "";
            commandDto.url = doc["url"] | "";

            DynamicJsonDocument optionsDoc(512);
            JsonObject optionsObj = optionsDoc.to<JsonObject>();

            if (doc.containsKey("options") && doc["options"].is<JsonObject>())
            {
                for (JsonPair kv : doc["options"].as<JsonObject>())
                {
                    optionsObj[kv.key()] = kv.value();
                }
            }

            if (doc.containsKey("noScanSensors") && !optionsObj.containsKey("noScanSensors"))
            {
                optionsObj["noScanSensors"] = doc["noScanSensors"];
            }

            if (commandDto.command == "ModbusWrite" || commandDto.command == "ModbusRead" || commandDto.command == "SerialWrite")
            {
                const char* rawKeys[] = {"channel", "address", "function", "register", "count", "data", "bytes", "expectResponse", "baudRate"};
                for (const char* rawKey : rawKeys)
                {
                    if (doc.containsKey(rawKey) && !optionsObj.containsKey(rawKey))
                    {
                        optionsObj[rawKey] = doc[rawKey];
                    }
                }
            }

            if (optionsObj.size() > 0)
            {
                std::string optionsJson;
                serializeJson(optionsObj, optionsJson);
                commandDto.options = optionsJson;
            }
        }

        // SensorTypeCommandNotification fields
        if (commandDto.isSensorTypeCommand())
        {
            commandDto.sensorUid = doc["sensorUid"] | "";
            commandDto.sensorType = doc["sensorType"] | "";
            commandDto.model = doc["model"] | "";
            commandDto.command = doc["command"] | "";
            
            // Serialize options object back to JSON string for later parsing
            if (doc.containsKey("options"))
            {
                std::string optionsJson;
                serializeJson(doc["options"], optionsJson);
                commandDto.options = optionsJson;
            }
        }

        return true;
    }

    std::string commandToJson(const CommandDto &commandDto) override
    {
        DynamicJsonDocument doc(4096);

        doc["growDirectorUid"] = commandDto.growDirectorUid;
        doc["type"] = commandDto.type;
        
        if (!commandDto.createdAt.empty())
        {
            doc["createdAt"] = commandDto.createdAt;
        }
        
        if (!commandDto.commandUid.empty())
        {
            doc["commandUid"] = commandDto.commandUid;
        }

        // DeviceCommandNotification fields
        if (commandDto.isDeviceCommand())
        {
            doc["deviceUid"] = commandDto.deviceUid;
            doc["commandId"] = commandDto.commandId;
            doc["requestedState"] = commandDto.requestedState;
        }

        // RuleCommandNotification fields
        if (commandDto.isRuleCommand())
        {
            doc["ruleUid"] = commandDto.ruleUid;
            doc["rulesPayload"] = commandDto.rulesPayload;
        }

        // ModuleCommandNotification fields
        if (commandDto.isModuleCommand())
        {
            doc["command"] = commandDto.command;
            doc["version"] = commandDto.version;
            doc["url"] = commandDto.url;
        }

        // SensorTypeCommandNotification fields
        if (commandDto.isSensorTypeCommand())
        {
            doc["sensorUid"] = commandDto.sensorUid;
            doc["sensorType"] = commandDto.sensorType;
            
            if (!commandDto.model.empty())
            {
                doc["model"] = commandDto.model;
            }
            
            if (!commandDto.command.empty())
            {
                doc["command"] = commandDto.command;
            }
            
            // Parse options JSON string back to object
            if (!commandDto.options.empty())
            {
                DynamicJsonDocument optionsDoc(2048);
                DeserializationError optionsError = deserializeJson(optionsDoc, commandDto.options);
                if (!optionsError)
                {
                    doc["options"] = optionsDoc;
                }
            }
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    std::string commandResultToJson(const CommandResultDto &commandResultDto) override
    {
        DynamicJsonDocument doc(512);
        
        doc["status"] = commandResultDto.status;
        doc["command"] = commandResultDto.command;
        
        if (!commandResultDto.message.empty())
        {
            doc["message"] = commandResultDto.message;
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    std::string rawIoResultToJson(const RawIoResultDto &rawIoResult) override
    {
        DynamicJsonDocument doc(1024);

        doc["command"] = rawIoResult.command;
        doc["channel"] = rawIoResult.channel;
        if (rawIoResult.isError)
        {
            doc["error"] = rawIoResult.errorMessage;
        }
        else
        {
            doc["data"] = rawIoResult.dataHex;
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool commandResultFromJson(const std::string &jsonStr, CommandResultDto &commandResultDto) override
    {
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        commandResultDto.status = doc["status"] | "";
        commandResultDto.command = doc["command"] | "";
        commandResultDto.message = doc["message"] | "";

        return true;
    }

    std::string deviceCommandUpdateToJson(const DeviceCommandUpdateDto& commandUpdateDto) override
    {
        DynamicJsonDocument doc(1024);
        JsonArray arr = doc.to<JsonArray>();
        JsonObject obj = arr.createNestedObject();

        obj["uid"] = commandUpdateDto.uid;
        obj["status"] = commandUpdateDto.status;
        obj["errorMessage"] = commandUpdateDto.errorMessage;
        if (!commandUpdateDto.timestamp.empty())
            obj["timestamp"] = commandUpdateDto.timestamp;
        if (!commandUpdateDto.accountUid.empty())
            obj["accountUid"] = commandUpdateDto.accountUid;

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool deviceCommandFromJson(const std::string &jsonStr, DeviceCommandDto &commandDto) override
    {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        commandDto.id = doc["id"] | 0;
        commandDto.uid = doc["uid"] | "";
        commandDto.requestedState = doc["requestedState"] | "";
        commandDto.analogControlPinValue = doc["analogControlPinValue"] | 0;
        commandDto.status = doc["status"] | "";
        commandDto.needToSwitchBack = doc["needToSwitchBack"] | false;
        commandDto.secondsToSwitchBack = doc["secondsToSwitchBack"] | 0;
        commandDto.switchType = doc["switchType"] | "";
        commandDto.errorMessage = doc["errorMessage"] | "";
        commandDto.createdDate = doc["createdDate"] | "";
        commandDto.updatedDate = doc["updatedDate"] | "";

        return true;
    }

    std::string deviceCommandToJson(const DeviceCommandDto &commandDto) override
    {
        DynamicJsonDocument doc(2048);

        doc["id"] = commandDto.id;
        doc["uid"] = commandDto.uid;
        doc["requestedState"] = commandDto.requestedState;
        
        if (commandDto.analogControlPinValue > 0)
        {
            doc["analogControlPinValue"] = commandDto.analogControlPinValue;
        }
        
        doc["status"] = commandDto.status;
        doc["needToSwitchBack"] = commandDto.needToSwitchBack;
        
        if (commandDto.secondsToSwitchBack > 0)
        {
            doc["secondsToSwitchBack"] = commandDto.secondsToSwitchBack;
        }
        
        if (!commandDto.switchType.empty())
        {
            doc["switchType"] = commandDto.switchType;
        }
        
        if (!commandDto.errorMessage.empty())
        {
            doc["errorMessage"] = commandDto.errorMessage;
        }
        
        if (!commandDto.createdDate.empty())
        {
            doc["createdDate"] = commandDto.createdDate;
        }
        
        if (!commandDto.updatedDate.empty())
        {
            doc["updatedDate"] = commandDto.updatedDate;
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool deviceCommandsFromJson(const std::string &jsonStr, std::vector<DeviceCommandDto> &commands) override
    {
        DynamicJsonDocument doc(8192); // Larger capacity for array
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        commands.clear();
        
        // Check if it's an array
        if (doc.is<JsonArray>())
        {
            for (JsonObject commandObj : doc.as<JsonArray>())
            {
                DeviceCommandDto command;
                command.id = commandObj["id"] | 0;
                command.uid = commandObj["uid"] | "";
                command.requestedState = commandObj["requestedState"] | "";
                command.analogControlPinValue = commandObj["analogControlPinValue"] | 0;
                command.status = commandObj["status"] | "";
                command.needToSwitchBack = commandObj["needToSwitchBack"] | false;
                command.secondsToSwitchBack = commandObj["secondsToSwitchBack"] | 0;
                command.switchType = commandObj["switchType"] | "";
                command.errorMessage = commandObj["errorMessage"] | "";
                command.createdDate = commandObj["createdDate"] | "";
                command.updatedDate = commandObj["updatedDate"] | "";
                
                commands.push_back(command);
            }
        }
        else if (doc.is<JsonObject>())
        {
            // Single object - treat as single command
            DeviceCommandDto command;
            if (deviceCommandFromJson(jsonStr, command))
            {
                commands.push_back(command);
            }
        }

        return !commands.empty();
    }

    std::string ruleStateToJson(const RuleStateDto& ruleState) override
    {
        DynamicJsonDocument doc(512);

        doc["uid"] = ruleState.uid;
        doc["state"] = ruleState.state;
        doc["mode"] = ruleState.mode;
        
        if (!ruleState.modeReason.empty())
        {
            doc["modeReason"] = ruleState.modeReason;
        }
        
        if (!ruleState.errorType.empty())
        {
            doc["errorType"] = ruleState.errorType;
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool ruleStateFromJson(const std::string& jsonStr, RuleStateDto& ruleState) override
    {
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        ruleState.uid = doc["uid"] | "";
        ruleState.state = doc["state"] | "Synchronized";
        ruleState.mode = doc["mode"] | "Off";
        ruleState.modeReason = doc["modeReason"] | "";
        ruleState.errorType = doc["errorType"] | "";

        return true;
    }

    std::string ruleStateUpdateToJson(const RuleStateUpdateDto& ruleStateUpdate) override
    {
        DynamicJsonDocument doc(4096);

        doc["timestamp"] = ruleStateUpdate.timestamp;
        doc["status"] = ruleStateUpdate.status;
        
        if (!ruleStateUpdate.errorType.empty())
        {
            doc["errorType"] = ruleStateUpdate.errorType;
        }
        
        if (!ruleStateUpdate.accountUid.empty())
        {
            doc["accountUid"] = ruleStateUpdate.accountUid;
        }

        JsonArray rulesArr = doc.createNestedArray("rules");
        for (const auto& ruleState : ruleStateUpdate.rules)
        {
            JsonObject ruleObj = rulesArr.createNestedObject();
            ruleObj["uid"] = ruleState.uid;
            ruleObj["version"] = ruleState.version;
            ruleObj["state"] = ruleState.state;
            ruleObj["mode"] = ruleState.mode;
            
            if (!ruleState.modeReason.empty())
            {
                ruleObj["modeReason"] = ruleState.modeReason;
            }
            
            if (!ruleState.errorType.empty())
            {
                ruleObj["errorType"] = ruleState.errorType;
            }
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    std::string ruleStateUpdateSingleToJson(const RuleStateDto& ruleState, const std::string& timestamp) override
    {
        DynamicJsonDocument doc(1024);

        doc["timestamp"] = timestamp;
        if (!ruleState.accountUid.empty())
        {
            doc["accountUid"] = ruleState.accountUid;
        }
        doc["uid"] = ruleState.uid;
        doc["state"] = ruleState.state;
        doc["mode"] = ruleState.mode;
        doc["modeReason"] = ruleState.modeReason;

        if (!ruleState.errorType.empty())
        {
            doc["errorType"] = ruleState.errorType;
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    std::string ruleSensorOfflineEventToJson(const RuleSensorOfflineEventDto& event) override
    {
        DynamicJsonDocument doc(512);

        doc["uid"] = event.uid;
        doc["accountUid"] = event.accountUid;
        doc["eventType"] = event.eventType;
        doc["sensorUid"] = event.sensorUid;
        doc["ruleUid"] = event.ruleUid;
        doc["deviceUid"] = event.deviceUid;

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool ruleStateUpdateFromJson(const std::string& jsonStr, RuleStateUpdateDto& ruleStateUpdate) override
    {
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        ruleStateUpdate.timestamp = doc["timestamp"] | "";
        ruleStateUpdate.status = doc["status"] | "Success";
        ruleStateUpdate.errorType = doc["errorType"] | "";
        ruleStateUpdate.accountUid = doc["accountUid"] | "";

        ruleStateUpdate.rules.clear();
        if (doc.containsKey("rules"))
        {
            for (JsonObject ruleObj : doc["rules"].as<JsonArray>())
            {
                RuleStateDto ruleState;
                ruleState.uid = ruleObj["uid"] | "";
                ruleState.version = ruleObj["version"] | 0;
                ruleState.state = ruleObj["state"] | "Synchronized";
                ruleState.mode = ruleObj["mode"] | "Off";
                ruleState.modeReason = ruleObj["modeReason"] | "";
                ruleState.errorType = ruleObj["errorType"] | "";
                
                ruleStateUpdate.rules.push_back(ruleState);
            }
        }

        return true;
    }
    
    bool sensorFromJson(const std::string &jsonStr, SensorEntity &sensor) override
    {
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        sensor.uid = doc["uid"] | "";
        sensor.sensorType = doc["sensorType"] | "";
        sensor.model = doc["model"] | "";
        sensor.protocol = doc["protocol"] | "";
        sensor.createdAt = doc["createdAt"] | "";
        sensor.isActive = doc["isActive"] | true;
        sensor.isError = doc["isError"] | false;
        sensor.pin = doc["pin"] | -1;
        
        std::string statusStr = doc["status"] | "new";
        sensor.status = getSensorStatusFromString(statusStr);
        
        sensor.modbusDeviceAddress = doc["modbusDeviceAddress"] | 1;
        sensor.baudRate = doc["baudRate"] | 9600;
        sensor.modbusPingAddress = doc["modbusPingAddress"] | 0;
        sensor.registerToStoreModbusDeviceAddress = doc["registerToStoreModbusDeviceAddress"] | 0;
        sensor.writeRegistersCommand = doc["writeRegistersCommand"] | 0;
        sensor.i2cAddress = doc["i2cAddress"] | "";
        sensor.chipType = doc["chipType"] | "";
        
        sensor.currentValue = doc["currentValue"] | 0.0f;
        sensor.lastReadTime = doc["lastReadTime"] | 0;
        
        sensor.sensorTypeId = doc["sensorTypeId"] | 0;
        sensor.sensorTypeUid = doc["sensorTypeUid"] | "";

        // Note: channelValues are now handled by SensorValueModel, not stored in SensorEntity

        // Parse modbus register configs
        sensor.modbusRegisterConfigs.clear();
        if (doc.containsKey("modbusRegisterConfigs"))
        {
            for (JsonObject configObj : doc["modbusRegisterConfigs"].as<JsonArray>())
            {
                ModbusRegisterConfig config;
                config.functionCode = configObj["functionCode"] | 3;
                config.startRegister = configObj["startRegister"] | 0;
                config.numRegisters = configObj["numRegisters"] | 1;
                config.multiplier = configObj["multiplier"] | 1.0f;
                config.offset = configObj["offset"] | 0.0f;
                config.channelName = configObj["channel_name"] | "";
                config.channel = configObj["channel"] | 0;
                config.sensorType = configObj["sensorType"] | "";
                config.canConnectMinValue = configObj["canConnectMinValue"] | 0.0f;
                config.canConnectMaxValue = configObj["canConnectMaxValue"] | 100.0f;
                sensor.modbusRegisterConfigs.push_back(config);
            }
        }

        // Parse i2c register configs
        sensor.i2cRegisterConfigs.clear();
        if (doc.containsKey("i2cRegisterConfigs"))
        {
            for (JsonObject configObj : doc["i2cRegisterConfigs"].as<JsonArray>())
            {
                I2CRegisterConfig config;
                config.registerAddr = configObj["register"] | 0;
                config.length = configObj["length"] | 1;
                config.multiplier = configObj["multiplier"] | 1.0f;
                config.offset = configObj["offset"] | 0.0f;
                config.channelName = configObj["channel_name"] | "";
                config.channel = configObj["channel"] | 0;
                config.sensorType = configObj["sensorType"] | "";
                sensor.i2cRegisterConfigs.push_back(config);
            }
        }

        return true;
    }

    std::string sensorToJson(const SensorEntity &sensor) override
    {
        DynamicJsonDocument doc(4096);
        
        doc["uid"] = sensor.uid;
        doc["sensorType"] = sensor.sensorType;
        doc["model"] = sensor.model;
        doc["protocol"] = sensor.protocol;
        doc["createdAt"] = sensor.createdAt;
        doc["isActive"] = sensor.isActive;
        doc["isError"] = sensor.isError;
        doc["status"] = getSensorStatusName(sensor.status);
        doc["pin"] = sensor.pin;
        
        doc["modbusDeviceAddress"] = sensor.modbusDeviceAddress;
        doc["baudRate"] = sensor.baudRate;
        doc["modbusPingAddress"] = sensor.modbusPingAddress;
        doc["registerToStoreModbusDeviceAddress"] = sensor.registerToStoreModbusDeviceAddress;
        doc["writeRegistersCommand"] = sensor.writeRegistersCommand;
        doc["i2cAddress"] = sensor.i2cAddress;
        doc["chipType"] = sensor.chipType;
        
        doc["currentValue"] = sensor.currentValue;
        doc["lastReadTime"] = sensor.lastReadTime;
        
        doc["sensorTypeId"] = sensor.sensorTypeId;
        doc["sensorTypeUid"] = sensor.sensorTypeUid;

        // Note: channelValues are now handled by SensorValueModel and not serialized here

        // Add modbus register configs
        if (!sensor.modbusRegisterConfigs.empty())
        {
            JsonArray modbusConfigArr = doc.createNestedArray("modbusRegisterConfigs");
            for (const auto& config : sensor.modbusRegisterConfigs)
            {
                JsonObject configObj = modbusConfigArr.createNestedObject();
                configObj["functionCode"] = config.functionCode;
                configObj["startRegister"] = config.startRegister;
                configObj["numRegisters"] = config.numRegisters;
                configObj["multiplier"] = config.multiplier;
                configObj["offset"] = config.offset;
                configObj["channel_name"] = config.channelName;
                configObj["channel"] = config.channel;
                configObj["sensorType"] = config.sensorType;
                configObj["canConnectMinValue"] = config.canConnectMinValue;
                configObj["canConnectMaxValue"] = config.canConnectMaxValue;
            }
        }

        // Add i2c register configs
        if (!sensor.i2cRegisterConfigs.empty())
        {
            JsonArray i2cConfigArr = doc.createNestedArray("i2cRegisterConfigs");
            for (const auto& config : sensor.i2cRegisterConfigs)
            {
                JsonObject configObj = i2cConfigArr.createNestedObject();
                configObj["register"] = config.registerAddr;
                configObj["length"] = config.length;
                configObj["multiplier"] = config.multiplier;
                configObj["offset"] = config.offset;
                configObj["channel_name"] = config.channelName;
                configObj["channel"] = config.channel;
                configObj["sensorType"] = config.sensorType;
            }
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool sensorsFromJson(const std::string &jsonStr, std::vector<SensorEntity> &sensors) override
    {
        DynamicJsonDocument doc(16384); // Larger capacity for array
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        sensors.clear();
        for (JsonObject sensorObj : doc.as<JsonArray>())
        {
            SensorEntity sensor;

            // Copy to a separate document for individual processing
            DynamicJsonDocument sensorDoc(4096);
            JsonObject destObj = sensorDoc.to<JsonObject>();

            // Copy all properties from the array element
            for (JsonPair kv : sensorObj)
            {
                destObj[kv.key()] = kv.value();
            }

            // Serialize and then parse this single sensor
            std::string sensorJson;
            serializeJson(sensorDoc, sensorJson);

            if (sensorFromJson(sensorJson, sensor))
            {
                sensors.push_back(sensor);
            }
        }

        return !sensors.empty();
    }

    std::string sensorsToJson(const std::vector<SensorEntity> &sensors) override
    {
        DynamicJsonDocument doc(16384); // Larger capacity for array
        JsonArray sensorsArr = doc.to<JsonArray>();

        for (const SensorEntity &sensor : sensors)
        {
            sensorsArr.add(serialized(sensorToJson(sensor)));
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool modbusRegisterConfigFromJson(const std::string &jsonStr, ModbusRegisterConfig &config) override
    {
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        config.functionCode = doc["functionCode"] | 3;
        config.startRegister = doc["startRegister"] | 0;
        config.numRegisters = doc["numRegisters"] | 1;
        config.multiplier = doc["multiplier"] | 1.0f;
        config.offset = doc["offset"] | 0.0f;
        config.channelName = doc["channel_name"] | "";
        config.channel = doc["channel"] | 0;
        config.sensorType = doc["sensorType"] | "";
        config.canConnectMinValue = doc["canConnectMinValue"] | 0.0f;
        config.canConnectMaxValue = doc["canConnectMaxValue"] | 100.0f;

        return true;
    }

    std::string modbusRegisterConfigToJson(const ModbusRegisterConfig &config) override
    {
        DynamicJsonDocument doc(1024);

        doc["functionCode"] = config.functionCode;
        doc["startRegister"] = config.startRegister;
        doc["numRegisters"] = config.numRegisters;
        doc["multiplier"] = config.multiplier;
        doc["offset"] = config.offset;
        doc["channel_name"] = config.channelName;
        doc["channel"] = config.channel;
        doc["sensorType"] = config.sensorType;
        doc["canConnectMinValue"] = config.canConnectMinValue;
        doc["canConnectMaxValue"] = config.canConnectMaxValue;

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool modbusRegisterConfigsFromJson(const std::string &jsonStr, std::vector<ModbusRegisterConfig> &configs) override
    {
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        configs.clear();
        
        if (doc.is<JsonArray>())
        {
            for (JsonObject configObj : doc.as<JsonArray>())
            {
                ModbusRegisterConfig config;
                config.functionCode = configObj["functionCode"] | 3;
                config.startRegister = configObj["startRegister"] | 0;
                config.numRegisters = configObj["numRegisters"] | 1;
                config.multiplier = configObj["multiplier"] | 1.0f;
                config.offset = configObj["offset"] | 0.0f;
                config.channelName = configObj["channel_name"] | "";
                config.channel = configObj["channel"] | 0;
                config.sensorType = configObj["sensorType"] | "";
                config.canConnectMinValue = configObj["canConnectMinValue"] | 0.0f;
                config.canConnectMaxValue = configObj["canConnectMaxValue"] | 100.0f;
                
                configs.push_back(config);
            }
        }

        return !configs.empty();
    }

    std::string modbusRegisterConfigsToJson(const std::vector<ModbusRegisterConfig> &configs) override
    {
        DynamicJsonDocument doc(4096);
        JsonArray configsArr = doc.to<JsonArray>();

        for (const ModbusRegisterConfig &config : configs)
        {
            JsonObject configObj = configsArr.createNestedObject();
            configObj["functionCode"] = config.functionCode;
            configObj["startRegister"] = config.startRegister;
            configObj["numRegisters"] = config.numRegisters;
            configObj["multiplier"] = config.multiplier;
            configObj["offset"] = config.offset;
            configObj["channel_name"] = config.channelName;
            configObj["channel"] = config.channel;
            configObj["sensorType"] = config.sensorType;
            configObj["canConnectMinValue"] = config.canConnectMinValue;
            configObj["canConnectMaxValue"] = config.canConnectMaxValue;
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool i2cRegisterConfigFromJson(const std::string &jsonStr, I2CRegisterConfig &config) override
    {
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        config.registerAddr = doc["register"] | 0;
        config.length = doc["length"] | 1;
        config.multiplier = doc["multiplier"] | 1.0f;
        config.offset = doc["offset"] | 0.0f;
        config.channelName = doc["channel_name"] | "";
        config.channel = doc["channel"] | 0;
        config.sensorType = doc["sensorType"] | "";

        return true;
    }

    std::string i2cRegisterConfigToJson(const I2CRegisterConfig &config) override
    {
        DynamicJsonDocument doc(512);

        doc["register"] = config.registerAddr;
        doc["length"] = config.length;
        doc["multiplier"] = config.multiplier;
        doc["offset"] = config.offset;
        doc["channel_name"] = config.channelName;
        doc["channel"] = config.channel;
        doc["sensorType"] = config.sensorType;

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool i2cRegisterConfigsFromJson(const std::string &jsonStr, std::vector<I2CRegisterConfig> &configs) override
    {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        configs.clear();
        
        if (doc.is<JsonArray>())
        {
            for (JsonObject configObj : doc.as<JsonArray>())
            {
                I2CRegisterConfig config;
                config.registerAddr = configObj["register"] | 0;
                config.length = configObj["length"] | 1;
                config.multiplier = configObj["multiplier"] | 1.0f;
                config.offset = configObj["offset"] | 0.0f;
                config.channelName = configObj["channel_name"] | "";
                config.channel = configObj["channel"] | 0;
                config.sensorType = configObj["sensorType"] | "";
                
                configs.push_back(config);
            }
        }

        return !configs.empty();
    }

    std::string i2cRegisterConfigsToJson(const std::vector<I2CRegisterConfig> &configs) override
    {
        DynamicJsonDocument doc(2048);
        JsonArray configsArr = doc.to<JsonArray>();

        for (const auto &config : configs)
        {
            JsonObject configObj = configsArr.createNestedObject();
            configObj["register"] = config.registerAddr;
            configObj["length"] = config.length;
            configObj["multiplier"] = config.multiplier;
            configObj["offset"] = config.offset;
            configObj["channel_name"] = config.channelName;
            configObj["channel"] = config.channel;
            configObj["sensorType"] = config.sensorType;
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool sensorTypeFromJson(const std::string &jsonStr, SensorType &sensorType) override
    {
        DynamicJsonDocument doc(8192);
        
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error) {
            //Serial.printf("ArduinoJsonConverter: sensorTypeFromJson error: %s (JSON length: %d)\n", 
            //             error.c_str(), jsonStr.length());
            return false;
        }

        // Parse main sensor type properties
        sensorType.uid = doc["uid"] | "";
        sensorType.name = doc["name"] | "";
        sensorType.displayName = doc["displayName"] | "";
        sensorType.canScan = doc["canScan"] | true;
        sensorType.isSupported = doc["isSupported"] | true;
        sensorType.createdAt = doc["createdAt"] | "";
        sensorType.updatedAt = doc["updatedAt"] | "";
        
        std::string protocolStr = doc["protocol"] | "";
        sensorType.setProtocolFromString(protocolStr);
        
        sensorType.i2cAddress = doc["i2cAddress"] | "";
        sensorType.chipType = doc["chipType"] | "";
        sensorType.modbusDeviceAddress = doc["modbusDeviceAddress"] | 1;
        sensorType.baudRate = doc["baudRate"] | 9600;
        sensorType.modbusPingAddress = doc["modbusPingAddress"] | 0;
        sensorType.registerToStoreModbusDeviceAddress = doc["registerToStoreModbusDeviceAddress"] | 0;
        sensorType.writeRegistersCommand = doc["writeRegistersCommand"] | 0;
        sensorType.priorityForScan = doc["priorityForScan"] | 0;

        // Parse legacy id for backward compatibility
        sensorType.id = doc["id"] | 0;

        // Parse sensorTypes array
        sensorType.sensorTypes.clear();
        if (doc.containsKey("sensorTypes"))
        {
            for (JsonObject sensorTypeObj : doc["sensorTypes"].as<JsonArray>())
            {
                SubSensorType subSensorType;
                subSensorType.name = sensorTypeObj["name"] | "";
                subSensorType.displayName = sensorTypeObj["displayName"] | "";
                subSensorType.isAnalog = sensorTypeObj["isAnalog"] | false;
                subSensorType.canCalibrate = sensorTypeObj["canCalibrate"] | false;
                subSensorType.validCalibrationPeriodDays = sensorTypeObj["validCalibrationPeriodDays"] | 0;
                subSensorType.calibrationTimeInSeconds = sensorTypeObj["calibrationTimeInSeconds"] | 0;
                subSensorType.minValue = sensorTypeObj["minValue"] | 0.0f;
                subSensorType.maxValue = sensorTypeObj["maxValue"] | 0.0f;
                subSensorType.limitMinValue = sensorTypeObj["limitMinValue"] | 0.0f;
                subSensorType.limitMaxValue = sensorTypeObj["limitMaxValue"] | 0.0f;
                subSensorType.unit = sensorTypeObj["unit"] | "";
                subSensorType.step = sensorTypeObj["step"] | 0.0f;
                subSensorType.precisionPercent = sensorTypeObj["precisionPercent"] | 0.0f;
                subSensorType.precisionValue = sensorTypeObj["precisionValue"] | 0.0f;
                subSensorType.createdAt = sensorTypeObj["createdAt"] | "";
                subSensorType.updatedAt = sensorTypeObj["updatedAt"] | "";
                sensorType.sensorTypes.push_back(subSensorType);
            }
        }

        // Parse I2C register config (renamed in JSON)
        sensorType.i2cRegisterConfig.clear();
        if (doc.containsKey("i2CRegisterConfigs"))
        {
            for (JsonObject configObj : doc["i2CRegisterConfigs"].as<JsonArray>())
            {
                I2CRegisterConfig config;
                config.registerAddr = configObj["register"] | 0;
                config.length = configObj["length"] | 1;
                config.multiplier = configObj["multiplier"] | 1.0f;
                config.offset = configObj["offset"] | 0.0f;
                sensorType.i2cRegisterConfig.push_back(config);
            }
        }

        // Parse Modbus register config (renamed in JSON)
        sensorType.modbusRegisterConfig.clear();
        if (doc.containsKey("modbusRegisterConfigs"))
        {
            for (JsonObject configObj : doc["modbusRegisterConfigs"].as<JsonArray>())
            {
                ModbusRegisterConfig config;
                config.functionCode = configObj["functionCode"] | 3;
                config.startRegister = configObj["startRegister"] | 0;
                config.numRegisters = configObj["numRegisters"] | 1;
                config.multiplier = configObj["multiplier"] | 1.0f;
                config.offset = configObj["offset"] | 0.0f;
                config.channelName = configObj["channelName"] | "";
                config.channel = configObj["channel"] | 0;
                config.sensorType = configObj["sensorType"] | "";
                config.canConnectMinValue = configObj["canConnectMinValue"] | 0.0f;
                config.canConnectMaxValue = configObj["canConnectMaxValue"] | 100.0f;
                sensorType.modbusRegisterConfig.push_back(config);
            }
        }

        // Parse analog pin configs if present
        sensorType.analogPinConfig.clear();
        if (doc.containsKey("analogPinConfigs"))
        {
            for (JsonObject configObj : doc["analogPinConfigs"].as<JsonArray>())
            {
                AnalogPinConfig config;
                // Add analog pin config parsing here when needed
                sensorType.analogPinConfig.push_back(config);
            }
        }

        return true;
    }

    std::string sensorTypeToJson(const SensorType &sensorType) override
    {
        //Serial.printf("ArduinoJsonConverter: sensorTypeToJson for '%s' (UID: %s)\n", sensorType.name.c_str(), sensorType.uid.c_str());
        //Serial.printf("ArduinoJsonConverter: Free heap at start: %d\n", ESP.getFreeHeap());
        
        try {
            DynamicJsonDocument doc(16384);
            //Serial.printf("ArduinoJsonConverter: Document created, capacity: %d\n", doc.capacity());
            
            // Main sensor type properties
            //Serial.printf("ArduinoJsonConverter: Setting basic properties...\n");
            doc["uid"] = sensorType.uid;
            doc["name"] = sensorType.name;
            doc["displayName"] = sensorType.displayName;
            doc["canScan"] = sensorType.canScan;
            
            //Serial.printf("ArduinoJsonConverter: Getting protocol string...\n");
            std::string protocolStr = sensorType.getProtocolString();
            //Serial.printf("ArduinoJsonConverter: Protocol string: %s\n", protocolStr.c_str());
            doc["protocol"] = protocolStr;
            
            doc["isSupported"] = sensorType.isSupported;
            
            if (!sensorType.createdAt.empty()) {
                doc["createdAt"] = sensorType.createdAt;
            }
            if (!sensorType.updatedAt.empty()) {
                doc["updatedAt"] = sensorType.updatedAt;
            }
            
            // Handle optional string fields properly
            if (!sensorType.i2cAddress.empty()) {
                doc["i2cAddress"] = sensorType.i2cAddress;
            }
            if (!sensorType.chipType.empty()) {
                doc["chipType"] = sensorType.chipType;
            }
            
            doc["modbusDeviceAddress"] = sensorType.modbusDeviceAddress;
            doc["baudRate"] = sensorType.baudRate;
            doc["modbusPingAddress"] = sensorType.modbusPingAddress;
            doc["registerToStoreModbusDeviceAddress"] = sensorType.registerToStoreModbusDeviceAddress;
            doc["writeRegistersCommand"] = sensorType.writeRegistersCommand;
            doc["priorityForScan"] = sensorType.priorityForScan;

            // Legacy id for backward compatibility (if needed)
            if (sensorType.id != 0) {
                doc["id"] = sensorType.id;
            }
            
            //Serial.printf("ArduinoJsonConverter: Basic properties set, free heap: %d\n", ESP.getFreeHeap());

            // Add sensorTypes array
            //Serial.printf("ArduinoJsonConverter: Creating sensorTypes array (%d items)...\n", sensorType.sensorTypes.size());
            JsonArray sensorTypesArr = doc.createNestedArray("sensorTypes");
            for (size_t i = 0; i < sensorType.sensorTypes.size(); i++)
            {
                //Serial.printf("ArduinoJsonConverter: Processing sub-sensor %d: %s\n", i, sensorType.sensorTypes[i].name.c_str());
                const auto& subSensorType = sensorType.sensorTypes[i];
                JsonObject sensorTypeObj = sensorTypesArr.createNestedObject();
                sensorTypeObj["name"] = subSensorType.name;
                sensorTypeObj["displayName"] = subSensorType.displayName;
                sensorTypeObj["isAnalog"] = subSensorType.isAnalog;
                sensorTypeObj["canCalibrate"] = subSensorType.canCalibrate;
                sensorTypeObj["validCalibrationPeriodDays"] = subSensorType.validCalibrationPeriodDays;
                sensorTypeObj["calibrationTimeInSeconds"] = subSensorType.calibrationTimeInSeconds;
                sensorTypeObj["minValue"] = subSensorType.minValue;
                sensorTypeObj["maxValue"] = subSensorType.maxValue;
                sensorTypeObj["limitMinValue"] = subSensorType.limitMinValue;
                sensorTypeObj["limitMaxValue"] = subSensorType.limitMaxValue;
                sensorTypeObj["unit"] = subSensorType.unit;
                sensorTypeObj["step"] = subSensorType.step;
                sensorTypeObj["precisionPercent"] = subSensorType.precisionPercent;
                sensorTypeObj["precisionValue"] = subSensorType.precisionValue;
                if (!subSensorType.createdAt.empty()) {
                    sensorTypeObj["createdAt"] = subSensorType.createdAt;
                }
                if (!subSensorType.updatedAt.empty()) {
                    sensorTypeObj["updatedAt"] = subSensorType.updatedAt;
                }
                //Serial.printf("ArduinoJsonConverter: Sub-sensor %d processed, free heap: %d\n", i, ESP.getFreeHeap());
            }

            // Add I2C register config (renamed in JSON)
            //Serial.printf("ArduinoJsonConverter: Creating i2CRegisterConfigs array (%d items)...\n", sensorType.i2cRegisterConfig.size());
            JsonArray i2cConfigArr = doc.createNestedArray("i2CRegisterConfigs");
            for (const auto& config : sensorType.i2cRegisterConfig)
            {
                JsonObject configObj = i2cConfigArr.createNestedObject();
                configObj["register"] = config.registerAddr;
                configObj["length"] = config.length;
                configObj["multiplier"] = config.multiplier;
                configObj["offset"] = config.offset;
            }

            // Add Modbus register config (renamed in JSON)
            //Serial.printf("ArduinoJsonConverter: Creating modbusRegisterConfigs array (%d items)...\n", sensorType.modbusRegisterConfig.size());
            JsonArray modbusConfigArr = doc.createNestedArray("modbusRegisterConfigs");
            for (size_t i = 0; i < sensorType.modbusRegisterConfig.size(); i++)
            {
                //Serial.printf("ArduinoJsonConverter: Processing modbus config %d...\n", i);
                const auto& config = sensorType.modbusRegisterConfig[i];
                JsonObject configObj = modbusConfigArr.createNestedObject();
                configObj["functionCode"] = config.functionCode;
                configObj["startRegister"] = config.startRegister;
                configObj["numRegisters"] = config.numRegisters;
                configObj["multiplier"] = config.multiplier;
                configObj["offset"] = config.offset;
                configObj["channelName"] = config.channelName;
                configObj["channel"] = config.channel;
                configObj["sensorType"] = config.sensorType;
                configObj["canConnectMinValue"] = config.canConnectMinValue;
                configObj["canConnectMaxValue"] = config.canConnectMaxValue;
                //Serial.printf("ArduinoJsonConverter: Modbus config %d processed, free heap: %d\n", i, ESP.getFreeHeap());
            }

            // Add analog pin configs
            //Serial.printf("ArduinoJsonConverter: Creating analogPinConfigs array (%d items)...\n", sensorType.analogPinConfig.size());
            JsonArray analogConfigArr = doc.createNestedArray("analogPinConfigs");
            for (const auto& config : sensorType.analogPinConfig)
            {
                JsonObject configObj = analogConfigArr.createNestedObject();
                // Add analog pin config serialization here when needed
            }

            //Serial.printf("ArduinoJsonConverter: All arrays created, serializing...\n");
            //Serial.printf("ArduinoJsonConverter: Free heap before serialization: %d\n", ESP.getFreeHeap());
            
            std::string output;
            serializeJson(doc, output);
            
            //Serial.printf("ArduinoJsonConverter: Serialized successfully, length: %d\n", output.length());
            //Serial.printf("ArduinoJsonConverter: Free heap after serialization: %d\n", ESP.getFreeHeap());
            //Serial.printf("ArduinoJsonConverter: Output: %s\n", output.c_str());
            return output;
        }
        catch (const std::exception& e) {
            //Serial.printf("ArduinoJsonConverter: Exception in sensorTypeToJson: %s\n", e.what());
            return "{}";
        }
        catch (...) {
            //Serial.printf("ArduinoJsonConverter: Unknown exception in sensorTypeToJson\n");
            return "{}";
        }
    }

    bool sensorTypesFromJson(const std::string &jsonStr, std::vector<SensorType> &sensorTypes) override
    {
        sensorTypes.clear();

        //Serial.printf("ArduinoJsonConverter: Parsing sensor types JSON (length: %d bytes)\n", jsonStr.length());
        //Serial.printf("ArduinoJsonConverter: Free heap before parsing: %d\n", ESP.getFreeHeap());

        if (jsonStr.empty() || jsonStr == "[]") return false;

        // Extract each top-level JSON object from the array without loading the whole
        // array into a single ArduinoJson document.  This removes the ~64 KB heap
        // constraint (which caused "[]" writes on low memory) and allows files of
        // any size to be read correctly.
        const size_t len = jsonStr.size();
        size_t pos = 0;
        while (pos < len) {
            size_t start = jsonStr.find('{', pos);
            if (start == std::string::npos) break;

            int depth = 0;
            size_t end = start;
            while (end < len) {
                char c = jsonStr[end];
                if (c == '{') {
                    ++depth;
                    ++end;
                } else if (c == '}') {
                    --depth;
                    if (depth == 0) break; // end points to closing '}'
                    ++end;
                } else if (c == '"') {
                    ++end; // skip opening '"'
                    while (end < len) {
                        if (jsonStr[end] == '\\') { end += 2; } // skip escape
                        else if (jsonStr[end] == '"') { ++end; break; } // past closing '"'
                        else { ++end; }
                    }
                } else {
                    ++end;
                }
            }
            if (depth != 0) break; // malformed JSON

            std::string typeJson = jsonStr.substr(start, end - start + 1);
            SensorType sensorType;
            if (sensorTypeFromJson(typeJson, sensorType)) {
                sensorTypes.push_back(sensorType);
                //Serial.printf("ArduinoJsonConverter: Successfully parsed sensor type '%s'\n", sensorType.name.c_str());
            }
            pos = end + 1;
        }

        //Serial.printf("ArduinoJsonConverter: Total sensor types parsed: %d\n", sensorTypes.size());
        //Serial.printf("ArduinoJsonConverter: Free heap after processing: %d\n", ESP.getFreeHeap());

        return !sensorTypes.empty();
    }

    std::string sensorTypesToJson(const std::vector<SensorType> &sensorTypes) override
    {
        //Serial.printf("ArduinoJsonConverter: sensorTypesToJson called with %d sensor types\n", sensorTypes.size());
        //Serial.printf("ArduinoJsonConverter: Free heap at start: %d\n", ESP.getFreeHeap());
        
        try {
            // Build the JSON array by concatenating individual sensor type JSON strings.
            // This avoids allocating a large ArduinoJson document for the outer array,
            // removing the ~64 KB heap constraint that could cause "[]" on low memory.
            std::string output;
            output.reserve(sensorTypes.size() * 512);
            output += "[";
            for (size_t i = 0; i < sensorTypes.size(); i++) {
                if (i > 0) output += ",";
                output += sensorTypeToJson(sensorTypes[i]);
            }
            output += "]";

            //Serial.printf("ArduinoJsonConverter: Serialization complete, output length: %d\n", output.length());
            //Serial.printf("ArduinoJsonConverter: Free heap after serialization: %d\n", ESP.getFreeHeap());

            return output;
        }
        catch (const std::exception& e) {
            //Serial.printf("ArduinoJsonConverter: Exception in sensorTypesToJson: %s\n", e.what());
            return "[]";
        }
        catch (...) {
            //Serial.printf("ArduinoJsonConverter: Unknown exception in sensorTypesToJson\n");
            return "[]";
        }
    }

    std::string moduleCommandUpdateToJson(const ModuleCommandUpdateDto& commandUpdateDto) override
    {
        DynamicJsonDocument doc(2048);

        doc["uid"] = commandUpdateDto.uid;
        doc["status"] = commandUpdateDto.status;
        
        if (!commandUpdateDto.errorMessage.empty())
        {
            doc["errorMessage"] = commandUpdateDto.errorMessage;
        }
        
        if (!commandUpdateDto.timestamp.empty())
        {
            doc["timestamp"] = commandUpdateDto.timestamp;
        }
        
        if (!commandUpdateDto.accountUid.empty())
        {
            doc["accountUid"] = commandUpdateDto.accountUid;
        }
        
        // Add result field with sensors array if present
        if (!commandUpdateDto.result.sensors.empty())
        {
            JsonObject resultObj = doc.createNestedObject("result");
            JsonArray sensorsArray = resultObj.createNestedArray("sensors");
            
            for (const auto& sensor : commandUpdateDto.result.sensors)
            {
                JsonObject sensorObj = sensorsArray.createNestedObject();
                sensorObj["uid"] = sensor.uid;
                sensorObj["type"] = sensor.type;
                sensorObj["pin"] = sensor.pin;
                sensorObj["group"] = sensor.group;
                sensorObj["modelUid"] = sensor.modelUid;
            }
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    std::string moduleCommandUpdatesToJson(const std::vector<ModuleCommandUpdateDto>& commandUpdates) override
    {
        DynamicJsonDocument doc(8192);
        JsonArray array = doc.to<JsonArray>();

        for (const auto& commandUpdateDto : commandUpdates)
        {
            JsonObject obj = array.createNestedObject();
            obj["uid"] = commandUpdateDto.uid;
            obj["status"] = commandUpdateDto.status;
            
            if (!commandUpdateDto.errorMessage.empty())
            {
                obj["errorMessage"] = commandUpdateDto.errorMessage;
            }
            
            if (!commandUpdateDto.timestamp.empty())
            {
                obj["timestamp"] = commandUpdateDto.timestamp;
            }
            
            if (!commandUpdateDto.accountUid.empty())
            {
                obj["accountUid"] = commandUpdateDto.accountUid;
            }
            
            // Add result field with sensors array if present
            if (!commandUpdateDto.result.sensors.empty())
            {
                JsonObject resultObj = obj.createNestedObject("result");
                JsonArray sensorsArray = resultObj.createNestedArray("sensors");
                
                for (const auto& sensor : commandUpdateDto.result.sensors)
                {
                    JsonObject sensorObj = sensorsArray.createNestedObject();
                    sensorObj["uid"] = sensor.uid;
                    sensorObj["type"] = sensor.type;
                    sensorObj["pin"] = sensor.pin;
                    sensorObj["group"] = sensor.group;
                    sensorObj["modelUid"] = sensor.modelUid;
                }
            }
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool moduleCommandFromJson(const std::string &jsonStr, ModuleCommandDto &commandDto) override
    {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        commandDto.uid = doc["uid"] | "";
        commandDto.command = doc["command"] | "";
        
        // Serialize options object to JSON string
        if (doc.containsKey("options") && !doc["options"].isNull())
        {
            if (doc["options"].is<JsonObject>() || doc["options"].is<JsonArray>())
            {
                std::string optionsJson;
                serializeJson(doc["options"], optionsJson);
                commandDto.options = optionsJson;
            }
            else
            {
                commandDto.options = doc["options"] | "";
            }
        }
        else
        {
            commandDto.options = "";
        }
        
        commandDto.createdAt = doc["createdAt"] | "";

        return true;
    }

    std::string moduleCommandToJson(const ModuleCommandDto &commandDto) override
    {
        DynamicJsonDocument doc(2048);

        doc["uid"] = commandDto.uid;
        doc["command"] = commandDto.command;
        doc["options"] = commandDto.options;
        
        if (!commandDto.createdAt.empty())
        {
            doc["createdAt"] = commandDto.createdAt;
        }

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool moduleCommandsFromJson(const std::string &jsonStr, std::vector<ModuleCommandDto> &commands) override
    {
        DynamicJsonDocument doc(8192); // Larger capacity for array
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;

        commands.clear();
        
        // Check if it's an array
        if (doc.is<JsonArray>())
        {
            for (JsonObject commandObj : doc.as<JsonArray>())
            {
                ModuleCommandDto command;
                command.uid = commandObj["uid"] | "";
                command.command = commandObj["command"] | "";
                
                // Serialize options object to JSON string
                if (commandObj.containsKey("options") && !commandObj["options"].isNull())
                {
                    if (commandObj["options"].is<JsonObject>() || commandObj["options"].is<JsonArray>())
                    {
                        std::string optionsJson;
                        serializeJson(commandObj["options"], optionsJson);
                        command.options = optionsJson;
                    }
                    else
                    {
                        command.options = commandObj["options"] | "";
                    }
                }
                else
                {
                    command.options = "";
                }
                
                command.createdAt = commandObj["createdAt"] | "";
                
                commands.push_back(command);
            }
        }
        else if (doc.is<JsonObject>())
        {
            // Single object - treat as single command
            ModuleCommandDto command;
            if (moduleCommandFromJson(jsonStr, command))
            {
                commands.push_back(command);
            }
        }

        return !commands.empty();
    }

    bool wifiNetworksFromJson(const std::string &jsonStr, std::vector<WifiNetworkEntry> &networks) override
    {
        networks.clear();
        if (jsonStr.empty())
            return false;
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, jsonStr);
        if (error)
            return false;
        if (!doc.is<JsonArray>())
            return false;
        for (JsonObject netObj : doc.as<JsonArray>())
        {
            WifiNetworkEntry entry;
            entry.ssid = netObj["ssid"] | "";
            entry.password = netObj["password"] | "";
            if (!entry.ssid.empty() && !entry.password.empty())
                networks.push_back(entry);
        }
        return !networks.empty();
    }

    std::string wifiNetworksToJson(const std::vector<WifiNetworkEntry> &networks) override
    {
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.to<JsonArray>();
        for (const auto &entry : networks)
        {
            JsonObject obj = arr.createNestedObject();
            obj["ssid"] = entry.ssid;
            obj["password"] = entry.password;
        }
        std::string output;
        serializeJson(doc, output);
        return output;
    }

    bool sensorValuesFromUpdateJson(const std::string &json, std::vector<std::pair<std::string, float>> &out) override
    {
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, json);
        if (error)
            return false;

        if (!doc.containsKey("sensors") || !doc["sensors"].is<JsonArray>())
            return false;

        JsonArray sensors = doc["sensors"].as<JsonArray>();
        if (sensors.size() == 0)
            return false;

        for (JsonObject sensor : sensors)
        {
            if (!sensor.containsKey("uid") || !sensor.containsKey("value"))
                continue;

            const char *uid = sensor["uid"];
            if (!uid || uid[0] == '\0')
                continue;

            if (!sensor["value"].is<float>() && !sensor["value"].is<int>())
                continue;

            out.push_back(std::make_pair(std::string(uid), sensor["value"].as<float>()));
        }

        return true;
    }
};
