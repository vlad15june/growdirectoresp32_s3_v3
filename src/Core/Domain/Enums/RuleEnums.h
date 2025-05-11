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