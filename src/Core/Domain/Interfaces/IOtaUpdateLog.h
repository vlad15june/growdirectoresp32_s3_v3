#pragma once

#include <cstddef>

class IOtaUpdateLog {
public:
    virtual ~IOtaUpdateLog() = default;

    virtual void startSession(const char* url) = 0;
    virtual void write(const char* msg) = 0;
    virtual void writeProgress(size_t cur, size_t total) = 0;
    virtual bool hasPending() = 0;
    virtual bool flushToMqttIfAny() = 0;
};
