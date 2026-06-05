#ifndef RULE_ENUMS_H
#define RULE_ENUMS_H

#include <cstring>

enum class RuleType {
    STEP,
    SENSOR,
    SCHEDULE,
    EC,
    PH,
    LOOP,
    DIMMER_AUTO,
    DIMMER_MANUAL,
    AVERAGE_SENSOR,
    UNKNOWN
};

enum class RuleDirection {
    UNKNOWN,
    UP,
    DOWN
};

// Utility function to get string representation of rule direction
inline const char* getRuleDirectionName(RuleDirection direction) {
    switch (direction) {
        case RuleDirection::UP:
            return "up";
        case RuleDirection::DOWN:
            return "down";
        default:
            return "unknown";
    }
}

// Utility function to convert string to rule direction enum
inline RuleDirection getRuleDirectionFromString(const char* directionStr) {
    if (!directionStr) return RuleDirection::DOWN;
    
    if (strcmp(directionStr, "up") == 0) {
        return RuleDirection::UP;
    } else if (strcmp(directionStr, "down") == 0) {
        return RuleDirection::DOWN;
    }
    
    return RuleDirection::UNKNOWN;
}

enum class RuleMode {
    ON,
    OFF,
    RULE_MODE_WAITING_FOR_USER_CONFIRM
};

enum class RuleState {
    SYNCHRONIZED,
    UNSYNCHRONIZED,
    SYNCHRONIZING,
    DELETING,
    ERROR
};

#endif // RULE_ENUMS_H