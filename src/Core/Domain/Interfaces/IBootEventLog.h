#pragma once

class IBootEventLog {
public:
    virtual ~IBootEventLog() = default;

    virtual void write(const char* msg) = 0;
    virtual bool hasPending() = 0;
    virtual bool flushToMqttIfAny() = 0;
};
