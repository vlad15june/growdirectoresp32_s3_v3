#ifndef LOOP_SERVICE_CONFIGURE_DONE_H
#define LOOP_SERVICE_CONFIGURE_DONE_H

#include "LoopServiceBase.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"

class LoopServiceConfigureDone : public LoopServiceBase {
private:
    IOnChangeLocalIpUseCase* onChangeLocalIpUseCase;
    IMqttManager* mqttManager;

public:
    LoopServiceConfigureDone() 
        : LoopServiceBase(), 
          onChangeLocalIpUseCase(nullptr),
          mqttManager(nullptr) {}
    
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
    }
    
    // Setter for the OnChangeLocalIpUseCase
    virtual void setOnChangeLocalIpUseCase(IOnChangeLocalIpUseCase* useCase) {
        this->onChangeLocalIpUseCase = useCase;
    }
    
    // Setter for the MqttManager
    virtual void setMqttManager(IMqttManager* manager) {
        this->mqttManager = manager;
    }
    
private:
    void performStatusCheck() {
        log("Performing regular status check...");
        
        // Example status indicators
        if (ledUseCases) {
            // Brief LED flash to indicate normal operation
            ledUseCases->ledBlinkGreen2Times();
        }
        
        // Could check:
        // - Connection status
        // - Device health
        // - Sensor readings
        // - etc.
    }
};

#endif // LOOP_SERVICE_CONFIGURE_DONE_H
