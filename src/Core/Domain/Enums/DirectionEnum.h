#ifndef DIRECTION_ENUM_H
#define DIRECTION_ENUM_H

#include <cstring>

enum DirectionEnum {
    DIRECTION_NONE = 0,
    DIRECTION_UP = 1,
    DIRECTION_DOWN = 2
};

// Utility function to get string representation of direction
inline const char* getDirectionName(DirectionEnum direction) {
    switch (direction) {
        case DIRECTION_NONE:
            return "none";
        case DIRECTION_UP:
            return "up";
        case DIRECTION_DOWN:
            return "down";
        default:
            return "unknown";
    }
}

// Utility function to convert string to direction enum
inline DirectionEnum getDirectionFromString(const char* directionStr) {
    if (!directionStr) return DIRECTION_NONE;
    
    if (strcmp(directionStr, "up") == 0) {
        return DIRECTION_UP;
    } else if (strcmp(directionStr, "down") == 0) {
        return DIRECTION_DOWN;
    }
    
    return DIRECTION_NONE;
}

#endif // DIRECTION_ENUM_H
