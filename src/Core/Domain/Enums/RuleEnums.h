#ifndef RULE_ENUMS_H
#define RULE_ENUMS_H

#include <cstring>

enum class RuleType {
    STEP,
    SENSOR,
    SCHEDULE,
    EC,
    PH,
    LOOP
};

enum class RuleDirection {
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
    
    return RuleDirection::DOWN;
}

enum class RuleMode {
    ON,
    OFF,
    AUTO
};

enum class RuleState {
    SYNCHRONIZED,
    UNSYNCHRONIZED,
    PENDING,
    DELETING
};

#endif // RULE_ENUMS_H