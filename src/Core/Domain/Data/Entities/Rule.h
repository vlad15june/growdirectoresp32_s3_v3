#ifndef RULE_H
#define RULE_H

#include <cstddef>
#include <string>
#include <vector>
#include "Core/Domain/Enums/RuleEnums.h"

namespace RuleLimits {

/**
 * @brief How many rules one module keeps, at most.
 *
 * A declared capacity: rules are read one at a time, so nothing in the parsing decides how many of
 * them there can be, and something has to. The number is read off the product - a module drives at
 * most four devices (ModuleConfig assigns 4 to the relay board, 3 to the hydro one, 2 to a socket
 * or a dimmer) and every rule names one, because a rule without a deviceUid is refused - so this
 * is six rules per device on the largest board, against rule sets of nine to fifteen in the field.
 *
 * It bounds three things that have to agree: what the registry stores, how many runtime states it
 * can hold, and how many rules the module builds out of a server's answer, however long that
 * answer is.
 */
static const size_t MAX_RULES_PER_MODULE = 24;

} // namespace RuleLimits

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
    RuleJoinOp joinOp = RuleJoinOp::UNSET;
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
    RuleDirection direction = RuleDirection::UNKNOWN;
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
