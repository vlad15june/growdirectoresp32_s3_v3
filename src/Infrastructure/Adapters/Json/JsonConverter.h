#ifndef JSON_CONVERTER_H
#define JSON_CONVERTER_H

#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
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

    // Skip spaces after colon
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

// Helper function to extract array section
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

// Helper function to split array items (top-level only)
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

// Helper function to extract int value
static int extractIntValue(const std::string &src, const std::string &key)
{
    auto keyPos = src.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
        return 0;
    auto colon = src.find(':', keyPos);
    if (colon == std::string::npos)
        return 0;

    // Skip spaces after colon
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
        ++start; // Skip the minus sign
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

// Helper function to extract double value
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
        }

        return true;
    }

    std::string commandToJson(const CommandDto &commandDto) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"growDirectorUid\":\"" << commandDto.growDirectorUid << "\"";
        oss << ",\"type\":\"" << commandDto.type << "\"";

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
        }

        oss << "}";
        return oss.str();
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
};

#endif // JSON_CONVERTER_H
