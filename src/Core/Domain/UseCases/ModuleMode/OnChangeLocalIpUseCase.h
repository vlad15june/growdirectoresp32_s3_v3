#ifndef ON_CHANGE_LOCAL_IP_USE_CASE_H
#define ON_CHANGE_LOCAL_IP_USE_CASE_H

#include <cstdarg>
#include <cstdio>
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/IWifiManager.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include <string.h>

class OnChangeLocalIpUseCase : public IOnChangeLocalIpUseCase
{
private:
    ILogger &_logger;
    IDebugSystem &_debugSystem;
    IWifiAdapter &_wifiAdapter;
    IWifiManager &_wifiManager;
    ModuleStructure &_moduleStructure;
    IHybridAPIService &_apiService;
    IModuleConfig &_moduleConfig;
    char _oldIpStr[16]; // Store previous IP address

public:
    OnChangeLocalIpUseCase(
        IWifiAdapter &wifiAdapter,
        IWifiManager &wifiManager,
        ModuleStructure &moduleStructure,
        IHybridAPIService &apiService,
        IModuleConfig &moduleConfig,
        ILogger &loggerInstance,
        IDebugSystem &debugSystemInstance)
        : _wifiAdapter(wifiAdapter),
          _wifiManager(wifiManager),
          _moduleStructure(moduleStructure),
          _apiService(apiService),
          _moduleConfig(moduleConfig),
          _logger(loggerInstance),
          _debugSystem(debugSystemInstance)
    {

        // Initialize oldIpStr to empty string
        _oldIpStr[0] = '\0';
        log("OnChangeLocalIpUseCase: Initialized");
    }

    void perform() override
    {
        // log("OnChangeLocalIpUseCase: Performing local IP update");

        // Get the new local IP from the WiFi adapter
        const char *newIpStr = _wifiAdapter.getLocalIP();

        // Skip processing if IP is missing or is 0.0.0.0
        if (newIpStr == nullptr || strcmp(newIpStr, "0.0.0.0") == 0)
        {
            log("OnChangeLocalIpUseCase: No valid IP available (null or 0.0.0.0)");
            return;
        }

        // Check if IP has changed
        bool ipChanged = (strlen(_oldIpStr) == 0 || strcmp(_oldIpStr, newIpStr) != 0);

        if (ipChanged)
        {
            log("OnChangeLocalIpUseCase: IP changed - old IP: %s, new IP: %s",
                (strlen(_oldIpStr) > 0 ? _oldIpStr : "not set"), newIpStr);

            // Only process if WiFi is connected
            if (_wifiAdapter.isConnected())
            {
                log("OnChangeLocalIpUseCase: WiFi connected, updating module structure");

                // Store new IP as old IP for next comparison
                strncpy(_oldIpStr, newIpStr, sizeof(_oldIpStr) - 1);
                _oldIpStr[sizeof(_oldIpStr) - 1] = '\0'; // Ensure null-termination

                // Update IP in all nodes in the module structure
                std::string newIpString(newIpStr);
                for (auto &node : _moduleStructure.nodes)
                {
                    node.ip = newIpString;
                    log("OnChangeLocalIpUseCase: Updated IP for node to %s", newIpStr);
                }

                // Update SSID in module structure
                int networkIndex = 0; // Assuming the connected network is at index 0
                const char *ssid = _wifiAdapter.getSSID(networkIndex);
                if (ssid != nullptr)
                {
                    std::string ssidString(ssid);
                    for (auto &node : _moduleStructure.nodes)
                    {
                        node.ssid = ssidString;
                        log("OnChangeLocalIpUseCase: Updated SSID for node to %s", ssid);
                    }
                }

                log("OnChangeLocalIpUseCase: Sending updated module structure to server");
                bool sendSuccess = _apiService.sendToServerModuleStructure(_moduleStructure);
                if (!sendSuccess) {
                    log("NET: Failed to send structure after 3 attempts, restarting...");
                    _moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_NEED_RESTART);
                }

                // Process additional server communications
                log("OnChangeLocalIpUseCase: Processing delete command and restoring account info");
                // These would be implemented in your HybridAPIService
                // _apiService.processDeleteGdCommand();
                // _apiService.restoreAccountInfoFromServer();
            }
            else
            {
                log("OnChangeLocalIpUseCase: WiFi not connected, skipping update");
            }
        }
        else
        {
            // log("OnChangeLocalIpUseCase: IP has not changed, no update needed");
        }
    }

private:
    void log(const char *format, ...)
    {
        if (_debugSystem.isFeatureEnabled(DebugFeatures::IP_CHANGE_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            _logger.println(DebugFeatures::IP_CHANGE_DEBUG, buffer);
        }
    }
};

#endif // ON_CHANGE_LOCAL_IP_USE_CASE_H
