#ifndef RULE_SERVICE_H
#define RULE_SERVICE_H

#include "IRuleService.h"
#include "Core/Domain/Interfaces/UseCases/Rules/IEvaluateRulesUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Rules/ISyncRulesUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include <vector>

/**
 * Implementation of rule service that manages rule operations
 */
class RuleService : public IRuleService {
private:
    IEvaluateRulesUseCase* _evaluateRulesUseCase;
    ISyncRulesUseCase* _syncRulesUseCase;
    IDeviceModel* _deviceModel;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    
    void log(const char* format, ...) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::RULE_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            _logger->println(DebugFeatures::RULE_DEBUG, buffer);
        }
    }

public:
    RuleService(
        IEvaluateRulesUseCase* evaluateRulesUseCase,
        ISyncRulesUseCase* syncRulesUseCase,
        IDeviceModel* deviceModel,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr
    ) : _evaluateRulesUseCase(evaluateRulesUseCase),
        _syncRulesUseCase(syncRulesUseCase),
        _deviceModel(deviceModel),
        _logger(logger),
        _debugSystem(debugSystem) {}
    
    ~RuleService() override = default;
    
    bool init() override {
        log("RULE_SERVICE: Initializing rule service");
        
        bool success = true;
        
        if (_evaluateRulesUseCase) {
            success &= _evaluateRulesUseCase->init();
        }
        
        if (_syncRulesUseCase) {
            success &= _syncRulesUseCase->init();
        }
        
        log("RULE_SERVICE: Rule service initialization %s", success ? "successful" : "failed");
        return success;
    }
    
    void loop() override {
        // Check if we have devices before processing rules
        if (!_deviceModel || _deviceModel->getDeviceCount() == 0) {
            return;
        }
        
        if (_evaluateRulesUseCase) {
            _evaluateRulesUseCase->loop();
        }

        if (_syncRulesUseCase) {
            _syncRulesUseCase->loop();
        }
    }
    
    bool forceSyncRules() override {
        log("RULE_SERVICE: Forcing rule synchronization");
        
        if (_syncRulesUseCase) {
            return _syncRulesUseCase->forceSync();
        }
        
        log("RULE_SERVICE: Sync use case not available");
        return false;
    }
};

#endif // RULE_SERVICE_H
