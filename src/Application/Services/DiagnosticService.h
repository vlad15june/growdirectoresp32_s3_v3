#pragma once

#include "Application/Interfaces/IDiagnosticService.h"
#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"

class DiagnosticService : public IDiagnosticService {
private:
    IRestAPIService* _restApiService;
    IModuleConfig* _moduleConfig;
    ILogger& _logger;
    IDebugSystem& _debugSystem;
    IJsonConverter* _jsonConverter;
    IWifiAdapter* _wifiAdapter;
    INTPTimeAdapter* _timeAdapter;
    IArduinoAdapter* _arduinoAdapter;
    
    // Diagnostic counters
    int _networkFails;
    int _apiSendFails;
    int _mqttFails;
    float _apiSendFailRatio;
    int _countOffline;
    int _maxCountOffline;
    bool _mqttConnected;
    
    // Array to keep track of failures for calculating failsInHour
    static const int MAX_FAILS_HISTORY = 60; // Store last 60 minutes
    unsigned long _failsHistory[MAX_FAILS_HISTORY];
    int _failsHistoryIndex;
    
    // Calculate failures in the last hour
    unsigned long calculateFailsInHour() {
        unsigned long totalFails = 0;
        unsigned long currentTime = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        const unsigned long ONE_HOUR_MS = 60 * 60 * 1000;
        
        for (int i = 0; i < MAX_FAILS_HISTORY; i++) {
            if (_failsHistory[i] > 0 && currentTime - _failsHistory[i] < ONE_HOUR_MS) {
                totalFails++;
            }
        }
        
        return totalFails;
    }
    
    void log(const char* format, ...) {
        if (!_debugSystem.isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG)) {
            return;
        }
        
        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        _logger.println(buffer);
    }

public:
    DiagnosticService(
        IRestAPIService* restApiService, 
        IModuleConfig* moduleConfig,
        ILogger& logger,
        IDebugSystem& debugSystem,
        IJsonConverter* jsonConverter = nullptr,
        IWifiAdapter* wifiAdapter = nullptr,
        INTPTimeAdapter* timeAdapter = nullptr,
        IArduinoAdapter* arduinoAdapter = nullptr
    ) : 
        _restApiService(restApiService),
        _moduleConfig(moduleConfig),
        _logger(logger),
        _debugSystem(debugSystem),
        _jsonConverter(jsonConverter),
        _wifiAdapter(wifiAdapter),
        _timeAdapter(timeAdapter),
        _arduinoAdapter(arduinoAdapter),
        _networkFails(0),
        _apiSendFails(0),
        _mqttFails(0),
        _apiSendFailRatio(0),
        _countOffline(0),
        _maxCountOffline(5),
        _mqttConnected(false),
        _failsHistoryIndex(0)
    {
        // Initialize fails history
        for (int i = 0; i < MAX_FAILS_HISTORY; i++) {
            _failsHistory[i] = 0;
        }
    }
    
    // Diagnostic counter setters
    void setNetworkFails(int fails) { _networkFails = fails; }
    void setApiSendFails(int fails) { _apiSendFails = fails; }
    void setMqttFails(int fails) { _mqttFails = fails; }
    void setApiSendFailRatio(float ratio) { _apiSendFailRatio = ratio; }
    void setCountOffline(int count) { _countOffline = count; }
    void setMaxCountOffline(int max) { _maxCountOffline = max; }
    void setMqttConnected(bool connected) { _mqttConnected = connected; }
    
    // Record a new failure for hourly calculation
    void recordFailure() {
        if (_arduinoAdapter) {
            _failsHistory[_failsHistoryIndex] = _arduinoAdapter->millis();
            _failsHistoryIndex = (_failsHistoryIndex + 1) % MAX_FAILS_HISTORY;
        }
    }
    
    DiagnosticDto perform() override {
        DiagnosticDto diagnosticData;
        
        // Fill the DTO with diagnostic information
        if (_wifiAdapter != nullptr) {
            diagnosticData.rssi = _wifiAdapter->getRSSI();
            diagnosticData.uid = _wifiAdapter->getMACAddress();
        } else {
            diagnosticData.rssi = 0;
            diagnosticData.uid = "unknown";
        }
        
        if (_moduleConfig != nullptr) {
            ISettings* settings = _moduleConfig->getSettings();
            if (settings != nullptr) {
                char buffer[BUFSIZE];                
                settings->getUser(buffer, BUFSIZE);
                diagnosticData.email = buffer;                
                settings->getAccountUid(buffer, BUFSIZE);
                diagnosticData.accountUid = buffer;
            }
        }
        
        // Use time adapter for timestamp if available
        if (_timeAdapter != nullptr) {
            diagnosticData.timestamp = _timeAdapter->getFormattedTime();
        } else {
            diagnosticData.timestamp = "Unknown time";
        }
        
        // Use Arduino adapter for system metrics
        if (_arduinoAdapter) {
            diagnosticData.uptimeMs = _arduinoAdapter->millis();
            diagnosticData.freeHeap = _arduinoAdapter->getFreeHeap();
            diagnosticData.minFreeHeap = _arduinoAdapter->getMinFreeHeap();
            diagnosticData.heapSize = _arduinoAdapter->getHeapSize();
            diagnosticData.maxAllocHeap = _arduinoAdapter->getMaxAllocHeap();
        } else {
            diagnosticData.uptimeMs = 0;
            diagnosticData.freeHeap = 0;
            diagnosticData.minFreeHeap = 0;
            diagnosticData.heapSize = 0;
            diagnosticData.maxAllocHeap = 0;
        }
        
        diagnosticData.networkFails = _networkFails;
        diagnosticData.apiSendFails = _apiSendFails;
        diagnosticData.apiStatus = (_countOffline > _maxCountOffline) ? "disconnected" : "connected";
        diagnosticData.mqttStatus = _mqttConnected ? "connected" : "disconnected";
        diagnosticData.mqttFails = _mqttFails;
        diagnosticData.apiSendFailRatio = _apiSendFailRatio;
        
        // Use firmware version from module config instead of global variable
        if (_moduleConfig != nullptr) {
            diagnosticData.firmware = _moduleConfig->getFirmwareVersion();
        } else {
            diagnosticData.firmware = "unknown";
        }
        
        diagnosticData.failsInHour = calculateFailsInHour();
        
        log("DIAG: Performed diagnostics - Free heap: %d, API status: %s", 
            diagnosticData.freeHeap, diagnosticData.apiStatus.c_str());
        
        return diagnosticData;
    }
    
    bool performAndSend() override {
        if (_moduleConfig == nullptr) {
            log("DIAG: Module configuration not available");
            return false;
        }
        
        DiagnosticDto diagnosticData = perform();
        
        if (_restApiService != nullptr) {
            log("DIAG: Sending diagnostic data to server");
            return _restApiService->sendDiagnosticData(diagnosticData);
        }
        
        log("DIAG: REST API service not available");
        return false;
    }
};
