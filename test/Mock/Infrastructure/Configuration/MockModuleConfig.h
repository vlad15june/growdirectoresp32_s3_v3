#ifndef MOCK_MODULE_CONFIG_H
#define MOCK_MODULE_CONFIG_H

#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include <map>
#include <string>

class MockModuleConfig : public IModuleConfig {
private:
    int _moduleType;
    int _moduleSubType;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    ISettings* _settings;
    INTPTimeAdapter* _timeAdapter;
    bool _isInitialized;
    bool _isTypeConfigure;
    std::map<std::string, bool> _modifiedFields;

public:
    MockModuleConfig(ILogger* logger = nullptr, 
                    IDebugSystem* debugSystem = nullptr, 
                    ISettings* settings = nullptr,
                    INTPTimeAdapter* timeAdapter = nullptr)
        : _moduleType(MODULE_TYPE_DEVICE),
          _moduleSubType(MODULE_SUBTYPE_RELAY),
          _logger(logger),
          _debugSystem(debugSystem),
          _settings(settings),
          _timeAdapter(timeAdapter),
          _isInitialized(false),
          _isTypeConfigure(false) {}

    void configureModuleType() override {
        _isTypeConfigure = true;
        _modifiedFields["configureModuleType"] = true;
    }

    void Init() override {
        _isInitialized = true;
        configureModuleType();
        
        if (_settings != nullptr) {
            _settings->begin();
        }
        
        if (_timeAdapter != nullptr) {
            _timeAdapter->setup();
            _timeAdapter->restoreTimeFromStorage();
        }
        
        _modifiedFields["init"] = true;
    }

    int getModuleType() const override {
        return _moduleType;
    }

    int getModuleSubType() const override {
        return _moduleSubType;
    }

    ISettings* getSettings() override {
        return _settings;
    }

    // Additional methods for testing
    void setModuleType(int type) {
        _moduleType = type;
        _modifiedFields["moduleType"] = true;
    }

    void setModuleSubType(int subType) {
        _moduleSubType = subType;
        _modifiedFields["moduleSubType"] = true;
    }

    void setLogger(ILogger* logger) {
        _logger = logger;
        _modifiedFields["logger"] = true;
    }

    void setDebugSystem(IDebugSystem* debugSystem) {
        _debugSystem = debugSystem;
        _modifiedFields["debugSystem"] = true;
    }

    void setSettings(ISettings* settings) {
        _settings = settings;
        _modifiedFields["settings"] = true;
    }

    void setTimeAdapter(INTPTimeAdapter* timeAdapter) {
        _timeAdapter = timeAdapter;
        _modifiedFields["timeAdapter"] = true;
    }

    ILogger* getLogger() const {
        return _logger;
    }

    IDebugSystem* getDebugSystem() const {
        return _debugSystem;
    }

    INTPTimeAdapter* getTimeAdapter() const {
        return _timeAdapter;
    }

    bool isInitialized() const {
        return _isInitialized;
    }

    bool isTypeConfigured() const {
        return _isTypeConfigure;
    }

    bool wasModified(const char* field) const {
        auto it = _modifiedFields.find(field);
        return (it != _modifiedFields.end()) ? it->second : false;
    }

    void reset() {
        _moduleType = MODULE_TYPE_DEVICE;
        _moduleSubType = MODULE_SUBTYPE_RELAY;
        _isInitialized = false;
        _isTypeConfigure = false;
        _modifiedFields.clear();
    }
};

#endif // MOCK_MODULE_CONFIG_H
