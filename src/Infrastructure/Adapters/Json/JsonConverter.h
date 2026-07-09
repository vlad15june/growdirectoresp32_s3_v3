#ifndef JSON_CONVERTER_H
#define JSON_CONVERTER_H

#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Data/Entities/WifiNetworkEntry.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Core/Domain/Enums/SensorStatusEnum.h"
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <utility>

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

// Helper function to convert internal pin numbering to external JSON format
static int convertPinToJson(int internalPin) {
    // Mapping: internal -> JSON
    // 0 -> 4, 1 -> 3, 2 -> 2, 3 -> 1
    switch (internalPin) {
        case 0: return 4;
        case 1: return 3;
        case 2: return 2;
        case 3: return 1;
        default: return internalPin; // Return unchanged if not in mapping
    }
}

// Helper function to convert external JSON pin numbering to internal format
static int convertPinFromJson(int jsonPin) {
    // Mapping: JSON -> internal
    // 4 -> 0, 3 -> 1, 2 -> 2, 1 -> 3
    switch (jsonPin) {
        case 4: return 0;
        case 3: return 1;
        case 2: return 2;
        case 1: return 3;
        default: return jsonPin; // Return unchanged if not in mapping
    }
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

// Replace the parseTimeToMinutes function with parseISOTimeToTimeT
static time_t parseISOTimeToTimeT(const std::string& isoTimeStr) {
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

static std::string timeToISOString(time_t secondsSinceMidnight) {
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
            node.psram = extractValue(nodeStr, "psram");
            node.uid = extractValue(nodeStr, "uid");
            node.firmware = extractValue(nodeStr, "firmware");
            node.ssid = extractValue(nodeStr, "ssid");
            node.ip = extractValue(nodeStr, "ip");
            node.chip = extractValue(nodeStr, "chip");
            if (node.chip.empty()) node.chip = "S3";
            node.board = extractIntValue(nodeStr, "board");

            // Sensors
            std::string sensorsArr = extractArray(nodeStr, "sensors");
            auto sensorObjs = splitObjects(sensorsArr);
            for (const auto &sensorStr : sensorObjs)
            {
                Sensor sensor;
                sensor.uid = extractValue(sensorStr, "uid");
                sensor.type = extractValue(sensorStr, "type");
                sensor.pin = convertPinFromJson(extractIntValue(sensorStr, "pin"));
                sensor.group = extractIntValue(sensorStr, "group");
                sensor.modelUid = extractValue(sensorStr, "modelUid");
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
            if (!node.psram.empty())
                oss << ",\"psram\":\"" << node.psram << "\"";
            oss << ",\"uid\":\"" << node.uid << "\"";
            if (!node.firmware.empty())
                oss << ",\"firmware\":\"" << node.firmware << "\"";
            if (!node.ssid.empty())
                oss << ",\"ssid\":\"" << node.ssid << "\"";
            if (!node.ip.empty())
                oss << ",\"ip\":\"" << node.ip << "\"";
            oss << ",\"chip\":\"" << node.chip << "\"";
            oss << ",\"board\":" << node.board;

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
                    oss << ",\"pin\":" << convertPinToJson(s.pin);
                    oss << ",\"group\":" << s.group;
                    oss << ",\"modelUid\":\"" << s.modelUid << "\"}";
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
        oss << ",\"psramSize\":" << diagnosticData.psramSize;
        oss << ",\"psramFree\":" << diagnosticData.psramFree;
        oss << ",\"failsInHour\":" << diagnosticData.failsInHour;
        oss << "}";

        return oss.str();
    }

    bool ruleFromJson(const std::string &jsonStr, Rule &rule) override
    {
        rule.uid = extractValue(jsonStr, "uid");
        rule.version = extractIntValue(jsonStr, "version");
        rule.workflowUid = extractValue(jsonStr, "workflowUid");
        rule.workflowVersion = extractIntValue(jsonStr, "workflowVersion");
        rule.workflowDisplayOrder = extractIntValue(jsonStr, "workflowDisplayOrder");
        rule.workflowPriority = extractIntValue(jsonStr, "workflowPriority");
        rule.dependsOnRuleUid = extractValue(jsonStr, "dependsOnRuleUid");
        rule.deviceUid = extractValue(jsonStr, "deviceUid");

        // Parse ruleType
        std::string ruleTypeStr = extractValue(jsonStr, "ruleType");
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
        std::string modeStr = extractValue(jsonStr, "mode");
        rule.mode = RuleMode::OFF;
        if (modeStr == "On")
            rule.mode = RuleMode::ON;
        else if (modeStr == "WaitingForUserConfirm")
            rule.mode = RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM;

        rule.modeReason = extractValue(jsonStr, "modeReason");
        rule.errorType = extractValue(jsonStr, "errorType");
        rule.protectiveShutdown = jsonStr.find("\"protectiveShutdown\":true") != std::string::npos;

        // Parse state
        std::string stateStr = extractValue(jsonStr, "state");
        if (stateStr == "Synchronized")
            rule.state = RuleState::SYNCHRONIZED;
        else if (stateStr == "Synchronizing")
            rule.state = RuleState::SYNCHRONIZING;
        else if (stateStr == "Deleting")
            rule.state = RuleState::DELETING;
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
        else if (directionStr == "between")
            rule.direction = RuleDirection::BETWEEN;

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
            else if (condDirStr == "between")
                condition.direction = RuleDirection::BETWEEN;

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
                item.startAt = parseISOTimeToTimeT(startAtStr);
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
        oss << ",\"workflowUid\":\"" << rule.workflowUid << "\"";
        oss << ",\"workflowVersion\":" << rule.workflowVersion;
        oss << ",\"workflowDisplayOrder\":" << rule.workflowDisplayOrder;
        oss << ",\"workflowPriority\":" << rule.workflowPriority;
        oss << ",\"dependsOnRuleUid\":\"" << rule.dependsOnRuleUid << "\"";
        oss << ",\"deviceUid\":\"" << rule.deviceUid << "\"";

        // RuleType
        oss << ",\"ruleType\":\"";
        if (rule.ruleType == RuleType::STEP)
            oss << "step";
        else if (rule.ruleType == RuleType::SENSOR)
            oss << "sensor";
        else if (rule.ruleType == RuleType::SCHEDULE)
            oss << "schedule";
        else if (rule.ruleType == RuleType::LOOP)
            oss << "loop";
        else if (rule.ruleType == RuleType::AVERAGE_SENSOR)
            oss << "average_sensor";
        else if (rule.ruleType == RuleType::PH)
            oss << "ph";
        else if (rule.ruleType == RuleType::EC)
            oss << "ec";
        else if (rule.ruleType == RuleType::DIMMER_AUTO)
            oss << "dimmer_auto";
        else if (rule.ruleType == RuleType::DIMMER_MANUAL)
            oss << "dimmer_manual";
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

        if (!rule.errorType.empty())
            oss << ",\"errorType\":\"" << rule.errorType << "\"";
        else
            oss << ",\"errorType\":null";

        oss << ",\"protectiveShutdown\":" << (rule.protectiveShutdown ? "true" : "false");

        // State
        oss << ",\"state\":\"";
        if (rule.state == RuleState::SYNCHRONIZED)
            oss << "Synchronized";
        else if (rule.state == RuleState::SYNCHRONIZING)
            oss << "Synchronizing";
        else if (rule.state == RuleState::DELETING)
            oss << "Deleting";
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
            else if (rule.direction == RuleDirection::BETWEEN)
                oss << "between";
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
                else if (cond.direction == RuleDirection::BETWEEN)
                    oss << "between";
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

        // Loop / ph / ec working-cycle properties
        if (rule.ruleType == RuleType::LOOP ||
            rule.ruleType == RuleType::PH ||
            rule.ruleType == RuleType::EC)
        {
            oss << ",\"workingTime\":" << rule.workingTime;
            oss << ",\"waitingTime\":" << rule.waitingTime;

            if (rule.cyclesToSetPoint > 0)
                oss << ",\"cyclesToSetPoint\":" << rule.cyclesToSetPoint;
            else
                oss << ",\"cyclesToSetPoint\":null";
        }

        // Average sensor / ph / ec threshold properties
        if (rule.ruleType == RuleType::AVERAGE_SENSOR ||
            rule.ruleType == RuleType::PH ||
            rule.ruleType == RuleType::EC)
        {
            oss << ",\"valueLow\":" << rule.valueLow;
            oss << ",\"valueHigh\":" << rule.valueHigh;
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
                oss << "\"startAt\":\"" << timeToISOString(item.startAt) << "\"";
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

                if (!device.workflowUid.empty())
                {
                    oss << ",\"workflowUid\":\"" << device.workflowUid << "\"";
                    oss << ",\"workflowVersion\":" << device.workflowVersion;
                }

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
                oss << ",\"commandId\":" << device.commandId;
                oss << ",\"secondsToSwitchBack\":" << device.secondsToSwitchBack;
                oss << ",\"secondsEnabledFor\":" << device.secondsEnabledFor;
            }

            oss << "}";
            if (i + 1 < updateData.devices.size())
                oss << ",";
        }
        oss << "]";

        oss << ",\"sensors\":[";
        for (size_t i = 0; i < updateData.sensors.size(); ++i)
        {
            const auto &sensor = updateData.sensors[i];
            oss << "{";
            oss << "\"uid\":\"" << sensor.uid << "\"";
            oss << ",\"value\":" << sensor.value;
            oss << ",\"state\":\"" << sensor.state << "\"";
            oss << "}";
            if (i + 1 < updateData.sensors.size())
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

            std::string optionsObj = extractJsonObject(jsonStr, "options");
            std::string topNoScan = extractValue(jsonStr, "noScanSensors");

            if (!optionsObj.empty())
            {
                if (!topNoScan.empty() && optionsObj.find("\"noScanSensors\"") == std::string::npos)
                {
                    size_t closeBrace = optionsObj.rfind('}');
                    if (closeBrace != std::string::npos)
                    {
                        std::string injected = "\"noScanSensors\":\"" + topNoScan + "\"";
                        bool needsComma = false;
                        for (size_t i = closeBrace; i > 0; --i)
                        {
                            char c = optionsObj[i - 1];
                            if (c == '{') break;
                            if (!std::isspace(static_cast<unsigned char>(c)))
                            {
                                needsComma = true;
                                break;
                            }
                        }
                        optionsObj.insert(closeBrace, (needsComma ? "," : "") + injected);
                    }
                }
                commandDto.options = optionsObj;
            }
            else if (!topNoScan.empty())
            {
                commandDto.options = "{\"noScanSensors\":\"" + topNoScan + "\"}";
            }

            if (commandDto.command == "ModbusWrite" || commandDto.command == "ModbusRead" || commandDto.command == "SerialWrite")
            {
                commandDto.options = buildRawCommandOptions(jsonStr);
            }
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

    std::string rawIoResultToJson(const RawIoResultDto &rawIoResult) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"command\":\"" << rawIoResult.command << "\"";
        oss << ",\"channel\":" << rawIoResult.channel;
        if (rawIoResult.isError)
        {
            oss << ",\"error\":\"" << rawIoResult.errorMessage << "\"";
        }
        else
        {
            oss << ",\"data\":\"" << rawIoResult.dataHex << "\"";
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
        oss << "[{";
        oss << "\"uid\":\"" << commandUpdateDto.uid << "\"";
        oss << ",\"status\":\"" << commandUpdateDto.status << "\"";
        oss << ",\"errorMessage\":\"" << commandUpdateDto.errorMessage << "\"";
        if (!commandUpdateDto.timestamp.empty())
            oss << ",\"timestamp\":\"" << commandUpdateDto.timestamp << "\"";
        if (!commandUpdateDto.accountUid.empty())
            oss << ",\"accountUid\":\"" << commandUpdateDto.accountUid << "\"";
        oss << "}]";
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

    std::string ruleStateToJson(const RuleStateDto& ruleState) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"uid\":\"" << ruleState.uid << "\"";
        oss << ",\"state\":\"" << ruleState.state << "\"";
        oss << ",\"mode\":\"" << ruleState.mode << "\"";
        
        if (!ruleState.modeReason.empty())
        {
            oss << ",\"modeReason\":\"" << ruleState.modeReason << "\"";
        }
        
        if (!ruleState.errorType.empty())
        {
            oss << ",\"errorType\":\"" << ruleState.errorType << "\"";
        }
        
        oss << "}";
        return oss.str();
    }

    bool ruleStateFromJson(const std::string& jsonStr, RuleStateDto& ruleState) override
    {
        ruleState.uid = extractValue(jsonStr, "uid");
        ruleState.state = extractValue(jsonStr, "state");
        if (ruleState.state.empty())
            ruleState.state = "Synchronized";
        
        ruleState.mode = extractValue(jsonStr, "mode");
        if (ruleState.mode.empty())
            ruleState.mode = "Off";
        
        ruleState.modeReason = extractValue(jsonStr, "modeReason");
        ruleState.errorType = extractValue(jsonStr, "errorType");
        
        return !ruleState.uid.empty();
    }

    std::string ruleStateUpdateToJson(const RuleStateUpdateDto& ruleStateUpdate) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"timestamp\":\"" << ruleStateUpdate.timestamp << "\"";
        oss << ",\"status\":\"" << ruleStateUpdate.status << "\"";
        
        if (!ruleStateUpdate.errorType.empty())
        {
            oss << ",\"errorType\":\"" << ruleStateUpdate.errorType << "\"";
        }
        
        if (!ruleStateUpdate.accountUid.empty())
        {
            oss << ",\"accountUid\":\"" << ruleStateUpdate.accountUid << "\"";
        }

        oss << ",\"rules\":[";
        for (size_t i = 0; i < ruleStateUpdate.rules.size(); ++i)
        {
            const auto& ruleState = ruleStateUpdate.rules[i];
            oss << "{";
            oss << "\"uid\":\"" << ruleState.uid << "\"";
            oss << ",\"version\":" << ruleState.version;
            oss << ",\"state\":\"" << ruleState.state << "\"";
            oss << ",\"mode\":\"" << ruleState.mode << "\"";
            
            if (!ruleState.modeReason.empty())
            {
                oss << ",\"modeReason\":\"" << ruleState.modeReason << "\"";
            }
            
            if (!ruleState.errorType.empty())
            {
                oss << ",\"errorType\":\"" << ruleState.errorType << "\"";
            }
            
            oss << "}";
            if (i + 1 < ruleStateUpdate.rules.size())
                oss << ",";
        }
        oss << "]}";

        return oss.str();
    }

    std::string ruleStateUpdateSingleToJson(const RuleStateDto& ruleState, const std::string& timestamp) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"timestamp\":\"" << timestamp << "\"";
        if (!ruleState.accountUid.empty())
        {
            oss << ",\"accountUid\":\"" << ruleState.accountUid << "\"";
        }
        oss << ",\"uid\":\"" << ruleState.uid << "\"";
        oss << ",\"state\":\"" << ruleState.state << "\"";
        oss << ",\"mode\":\"" << ruleState.mode << "\"";
        oss << ",\"modeReason\":\"" << ruleState.modeReason << "\"";

        if (!ruleState.errorType.empty())
        {
            oss << ",\"errorType\":\"" << ruleState.errorType << "\"";
        }

        oss << "}";

        return oss.str();
    }

    std::string ruleSensorOfflineEventToJson(const RuleSensorOfflineEventDto& event) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"uid\":\"" << event.uid << "\"";
        oss << ",\"accountUid\":\"" << event.accountUid << "\"";
        oss << ",\"eventType\":\"" << event.eventType << "\"";
        oss << ",\"sensorUid\":\"" << event.sensorUid << "\"";
        oss << ",\"ruleUid\":\"" << event.ruleUid << "\"";
        oss << ",\"deviceUid\":\"" << event.deviceUid << "\"";
        oss << "}";

        return oss.str();
    }

    bool ruleStateUpdateFromJson(const std::string& jsonStr, RuleStateUpdateDto& ruleStateUpdate) override
    {
        ruleStateUpdate.timestamp = extractValue(jsonStr, "timestamp");
        ruleStateUpdate.status = extractValue(jsonStr, "status");
        if (ruleStateUpdate.status.empty())
            ruleStateUpdate.status = "Success";
        
        ruleStateUpdate.errorType = extractValue(jsonStr, "errorType");
        ruleStateUpdate.accountUid = extractValue(jsonStr, "accountUid");

        ruleStateUpdate.rules.clear();
        std::string rulesArr = extractArray(jsonStr, "rules");
        auto ruleObjs = splitObjects(rulesArr);
        
        for (const auto& ruleStr : ruleObjs)
        {
            RuleStateDto ruleState;
            ruleState.uid = extractValue(ruleStr, "uid");
            ruleState.version = extractIntValue(ruleStr, "version");
            ruleState.state = extractValue(ruleStr, "state");
            if (ruleState.state.empty())
                ruleState.state = "Synchronized";
            
            ruleState.mode = extractValue(ruleStr, "mode");
            if (ruleState.mode.empty())
                ruleState.mode = "Off";
            
            ruleState.modeReason = extractValue(ruleStr, "modeReason");
            ruleState.errorType = extractValue(ruleStr, "errorType");
            
            ruleStateUpdate.rules.push_back(ruleState);
        }

        return !ruleStateUpdate.rules.empty() || jsonStr.find("\"rules\":[]") != std::string::npos;
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
        sensor.pin = extractIntValue(jsonStr, "pin");
        if (sensor.pin == 0) sensor.pin = -1; // Default to -1 if not found
        
        std::string statusStr = extractValue(jsonStr, "status");
        if (statusStr.empty()) statusStr = "new"; // default
        sensor.status = getSensorStatusFromString(statusStr);
        
        sensor.modbusDeviceAddress = extractIntValue(jsonStr, "modbusDeviceAddress");
        sensor.baudRate = extractIntValue(jsonStr, "baudRate");
        sensor.modbusPingAddress = static_cast<uint16_t>(extractIntValue(jsonStr, "modbusPingAddress"));
        sensor.registerToStoreModbusDeviceAddress = static_cast<uint16_t>(extractIntValue(jsonStr, "registerToStoreModbusDeviceAddress"));
        sensor.writeRegistersCommand = static_cast<uint8_t>(extractIntValue(jsonStr, "writeRegistersCommand"));
        sensor.i2cAddress = extractValue(jsonStr, "i2cAddress");
        sensor.chipType = extractValue(jsonStr, "chipType");
        
        sensor.currentValue = static_cast<float>(extractDoubleValue(jsonStr, "currentValue"));
        sensor.lastReadTime = static_cast<uint32_t>(extractIntValue(jsonStr, "lastReadTime"));
        
        sensor.sensorTypeId = extractIntValue(jsonStr, "sensorTypeId");
        sensor.sensorTypeUid = extractValue(jsonStr, "sensorTypeUid");

        // Note: channelValues are now handled by SensorValueModel, not stored in SensorEntity

        // Parse modbus register configs
        sensor.modbusRegisterConfigs.clear();
        std::string modbusConfigsArr = extractArray(jsonStr, "modbusRegisterConfigs");
        auto modbusConfigObjs = splitObjects(modbusConfigsArr);
        for (const auto &configStr : modbusConfigObjs)
        {
            ModbusRegisterConfig config;
            config.functionCode = static_cast<uint8_t>(extractIntValue(configStr, "functionCode"));
            config.startRegister = static_cast<uint16_t>(extractIntValue(configStr, "startRegister"));
            config.numRegisters = static_cast<uint16_t>(extractIntValue(configStr, "numRegisters"));
            config.multiplier = static_cast<float>(extractDoubleValue(configStr, "multiplier"));
            config.offset = static_cast<float>(extractDoubleValue(configStr, "offset"));
            config.channelName = extractValue(configStr, "channel_name");
            config.channel = static_cast<uint8_t>(extractIntValue(configStr, "channel"));
            config.sensorType = extractValue(configStr, "sensorType");
            config.canConnectMinValue = static_cast<float>(extractDoubleValue(configStr, "canConnectMinValue"));
            config.canConnectMaxValue = static_cast<float>(extractDoubleValue(configStr, "canConnectMaxValue"));
            sensor.modbusRegisterConfigs.push_back(config);
        }

        // Parse i2c register configs
        sensor.i2cRegisterConfigs.clear();
        std::string i2cConfigsArr = extractArray(jsonStr, "i2cRegisterConfigs");
        auto i2cConfigObjs = splitObjects(i2cConfigsArr);
        for (const auto &configStr : i2cConfigObjs)
        {
            I2CRegisterConfig config;
            config.registerAddr = static_cast<uint16_t>(extractIntValue(configStr, "register"));
            config.length = static_cast<uint8_t>(extractIntValue(configStr, "length"));
            config.multiplier = static_cast<float>(extractDoubleValue(configStr, "multiplier"));
            config.offset = static_cast<float>(extractDoubleValue(configStr, "offset"));
            config.channelName = extractValue(configStr, "channel_name");
            config.channel = static_cast<uint8_t>(extractIntValue(configStr, "channel"));
            config.sensorType = extractValue(configStr, "sensorType");
            sensor.i2cRegisterConfigs.push_back(config);
        }

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
        oss << ",\"status\":\"" << getSensorStatusName(sensor.status) << "\"";
        oss << ",\"pin\":" << sensor.pin;
        oss << ",\"modbusDeviceAddress\":" << sensor.modbusDeviceAddress;
        oss << ",\"baudRate\":" << sensor.baudRate;
        oss << ",\"modbusPingAddress\":" << sensor.modbusPingAddress;
        oss << ",\"registerToStoreModbusDeviceAddress\":" << sensor.registerToStoreModbusDeviceAddress;
        oss << ",\"writeRegistersCommand\":" << static_cast<int>(sensor.writeRegistersCommand);
        oss << ",\"i2cAddress\":\"" << sensor.i2cAddress << "\"";
        oss << ",\"chipType\":\"" << sensor.chipType << "\"";
        oss << ",\"currentValue\":" << sensor.currentValue;
        oss << ",\"lastReadTime\":" << sensor.lastReadTime;
        oss << ",\"sensorTypeId\":" << sensor.sensorTypeId;
        oss << ",\"sensorTypeUid\":\"" << sensor.sensorTypeUid << "\"";

        // Note: channelValues are now handled by SensorValueModel and not serialized here

        // Add modbus register configs
        if (!sensor.modbusRegisterConfigs.empty())
        {
            oss << ",\"modbusRegisterConfigs\":[";
            for (size_t i = 0; i < sensor.modbusRegisterConfigs.size(); ++i)
            {
                const auto& config = sensor.modbusRegisterConfigs[i];
                oss << "{";
                oss << "\"functionCode\":" << static_cast<int>(config.functionCode);
                oss << ",\"startRegister\":" << config.startRegister;
                oss << ",\"numRegisters\":" << config.numRegisters;
                oss << ",\"multiplier\":" << config.multiplier;
                oss << ",\"offset\":" << config.offset;
                oss << ",\"channel_name\":\"" << config.channelName << "\"";
                oss << ",\"channel\":" << static_cast<int>(config.channel);
                oss << ",\"sensorType\":\"" << config.sensorType << "\"";
                oss << ",\"canConnectMinValue\":" << config.canConnectMinValue;
                oss << ",\"canConnectMaxValue\":" << config.canConnectMaxValue;
                oss << "}";
                if (i + 1 < sensor.modbusRegisterConfigs.size())
                    oss << ",";
            }
            oss << "]";
        }

        // Add i2c register configs
        if (!sensor.i2cRegisterConfigs.empty())
        {
            oss << ",\"i2cRegisterConfigs\":[";
            for (size_t i = 0; i < sensor.i2cRegisterConfigs.size(); ++i)
            {
                const auto& config = sensor.i2cRegisterConfigs[i];
                oss << "{";
                oss << "\"register\":" << config.registerAddr;
                oss << ",\"length\":" << static_cast<int>(config.length);
                oss << ",\"multiplier\":" << config.multiplier;
                oss << ",\"offset\":" << config.offset;
                oss << ",\"channel_name\":\"" << config.channelName << "\"";
                oss << ",\"channel\":" << static_cast<int>(config.channel);
                oss << ",\"sensorType\":\"" << config.sensorType << "\"";
                oss << "}";
                if (i + 1 < sensor.i2cRegisterConfigs.size())
                    oss << ",";
            }
            oss << "]";
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
        config.channelName = extractValue(jsonStr, "channel_name");
        config.channel = static_cast<uint8_t>(extractIntValue(jsonStr, "channel"));
        config.sensorType = extractValue(jsonStr, "sensorType");
        config.canConnectMinValue = static_cast<float>(extractDoubleValue(jsonStr, "canConnectMinValue"));
        config.canConnectMaxValue = static_cast<float>(extractDoubleValue(jsonStr, "canConnectMaxValue"));

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
        oss << ",\"channel_name\":\"" << config.channelName << "\"";
        oss << ",\"channel\":" << static_cast<int>(config.channel);
        oss << ",\"sensorType\":\"" << config.sensorType << "\"";
        oss << ",\"canConnectMinValue\":" << config.canConnectMinValue;
        oss << ",\"canConnectMaxValue\":" << config.canConnectMaxValue;
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
        config.channelName = extractValue(jsonStr, "channel_name");
        config.channel = static_cast<uint8_t>(extractIntValue(jsonStr, "channel"));
        config.sensorType = extractValue(jsonStr, "sensorType");

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

    bool i2cRegisterConfigsFromJson(const std::string &jsonStr, std::vector<I2CRegisterConfig> &configs) override
    {
        configs.clear();
        auto configObjs = splitObjects(jsonStr);
        for (const auto &configStr : configObjs)
        {
            I2CRegisterConfig config;
            if (i2cRegisterConfigFromJson(configStr, config))
            {
                // Extract channel name and channel from the config JSON
                config.channelName = extractValue(configStr, "channel_name");
                config.channel = static_cast<uint8_t>(extractIntValue(configStr, "channel"));
                config.sensorType = extractValue(configStr, "sensorType");
                configs.push_back(config);
            }
        }
        return !configs.empty();
    }

    std::string i2cRegisterConfigsToJson(const std::vector<I2CRegisterConfig> &configs) override
    {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < configs.size(); ++i)
        {
            const auto& config = configs[i];
            oss << "{";
            oss << "\"register\":" << config.registerAddr;
            oss << ",\"length\":" << static_cast<int>(config.length);
            oss << ",\"multiplier\":" << config.multiplier;
            oss << ",\"offset\":" << config.offset;
            oss << ",\"channel_name\":\"" << config.channelName << "\"";
            oss << ",\"channel\":" << static_cast<int>(config.channel);
            oss << ",\"sensorType\":\"" << config.sensorType << "\"";
            oss << "}";
            if (i + 1 < configs.size())
                oss << ",";
        }
        oss << "]";
        return oss.str();
    }

    std::string moduleCommandUpdateToJson(const ModuleCommandUpdateDto& commandUpdateDto) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"uid\":\"" << commandUpdateDto.uid << "\"";
        oss << ",\"status\":\"" << commandUpdateDto.status << "\"";
        
        if (!commandUpdateDto.errorMessage.empty())
        {
            oss << ",\"errorMessage\":\"" << commandUpdateDto.errorMessage << "\"";
        }
        
        if (!commandUpdateDto.timestamp.empty())
        {
            oss << ",\"timestamp\":\"" << commandUpdateDto.timestamp << "\"";
        }
        
        if (!commandUpdateDto.accountUid.empty())
        {
            oss << ",\"accountUid\":\"" << commandUpdateDto.accountUid << "\"";
        }
        
        // Add result field with sensors array if present
        if (!commandUpdateDto.result.sensors.empty())
        {
            oss << ",\"result\":{\"sensors\":[";
            for (size_t i = 0; i < commandUpdateDto.result.sensors.size(); ++i)
            {
                if (i > 0) oss << ",";
                const auto& sensor = commandUpdateDto.result.sensors[i];
                oss << "{";
                oss << "\"uid\":\"" << sensor.uid << "\"";
                oss << ",\"type\":\"" << sensor.type << "\"";
                oss << ",\"pin\":" << sensor.pin;
                oss << ",\"group\":" << sensor.group;
                oss << ",\"modelUid\":\"" << sensor.modelUid << "\"";
                oss << "}";
            }
            oss << "]}";
        }
        
        oss << "}";
        return oss.str();
    }

    std::string moduleCommandUpdatesToJson(const std::vector<ModuleCommandUpdateDto>& commandUpdates) override
    {
        std::ostringstream oss;
        oss << "[";
        
        for (size_t i = 0; i < commandUpdates.size(); ++i)
        {
            if (i > 0) oss << ",";
            
            const auto& commandUpdateDto = commandUpdates[i];
            oss << "{";
            oss << "\"uid\":\"" << commandUpdateDto.uid << "\"";
            oss << ",\"status\":\"" << commandUpdateDto.status << "\"";
            
            if (!commandUpdateDto.errorMessage.empty())
            {
                oss << ",\"errorMessage\":\"" << commandUpdateDto.errorMessage << "\"";
            }
            
            if (!commandUpdateDto.timestamp.empty())
            {
                oss << ",\"timestamp\":\"" << commandUpdateDto.timestamp << "\"";
            }
            
            if (!commandUpdateDto.accountUid.empty())
            {
                oss << ",\"accountUid\":\"" << commandUpdateDto.accountUid << "\"";
            }
            
            // Add result field with sensors array if present
            if (!commandUpdateDto.result.sensors.empty())
            {
                oss << ",\"result\":{\"sensors\":[";
                for (size_t j = 0; j < commandUpdateDto.result.sensors.size(); ++j)
                {
                    if (j > 0) oss << ",";
                    const auto& sensor = commandUpdateDto.result.sensors[j];
                    oss << "{";
                    oss << "\"uid\":\"" << sensor.uid << "\"";
                    oss << ",\"type\":\"" << sensor.type << "\"";
                    oss << ",\"pin\":" << sensor.pin;
                    oss << ",\"group\":" << sensor.group;
                    oss << ",\"modelUid\":\"" << sensor.modelUid << "\"";
                    oss << "}";
                }
                oss << "]}";
            }
            
            oss << "}";
        }
        
        oss << "]";
        return oss.str();
    }

    bool moduleCommandFromJson(const std::string &jsonStr, ModuleCommandDto &commandDto) override
    {
        commandDto.uid = extractValue(jsonStr, "uid");
        commandDto.command = extractValue(jsonStr, "command");
        
        // Try to extract options as JSON object first, if not found try as string
        std::string optionsObj = extractJsonObject(jsonStr, "options");
        if (!optionsObj.empty())
        {
            commandDto.options = optionsObj;
        }
        else
        {
            commandDto.options = extractValue(jsonStr, "options");
        }
        
        commandDto.createdAt = extractValue(jsonStr, "createdAt");

        return !commandDto.uid.empty();
    }

    std::string moduleCommandToJson(const ModuleCommandDto &commandDto) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"uid\":\"" << commandDto.uid << "\"";
        oss << ",\"command\":\"" << commandDto.command << "\"";
        oss << ",\"options\":\"" << commandDto.options << "\"";
        
        if (!commandDto.createdAt.empty())
        {
            oss << ",\"createdAt\":\"" << commandDto.createdAt << "\"";
        }

        oss << "}";
        return oss.str();
    }

    bool moduleCommandsFromJson(const std::string &jsonStr, std::vector<ModuleCommandDto> &commands) override
    {
        commands.clear();
        
        // Simple array parsing - look for array brackets
        if (jsonStr.front() == '[' && jsonStr.back() == ']')
        {
            // Parse as array
            auto commandObjs = splitObjects(jsonStr);
            for (const auto &commandStr : commandObjs)
            {
                ModuleCommandDto command;
                if (moduleCommandFromJson(commandStr, command))
                {
                    commands.push_back(command);
                }
            }
        }
        else if (jsonStr.front() == '{' && jsonStr.back() == '}')
        {
            // Single object
            ModuleCommandDto command;
            if (moduleCommandFromJson(jsonStr, command))
            {
                commands.push_back(command);
            }
        }
        
        return !commands.empty();
    }

private:
    std::string buildRawCommandOptions(const std::string &jsonStr)
    {
        std::string opts = "{";
        bool first = true;

        const char* intKeys[] = {"channel", "address", "function", "register", "count", "baudRate"};
        for (const char* key : intKeys)
        {
            std::string token = "\"" + std::string(key) + "\"";
            if (jsonStr.find(token) != std::string::npos)
            {
                if (!first) opts += ",";
                opts += token + ":" + std::to_string(extractIntValue(jsonStr, key));
                first = false;
            }
        }

        const char* strKeys[] = {"data", "bytes"};
        for (const char* key : strKeys)
        {
            std::string token = "\"" + std::string(key) + "\"";
            if (jsonStr.find(token) != std::string::npos)
            {
                if (!first) opts += ",";
                opts += token + ":\"" + extractValue(jsonStr, key) + "\"";
                first = false;
            }
        }

        size_t erPos = jsonStr.find("\"expectResponse\"");
        if (erPos != std::string::npos)
        {
            size_t colon = jsonStr.find(":", erPos);
            bool er = false;
            if (colon != std::string::npos)
            {
                std::string tail = jsonStr.substr(colon + 1, 8);
                er = tail.find("true") != std::string::npos;
            }
            if (!first) opts += ",";
            opts += "\"expectResponse\":";
            opts += (er ? "true" : "false");
            first = false;
        }

        opts += "}";
        return opts;
    }

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

    // SensorType conversion methods - simplified for new structure
    bool sensorTypeFromJson(const std::string &jsonStr, SensorType &sensorType) override
    {
        sensorType.uid = extractValue(jsonStr, "uid");
        sensorType.name = extractValue(jsonStr, "name");
        sensorType.displayName = extractValue(jsonStr, "displayName");
        sensorType.canScan = extractBoolValue(jsonStr, "canScan");
        sensorType.isSupported = extractBoolValue(jsonStr, "isSupported");
        sensorType.createdAt = extractValue(jsonStr, "createdAt");
        sensorType.updatedAt = extractValue(jsonStr, "updatedAt");
        
        std::string protocolStr = extractValue(jsonStr, "protocol");
        sensorType.setProtocolFromString(protocolStr);
        
        sensorType.i2cAddress = extractValue(jsonStr, "i2cAddress");
        sensorType.chipType = extractValue(jsonStr, "chipType");
        sensorType.modbusDeviceAddress = extractIntValue(jsonStr, "modbusDeviceAddress");
        sensorType.baudRate = extractIntValue(jsonStr, "baudRate");
        sensorType.modbusPingAddress = static_cast<uint16_t>(extractIntValue(jsonStr, "modbusPingAddress"));
        sensorType.registerToStoreModbusDeviceAddress = static_cast<uint16_t>(extractIntValue(jsonStr, "registerToStoreModbusDeviceAddress"));
        sensorType.writeRegistersCommand = static_cast<uint8_t>(extractIntValue(jsonStr, "writeRegistersCommand"));
        sensorType.priorityForScan = extractIntValue(jsonStr, "priorityForScan");

        // Legacy id for backward compatibility
        sensorType.id = extractIntValue(jsonStr, "id");

        // Parse arrays (simplified implementation - complex parsing should use ArduinoJsonConverter)
        sensorType.i2cRegisterConfig.clear();
        sensorType.modbusRegisterConfig.clear();
        sensorType.sensorTypes.clear();
        
        // Parse sensorTypes array - basic implementation for testing
        auto sensorTypesPos = jsonStr.find("\"sensorTypes\":");
        if (sensorTypesPos != std::string::npos) {
            auto arrayStart = jsonStr.find("[", sensorTypesPos);
            auto arrayEnd = jsonStr.find("]", arrayStart);
            if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
                std::string arrayContent = jsonStr.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
                if (!arrayContent.empty() && arrayContent != " ") {
                    // Simple parsing - assuming single object for now
                    if (arrayContent.find("{") != std::string::npos) {
                        SubSensorType subType;
                        subType.name = extractValue(arrayContent, "name");
                        subType.displayName = extractValue(arrayContent, "displayName");
                        subType.canCalibrate = extractBoolValue(arrayContent, "canCalibrate");
                        subType.createdAt = extractValue(arrayContent, "createdAt");
                        subType.updatedAt = extractValue(arrayContent, "updatedAt");
                        sensorType.sensorTypes.push_back(subType);
                    }
                }
            }
        }
        
        return !sensorType.name.empty();
    }

    std::string sensorTypeToJson(const SensorType &sensorType) override
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"uid\":\"" << sensorType.uid << "\"";
        oss << ",\"name\":\"" << sensorType.name << "\"";
        oss << ",\"displayName\":\"" << sensorType.displayName << "\"";
        oss << ",\"canScan\":" << (sensorType.canScan ? "true" : "false");
        oss << ",\"protocol\":\"" << sensorType.getProtocolString() << "\"";
        oss << ",\"isSupported\":" << (sensorType.isSupported ? "true" : "false");
        if (!sensorType.createdAt.empty())
            oss << ",\"createdAt\":\"" << sensorType.createdAt << "\"";
        if (!sensorType.updatedAt.empty())
            oss << ",\"updatedAt\":\"" << sensorType.updatedAt << "\"";
        oss << ",\"i2cAddress\":\"" << sensorType.i2cAddress << "\"";
        oss << ",\"chipType\":\"" << sensorType.chipType << "\"";
        oss << ",\"modbusDeviceAddress\":" << sensorType.modbusDeviceAddress;
        oss << ",\"baudRate\":" << sensorType.baudRate;
        oss << ",\"modbusPingAddress\":" << sensorType.modbusPingAddress;
        oss << ",\"registerToStoreModbusDeviceAddress\":" << sensorType.registerToStoreModbusDeviceAddress;
        oss << ",\"writeRegistersCommand\":" << static_cast<int>(sensorType.writeRegistersCommand);
        oss << ",\"priorityForScan\":" << sensorType.priorityForScan;
        
        // Add sensorTypes array - simplified (for complex serialization use ArduinoJsonConverter)
        oss << ",\"sensorTypes\":[";
        for (size_t i = 0; i < sensorType.sensorTypes.size(); i++) {
            if (i > 0) oss << ",";
            const auto& subType = sensorType.sensorTypes[i];
            oss << "{\"name\":\"" << subType.name << "\",\"displayName\":\"" << subType.displayName << "\"";
            oss << ",\"canCalibrate\":" << (subType.canCalibrate ? "true" : "false");
            if (!subType.createdAt.empty()) {
                oss << ",\"createdAt\":\"" << subType.createdAt << "\"";
            }
            if (!subType.updatedAt.empty()) {
                oss << ",\"updatedAt\":\"" << subType.updatedAt << "\"";
            }
            oss << "}";
        }
        oss << "]";
        
        if (sensorType.id != 0) {
            oss << ",\"id\":" << sensorType.id;
        }
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

    bool wifiNetworksFromJson(const std::string &jsonStr, std::vector<WifiNetworkEntry> &networks) override
    {
        networks.clear();
        if (jsonStr.empty())
            return false;
        auto objects = splitObjects(jsonStr);
        for (const auto &obj : objects)
        {
            std::string ssid = extractValue(obj, "ssid");
            std::string password = extractValue(obj, "password");
            if (!ssid.empty() && !password.empty())
                networks.push_back({ssid, password});
        }
        return !networks.empty();
    }

    std::string wifiNetworksToJson(const std::vector<WifiNetworkEntry> &networks) override
    {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < networks.size(); ++i)
        {
            oss << "{\"ssid\":\"" << networks[i].ssid << "\",\"password\":\"" << networks[i].password << "\"}";
            if (i + 1 < networks.size())
                oss << ",";
        }
        oss << "]";
        return oss.str();
    }

    bool sensorValuesFromUpdateJson(const std::string &json, std::vector<std::pair<std::string, float>> &out) override
    {
        std::string sensorsArr = extractArray(json, "sensors");
        if (sensorsArr.empty())
            return false;

        auto sensorObjs = splitObjects(sensorsArr);
        if (sensorObjs.empty())
            return false;

        for (const auto &sensorStr : sensorObjs)
        {
            std::string uid = extractValue(sensorStr, "uid");
            if (uid.empty())
                continue;

            if (sensorStr.find("\"value\"") == std::string::npos)
                continue;

            float value = static_cast<float>(extractDoubleValue(sensorStr, "value"));
            out.push_back(std::make_pair(uid, value));
        }

        return true;
    }
};

#endif // JSON_CONVERTER_H
