#ifndef RULE_H
#define RULE_H

#include <string>
#include <vector>
#include "Core/Domain/Enums/RuleEnums.h"

class RuleCondition;
class ScheduleItem;

class RuleCondition {
public:
    RuleCondition() = default;

    std::vector<std::string> sensorUids;
    double valueTarget = 0.0;     // Used in step rules
    double valueLow = 0.0;        // Used in sensor rules
    double valueHigh = 0.0;       // Used in sensor rules
    RuleDirection direction;
};

class ScheduleItem {
public:
    ScheduleItem() = default;
    ScheduleItem(time_t startAt, int duration) 
        : startAt(startAt), duration(duration) {}

    time_t startAt;
    int duration;
};

class AdvancedRuleValue {
public:
    AdvancedRuleValue() = default;
    
    time_t time;
    double value;
};

class Rule {
public:
    Rule() = default;
    
    std::string uid;
    int version = 0;
    std::string workflowUid;
    int workflowVersion = 0;
    int workflowDisplayOrder = 0;
    int workflowPriority = 0;
    std::string dependsOnRuleUid;
    std::string deviceUid;
    RuleType ruleType = RuleType::UNKNOWN;
    RuleMode mode = RuleMode::OFF;
    std::string modeReason;
    std::string errorType;
    bool protectiveShutdown = false;
    RuleState state = RuleState::UNSYNCHRONIZED;
    std::vector<int> daysOfWeek;
    std::vector<int> daysOfMonth;
    
    // Sensor and Step rule properties
    std::string sensorUid;
    RuleDirection direction;
    std::vector<RuleCondition> conditions;
    bool conditionOr = false;
    int delayOn = 0;
    int delayOff = 0;
    
    // Step rule specific properties
    double valueTarget = 0.0;
    int cyclesToSetPoint = 0;
    int workingTime = 0;
    int waitingTime = 0;
    
    // Sensor rule specific properties
    double valueLow = 0.0;
    double valueHigh = 0.0;
    int timeToSetPoint = 0;
    bool isAdvanced = false;
    time_t startDayTime = 0;
    time_t endDayTime = 0;
    std::vector<AdvancedRuleValue> advancedRuleValues;
    
    // Schedule rule specific properties
    std::vector<ScheduleItem> schedule;
    
    bool isStepRule() const { return ruleType == RuleType::STEP; }
    bool isSensorRule() const { return ruleType == RuleType::SENSOR; }
    bool isScheduleRule() const { return ruleType == RuleType::SCHEDULE; }
};

#endif // RULE_H
