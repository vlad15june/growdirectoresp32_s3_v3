#ifndef LOOP_SERVICE_CONFIGURE_DONE_H
#define LOOP_SERVICE_CONFIGURE_DONE_H

#include "LoopServiceBase.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Application/Interfaces/IDiagnosticService.h"

class LoopServiceConfigureDone : public LoopServiceBase {
private:
    IOnChangeLocalIpUseCase* onChangeLocalIpUseCase;
    IMqttManager* mqttManager;
    IDiagnosticService* diagnosticService;
    
    // Diagnostic timer variables
    unsigned long lastDiagnosticTime = 0;
    const unsigned long DIAGNOSTIC_INTERVAL = 5 * 60 * 1000; // 5 minutes in milliseconds

public:
    LoopServiceConfigureDone() 
        : LoopServiceBase(), 
          onChangeLocalIpUseCase(nullptr),
          mqttManager(nullptr),
          diagnosticService(nullptr) {}
    
    ~LoopServiceConfigureDone() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for CONFIGURED_DONE mode");        
        
        // Handle special button combinations for maintenance or reconfiguration
        if (buttonUseCases) {
            // Example: Long press to trigger reconfiguration
            if (buttonUseCases->isButtonHeldForDuration()) {
                log("Long button press detected in CONFIGURED_DONE mode - potential reconfiguration");
                
                moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
            }
        }
        
        // Check for IP address changes and update as needed
        if (onChangeLocalIpUseCase) {
            log("Checking for IP address changes");
            onChangeLocalIpUseCase->perform();
        }
        
        // Process MQTT messages and maintain connection
        if (mqttManager) {
            log("Processing MQTT messages");
            mqttManager->loop();
        }
        
        // Run diagnostic service at regular intervals
        if (diagnosticService && arduinoAdapter) {
            unsigned long currentTime = arduinoAdapter->millis();
            if (currentTime - lastDiagnosticTime >= DIAGNOSTIC_INTERVAL) {
                log("Running diagnostic service");
                diagnosticService->performAndSend();
                lastDiagnosticTime = currentTime;
            }
        }
    }
    
    // Setter for the OnChangeLocalIpUseCase
    virtual void setOnChangeLocalIpUseCase(IOnChangeLocalIpUseCase* useCase) {
        this->onChangeLocalIpUseCase = useCase;
    }
    
    // Setter for the MqttManager
    virtual void setMqttManager(IMqttManager* manager) {
        this->mqttManager = manager;
    }
    
    // Setter for the DiagnosticService
    virtual void setDiagnosticService(IDiagnosticService* service) {
        this->diagnosticService = service;
    }
};

#endif // LOOP_SERVICE_CONFIGURE_DONE_H
