#pragma once

#include <cstddef>
#include <cstdint>

struct OtaProgressThrottle {
    int lastPct = -1;
    unsigned long lastMs = 0;

    void reset() {
        lastPct = -1;
        lastMs = 0;
    }

    bool shouldEmit(size_t cur, size_t total, unsigned long now,
                    int pctStep, unsigned long msStep, int& outPct) {
        int pct = total > 0 ? (int)((uint64_t)cur * 100 / total) : 0;
        outPct = pct;
        if (pct >= lastPct + pctStep && now - lastMs >= msStep) {
            lastPct = pct;
            lastMs = now;
            return true;
        }
        return false;
    }
};
