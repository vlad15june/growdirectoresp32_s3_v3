#ifndef RULE_ENUMS_H
#define RULE_ENUMS_H

enum class RuleType {
    STEP,
    SENSOR,
    SCHEDULE
};

enum class RuleDirection {
    UP,
    DOWN
};

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