#ifndef MOCK_MODULE_CONFIG_H
#define MOCK_MODULE_CONFIG_H

#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include <map>
#include <string>

class MockModuleConfig : public IModuleConfig {
private:
    int _moduleType;
    int _moduleSubType;
    int _deviceCount;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    ISettings* _settings;
    INTPTimeAdapter* _timeAdapter;
    IWifiAdapter* _wifiAdapter;
    bool _isInitialized;
    bool _isTypeConfigure;
    std::map<std::string, bool> _modifiedFields;
    
    // LED related members
    uint8_t* _redLeds;
    uint8_t* _greenLeds;
    uint8_t _ledsCount;
    
    // Module mode - updated to use CONFIGURED_DONE as default
    ModuleModeEnum _requestedModuleMode = ModuleModeEnum::CONFIGURED_DONE;

    // Add module UID field
    std::string _moduleUid;

public:
    MockModuleConfig(ILogger* logger = nullptr, 
                    IDebugSystem* debugSystem = nullptr, 
                    ISettings* settings = nullptr,
                    INTPTimeAdapter* timeAdapter = nullptr,
                    IWifiAdapter* wifiAdapter = nullptr)
        : _moduleType(MODULE_TYPE_DEVICE),
          _moduleSubType(MODULE_SUBTYPE_RELAY),
          _deviceCount(4),
          _logger(logger),
          _debugSystem(debugSystem),
          _settings(settings),
          _timeAdapter(timeAdapter),
          _wifiAdapter(wifiAdapter),
          _isInitialized(false),
          _isTypeConfigure(false),
          _redLeds(nullptr),
          _greenLeds(nullptr),
          _ledsCount(4),
          _moduleUid("test_module_uid") { // Initialize with a default module UID
        // Initialize LED arrays with default values
        _redLeds = new uint8_t[_ledsCount];
        _greenLeds = new uint8_t[_ledsCount];
        for (int i = 0; i < _ledsCount; i++) {
            _redLeds[i] = i + 10;    // Example default values
            _greenLeds[i] = i + 20;  // Example default values
        }
    }
    
    ~MockModuleConfig() {
        if (_redLeds != nullptr) {
            delete[] _redLeds;
        }
        if (_greenLeds != nullptr) {
            delete[] _greenLeds;
        }
    }

    void configureModuleType() override {
        _isTypeConfigure = true;
        _modifiedFields["configureModuleType"] = true;
    }

    void init() override {
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
    
    int getDeviceCount() const override {
        return _deviceCount;
    }

    ISettings* getSettings() override {
        return _settings;
    }
    
    uint8_t* getRedLeds() const override {
        return _redLeds;
    }
    
    uint8_t* getGreenLeds() const override {
        return _greenLeds;
    }
    
    uint8_t getLedsCount() const override {
        return _ledsCount;
    }

    ModuleModeEnum getRequestedModuleMode() const override {
        return _requestedModuleMode;
    }
    
    void setRequestedModuleMode(ModuleModeEnum mode) override {
        _requestedModuleMode = mode;
        _modifiedFields["requestedModuleMode"] = true;
    }

    const char* getModuleUid() const override {
        return _moduleUid.c_str();
    }

    void setModuleUid(const char* uid) {
        if (uid != nullptr) {
            _moduleUid = uid;
            _modifiedFields["moduleUid"] = true;
        }
    }

    IWifiAdapter* getWifiAdapter() const {
        return _wifiAdapter;
    }

    void setWifiAdapter(IWifiAdapter* wifiAdapter) {
        _wifiAdapter = wifiAdapter;
        _modifiedFields["wifiAdapter"] = true;
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
    
    void setDeviceCount(int count) {
        _deviceCount = count;
        _modifiedFields["deviceCount"] = true;
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
    
    void setLedsCount(uint8_t count) {
        // Clean up old arrays
        if (_redLeds) delete[] _redLeds;
        if (_greenLeds) delete[] _greenLeds;
        
        // Create new arrays
        _ledsCount = count;
        _redLeds = new uint8_t[count];
        _greenLeds = new uint8_t[count];
        
        // Initialize with default values
        for (int i = 0; i < count; i++) {
            _redLeds[i] = i + 10;    // Example default values
            _greenLeds[i] = i + 20;  // Example default values
        }
        
        _modifiedFields["ledsCount"] = true;
    }
    
    void setRedLeds(uint8_t* leds, uint8_t count) {
        if (_redLeds && count <= _ledsCount) {
            for (int i = 0; i < count; i++) {
                _redLeds[i] = leds[i];
            }
            _modifiedFields["redLeds"] = true;
        }
    }
    
    void setGreenLeds(uint8_t* leds, uint8_t count) {
        if (_greenLeds && count <= _ledsCount) {
            for (int i = 0; i < count; i++) {
                _greenLeds[i] = leds[i];
            }
            _modifiedFields["greenLeds"] = true;
        }
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
        _deviceCount = 4;
        _ledsCount = 4;
        _isInitialized = false;
        _isTypeConfigure = false;
        _requestedModuleMode = ModuleModeEnum::CONFIGURED_DONE; // Reset mode too
        _moduleUid = "test_module_uid";
        
        // Re-initialize LED arrays with default values
        if (_redLeds) {
            for (int i = 0; i < _ledsCount; i++) {
                _redLeds[i] = i + 10;
            }
        }
        
        if (_greenLeds) {
            for (int i = 0; i < _ledsCount; i++) {
                _greenLeds[i] = i + 20;
            }
        }
        
        _modifiedFields.clear();
    }
};

#endif // MOCK_MODULE_CONFIG_H
