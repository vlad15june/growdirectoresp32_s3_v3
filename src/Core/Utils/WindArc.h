#pragma once

namespace Utils {

inline bool isInsideArc(double value, double low, double high) {
    if (low <= high) {
        return value >= low && value <= high;
    }
    return value >= low || value <= high;
}

} // namespace Utils
