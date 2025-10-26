#ifndef MOCK_LOGGER_H
#define MOCK_LOGGER_H

#include "Core/Domain/Interfaces/ILogger.h"
#include <string>
#include <fstream>  // Added for file operations

class MockLogger : public ILogger {
public:
    bool printCalled = false;
    std::string lastMessage;
    std::ofstream logFile;
    std::string logFilePath = "mock_logger.log";
    std::string moduleUid;
    
    MockLogger() {
        logFile.open(logFilePath, std::ios::out | std::ios::app);
    }
    
    ~MockLogger() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }
    
    void setLogFile(const std::string& filePath) {
        if (logFile.is_open()) {
            logFile.close();
        }
        logFilePath = filePath;
        logFile.open(logFilePath, std::ios::out | std::ios::app);
    }
    
    void reset() {
        printCalled = false;
        lastMessage.clear();
    }
    
    // Implementation of setModuleUid
    void setModuleUid(const char* moduleUid) override {
        this->moduleUid = moduleUid ? moduleUid : "";
        if (logFile.is_open()) {
            logFile << "Module UID set to: " << this->moduleUid << std::endl;
            logFile.flush();
        }
    }
    
    // Simple implementations that just set flags
    void print(const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
        if (logFile.is_open() && message) {
            logFile << message;
            logFile.flush();
        }
    }
    
    void println(const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
        if (logFile.is_open()) {
            logFile << (message ? message : "") << std::endl;
            logFile.flush();
        }
    }
    
    void printf(const char* format, ...) override {
        printCalled = true;
        // Basic implementation - just log the format string
        if (logFile.is_open() && format) {
            logFile << "printf: " << format << std::endl;
            logFile.flush();
        }
    }
    
    void print(const char* topic, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
        if (logFile.is_open()) {
            logFile << "[" << (topic ? topic : "NULL") << "] " 
                   << (message ? message : "") << std::endl;
            logFile.flush();
        }
    }
    
    void println(const char* topic, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
        if (logFile.is_open()) {
            logFile << "[" << (topic ? topic : "NULL") << "] " 
                   << (message ? message : "") << std::endl;
            logFile.flush();
        }
    }
    
    void printf(const char* topic, const char* format, ...) override {
        printCalled = true;
        if (logFile.is_open() && topic && format) {
            logFile << "[" << topic << "] printf: " << format << std::endl;
            logFile.flush();
        }
    }
    
    // Log to specific topic with custom mode
    void print(const char* topic, MqttLoggerMode mode, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
        if (logFile.is_open()) {
            logFile << "[" << (topic ? topic : "NULL") << "][Mode:" << static_cast<int>(mode) << "] " 
                   << (message ? message : "") << std::endl;
            logFile.flush();
        }
    }
    
    void println(const char* topic, MqttLoggerMode mode, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
        if (logFile.is_open()) {
            logFile << "[" << (topic ? topic : "NULL") << "][Mode:" << static_cast<int>(mode) << "] " 
                   << (message ? message : "") << std::endl;
            logFile.flush();
        }
    }
    
    void printf(const char* topic, MqttLoggerMode mode, const char* format, ...) override {
        printCalled = true;
        if (logFile.is_open() && topic && format) {
            logFile << "[" << topic << "][Mode:" << static_cast<int>(mode) << "] printf: " << format << std::endl;
            logFile.flush();
        }
    }
    
    void log(LogLevel level, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
        if (logFile.is_open()) {
            logFile << "LogLevel[" << static_cast<int>(level) << "]: " 
                   << (message ? message : "") << std::endl;
            logFile.flush();
        }
    }
    
    void log(LogLevel level, const char* topic, const char* message) override {
        printCalled = true;
        lastMessage = message ? message : "";
        if (logFile.is_open()) {
            logFile << "LogLevel[" << static_cast<int>(level) << "] [" 
                   << (topic ? topic : "NULL") << "]: " 
                   << (message ? message : "") << std::endl;
            logFile.flush();
        }
    }
    
    void logJson(const char* feature, const char* action, const char* details) override {
        printCalled = true;
        if (logFile.is_open()) {
            logFile << "JSON: { \"feature\": \"" << (feature ? feature : "NULL") 
                   << "\", \"action\": \"" << (action ? action : "NULL") 
                   << "\", \"details\": \"" << (details ? details : "NULL") << "\" }" << std::endl;
            logFile.flush();
        }
    }
    
    void logJson(const char* topic, const char* feature, const char* action, const char* details) override {
        printCalled = true;
        if (logFile.is_open()) {
            logFile << "[" << (topic ? topic : "NULL") << "] JSON: { \"feature\": \"" 
                   << (feature ? feature : "NULL") << "\", \"action\": \"" 
                   << (action ? action : "NULL") << "\", \"details\": \"" 
                   << (details ? details : "NULL") << "\" }" << std::endl;
            logFile.flush();
        }
    }
};

#endif // MOCK_LOGGER_H
