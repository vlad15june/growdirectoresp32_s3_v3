#ifndef MOCK_BOOT_EVENT_LOG_H
#define MOCK_BOOT_EVENT_LOG_H

#include "Core/Domain/Interfaces/IBootEventLog.h"
#include <string>
#include <vector>

class MockBootEventLog : public IBootEventLog {
private:
    std::vector<std::string> _messages;
    bool _hasPending = false;
    int _flushCount = 0;

public:
    void write(const char* msg) override {
        if (msg != nullptr) {
            _messages.push_back(msg);
            _hasPending = true;
        }
    }

    bool hasPending() override {
        return _hasPending;
    }

    bool flushToMqttIfAny() override {
        if (_hasPending) {
            _flushCount++;
            _hasPending = false;
            return true;
        }
        return false;
    }

    const std::vector<std::string>& getMessages() const {
        return _messages;
    }

    int getFlushCount() const {
        return _flushCount;
    }

    void reset() {
        _messages.clear();
        _hasPending = false;
        _flushCount = 0;
    }
};

#endif // MOCK_BOOT_EVENT_LOG_H
