#pragma once

#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include <ArduinoJson.h>

class ArduinoJsonConverter : public IJsonConverter
{
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
                node.uid = nodeObj["uid"] | "";
                node.firmware = nodeObj["firmware"] | "";
                node.ssid = nodeObj["ssid"] | "";
                node.ip = nodeObj["ip"] | "";

                node.sensors.clear();
                if (nodeObj.containsKey("sensors"))
                {
                    for (JsonObject sensorObj : nodeObj["sensors"].as<JsonArray>())
                    {
                        Sensor sensor;
                        sensor.uid = sensorObj["uid"] | "";
                        sensor.type = sensorObj["type"] | "";
                        sensor.pin = sensorObj["pin"] | 0;
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
            nodeObj["uid"] = node.uid;
            nodeObj["firmware"] = node.firmware;
            nodeObj["ssid"] = node.ssid;
            nodeObj["ip"] = node.ip;

            if (!node.sensors.empty())
            {
                JsonArray sensorsArr = nodeObj.createNestedArray("sensors");
                for (const auto &sensor : node.sensors)
                {
                    JsonObject sensorObj = sensorsArr.createNestedObject();
                    sensorObj["uid"] = sensor.uid;
                    sensorObj["type"] = sensor.type;
                    sensorObj["pin"] = sensor.pin;
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
        rule.deviceUid = doc["deviceUid"] | "";

        // Parse ruleType
        std::string ruleTypeStr = doc["ruleType"] | "";
        if (ruleTypeStr == "step")
            rule.ruleType = RuleType::STEP;
        else if (ruleTypeStr == "sensor")
            rule.ruleType = RuleType::SENSOR;
        else if (ruleTypeStr == "schedule")
            rule.ruleType = RuleType::SCHEDULE;

        // Parse mode
        std::string modeStr = doc["mode"] | "";
        if (modeStr == "On")
            rule.mode = RuleMode::ON;
        else if (modeStr == "Off")
            rule.mode = RuleMode::OFF;
        else if (modeStr == "Auto")
            rule.mode = RuleMode::AUTO;

        if (doc.containsKey("modeReason") && !doc["modeReason"].isNull())
        {
            rule.modeReason = doc["modeReason"].as<std::string>();
        }
        else
        {
            rule.modeReason = "";
        }

        rule.protectiveShutdown = doc["protectiveShutdown"] | false;

        // Parse state
        std::string stateStr = doc["state"] | "";
        if (stateStr == "Synchronized")
            rule.state = RuleState::SYNCHRONIZED;
        else if (stateStr == "Pending")
            rule.state = RuleState::PENDING;
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
                    // TODO: Parse time properly
                    item.startAt = 0;
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
        doc["deviceUid"] = rule.deviceUid;

        // RuleType
        if (rule.ruleType == RuleType::STEP)
            doc["ruleType"] = "step";
        else if (rule.ruleType == RuleType::SENSOR)
            doc["ruleType"] = "sensor";
        else if (rule.ruleType == RuleType::SCHEDULE)
            doc["ruleType"] = "schedule";

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

        doc["protectiveShutdown"] = rule.protectiveShutdown;

        // State
        if (rule.state == RuleState::SYNCHRONIZED)
            doc["state"] = "Synchronized";
        else if (rule.state == RuleState::PENDING)
            doc["state"] = "Pending";
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
                // TODO: Format time properly
                itemObj["startAt"] = "";
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
        }

        // SensorTypeCommandNotification fields
        if (commandDto.isSensorTypeCommand())
        {
            commandDto.sensorUid = doc["sensorUid"] | "";
            commandDto.sensorType = doc["sensorType"] | "";
        }

        return true;
    }

    std::string commandToJson(const CommandDto &commandDto) override
    {
        DynamicJsonDocument doc(4096);

        doc["growDirectorUid"] = commandDto.growDirectorUid;
        doc["type"] = commandDto.type;

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

        doc["uid"] = commandUpdateDto.uid;
        doc["status"] = commandUpdateDto.status;
        doc["errorMessage"] = commandUpdateDto.errorMessage;

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
};
