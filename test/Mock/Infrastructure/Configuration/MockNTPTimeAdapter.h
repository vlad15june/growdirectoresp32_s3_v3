#ifndef MOCK_NTP_TIME_ADAPTER_H
#define MOCK_NTP_TIME_ADAPTER_H

#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include <map>
#include <string>

class MockNTPTimeAdapter : public INTPTimeAdapter {
private:
    unsigned long _epochTime;
    std::string _formattedTime;
    bool _isSetupCalled;
    bool _isTimeSaved;
    bool _isTimeRestored;
    std::map<std::string, bool> _modifiedFields;
    bool _commandTimeValidReturn = true;
    bool _isCommandTimeValidCalled = false;
    std::string _lastCommandTimeCreatedDate;
    unsigned long _lastCommandTimeMaxAge = 0;

public:
    MockNTPTimeAdapter() 
        : _epochTime(1609459200), // Default to 2021-01-01 00:00:00 UTC
          _formattedTime("2021-01-01 00:00:00"),
          _isSetupCalled(false),
          _isTimeSaved(false),
          _isTimeRestored(false) {}

    void setup() override {
        _isSetupCalled = true;
        _modifiedFields["setup"] = true;
    }

    unsigned long getEpochTime() override {
        return _epochTime;
    }

    const char* getFormattedTime() override {
        return _formattedTime.c_str();
    }

    void printTime() override {
        // Mock implementation, does nothing
    }

    bool isCommandTimeValid(const char* createdDate, unsigned long maxAgeSeconds) override {
        _isCommandTimeValidCalled = true;
        _lastCommandTimeCreatedDate = createdDate ? createdDate : "";
        _lastCommandTimeMaxAge = maxAgeSeconds;
        return _commandTimeValidReturn;
    }

    void saveTimeToStorage() override {
        _isTimeSaved = true;
        _modifiedFields["saveTime"] = true;
    }

    void restoreTimeFromStorage() override {
        _isTimeRestored = true;
        _modifiedFields["restoreTime"] = true;
    }

    void forceSyncTime() override {
        _modifiedFields["forceSyncTime"] = true;
    }

    bool isTimeSynchronized() override {
        // Default to synchronized for testing
        return true;
    }

    // Additional methods for testing
    void setEpochTime(unsigned long time) {
        _epochTime = time;
        _modifiedFields["epochTime"] = true;
    }

    void setFormattedTime(std::string time) {
        _formattedTime = time;
        _modifiedFields["formattedTime"] = true;
    }

    void setCommandTimeValid(bool valid) {
        _commandTimeValidReturn = valid;
        _modifiedFields["commandTimeValid"] = true;
    }

    bool wasCommandTimeValidCalled() const {
        return _isCommandTimeValidCalled;
    }

    const std::string& getLastCommandTimeCreatedDate() const {
        return _lastCommandTimeCreatedDate;
    }

    unsigned long getLastCommandTimeMaxAge() const {
        return _lastCommandTimeMaxAge;
    }

    bool wasSetupCalled() const {
        return _isSetupCalled;
    }

    bool wasTimeSaved() const {
        return _isTimeSaved;
    }

    bool wasTimeRestored() const {
        return _isTimeRestored;
    }

    bool wasModified(const char* field) const {
        auto it = _modifiedFields.find(field);
        return (it != _modifiedFields.end()) ? it->second : false;
    }

    void reset() {
        _epochTime = 1609459200;
        _formattedTime = "2021-01-01 00:00:00";
        _isSetupCalled = false;
        _isTimeSaved = false;
        _isTimeRestored = false;
        _modifiedFields.clear();
        _commandTimeValidReturn = true;
        _isCommandTimeValidCalled = false;
        _lastCommandTimeCreatedDate.clear();
        _lastCommandTimeMaxAge = 0;
    }
};

#endif // MOCK_NTP_TIME_ADAPTER_H
