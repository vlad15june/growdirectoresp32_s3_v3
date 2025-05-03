#ifndef LOOP_SERVICE_CONFIGURE_DONE_H
#define LOOP_SERVICE_CONFIGURE_DONE_H

#include "LoopServiceBase.h"

class LoopServiceConfigureDone : public LoopServiceBase {
private:
    unsigned long lastStatusCheck = 0;
    const unsigned long STATUS_CHECK_INTERVAL_MS = 30000; // 30 seconds

public:
    LoopServiceConfigureDone() : LoopServiceBase() {}
    ~LoopServiceConfigureDone() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for CONFIGURED_DONE mode");
        
        // Normal operation mode
        unsigned long currentTime = millis();
        
        // Periodic status check
        if (currentTime - lastStatusCheck >= STATUS_CHECK_INTERVAL_MS) {
            lastStatusCheck = currentTime;
            performStatusCheck();
        }
        
        // Handle special button combinations for maintenance or reconfiguration
        if (buttonUseCases) {
            // Example: Long press to trigger reconfiguration
            if (buttonUseCases->isButtonHeldForDuration()) {
                log("Long button press detected in CONFIGURED_DONE mode - potential reconfiguration");
                
                // Could trigger a mode change, for example:
                // if (moduleModeUseCase) {
                //    // Request mode change to reconfiguration
                // }
            }
        }
        
        // Regular operational checks and activities
        // This could include sensor monitoring, control logic, etc.
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
