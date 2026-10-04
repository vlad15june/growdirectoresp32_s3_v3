#pragma once

#include "Core/Domain/Interfaces/IOtaUpdateLog.h"

#include <string>
#include <vector>

/**
 * @brief Records every line the code under test writes to the OTA log.
 */
class MockOtaUpdateLog : public IOtaUpdateLog {
public:
    std::vector<std::string> lines;
    std::vector<std::string> sessions;
    int progressCalls = 0;

    void startSession(const char* url) override
    {
        sessions.push_back((url != nullptr) ? url : "");
    }

    void write(const char* msg) override
    {
        lines.push_back((msg != nullptr) ? msg : "");
    }

    void writeProgress(size_t, size_t) override { progressCalls++; }

    bool hasPending() override { return false; }

    bool flushToMqttIfAny() override { return false; }

    /** @brief Whether any recorded line contains the given fragment. */
    bool hasLineContaining(const char* fragment) const
    {
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i].find(fragment) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }
};
