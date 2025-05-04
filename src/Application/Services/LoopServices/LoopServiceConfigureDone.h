#ifndef LOOP_SERVICE_CONFIGURE_DONE_H
#define LOOP_SERVICE_CONFIGURE_DONE_H

#include "LoopServiceBase.h"

class LoopServiceConfigureDone : public LoopServiceBase {
private:


public:
    LoopServiceConfigureDone() : LoopServiceBase() {}
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
        
        // Regular operational checks and activities
        // This could include sensor monitoring, control logic, etc.


        //MqttManager.loop()
        //OnChangeLocalIpUseCase.prerform()
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
