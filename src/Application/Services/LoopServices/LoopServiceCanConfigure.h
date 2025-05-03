#ifndef LOOP_SERVICE_CAN_CONFIGURE_H
#define LOOP_SERVICE_CAN_CONFIGURE_H

#include "LoopServiceBase.h"

class LoopServiceCanConfigure : public LoopServiceBase {
public:
    LoopServiceCanConfigure() : LoopServiceBase() {}
    ~LoopServiceCanConfigure() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for USER_CAN_CONFIGURE mode");
        
        // Check if button is held to potentially trigger a mode change
        if (buttonUseCases && buttonUseCases->isButtonHeldForDuration()) {
            log("Button held in USER_CAN_CONFIGURE mode - potential action needed");
            
            // Specific functionality for configuration mode
            // Examples: blinking LED pattern, network status checks, etc.
            if (ledUseCases) {
                ledUseCases->ledBlinkGreen2Times();
            }
        }
        
        // Additional configuration mode specific operations
        // For example, checking if WiFi configuration is completed
    }
};

#endif // LOOP_SERVICE_CAN_CONFIGURE_H
