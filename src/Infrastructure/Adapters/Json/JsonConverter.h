#ifndef JSON_CONVERTER_H
#define JSON_CONVERTER_H

#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include <string>
#include <sstream>
#include <algorithm>

static std::string extractValue(const std::string &src, const std::string &key)
{
    auto keyPos = src.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
        return "";
    auto colon = src.find(':', keyPos);
    if (colon == std::string::npos)
        return "";
    size_t valueStart = colon + 1;
    while (valueStart < src.size() && std::isspace(src[valueStart]))
        ++valueStart;
    if (valueStart < src.size() && src[valueStart] == '"')
    {
        auto start = valueStart;
        auto end = src.find_first_of("\"", start + 1);
        if (end == std::string::npos)
            return "";
        return src.substr(start + 1, end - start - 1);
    }
    if (src.substr(valueStart, 4) == "null")
    {
        return "";
    }
    return "";
}

static std::string extractArray(const std::string &src, const std::string &key)
{
    auto keyPos = src.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
        return "";
    auto start = src.find('[', keyPos);
    if (start == std::string::npos)
        return "";
    int depth = 1;
    auto end = start + 1;
    while (end < src.size() && depth > 0)
    {
        if (src[end] == '[')
            ++depth;
        if (src[end] == ']')
            --depth;
        ++end;
    }
    return src.substr(start, end - start);
}

static std::vector<std::string> splitObjects(const std::string &arrayStr)
{
    std::vector<std::string> result;
    int depth = 0;
    size_t last = 0;
    for (size_t i = 0; i < arrayStr.size(); ++i)
    {
        if (arrayStr[i] == '{')
        {
            if (depth == 0)
                last = i;
            ++depth;
        }
        else if (arrayStr[i] == '}')
        {
            --depth;
            if (depth == 0)
            {
                result.push_back(arrayStr.substr(last, i - last + 1));
            }
        }
    }
    return result;
}

static int extractIntValue(const std::string &src, const std::string &key)
{
    auto keyPos = src.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
        return 0;
    auto colon = src.find(':', keyPos);
    if (colon == std::string::npos)
        return 0;
    size_t valueStart = colon + 1;
    while (valueStart < src.size() && std::isspace(src[valueStart]))
        ++valueStart;
    if (valueStart + 4 <= src.size() && src.substr(valueStart, 4) == "null")
    {
        return 0;
    }
    auto start = valueStart;
    if (start < src.size() && src[start] == '-')
    {
        ++start;
    }
    if (start >= src.size() || !std::isdigit(src[start]))
    {
        return 0;
    }
    auto end = src.find_first_not_of("0123456789", start);
    if (end == std::string::npos)
    {
        end = src.size();
    }
    try
    {
        std::string numStr = src.substr(valueStart, end - valueStart);
        return std::stoi(numStr);
    }
    catch (const std::exception &e)
    {
        return 0;
    }
}

static double extractDoubleValue(const std::string &src, const std::string &key)
{
    auto keyPos = src.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
        return 0.0;
    auto colon = src.find(':', keyPos);
    if (colon == std::string::npos)
        return 0.0;
    auto start = src.find_first_of("0123456789.-", colon + 1);
    if (start == std::string::npos)
        return 0.0;
    auto end = src.find_first_not_of("0123456789.-", start);
    if (end == std::string::npos)
        end = src.length();
    return std::stod(src.substr(start, end - start));
}

static bool extractBoolValue(const std::string &src, const std::string &key)
{
    auto keyPos = src.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
        return false;
    auto colon = src.find(':', keyPos);
    if (colon == std::string::npos)
        return false;
    size_t valueStart = colon + 1;
    while (valueStart < src.size() && std::isspace(src[valueStart]))
        ++valueStart;
    return src.substr(valueStart, 4) == "true";
}

class JsonConverter : public IJsonConverter
{
public:
    // Implement the interface methods
    bool moduleStructureFromJson(const std::string &jsonStr, ModuleStructure &moduleStructure) override
    {
        moduleStructure.nodes.clear();
        moduleStructure.growdirectorUid = extractValue(jsonStr, "GrowdirectorUid");
        moduleStructure.firmware = extractValue(jsonStr, "firmware");
        moduleStructure.email = extractValue(jsonStr, "email");
        moduleStructure.accountUid = extractValue(jsonStr, "accountUid");

        // Extract latitude and longitude
        moduleStructure.latitude = extractDoubleValue(jsonStr, "lat");
        moduleStructure.longitude = extractDoubleValue(jsonStr, "lng");

        std::string nodesArr = extractArray(jsonStr, "nodes");
        auto nodeObjs = splitObjects(nodesArr);
        for (const auto &nodeStr : nodeObjs)
        {
            Node node;
            node.type = extractValue(nodeStr, "type");
            node.subtype = extractValue(nodeStr, "subtype");
            node.bootType = extractValue(nodeStr, "bootType");
            node.fileSystem = extractValue(nodeStr, "fileSystem");
            node.uid = extractValue(nodeStr, "uid");
            node.firmware = extractValue(nodeStr, "firmware");
            node.ssid = extractValue(nodeStr, "ssid");
            node.ip = extractValue(nodeStr, "ip");

            // Sensors
            std::string sensorsArr = extractArray(nodeStr, "sensors");
            auto sensorObjs = splitObjects(sensorsArr);
            for (const auto &sensorStr : sensorObjs)
            {
                Sensor sensor;
                sensor.uid = extractValue(sensorStr, "uid");
                sensor.type = extractValue(sensorStr, "type");
                sensor.pin = extractIntValue(sensorStr, "pin");
                node.sensors.push_back(sensor);
            }

            // Relays
            std::string relaysArr = extractArray(nodeStr, "relays");
            auto relayObjs = splitObjects(relaysArr);
            for (const auto &relayStr : relayObjs)
            {
                Relay relay;
                relay.uid = extractValue(relayStr, "uid");
                relay.pin = extractIntValue(relayStr, "pin");
                node.relays.push_back(relay);
            }

            // Pumps
            std::string pumpsArr = extractArray(nodeStr, "pumps");
            auto pumpObjs = splitObjects(pumpsArr);
            for (const auto &pumpStr : pumpObjs)
            {
                Pump pump;
                pump.uid = extractValue(pumpStr, "uid");
                pump.pin = extractIntValue(pumpStr, "pin");
                node.pumps.push_back(pump);
            }

            moduleStructure.nodes.push_back(node);
        }
        return true;
    }

    std::string moduleStructureToJson(const ModuleStructure &moduleStructure) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"GrowdirectorUid\":\"" << moduleStructure.growdirectorUid << "\"";
        if (!moduleStructure.firmware.empty())
            oss << ",\"firmware\":\"" << moduleStructure.firmware << "\"";
        if (!moduleStructure.email.empty())
            oss << ",\"email\":\"" << moduleStructure.email << "\"";
        if (!moduleStructure.accountUid.empty())
            oss << ",\"accountUid\":\"" << moduleStructure.accountUid << "\"";
        if (moduleStructure.latitude != 0.0)
            oss << ",\"lat\":" << moduleStructure.latitude;
        if (moduleStructure.longitude != 0.0)
            oss << ",\"lng\":" << moduleStructure.longitude;
        oss << ",\"nodes\":[";
        for (size_t i = 0; i < moduleStructure.nodes.size(); ++i)
        {
            const Node &node = moduleStructure.nodes[i];
            oss << "{";
            oss << "\"type\":\"" << node.type << "\"";
            if (!node.subtype.empty())
                oss << ",\"subtype\":\"" << node.subtype << "\"";
            if (!node.bootType.empty())
                oss << ",\"bootType\":\"" << node.bootType << "\"";
            if (!node.fileSystem.empty())
                oss << ",\"fileSystem\":\"" << node.fileSystem << "\"";
            oss << ",\"uid\":\"" << node.uid << "\"";
            if (!node.firmware.empty())
                oss << ",\"firmware\":\"" << node.firmware << "\"";
            if (!node.ssid.empty())
                oss << ",\"ssid\":\"" << node.ssid << "\"";
            if (!node.ip.empty())
                oss << ",\"ip\":\"" << node.ip << "\"";

            // Sensors
            if (!node.sensors.empty())
            {
                oss << ",\"sensors\":[";
                for (size_t j = 0; j < node.sensors.size(); ++j)
                {
                    const Sensor &s = node.sensors[j];
                    oss << "{\"uid\":\"" << s.uid << "\"";
                    if (!s.type.empty())
                        oss << ",\"type\":\"" << s.type << "\"";
                    oss << ",\"pin\":" << s.pin << "}";
                    if (j + 1 < node.sensors.size())
                        oss << ",";
                }
                oss << "]";
            }

            // Relays
            if (!node.relays.empty())
            {
                oss << ",\"relays\":[";
                for (size_t j = 0; j < node.relays.size(); ++j)
                {
                    const Relay &r = node.relays[j];
                    oss << "{\"uid\":\"" << r.uid << "\",\"pin\":" << r.pin << "}";
                    if (j + 1 < node.relays.size())
                        oss << ",";
                }
                oss << "]";
            }

            // Pumps
            if (!node.pumps.empty())
            {
                oss << ",\"pumps\":[";
                for (size_t j = 0; j < node.pumps.size(); ++j)
                {
                    const Pump &p = node.pumps[j];
                    oss << "{\"uid\":\"" << p.uid << "\",\"pin\":" << p.pin << "}";
                    if (j + 1 < node.pumps.size())
                        oss << ",";
                }
                oss << "]";
            }

            oss << "}";
            if (i + 1 < moduleStructure.nodes.size())
                oss << ",";
        }
        oss << "]";
        oss << "}";
        return oss.str();
    }

    std::string diagnosticToJson(const DiagnosticDto &diagnosticData) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"rssi\":" << diagnosticData.rssi;
        oss << ",\"uid\":\"" << diagnosticData.uid << "\"";
        oss << ",\"email\":\"" << diagnosticData.email << "\"";

        if (!diagnosticData.accountUid.empty())
            oss << ",\"accountUid\":\"" << diagnosticData.accountUid << "\"";

        oss << ",\"timestamp\":\"" << diagnosticData.timestamp << "\"";
        oss << ",\"uptimeMs\":" << diagnosticData.uptimeMs;
        oss << ",\"networkFails\":" << diagnosticData.networkFails;
        oss << ",\"apiSendFails\":" << diagnosticData.apiSendFails;
        oss << ",\"apiStatus\":\"" << diagnosticData.apiStatus << "\"";
        oss << ",\"mqttStatus\":\"" << diagnosticData.mqttStatus << "\"";
        oss << ",\"mqttFails\":" << diagnosticData.mqttFails;
        oss << ",\"apiSendFailRatio\":" << diagnosticData.apiSendFailRatio;
        oss << ",\"firmware\":\"" << diagnosticData.firmware << "\"";
        oss << ",\"freeHeap\":" << diagnosticData.freeHeap;
        oss << ",\"minFreeHeap\":" << diagnosticData.minFreeHeap;
        oss << ",\"heapSize\":" << diagnosticData.heapSize;
        oss << ",\"maxAllocHeap\":" << diagnosticData.maxAllocHeap;
        oss << ",\"failsInHour\":" << diagnosticData.failsInHour;
        oss << "}";

        return oss.str();
    }

    bool ruleFromJson(const std::string &jsonStr, Rule &rule) override
    {
        rule.uid = extractValue(jsonStr, "uid");
        rule.version = extractIntValue(jsonStr, "version");
        rule.deviceUid = extractValue(jsonStr, "deviceUid");

        // Parse ruleType
        std::string ruleTypeStr = extractValue(jsonStr, "ruleType");
        if (ruleTypeStr == "step")
            rule.ruleType = RuleType::STEP;
        else if (ruleTypeStr == "sensor")
            rule.ruleType = RuleType::SENSOR;
        else if (ruleTypeStr == "schedule")
            rule.ruleType = RuleType::SCHEDULE;

        // Parse mode
        std::string modeStr = extractValue(jsonStr, "mode");
        if (modeStr == "On")
            rule.mode = RuleMode::ON;
        else if (modeStr == "Off")
            rule.mode = RuleMode::OFF;
        else if (modeStr == "Auto")
            rule.mode = RuleMode::AUTO;

        rule.modeReason = extractValue(jsonStr, "modeReason");
        rule.protectiveShutdown = jsonStr.find("\"protectiveShutdown\":true") != std::string::npos;

        // Parse state
        std::string stateStr = extractValue(jsonStr, "state");
        if (stateStr == "Synchronized")
            rule.state = RuleState::SYNCHRONIZED;
        else if (stateStr == "Pending")
            rule.state = RuleState::PENDING;
        else
            rule.state = RuleState::UNSYNCHRONIZED;

        // Parse daysOfWeek and daysOfMonth
        // Extract arrays
        std::string daysOfWeekArr = extractArray(jsonStr, "daysOfWeek");
        // Parse simple int array
        rule.daysOfWeek.clear();
        for (size_t i = 0; i < daysOfWeekArr.length(); ++i)
        {
            if (isdigit(daysOfWeekArr[i]))
            {
                rule.daysOfWeek.push_back(daysOfWeekArr[i] - '0');
            }
        }

        std::string daysOfMonthArr = extractArray(jsonStr, "daysOfMonth");
        rule.daysOfMonth.clear();
        for (size_t i = 0; i < daysOfMonthArr.length(); ++i)
        {
            if (isdigit(daysOfMonthArr[i]))
            {
                int num = daysOfMonthArr[i] - '0';
                if (i + 1 < daysOfMonthArr.length() && isdigit(daysOfMonthArr[i + 1]))
                {
                    num = num * 10 + (daysOfMonthArr[i + 1] - '0');
                    ++i;
                }
                rule.daysOfMonth.push_back(num);
            }
        }

        // Parse type-specific properties
        rule.sensorUid = extractValue(jsonStr, "sensorUid");

        // Parse direction
        std::string directionStr = extractValue(jsonStr, "direction");
        if (directionStr == "up")
            rule.direction = RuleDirection::UP;
        else if (directionStr == "down")
            rule.direction = RuleDirection::DOWN;

        rule.valueTarget = extractDoubleValue(jsonStr, "valueTarget");
        rule.cyclesToSetPoint = extractIntValue(jsonStr, "cyclesToSetPoint");
        rule.workingTime = extractIntValue(jsonStr, "workingTime");
        rule.waitingTime = extractIntValue(jsonStr, "waitingTime");

        rule.valueLow = extractDoubleValue(jsonStr, "valueLow");
        rule.valueHigh = extractDoubleValue(jsonStr, "valueHigh");
        rule.timeToSetPoint = extractIntValue(jsonStr, "timeToSetPoint");
        rule.isAdvanced = jsonStr.find("\"isAdvanced\":true") != std::string::npos;

        // Parse conditions
        std::string conditionsArr = extractArray(jsonStr, "conditions");
        auto conditionObjs = splitObjects(conditionsArr);
        rule.conditions.clear();
        for (const auto &conditionStr : conditionObjs)
        {
            RuleCondition condition;

            // Parse sensorUids
            std::string sensorUidsArr = extractArray(conditionStr, "sensorUids");
            condition.sensorUids.clear();
            size_t pos = 0;
            while ((pos = sensorUidsArr.find("\"", pos)) != std::string::npos)
            {
                size_t end = sensorUidsArr.find("\"", pos + 1);
                if (end == std::string::npos)
                    break;
                condition.sensorUids.push_back(sensorUidsArr.substr(pos + 1, end - pos - 1));
                pos = end + 1;
            }

            condition.valueTarget = extractDoubleValue(conditionStr, "valueTarget");
            condition.valueLow = extractDoubleValue(conditionStr, "valueLow");
            condition.valueHigh = extractDoubleValue(conditionStr, "valueHigh");

            // Parse direction
            std::string condDirStr = extractValue(conditionStr, "direction");
            if (condDirStr == "up")
                condition.direction = RuleDirection::UP;
            else if (condDirStr == "down")
                condition.direction = RuleDirection::DOWN;

            rule.conditions.push_back(condition);
        }

        rule.conditionOr = jsonStr.find("\"conditionOr\":true") != std::string::npos;
        rule.delayOn = extractIntValue(jsonStr, "delayOn");
        rule.delayOff = extractIntValue(jsonStr, "delayOff");

        // Parse schedule items for schedule rules
        if (rule.isScheduleRule())
        {
            std::string scheduleArr = extractArray(jsonStr, "schedule");
            auto scheduleObjs = splitObjects(scheduleArr);
            rule.schedule.clear();
            for (const auto &scheduleItemStr : scheduleObjs)
            {
                ScheduleItem item;
                std::string startAtStr = extractValue(scheduleItemStr, "startAt");
                // Simple time parsing (this should be improved for real implementation)
                item.startAt = 0; // Should parse ISO time string
                item.duration = extractIntValue(scheduleItemStr, "duration");
                rule.schedule.push_back(item);
            }
        }

        return true;
    }

    std::string ruleToJson(const Rule &rule) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"uid\":\"" << rule.uid << "\"";
        oss << ",\"version\":" << rule.version;
        oss << ",\"deviceUid\":\"" << rule.deviceUid << "\"";

        // RuleType
        oss << ",\"ruleType\":\"";
        if (rule.ruleType == RuleType::STEP)
            oss << "step";
        else if (rule.ruleType == RuleType::SENSOR)
            oss << "sensor";
        else if (rule.ruleType == RuleType::SCHEDULE)
            oss << "schedule";
        oss << "\"";

        // Mode
        oss << ",\"mode\":\"";
        if (rule.mode == RuleMode::ON)
            oss << "On";
        else if (rule.mode == RuleMode::OFF)
            oss << "Off";
        else
            oss << "Auto";
        oss << "\"";

        if (!rule.modeReason.empty())
            oss << ",\"modeReason\":\"" << rule.modeReason << "\"";
        else
            oss << ",\"modeReason\":null";

        oss << ",\"protectiveShutdown\":" << (rule.protectiveShutdown ? "true" : "false");

        // State
        oss << ",\"state\":\"";
        if (rule.state == RuleState::SYNCHRONIZED)
            oss << "Synchronized";
        else if (rule.state == RuleState::PENDING)
            oss << "Pending";
        else
            oss << "Unsynchronized";
        oss << "\"";

        // Arrays
        oss << ",\"daysOfWeek\":[";
        for (size_t i = 0; i < rule.daysOfWeek.size(); ++i)
        {
            oss << rule.daysOfWeek[i];
            if (i + 1 < rule.daysOfWeek.size())
                oss << ",";
        }
        oss << "]";

        oss << ",\"daysOfMonth\":[";
        for (size_t i = 0; i < rule.daysOfMonth.size(); ++i)
        {
            oss << rule.daysOfMonth[i];
            if (i + 1 < rule.daysOfMonth.size())
                oss << ",";
        }
        oss << "]";

        // Type-specific properties
        if (!rule.isScheduleRule())
        {
            oss << ",\"sensorUid\":\"" << rule.sensorUid << "\"";

            // Direction
            oss << ",\"direction\":\"";
            if (rule.direction == RuleDirection::UP)
                oss << "up";
            else
                oss << "down";
            oss << "\"";

            // Conditions
            oss << ",\"conditions\":[";
            for (size_t i = 0; i < rule.conditions.size(); ++i)
            {
                const RuleCondition &cond = rule.conditions[i];
                oss << "{";

                // SensorUids array
                oss << "\"sensorUids\":[";
                for (size_t j = 0; j < cond.sensorUids.size(); ++j)
                {
                    oss << "\"" << cond.sensorUids[j] << "\"";
                    if (j + 1 < cond.sensorUids.size())
                        oss << ",";
                }
                oss << "]";

                if (rule.isStepRule())
                {
                    oss << ",\"valueTarget\":" << cond.valueTarget;
                }
                else
                {
                    oss << ",\"valueLow\":" << cond.valueLow;
                    oss << ",\"valueHigh\":" << cond.valueHigh;
                }

                // Direction
                oss << ",\"direction\":\"";
                if (cond.direction == RuleDirection::UP)
                    oss << "up";
                else
                    oss << "down";
                oss << "\"";

                oss << "}";
                if (i + 1 < rule.conditions.size())
                    oss << ",";
            }
            oss << "]";

            oss << ",\"conditionOr\":" << (rule.conditionOr ? "true" : "false");

            if (rule.delayOn > 0)
                oss << ",\"delayOn\":" << rule.delayOn;
            else
                oss << ",\"delayOn\":null";

            if (rule.delayOff > 0)
                oss << ",\"delayOff\":" << rule.delayOff;
            else
                oss << ",\"delayOff\":null";
        }

        // Step rule specific properties
        if (rule.isStepRule())
        {
            oss << ",\"valueTarget\":" << rule.valueTarget;

            if (rule.cyclesToSetPoint > 0)
                oss << ",\"cyclesToSetPoint\":" << rule.cyclesToSetPoint;
            else
                oss << ",\"cyclesToSetPoint\":null";

            oss << ",\"workingTime\":" << rule.workingTime;
            oss << ",\"waitingTime\":" << rule.waitingTime;
        }

        // Sensor rule specific properties
        if (rule.isSensorRule())
        {
            oss << ",\"valueLow\":" << rule.valueLow;
            oss << ",\"valueHigh\":" << rule.valueHigh;
            oss << ",\"timeToSetPoint\":" << rule.timeToSetPoint;
            oss << ",\"isAdvanced\":" << (rule.isAdvanced ? "true" : "false");

            // These should be properly formatted to ISO standard
            oss << ",\"startDayTime\":\"" << rule.startDayTime << "\"";
            oss << ",\"endDayTime\":\"" << rule.endDayTime << "\"";

            oss << ",\"advancedRuleValues\":[]"; // Empty for now
        }

        // Schedule rule specific properties
        if (rule.isScheduleRule())
        {
            oss << ",\"schedule\":[";
            for (size_t i = 0; i < rule.schedule.size(); ++i)
            {
                const ScheduleItem &item = rule.schedule[i];
                oss << "{";
                // Should format to ISO time string
                oss << "\"startAt\":\"" << item.startAt << "\"";
                oss << ",\"duration\":" << item.duration;
                oss << "}";
                if (i + 1 < rule.schedule.size())
                    oss << ",";
            }
            oss << "]";
        }

        oss << "}";
        return oss.str();
    }

    bool rulesFromJson(const std::string &jsonStr, std::vector<Rule> &rules) override
    {
        rules.clear();
        auto ruleObjs = splitObjects(jsonStr);
        for (const auto &ruleStr : ruleObjs)
        {
            Rule rule;
            if (ruleFromJson(ruleStr, rule))
            {
                rules.push_back(rule);
            }
        }
        return !rules.empty();
    }

    std::string rulesToJson(const std::vector<Rule> &rules) override
    {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < rules.size(); ++i)
        {
            oss << ruleToJson(rules[i]);
            if (i + 1 < rules.size())
                oss << ",";
        }
        oss << "]";
        return oss.str();
    }

    std::string updateToJson(const UpdateDto &updateData) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"GrowdirectorUid\":\"" << updateData.growdirectorUid << "\"";
        oss << ",\"email\":\"" << updateData.email << "\"";

        if (!updateData.accountUid.empty())
            oss << ",\"accountUid\":\"" << updateData.accountUid << "\"";

        oss << ",\"time\":\"" << updateData.time << "\"";
        oss << ",\"millis\":" << updateData.millis;

        oss << ",\"devices\":[";
        for (size_t i = 0; i < updateData.devices.size(); ++i)
        {
            const auto &device = updateData.devices[i];
            oss << "{";
            oss << "\"uid\":\"" << device.uid << "\"";

            if (!device.switchedAt.empty())
                oss << ",\"switchedAt\":\"" << device.switchedAt << "\"";

            oss << ",\"state\":\"" << device.state << "\"";
            oss << ",\"state_hw\":\"" << device.state_hw << "\"";
            oss << ",\"isAuto\":" << (device.isAuto ? "true" : "false");
            oss << ",\"isManual\":" << (device.isManual ? "true" : "false");

            if (!device.ruleUid.empty())
            {
                oss << ",\"ruleUid\":\"" << device.ruleUid << "\"";
                oss << ",\"ruleType\":\"" << device.ruleType << "\"";
            }

            if (device.isAuto)
            {
                oss << ",\"isRuleTurnOnDevice\":" << (device.isRuleTurnOnDevice ? "true" : "false");

                if (!device.isRuleTurnOnDevice && device.state != "delay_on")
                {
                    oss << ",\"ruleSensorStopAt\":\"" << device.ruleSensorStopAt << "\"";
                    oss << ",\"ruleSensorValueStop\":" << device.ruleSensorValueStop;
                    oss << ",\"ruleStopReason\":\"" << device.ruleStopReason << "\"";
                }

                if (!device.ruleSensorStartAt.empty())
                    oss << ",\"ruleSensorStartAt\":\"" << device.ruleSensorStartAt << "\"";

                oss << ",\"ruleVersion\":" << device.ruleVersion;

                if (device.ruleType == "ph" || device.ruleType == "ec" ||
                    device.ruleType == "sensor" || device.ruleType == "step")
                {
                    oss << ",\"sensorUid\":\"" << device.sensorUid << "\"";
                    oss << ",\"sensorValueLow\":" << device.sensorValueLow;
                    oss << ",\"sensorValueHigh\":" << device.sensorValueHigh;
                    oss << ",\"sensorValue\":" << device.sensorValue;
                    oss << ",\"ruleSensorValueStart\":" << device.ruleSensorValueStart;

                    if (device.ruleType == "sensor" && device.timeToSetPoint > 0)
                        oss << ",\"timeToSetPoint\":" << device.timeToSetPoint;

                    if ((device.ruleType == "ph" || device.ruleType == "ec" ||
                         device.ruleType == "step") &&
                        device.cyclesToSetPoint > 0)
                    {
                        oss << ",\"cyclesToSetPoint\":" << device.cyclesToSetPoint;
                        oss << ",\"currentCycle\":" << device.currentCycle;
                    }
                }

                if (device.ruleType == "schedule")
                {
                    oss << ",\"ruleScheduleStartAt\":\"" << device.ruleScheduleStartAt << "\"";
                    oss << ",\"ruleScheduleDuration\":" << device.ruleScheduleDuration;
                    oss << ",\"secondsToSwitchBack\":" << device.secondsToSwitchBack;
                    oss << ",\"secondsEnabledFor\":" << device.secondsEnabledFor;
                }

                if (device.ruleType == "ph" || device.ruleType == "ec" ||
                    device.ruleType == "step" || device.ruleType == "loop")
                {
                    oss << ",\"secondsToSwitchBack\":" << device.secondsToSwitchBack;
                    oss << ",\"secondsDisabledFor\":" << device.secondsDisabledFor;
                    oss << ",\"secondsEnabledFor\":" << device.secondsEnabledFor;
                }
            }

            if (device.isManual)
            {
                oss << ",\"commandId\":\"" << device.commandId << "\"";
                oss << ",\"secondsToSwitchBack\":" << device.secondsToSwitchBack;
                oss << ",\"secondsEnabledFor\":" << device.secondsEnabledFor;
            }

            oss << "}";
            if (i + 1 < updateData.devices.size())
                oss << ",";
        }
        oss << "]";
        oss << "}";

        return oss.str();
    }

    bool commandFromJson(const std::string &jsonStr, CommandDto &commandDto) override
    {
        commandDto.growDirectorUid = extractValue(jsonStr, "growDirectorUid");
        commandDto.type = extractValue(jsonStr, "type");
        commandDto.createdAt = extractValue(jsonStr, "createdAt");
        commandDto.commandUid = extractValue(jsonStr, "commandUid");

        // DeviceCommandNotification fields
        if (commandDto.isDeviceCommand())
        {
            commandDto.deviceUid = extractValue(jsonStr, "deviceUid");
            commandDto.commandId = extractIntValue(jsonStr, "commandId");
            commandDto.requestedState = extractValue(jsonStr, "requestedState");
        }

        // RuleCommandNotification fields
        if (commandDto.isRuleCommand())
        {
            commandDto.ruleUid = extractValue(jsonStr, "ruleUid");
            commandDto.rulesPayload = extractValue(jsonStr, "rulesPayload");
        }

        // ModuleCommandNotification fields
        if (commandDto.isModuleCommand())
        {
            commandDto.command = extractValue(jsonStr, "command");
            commandDto.version = extractValue(jsonStr, "version");
            commandDto.url = extractValue(jsonStr, "url");
        }

        // SensorTypeCommandNotification fields
        if (commandDto.isSensorTypeCommand())
        {
            commandDto.sensorUid = extractValue(jsonStr, "sensorUid");
            commandDto.sensorType = extractValue(jsonStr, "sensorType");
            commandDto.model = extractValue(jsonStr, "model");
            commandDto.command = extractValue(jsonStr, "command");
            
            // Extract options object as JSON string (simplified)
            commandDto.options = extractJsonObject(jsonStr, "options");
        }

        return true;
    }

    std::string commandToJson(const CommandDto &commandDto) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"growDirectorUid\":\"" << commandDto.growDirectorUid << "\"";
        oss << ",\"type\":\"" << commandDto.type << "\"";
        
        if (!commandDto.createdAt.empty())
        {
            oss << ",\"createdAt\":\"" << commandDto.createdAt << "\"";
        }
        
        if (!commandDto.commandUid.empty())
        {
            oss << ",\"commandUid\":\"" << commandDto.commandUid << "\"";
        }

        // DeviceCommandNotification fields
        if (commandDto.isDeviceCommand())
        {
            oss << ",\"deviceUid\":\"" << commandDto.deviceUid << "\"";
            oss << ",\"commandId\":" << commandDto.commandId;
            oss << ",\"requestedState\":\"" << commandDto.requestedState << "\"";
        }

        // RuleCommandNotification fields
        if (commandDto.isRuleCommand())
        {
            oss << ",\"ruleUid\":\"" << commandDto.ruleUid << "\"";
            oss << ",\"rulesPayload\":\"" << commandDto.rulesPayload << "\"";
        }

        // ModuleCommandNotification fields
        if (commandDto.isModuleCommand())
        {
            oss << ",\"command\":\"" << commandDto.command << "\"";
            oss << ",\"version\":\"" << commandDto.version << "\"";
            oss << ",\"url\":\"" << commandDto.url << "\"";
        }

        // SensorTypeCommandNotification fields
        if (commandDto.isSensorTypeCommand())
        {
            oss << ",\"sensorUid\":\"" << commandDto.sensorUid << "\"";
            oss << ",\"sensorType\":\"" << commandDto.sensorType << "\"";
            
            if (!commandDto.model.empty())
            {
                oss << ",\"model\":\"" << commandDto.model << "\"";
            }
            
            if (!commandDto.command.empty())
            {
                oss << ",\"command\":\"" << commandDto.command << "\"";
            }
            
            if (!commandDto.options.empty())
            {
                oss << ",\"options\":" << commandDto.options;
            }
        }

        oss << "}";
        return oss.str();
    }

    std::string commandResultToJson(const CommandResultDto &commandResultDto) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"status\":\"" << commandResultDto.status << "\"";
        oss << ",\"command\":\"" << commandResultDto.command << "\"";
        
        if (!commandResultDto.message.empty())
        {
            oss << ",\"message\":\"" << commandResultDto.message << "\"";
        }
        
        oss << "}";
        return oss.str();
    }

    bool commandResultFromJson(const std::string &jsonStr, CommandResultDto &commandResultDto) override
    {
        commandResultDto.status = extractValue(jsonStr, "status");
        commandResultDto.command = extractValue(jsonStr, "command");
        commandResultDto.message = extractValue(jsonStr, "message");

        return !commandResultDto.status.empty() && !commandResultDto.command.empty();
    }

    std::string deviceCommandUpdateToJson(const DeviceCommandUpdateDto& commandUpdateDto) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"uid\":\"" << commandUpdateDto.uid << "\"";
        oss << ",\"status\":\"" << commandUpdateDto.status << "\"";
        oss << ",\"errorMessage\":\"" << commandUpdateDto.errorMessage << "\"";
        oss << "}";
        return oss.str();
    }

    bool deviceCommandFromJson(const std::string &jsonStr, DeviceCommandDto &commandDto) override
    {
        commandDto.id = extractIntValue(jsonStr, "id");
        commandDto.uid = extractValue(jsonStr, "uid");
        commandDto.requestedState = extractValue(jsonStr, "requestedState");
        commandDto.analogControlPinValue = extractIntValue(jsonStr, "analogControlPinValue");
        commandDto.status = extractValue(jsonStr, "status");
        commandDto.needToSwitchBack = extractBoolValue(jsonStr, "needToSwitchBack");
        commandDto.secondsToSwitchBack = extractIntValue(jsonStr, "secondsToSwitchBack");
        commandDto.switchType = extractValue(jsonStr, "switchType");
        commandDto.errorMessage = extractValue(jsonStr, "errorMessage");
        commandDto.createdDate = extractValue(jsonStr, "createdDate");
        commandDto.updatedDate = extractValue(jsonStr, "updatedDate");

        return true;
    }

    std::string deviceCommandToJson(const DeviceCommandDto &commandDto) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"id\":" << commandDto.id;
        oss << ",\"uid\":\"" << commandDto.uid << "\"";
        oss << ",\"requestedState\":\"" << commandDto.requestedState << "\"";
        
        if (commandDto.analogControlPinValue > 0)
        {
            oss << ",\"analogControlPinValue\":" << commandDto.analogControlPinValue;
        }
        
        oss << ",\"status\":\"" << commandDto.status << "\"";
        oss << ",\"needToSwitchBack\":" << (commandDto.needToSwitchBack ? "true" : "false");
        
        if (commandDto.secondsToSwitchBack > 0)
        {
            oss << ",\"secondsToSwitchBack\":" << commandDto.secondsToSwitchBack;
        }
        
        if (!commandDto.switchType.empty())
        {
            oss << ",\"switchType\":\"" << commandDto.switchType << "\"";
        }
        
        if (!commandDto.errorMessage.empty())
        {
            oss << ",\"errorMessage\":\"" << commandDto.errorMessage << "\"";
        }
        
        if (!commandDto.createdDate.empty())
        {
            oss << ",\"createdDate\":\"" << commandDto.createdDate << "\"";
        }
        
        if (!commandDto.updatedDate.empty())
        {
            oss << ",\"updatedDate\":\"" << commandDto.updatedDate << "\"";
        }

        oss << "}";
        return oss.str();
    }

    bool deviceCommandsFromJson(const std::string &jsonStr, std::vector<DeviceCommandDto> &commands) override
    {
        commands.clear();
        
        // Simple array parsing - look for array brackets
        if (jsonStr.front() == '[' && jsonStr.back() == ']')
        {
            // Parse as array
            auto commandObjs = splitObjects(jsonStr);
            for (const auto &commandStr : commandObjs)
            {
                DeviceCommandDto command;
                if (deviceCommandFromJson(commandStr, command))
                {
                    commands.push_back(command);
                }
            }
        }
        else if (jsonStr.front() == '{' && jsonStr.back() == '}')
        {
            // Single object
            DeviceCommandDto command;
            if (deviceCommandFromJson(jsonStr, command))
            {
                commands.push_back(command);
            }
        }
        
        return !commands.empty();
    }

    bool sensorFromJson(const std::string &jsonStr, SensorEntity &sensor) override
    {
        sensor.uid = extractValue(jsonStr, "uid");
        sensor.sensorType = extractValue(jsonStr, "sensorType");
        sensor.model = extractValue(jsonStr, "model");
        sensor.protocol = extractValue(jsonStr, "protocol");
        sensor.createdAt = extractValue(jsonStr, "createdAt");
        sensor.isActive = extractBoolValue(jsonStr, "isActive");
        sensor.isError = extractBoolValue(jsonStr, "isError");
        
        sensor.modbusDeviceAddress = extractIntValue(jsonStr, "modbusDeviceAddress");
        sensor.baudRate = extractIntValue(jsonStr, "baudRate");
        sensor.i2cAddress = extractValue(jsonStr, "i2cAddress");
        sensor.chipType = extractValue(jsonStr, "chipType");
        
        sensor.currentValue = static_cast<float>(extractDoubleValue(jsonStr, "currentValue"));
        sensor.lastReadTime = static_cast<uint32_t>(extractIntValue(jsonStr, "lastReadTime"));

        // Simple channel values parsing (basic implementation)
        sensor.channelValues.clear();
        // Note: This is a simplified implementation. A full implementation would parse the channelValues object

        return true;
    }

    std::string sensorToJson(const SensorEntity &sensor) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"uid\":\"" << sensor.uid << "\"";
        oss << ",\"sensorType\":\"" << sensor.sensorType << "\"";
        oss << ",\"model\":\"" << sensor.model << "\"";
        oss << ",\"protocol\":\"" << sensor.protocol << "\"";
        oss << ",\"createdAt\":\"" << sensor.createdAt << "\"";
        oss << ",\"isActive\":" << (sensor.isActive ? "true" : "false");
        oss << ",\"isError\":" << (sensor.isError ? "true" : "false");
        oss << ",\"modbusDeviceAddress\":" << sensor.modbusDeviceAddress;
        oss << ",\"baudRate\":" << sensor.baudRate;
        oss << ",\"i2cAddress\":\"" << sensor.i2cAddress << "\"";
        oss << ",\"chipType\":\"" << sensor.chipType << "\"";
        oss << ",\"currentValue\":" << sensor.currentValue;
        oss << ",\"lastReadTime\":" << sensor.lastReadTime;

        // Add channel values
        if (!sensor.channelValues.empty())
        {
            oss << ",\"channelValues\":{";
            for (size_t i = 0; i < sensor.channelValues.size(); ++i)
            {
                const auto& pair = sensor.channelValues[i];
                oss << "\"" << pair.first << "\":" << pair.second;
                if (i + 1 < sensor.channelValues.size())
                    oss << ",";
            }
            oss << "}";
        }

        oss << "}";
        return oss.str();
    }

    bool sensorsFromJson(const std::string &jsonStr, std::vector<SensorEntity> &sensors) override
    {
        sensors.clear();
        auto sensorObjs = splitObjects(jsonStr);
        for (const auto &sensorStr : sensorObjs)
        {
            SensorEntity sensor;
            if (sensorFromJson(sensorStr, sensor))
            {
                sensors.push_back(sensor);
            }
        }
        return !sensors.empty();
    }

    std::string sensorsToJson(const std::vector<SensorEntity> &sensors) override
    {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < sensors.size(); ++i)
        {
            oss << sensorToJson(sensors[i]);
            if (i + 1 < sensors.size())
                oss << ",";
        }
        oss << "]";
        return oss.str();
    }

    bool modbusRegisterConfigFromJson(const std::string &jsonStr, ModbusRegisterConfig &config) override
    {
        config.functionCode = extractIntValue(jsonStr, "functionCode");
        config.startRegister = static_cast<uint16_t>(extractIntValue(jsonStr, "startRegister"));
        config.numRegisters = static_cast<uint16_t>(extractIntValue(jsonStr, "numRegisters"));
        config.multiplier = static_cast<float>(extractDoubleValue(jsonStr, "multiplier"));
        config.offset = static_cast<float>(extractDoubleValue(jsonStr, "offset"));
        config.channelName = extractValue(jsonStr, "chanel_name");  // Note: API uses "chanel_name" (typo in original)
        config.channel = static_cast<uint8_t>(extractIntValue(jsonStr, "chanel"));  // Note: API uses "chanel"

        return !config.channelName.empty();
    }

    std::string modbusRegisterConfigToJson(const ModbusRegisterConfig &config) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"functionCode\":" << static_cast<int>(config.functionCode);
        oss << ",\"startRegister\":" << config.startRegister;
        oss << ",\"numRegisters\":" << config.numRegisters;
        oss << ",\"multiplier\":" << config.multiplier;
        oss << ",\"offset\":" << config.offset;
        oss << ",\"chanel_name\":\"" << config.channelName << "\"";
        oss << ",\"chanel\":" << static_cast<int>(config.channel);
        oss << "}";
        return oss.str();
    }

    bool modbusRegisterConfigsFromJson(const std::string &jsonStr, std::vector<ModbusRegisterConfig> &configs) override
    {
        configs.clear();
        auto configObjs = splitObjects(jsonStr);
        for (const auto &configStr : configObjs)
        {
            ModbusRegisterConfig config;
            if (modbusRegisterConfigFromJson(configStr, config))
            {
                configs.push_back(config);
            }
        }
        return !configs.empty();
    }

    std::string modbusRegisterConfigsToJson(const std::vector<ModbusRegisterConfig> &configs) override
    {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < configs.size(); ++i)
        {
            oss << modbusRegisterConfigToJson(configs[i]);
            if (i + 1 < configs.size())
                oss << ",";
        }
        oss << "]";
        return oss.str();
    }

    bool i2cRegisterConfigFromJson(const std::string &jsonStr, I2CRegisterConfig &config) override
    {
        config.registerAddr = static_cast<uint16_t>(extractIntValue(jsonStr, "register"));
        config.length = static_cast<uint8_t>(extractIntValue(jsonStr, "length"));
        config.multiplier = static_cast<float>(extractDoubleValue(jsonStr, "multiplier"));
        config.offset = static_cast<float>(extractDoubleValue(jsonStr, "offset"));

        return true;
    }

    std::string i2cRegisterConfigToJson(const I2CRegisterConfig &config) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"register\":" << config.registerAddr;
        oss << ",\"length\":" << static_cast<int>(config.length);
        oss << ",\"multiplier\":" << config.multiplier;
        oss << ",\"offset\":" << config.offset;
        oss << "}";
        return oss.str();
    }

    bool i2cRegisterConfigsFromJson(const std::string &jsonStr, std::vector<std::pair<std::string, I2CRegisterConfig>> &configs) override
    {
        configs.clear();
        
        // Extract I2C register config object (should be a JSON object, not array)
        std::string configObj = extractJsonObject(jsonStr, "i2cRegisterConfig");
        if (configObj.empty()) return false;
        
        // Parse each key-value pair in the object
        // This is a simplified implementation - in production, use proper JSON parser
        size_t pos = 1; // Start after opening brace
        while (pos < configObj.size())
        {
            // Find key
            size_t keyStart = configObj.find("\"", pos);
            if (keyStart == std::string::npos) break;
            
            size_t keyEnd = configObj.find("\"", keyStart + 1);
            if (keyEnd == std::string::npos) break;
            
            std::string key = configObj.substr(keyStart + 1, keyEnd - keyStart - 1);
            
            // Find value object
            size_t colonPos = configObj.find(":", keyEnd);
            if (colonPos == std::string::npos) break;
            
            size_t valueStart = configObj.find("{", colonPos);
            if (valueStart == std::string::npos) break;
            
            // Find matching closing brace
            int braceCount = 1;
            size_t valueEnd = valueStart + 1;
            while (valueEnd < configObj.size() && braceCount > 0)
            {
                if (configObj[valueEnd] == '{') braceCount++;
                else if (configObj[valueEnd] == '}') braceCount--;
                valueEnd++;
            }
            
            if (braceCount == 0)
            {
                std::string valueJson = configObj.substr(valueStart, valueEnd - valueStart);
                I2CRegisterConfig config;
                if (i2cRegisterConfigFromJson(valueJson, config))
                {
                    configs.push_back({key, config});
                }
            }
            
            pos = valueEnd;
        }
        
        return !configs.empty();
    }

    std::string i2cRegisterConfigsToJson(const std::vector<std::pair<std::string, I2CRegisterConfig>> &configs) override
    {
        std::ostringstream oss;
        oss << "{";
        for (size_t i = 0; i < configs.size(); ++i)
        {
            const auto& pair = configs[i];
            oss << "\"" << pair.first << "\":" << i2cRegisterConfigToJson(pair.second);
            if (i + 1 < configs.size())
                oss << ",";
        }
        oss << "}";
        return oss.str();
    }

private:
    /**
     * @brief Extract JSON object as string
     * @param json Source JSON string
     * @param key Key to extract
     * @return JSON object as string
     */
    std::string extractJsonObject(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";
        
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return "";
        
        size_t valueStart = colonPos + 1;
        while (valueStart < json.size() && std::isspace(json[valueStart])) valueStart++;
        if (valueStart < json.size() && json[valueStart] == '{')
        {
            int braceCount = 1;
            size_t valueEnd = valueStart + 1;
            while (valueEnd < json.size() && braceCount > 0)
            {
                if (json[valueEnd] == '{') braceCount++;
                else if (json[valueEnd] == '}') braceCount--;
                valueEnd++;
            }
            if (braceCount == 0)
            {
                return json.substr(valueStart, valueEnd - valueStart);
            }
        }
        return "";
    }

    // SensorType conversion methods
    bool sensorTypeFromJson(const std::string &jsonStr, SensorType &sensorType) override
    {
        sensorType.id = extractIntValue(jsonStr, "id");
        sensorType.name = extractValue(jsonStr, "name");
        sensorType.displayName = extractValue(jsonStr, "displayName");
        sensorType.isAnalog = extractBoolValue(jsonStr, "isAnalog");
        sensorType.canCalibrate = extractBoolValue(jsonStr, "canCalibrate");
        sensorType.validCalibrationPeriodDays = extractIntValue(jsonStr, "validCalibrationPeriodDays");
        sensorType.calibrationTimeInSeconds = extractIntValue(jsonStr, "calibrationTimeInSeconds");
        sensorType.minValue = static_cast<float>(extractDoubleValue(jsonStr, "minValue"));
        sensorType.maxValue = static_cast<float>(extractDoubleValue(jsonStr, "maxValue"));
        sensorType.limitMinValue = static_cast<float>(extractDoubleValue(jsonStr, "limitMinValue"));
        sensorType.limitMaxValue = static_cast<float>(extractDoubleValue(jsonStr, "limitMaxValue"));
        sensorType.unit = extractValue(jsonStr, "unit");
        sensorType.step = static_cast<float>(extractDoubleValue(jsonStr, "step"));
        sensorType.precisionPercent = static_cast<float>(extractDoubleValue(jsonStr, "precisionPercent"));
        sensorType.precisionValue = static_cast<float>(extractDoubleValue(jsonStr, "precisionValue"));
        
        std::string protocolStr = extractValue(jsonStr, "protocol");
        sensorType.setProtocolFromString(protocolStr);
        
        sensorType.i2cAddress = extractValue(jsonStr, "i2cAddress");
        sensorType.chipType = extractValue(jsonStr, "chipType");
        sensorType.modbusDeviceAddress = extractIntValue(jsonStr, "modbusDeviceAddress");
        sensorType.baudRate = extractIntValue(jsonStr, "baudRate");

        // Parse arrays (simplified implementation)
        sensorType.i2cRegisterConfig.clear();
        sensorType.modbusRegisterConfig.clear();

        return !sensorType.name.empty();
    }

    std::string sensorTypeToJson(const SensorType &sensorType) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"id\":" << sensorType.id;
        oss << ",\"name\":\"" << sensorType.name << "\"";
        oss << ",\"displayName\":\"" << sensorType.displayName << "\"";
        oss << ",\"isAnalog\":" << (sensorType.isAnalog ? "true" : "false");
        oss << ",\"canCalibrate\":" << (sensorType.canCalibrate ? "true" : "false");
        oss << ",\"validCalibrationPeriodDays\":" << sensorType.validCalibrationPeriodDays;
        oss << ",\"calibrationTimeInSeconds\":" << sensorType.calibrationTimeInSeconds;
        oss << ",\"minValue\":" << sensorType.minValue;
        oss << ",\"maxValue\":" << sensorType.maxValue;
        oss << ",\"limitMinValue\":" << sensorType.limitMinValue;
        oss << ",\"limitMaxValue\":" << sensorType.limitMaxValue;
        oss << ",\"unit\":\"" << sensorType.unit << "\"";
        oss << ",\"step\":" << sensorType.step;
        oss << ",\"precisionPercent\":" << sensorType.precisionPercent;
        oss << ",\"precisionValue\":" << sensorType.precisionValue;
        oss << ",\"protocol\":\"" << sensorType.getProtocolString() << "\"";
        oss << ",\"i2cAddress\":\"" << sensorType.i2cAddress << "\"";
        oss << ",\"chipType\":\"" << sensorType.chipType << "\"";
        oss << ",\"modbusDeviceAddress\":" << sensorType.modbusDeviceAddress;
        oss << ",\"baudRate\":" << sensorType.baudRate;
        oss << "}";
        return oss.str();
    }

    bool sensorTypesFromJson(const std::string &jsonStr, std::vector<SensorType> &sensorTypes) override
    {
        sensorTypes.clear();
        auto typeObjs = splitObjects(jsonStr);
        for (const auto &typeStr : typeObjs)
        {
            SensorType sensorType;
            if (sensorTypeFromJson(typeStr, sensorType))
            {
                sensorTypes.push_back(sensorType);
            }
        }
        return !sensorTypes.empty();
    }

    std::string sensorTypesToJson(const std::vector<SensorType> &sensorTypes) override
    {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < sensorTypes.size(); ++i)
        {
            oss << sensorTypeToJson(sensorTypes[i]);
            if (i + 1 < sensorTypes.size())
                oss << ",";
        }
        oss << "]";
        return oss.str();
    }
};

#endif // JSON_CONVERTER_H
