#ifndef ON_CONNECTIVITY_RESTORED_USE_CASE_H
#define ON_CONNECTIVITY_RESTORED_USE_CASE_H

#include <cstdarg>
#include "Core/Domain/Interfaces/UseCases/IOnConnectivityRestoredUseCase.h"
#include "Core/Domain/Interfaces/INetworkStatusProvider.h"
#include "Core/Domain/Interfaces/UseCases/Rules/ISyncRulesUseCase.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

class OnConnectivityRestoredUseCase : public IOnConnectivityRestoredUseCase
{
private:
    static const unsigned long FORCE_SYNC_COOLDOWN_MS = 5 * 60 * 1000;

    INetworkStatusProvider *_networkStatusProvider;
    ISyncRulesUseCase *_syncRulesUseCase;
    IModuleConfig *_moduleConfig;
    IArduinoAdapter *_arduinoAdapter;
    ILogger *_logger;
    IDebugSystem *_debugSystem;

    bool _wasOffline = false;
    bool _isRuleCapable = false;
    unsigned long _lastForceSyncTime = 0;

public:
    OnConnectivityRestoredUseCase(
        INetworkStatusProvider *networkStatusProvider,
        ISyncRulesUseCase *syncRulesUseCase,
        IModuleConfig *moduleConfig,
        IArduinoAdapter *arduinoAdapter,
        ILogger *logger,
        IDebugSystem *debugSystem)
        : _networkStatusProvider(networkStatusProvider),
          _syncRulesUseCase(syncRulesUseCase),
          _moduleConfig(moduleConfig),
          _arduinoAdapter(arduinoAdapter),
          _logger(logger),
          _debugSystem(debugSystem)
    {
    }

    bool init() override
    {
        _isRuleCapable = (_moduleConfig->getModuleType() != MODULE_TYPE_SENSOR);
        _wasOffline = _networkStatusProvider->isNetworkOffline();
        _lastForceSyncTime = (_arduinoAdapter ? _arduinoAdapter->millis() : 0) - FORCE_SYNC_COOLDOWN_MS;
        log("CONNECTIVITY_RESTORED: init isRuleCapable=%d wasOffline=%d", _isRuleCapable, _wasOffline);
        return true;
    }

    void perform() override
    {
        if (!_isRuleCapable)
        {
            return;
        }

        bool isOffline = _networkStatusProvider->isNetworkOffline();

        if (_wasOffline && !isOffline)
        {
            unsigned long now = _arduinoAdapter ? _arduinoAdapter->millis() : 0;
            unsigned long elapsed = now - _lastForceSyncTime;

            if (!_arduinoAdapter || elapsed >= FORCE_SYNC_COOLDOWN_MS)
            {
                _syncRulesUseCase->forceSync();
                _lastForceSyncTime = now;
                log("CONNECTIVITY_RESTORED: edge offline->online forcing rule sync");
            }
            else
            {
                log("CONNECTIVITY_RESTORED: edge offline->online throttled elapsed=%lu cooldown=%lu",
                    elapsed, (unsigned long)FORCE_SYNC_COOLDOWN_MS);
            }
        }

        _wasOffline = isOffline;
    }

private:
    void log(const char *format, ...)
    {
        if (!_debugSystem->isFeatureEnabled(DebugFeatures::RULE_SYNC_DEBUG))
        {
            return;
        }

        char buffer[160];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        IDebugFeature *feature = _debugSystem->getFeature(DebugFeatures::RULE_SYNC_DEBUG);
        _logger->println(DebugFeatures::RULE_SYNC_DEBUG, buffer,
                         feature ? feature->getLogChannel() : MqttLoggerMode::SerialOnly);
    }
};

#endif // ON_CONNECTIVITY_RESTORED_USE_CASE_H
