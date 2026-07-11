#pragma once

#include "Application/Interfaces/IDiagnosticService.h"
#include "Core/Domain/Interfaces/INetworkStatusProvider.h"
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
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include <cstdarg>
#include <cstdio>

// Replace defines with enum
enum DiagnosticsNetworkState {
    DIAGNOSTICS_NETWORK_STATE_OK = 0,
    DIAGNOSTICS_NETWORK_STATE_ERROR = 1
};

class DiagnosticService : public IDiagnosticService, public INetworkStatusProvider
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
    static const unsigned long WIFI_OFFLINE_DEBOUNCE_MS = 30 * 1000;
    unsigned long _wifiDownSince = 0;
    bool _mqttConnected;
    int _networkFails = 0;  // Track network failures
    DiagnosticsNetworkState _lastNetworkState = DIAGNOSTICS_NETWORK_STATE_OK;  // Track network state using enum

    static const int MAX_FAILS_HISTORY = 60; // Store last 60 minutes
    unsigned long _failsHistory[MAX_FAILS_HISTORY];
    int _failsHistoryIndex;

    // MQTT disconnection tracking
    unsigned long _mqttLastDisconnectedTime;
    bool _mqttWasConnected;
    static const unsigned long MQTT_DISCONNECT_THRESHOLD_MS = 10 * 60 * 1000; // 10 minutes in milliseconds

    // Full offline state tracking (WiFi down OR (MQTT down AND API down))
    unsigned long _allOfflineStartTime;
    bool _wasFullyOffline;
    static const unsigned long OFFLINE_RESET_THRESHOLD_SENSOR_MS = 15 * 60 * 1000; // 15 minutes for sensor directors
    static const unsigned long OFFLINE_RESET_THRESHOLD_DEVICE_MS = 4 * 60 * 60 * 1000; // 4 hours for other modules

    bool _wifiWasConnected = true;
    unsigned long _wifiLastReconnectAttempt = 0;
    static const unsigned long WIFI_RECONNECT_RETRY_INTERVAL_MS = 10 * 60 * 1000;

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

    void logSetup(const char *format, ...)
    {
        if (!_debugSystem.isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            return;
        }

        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        IDebugFeature *feature = _debugSystem.getFeature(DebugFeatures::SETUP_DEBUG);
        MqttLoggerMode channel = feature ? feature->getLogChannel() : MqttLoggerMode::MqttAndSerial;
        _logger.println(DebugFeatures::SETUP_DEBUG, buffer, channel);
    }

    void logOffline(const char *format, ...)
    {
        if (!_debugSystem.isFeatureEnabled(DebugFeatures::OFFLINE_INDICATION_DEBUG))
        {
            return;
        }

        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        IDebugFeature *feature = _debugSystem.getFeature(DebugFeatures::OFFLINE_INDICATION_DEBUG);
        MqttLoggerMode channel = feature ? feature->getLogChannel() : MqttLoggerMode::MqttAndSerial;
        _logger.println(DebugFeatures::OFFLINE_INDICATION_DEBUG, buffer, channel);
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

    // Check MQTT connection and trigger restart if disconnected for too long
    void checkMqttConnectionAndTriggerRestart()
    {
        if (_mqttManager == nullptr || _arduinoAdapter == nullptr)
        {
            return;
        }

        bool currentlyConnected = _mqttManager->isConnected();
        unsigned long currentTime = _arduinoAdapter->millis();

        if (!currentlyConnected)
        {
            // MQTT is disconnected
            if (_mqttWasConnected || _mqttLastDisconnectedTime == 0)
            {
                // Just disconnected, or booted while already disconnected - start tracking
                _mqttLastDisconnectedTime = currentTime;
                _mqttWasConnected = false;
                log("DIAG: MQTT disconnected, starting timer");
            }
            else
            {
                // Already disconnected, check if threshold exceeded
                unsigned long disconnectDuration = currentTime - _mqttLastDisconnectedTime;
                
                if (disconnectDuration >= MQTT_DISCONNECT_THRESHOLD_MS)
                {
                    log("DIAG: MQTT disconnected for %lu ms (>10 min), triggering module restart", disconnectDuration);
                    
                    // Get settings and moduleConfig to trigger restart
                    ISettings *settings = _moduleConfig ? _moduleConfig->getSettings() : nullptr;
                    
                    if (settings != nullptr && _moduleConfig != nullptr)
                    {
                        // Set no scan sensors flag
                        settings->setNoScanSensors(1);
                        
                        // Set boot type to technical
                        settings->setBootType(BOOT_TYPE_TECHNICAL);
                        
                        // Request module restart
                        _moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);
                        
                        log("DIAG: Module restart triggered due to MQTT disconnection");
                    }
                    else
                    {
                        log("DIAG: ERROR - Cannot trigger restart, settings or moduleConfig not available");
                    }
                    
                    // Reset timer to avoid repeated restart attempts
                    _mqttLastDisconnectedTime = currentTime;
                }
            }
        }
        else
        {
            // MQTT is connected
            if (!_mqttWasConnected && _mqttLastDisconnectedTime > 0)
            {
                // Just reconnected
                unsigned long disconnectDuration = currentTime - _mqttLastDisconnectedTime;
                log("DIAG: MQTT reconnected after %lu ms", disconnectDuration);
            }
            
            _mqttWasConnected = true;
            _mqttLastDisconnectedTime = 0; // Reset timer when connected
        }
    }

    // Check if module is fully offline and trigger reboot after threshold
    void checkOfflineStateAndTriggerReboot()
    {
        if (_arduinoAdapter == nullptr || _moduleConfig == nullptr)
        {
            return;
        }

        // Check connectivity status
        bool wifiConnected = (_wifiAdapter != nullptr) ? _wifiAdapter->isConnected() : false;
        bool mqttConnected = (_mqttManager != nullptr) ? _mqttManager->isConnected() : false;
        
        // Check API status
        const int countOffline = _restApiService ? _restApiService->getCountOffline() : 0;
        bool apiDisconnected = (countOffline > _maxCountOffline);

        // Module is fully offline if:
        // - WiFi is down OR
        // - (MQTT is down AND API is down)
        bool isFullyOffline = !wifiConnected || (!mqttConnected && apiDisconnected);

        unsigned long currentTime = _arduinoAdapter->millis();

        if (isFullyOffline)
        {
            // Module is fully offline
            if (!_wasFullyOffline)
            {
                // Just went offline, start tracking
                _allOfflineStartTime = currentTime;
                _wasFullyOffline = true;
                log("DIAG: Module went fully offline, starting timer (WiFi: %s, MQTT: %s, API: %s)",
                    wifiConnected ? "connected" : "disconnected",
                    mqttConnected ? "connected" : "disconnected",
                    apiDisconnected ? "disconnected" : "connected");
            }
            else if (_allOfflineStartTime > 0)
            {
                // Already offline, check if threshold exceeded
                unsigned long offlineDuration = currentTime - _allOfflineStartTime;
                
                // Determine module type and select appropriate threshold
                int moduleType = _moduleConfig->getModuleType();
                unsigned long threshold;
                const char* moduleTypeName;
                
                if (moduleType == MODULE_TYPE_SENSOR) // sensor_director
                {
                    threshold = OFFLINE_RESET_THRESHOLD_SENSOR_MS; // 15 minutes
                    moduleTypeName = "sensor_director";
                }
                else // grow_director, hydro, analog_input, etc.
                {
                    threshold = OFFLINE_RESET_THRESHOLD_DEVICE_MS; // 4 hours
                    moduleTypeName = "device";
                }
                
                if (offlineDuration >= threshold)
                {
                    log("DIAG: Module offline for %lu ms (threshold: %lu ms for %s), triggering automatic reboot",
                        offlineDuration, threshold, moduleTypeName);
                    
                    // Get settings to configure reboot
                    ISettings *settings = _moduleConfig->getSettings();
                    
                    if (settings != nullptr)
                    {
                        // Set noScanSensors flag to skip sensor scan after reboot
                        settings->setNoScanSensors(1);
                        log("DIAG: Set noScanSensors flag for faster recovery");
                        
                        // Set boot type to technical
                        settings->setBootType(BOOT_TYPE_TECHNICAL);
                        log("DIAG: Set boot type to BOOT_TYPE_TECHNICAL");
                        
                        // Request module restart
                        _moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);
                        log("DIAG: Automatic module restart triggered due to prolonged offline state");
                    }
                    else
                    {
                        log("DIAG: ERROR - Cannot trigger restart, settings not available");
                    }
                    
                    // Reset timer to avoid repeated restart attempts
                    _allOfflineStartTime = currentTime;
                }
            }
        }
        else
        {
            // Module is online
            if (_wasFullyOffline && _allOfflineStartTime > 0)
            {
                // Just reconnected
                unsigned long offlineDuration = currentTime - _allOfflineStartTime;
                log("DIAG: Module reconnected after %lu ms offline", offlineDuration);
            }
            
            _wasFullyOffline = false;
            _allOfflineStartTime = 0; // Reset timer when online
        }
    }

    void checkWifiConnectionAndReconnect()
    {
        if (_wifiAdapter == nullptr)
        {
            return;
        }

        if (_wifiAdapter->isAccessPointActive())
        {
            logSetup("WIFI_RECONNECT: skip (access point active)");
            return;
        }

        bool connected = _wifiAdapter->isConnected();
        unsigned long now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;

        if (connected)
        {
            if (!_wifiWasConnected)
            {
                logSetup("WIFI_RECONNECT: link restored");
            }
            _wifiWasConnected = true;
            return;
        }

        if (_wifiWasConnected)
        {
            bool result = _wifiAdapter->reconnect();
            _wifiLastReconnectAttempt = now;
            _wifiWasConnected = false;
            logSetup("WIFI_RECONNECT: edge down->reconnect result=%d", result ? 1 : 0);
        }
        else if (now - _wifiLastReconnectAttempt >= WIFI_RECONNECT_RETRY_INTERVAL_MS)
        {
            unsigned long elapsed = now - _wifiLastReconnectAttempt;
            bool result = _wifiAdapter->reconnect();
            _wifiLastReconnectAttempt = now;
            logSetup("WIFI_RECONNECT: retry after %lu ms down->reconnect result=%d", elapsed, result ? 1 : 0);
        }
        else
        {
            unsigned long remaining = WIFI_RECONNECT_RETRY_INTERVAL_MS - (now - _wifiLastReconnectAttempt);
            logSetup("WIFI_RECONNECT: still down, next retry in %lu ms", remaining);
        }
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
          _failsHistoryIndex(0),
          _mqttLastDisconnectedTime(0),
          _mqttWasConnected(false),
          _allOfflineStartTime(0),
          _wasFullyOffline(false)
    {
        for (int i = 0; i < MAX_FAILS_HISTORY; i++)
        {
            _failsHistory[i] = 0;
        }

        // Initialize MQTT connection status if manager is available
        if (_mqttManager != nullptr)
        {
            _mqttConnected = _mqttManager->isConnected();
            _mqttWasConnected = _mqttConnected;
        }
    }

    void setMqttConnected(bool connected) { _mqttConnected = connected; }

    bool isNetworkOffline() override
    {
        return _lastNetworkState == DIAGNOSTICS_NETWORK_STATE_ERROR;
    }

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

        if (_mqttManager != nullptr)
        {
            _mqttConnected = _mqttManager->isConnected();
        }

        // Track MQTT disconnection time and trigger restart if needed
        checkMqttConnectionAndTriggerRestart();

        // Check for prolonged offline state and trigger automatic reboot if needed
        checkOfflineStateAndTriggerReboot();

        // Catch WiFi link-down edge and self-reconnect (no reboot, throttled)
        checkWifiConnectionAndReconnect();

        unsigned long now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
        bool wifiConnected = (_wifiAdapter != nullptr) ? _wifiAdapter->isConnected() : true;
        if (wifiConnected)
        {
            _wifiDownSince = 0;
        }
        else if (_wifiDownSince == 0)
        {
            _wifiDownSince = now;
        }
        bool routerDown = (_wifiDownSince != 0) && (now - _wifiDownSince >= WIFI_OFFLINE_DEBOUNCE_MS);

        bool httpDown = (_restApiService ? _restApiService->getCountOffline() : 0) > _maxCountOffline;
        bool offline = routerDown || (!_mqttConnected && httpDown);

        if (offline)
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

        logOffline("OFFLINE_INDICATION: decision offline=%d state=%s netFails=%d | routerDown=%d wifiConn=%d wifiDownSince=%lu | httpDown=%d countOffline=%d | mqttConn=%d",
                   offline ? 1 : 0,
                   _lastNetworkState == DIAGNOSTICS_NETWORK_STATE_ERROR ? "ERROR" : "OK",
                   _networkFails,
                   routerDown ? 1 : 0,
                   wifiConnected ? 1 : 0,
                   _wifiDownSince,
                   httpDown ? 1 : 0,
                   (_restApiService ? _restApiService->getCountOffline() : 0),
                   _mqttConnected ? 1 : 0);

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
            diagnosticData.psramSize = _arduinoAdapter->getPsramSize();
            diagnosticData.psramFree = _arduinoAdapter->getFreePsram();
        }
        else
        {
            diagnosticData.uptimeMs = 0;
            diagnosticData.freeHeap = 0;
            diagnosticData.minFreeHeap = 0;
            diagnosticData.heapSize = 0;
            diagnosticData.maxAllocHeap = 0;
            diagnosticData.psramSize = 0;
            diagnosticData.psramFree = 0;
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
