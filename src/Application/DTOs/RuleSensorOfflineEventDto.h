#pragma once

#include <string>

class RuleSensorOfflineEventDto {
public:
    std::string uid;
    std::string accountUid;
    std::string eventType = "rule_sensor_offline";
    std::string sensorUid;
    std::string ruleUid;
    std::string deviceUid;

    RuleSensorOfflineEventDto() = default;
    RuleSensorOfflineEventDto(const std::string& moduleUid, const std::string& account,
                              const std::string& sensor, const std::string& rule,
                              const std::string& device)
        : uid(moduleUid), accountUid(account), sensorUid(sensor), ruleUid(rule), deviceUid(device) {}
};
