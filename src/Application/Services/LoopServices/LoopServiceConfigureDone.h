#ifndef LOOP_SERVICE_CONFIGURE_DONE_H
#define LOOP_SERVICE_CONFIGURE_DONE_H

#include "LoopServiceBase.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IOnConnectivityRestoredUseCase.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Application/Interfaces/IDiagnosticService.h"
#include "Application/Services/Devices/IDeviceService.h"
#include "Application/Services/Sensors/ISensorService.h"
#include "Application/Services/Rules/IRuleService.h"

class LoopServiceConfigureDone : public LoopServiceBase {
private:
    IOnChangeLocalIpUseCase* onChangeLocalIpUseCase;
    IOnConnectivityRestoredUseCase* onConnectivityRestoredUseCase;
    IMqttManager* mqttManager;
    IDiagnosticService* diagnosticService;
    IDeviceService* deviceService;
    ISensorService* sensorService;
    IRuleService* ruleService;
    
    // Diagnostic timer variables
    unsigned long lastDiagnosticTime = 0;
    const unsigned long DIAGNOSTIC_INTERVAL = 30 * 1000; // 30 sec in milliseconds

public:
    LoopServiceConfigureDone() 
        : LoopServiceBase(),
          onChangeLocalIpUseCase(nullptr),
          onConnectivityRestoredUseCase(nullptr),
          mqttManager(nullptr),
          diagnosticService(nullptr),
          deviceService(nullptr),
          sensorService(nullptr),
          ruleService(nullptr) {}
    
    ~LoopServiceConfigureDone() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for CONFIGURED_DONE mode");        
        
        if (deviceService) {
            deviceService->loop();
        }

        if (sensorService) {
            sensorService->loop();
        }

        if (ruleService) {
            ruleService->loop();
        }
        
        if (buttonUseCases) {
            if (buttonUseCases->isButtonHeldForDuration()) {
                log("Long button press detected in CONFIGURED_DONE mode - potential reconfiguration");
                
                moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
            }
        }
        
        if (onChangeLocalIpUseCase) {
            log("Checking for IP address changes");
            onChangeLocalIpUseCase->perform();
        }

        if (onConnectivityRestoredUseCase) {
            onConnectivityRestoredUseCase->perform();
        }

        if (mqttManager) {
            log("Processing MQTT messages");
            mqttManager->loop();
        }
        
        if (diagnosticService && arduinoAdapter) {
            unsigned long currentTime = arduinoAdapter->millis();
            if (currentTime - lastDiagnosticTime >= DIAGNOSTIC_INTERVAL) {
                log("Running diagnostic service");
                diagnosticService->performAndSend();
                lastDiagnosticTime = currentTime;
            }
        }
    }
    
    virtual void setOnChangeLocalIpUseCase(IOnChangeLocalIpUseCase* useCase) {
        this->onChangeLocalIpUseCase = useCase;
    }

    virtual void setOnConnectivityRestoredUseCase(IOnConnectivityRestoredUseCase* useCase) {
        this->onConnectivityRestoredUseCase = useCase;
    }

    virtual void setMqttManager(IMqttManager* manager) {
        this->mqttManager = manager;
    }
    
    virtual void setDiagnosticService(IDiagnosticService* service) {
        this->diagnosticService = service;
    }
    
    virtual void setDeviceService(IDeviceService* service) {
        this->deviceService = service;
    }
    
    virtual void setSensorService(ISensorService* service) {
        this->sensorService = service;
    }
    
    virtual void setRuleService(IRuleService* service) {
        this->ruleService = service;
    }
};

#endif // LOOP_SERVICE_CONFIGURE_DONE_H
