#pragma once

#include "Application/Interfaces/IDiagnosticService.h"
#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"

// Replace defines with enum
enum DiagnosticsNetworkState {
    DIAGNOSTICS_NETWORK_STATE_OK = 0,
    DIAGNOSTICS_NETWORK_STATE_ERROR = 1
};

class DiagnosticService : public IDiagnosticService
{
private:
    IRestAPIService *_restApiService;
    IHybridAPIService *_hybridApiService; // Added hybrid API service
    IModuleConfig *_moduleConfig;
    ILogger &_logger;
    IDebugSystem &_debugSystem;
    IJsonConverter *_jsonConverter;
    IWifiAdapter *_wifiAdapter;
    INTPTimeAdapter *_timeAdapter;
    IArduinoAdapter *_arduinoAdapter;
    IMqttManager *_mqttManager;

    static const int _maxCountOffline = 5;
    bool _mqttConnected;
    int _networkFails = 0;  // Track network failures
    DiagnosticsNetworkState _lastNetworkState = DIAGNOSTICS_NETWORK_STATE_OK;  // Track network state using enum

    static const int MAX_FAILS_HISTORY = 60; // Store last 60 minutes
    unsigned long _failsHistory[MAX_FAILS_HISTORY];
    int _failsHistoryIndex;

    unsigned long calculateFailsInHour()
    {
        unsigned long totalFails = 0;
        unsigned long currentTime = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        const unsigned long ONE_HOUR_MS = 60 * 60 * 1000;

        for (int i = 0; i < MAX_FAILS_HISTORY; i++)
        {
            if (_failsHistory[i] > 0 && currentTime - _failsHistory[i] < ONE_HOUR_MS)
            {
                totalFails++;
            }
        }

        return totalFails;
    }

    void log(const char *format, ...)
    {
        if (!_debugSystem.isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG))
        {
            return;
        }

        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        _logger.println(DebugFeatures::DIAGNOSTIC_DEBUG, buffer);
    }

    // Write board state to a file when network state changes
    void writeBoardStateToFile()
    {
        log("DIAG: Network state changed to %s, writing board state to file",
            _lastNetworkState == DIAGNOSTICS_NETWORK_STATE_OK ? "OK" : "ERROR");

        // TODO: Implement file writing logic or use a file adapter
        // This would depend on the specific implementation for your system

        log("DIAG: Board state recorded");
    }

public:
    DiagnosticService(
        IRestAPIService *restApiService,
        IModuleConfig *moduleConfig,
        ILogger &logger,
        IDebugSystem &debugSystem,
        IJsonConverter *jsonConverter = nullptr,
        IWifiAdapter *wifiAdapter = nullptr,
        INTPTimeAdapter *timeAdapter = nullptr,
        IArduinoAdapter *arduinoAdapter = nullptr,
        IMqttManager *mqttManager = nullptr,
        IHybridAPIService *hybridApiService = nullptr) // Added hybrid API service parameter
        : _restApiService(restApiService),
          _hybridApiService(hybridApiService),
          _moduleConfig(moduleConfig),
          _logger(logger),
          _debugSystem(debugSystem),
          _jsonConverter(jsonConverter),
          _wifiAdapter(wifiAdapter),
          _timeAdapter(timeAdapter),
          _arduinoAdapter(arduinoAdapter),
          _mqttManager(mqttManager),
          _mqttConnected(false),
          _failsHistoryIndex(0)
    {
        for (int i = 0; i < MAX_FAILS_HISTORY; i++)
        {
            _failsHistory[i] = 0;
        }

        // Initialize MQTT connection status if manager is available
        if (_mqttManager != nullptr)
        {
            _mqttConnected = _mqttManager->isConnected();
        }
    }

    void setMqttConnected(bool connected) { _mqttConnected = connected; }

    void recordFailure()
    {
        if (_arduinoAdapter)
        {
            _failsHistory[_failsHistoryIndex] = _arduinoAdapter->millis();
            _failsHistoryIndex = (_failsHistoryIndex + 1) % MAX_FAILS_HISTORY;
        }
    }

    DiagnosticDto perform() override
    {
        DiagnosticDto diagnosticData;

        // Check network state - WiFi and MQTT connection
        bool wifiConnected = false;
        if (_wifiAdapter != nullptr)
        {
            wifiConnected = _wifiAdapter->isConnected();
        }

        // Update MQTT connection status if manager is available
        if (_mqttManager != nullptr)
        {
            _mqttConnected = _mqttManager->isConnected();
        }

        // Check and update network state
        if (!_mqttConnected || !wifiConnected)
        {
            if (_lastNetworkState == DIAGNOSTICS_NETWORK_STATE_OK)
            {
                _lastNetworkState = DIAGNOSTICS_NETWORK_STATE_ERROR;
                writeBoardStateToFile(); // when switching ok to not ok or vice versa
                _networkFails++;

                log("DIAG: Network state changed to ERROR. Total network fails: %d", _networkFails);
            }
        }
        else
        {
            if (_lastNetworkState == DIAGNOSTICS_NETWORK_STATE_ERROR)
            {
                _lastNetworkState = DIAGNOSTICS_NETWORK_STATE_OK;
                writeBoardStateToFile(); // when switching ok to not ok or vice versa

                // When network is restored, force time synchronization
                if (_timeAdapter != nullptr)
                {
                    log("DIAG: Network restored, forcing time synchronization");
                    _timeAdapter->forceSyncTime();
                }

                log("DIAG: Network state changed to OK");
            }
        }

        if (_wifiAdapter != nullptr)
        {
            diagnosticData.rssi = _wifiAdapter->getRSSI();
            diagnosticData.uid = _moduleConfig->getModuleUid();
        }
        else
        {
            diagnosticData.rssi = 0;
            diagnosticData.uid = "unknown";
        }

        if (_moduleConfig != nullptr)
        {
            ISettings *settings = _moduleConfig->getSettings();
            if (settings != nullptr)
            {
                char buffer[BUFSIZE];
                settings->getUser(buffer, BUFSIZE);
                diagnosticData.email = buffer;
                settings->getAccountUid(buffer, BUFSIZE);
                diagnosticData.accountUid = buffer;
            }
        }

        if (_timeAdapter != nullptr)
        {
            diagnosticData.timestamp = _timeAdapter->getFormattedTime();
        }
        else
        {
            diagnosticData.timestamp = "Unknown time";
        }

        if (_arduinoAdapter)
        {
            diagnosticData.uptimeMs = _arduinoAdapter->millis();
            diagnosticData.freeHeap = _arduinoAdapter->getFreeHeap();
            diagnosticData.minFreeHeap = _arduinoAdapter->getMinFreeHeap();
            diagnosticData.heapSize = _arduinoAdapter->getHeapSize();
            diagnosticData.maxAllocHeap = _arduinoAdapter->getMaxAllocHeap();
        }
        else
        {
            diagnosticData.uptimeMs = 0;
            diagnosticData.freeHeap = 0;
            diagnosticData.minFreeHeap = 0;
            diagnosticData.heapSize = 0;
            diagnosticData.maxAllocHeap = 0;
        }

        diagnosticData.networkFails = _networkFails;
        diagnosticData.apiSendFails = _restApiService ? _restApiService->getApiSendFails() : 0;

        // Use REST API for API status determination
        const int countOffline = _restApiService ? _restApiService->getCountOffline() : 0;
        diagnosticData.apiStatus = (countOffline > _maxCountOffline) ? "disconnected" : "connected";

        // Use the actual MQTT connection status directly from the manager
        diagnosticData.mqttStatus = (_mqttManager && _mqttManager->isConnected()) ? "connected" : "disconnected";

        // Get MQTT failures from the manager instead of using local variable
        diagnosticData.mqttFails = _mqttManager ? _mqttManager->getMqttFails() : 0;

        diagnosticData.apiSendFailRatio = _restApiService ? _restApiService->getApiSendFailRatio() : 0.0f;

        if (_moduleConfig != nullptr)
        {
            diagnosticData.firmware = _moduleConfig->getFirmwareVersion();
        }
        else
        {
            diagnosticData.firmware = "unknown";
        }

        diagnosticData.failsInHour = calculateFailsInHour();

        log("DIAG: Performed diagnostics - Free heap: %d, API status: %s",
            diagnosticData.freeHeap, diagnosticData.apiStatus.c_str());

        return diagnosticData;
    }

    bool performAndSend() override
    {
        if (_moduleConfig == nullptr)
        {
            log("DIAG: Module configuration not available");
            return false;
        }

        DiagnosticDto diagnosticData = perform();

        // First try to send data using hybrid API if available
        if (_hybridApiService != nullptr)
        {
            log("DIAG: Sending diagnostic data via Hybrid API");
            return _hybridApiService->sendDiagnosticData(diagnosticData);
        }
        // Fall back to REST API if hybrid is not available
        else if (_restApiService != nullptr)
        {
            log("DIAG: Sending diagnostic data via REST API");
            return _restApiService->sendDiagnosticData(diagnosticData);
        }

        log("DIAG: No API service available");
        return false;
    }
};
