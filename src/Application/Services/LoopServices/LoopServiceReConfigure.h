#ifndef LOOP_SERVICE_RE_CONFIGURE_H
#define LOOP_SERVICE_RE_CONFIGURE_H

#include "LoopServiceBase.h"

class LoopServiceReConfigure : public LoopServiceBase {
public:
    LoopServiceReConfigure() : LoopServiceBase() {}
    ~LoopServiceReConfigure() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for USER_CAN_RE_CONFIGURE mode");
        
        // Re-configuration specific operations
        // Handle button presses for reconfiguration options
        if (buttonUseCases && buttonUseCases->isButtonHeldForDuration()) {
            log("Button held in RE_CONFIGURE mode - processing action");
            
            // Example: indicate reconfiguration activity with LED
            if (ledUseCases) {
                ledUseCases->ledBlinkYellow2Times();
            }
        }
        
        // Additional reconfiguration specific checks and operations
    }
};

#endif // LOOP_SERVICE_RE_CONFIGURE_H
