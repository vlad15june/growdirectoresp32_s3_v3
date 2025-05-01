#ifndef MOCK_LOGGER_H
#define MOCK_LOGGER_H

#include "Core/Domain/Interfaces/ILogger.h"
#include <string>

class MockLogger : public ILogger {
public:
    bool printCalled = false;
    std::string lastMessage;
    
    void reset() {
        printCalled = false;
        lastMessage.clear();
    }
    
    // Simple implementations that just set flags
    void print(const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
    }
    
    void println(const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
    }
    
    void printf(const char* format, ...) override {
        printCalled = true;
    }
    
    void print(const char* topic, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
    }
    
    void println(const char* topic, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
    }
    
    void printf(const char* topic, const char* format, ...) override {
        printCalled = true;
    }
    
    void log(LogLevel level, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
    }
    
    void log(LogLevel level, const char* topic, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
    }
    
    void logJson(const char* feature, const char* action, const char* details) override {
        printCalled = true;
    }
    
    void logJson(const char* topic, const char* feature, const char* action, const char* details) override {
        printCalled = true;
    }
};

#endif // MOCK_LOGGER_H
