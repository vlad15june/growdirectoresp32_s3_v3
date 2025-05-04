#ifndef LOOP_SERVICE_RE_CONFIGURE_H
#define LOOP_SERVICE_RE_CONFIGURE_H

#include "LoopServiceBase.h"

class LoopServiceReConfigure : public LoopServiceBase {
private:
    unsigned long startTime = 0;
    const unsigned long timeoutDuration = 5 * 60 * 1000; // 5 minutes in milliseconds
    
    bool isTimeoutReached() {
        if (arduinoAdapter) {
            return arduinoAdapter->millis() - startTime >= timeoutDuration;
        }
        return false; // If no adapter, avoid timeout
    }

public:
    LoopServiceReConfigure() : LoopServiceBase() {
        startTime = arduinoAdapter ? arduinoAdapter->millis() : 0; // Use adapter if available
    }
    ~LoopServiceReConfigure() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for USER_CAN_RE_CONFIGURE mode");
        
        // Check if 5 minutes have elapsed
        if (isTimeoutReached()) {
            log("5-minute timeout reached in RE_CONFIGURE mode, switching to CONFIGURED_DONE");
            if (moduleConfig) {
                moduleConfig->setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
            }
        }
        
        if (buttonUseCases && buttonUseCases->isButtonHeldForDuration()) {
            log("Button held in RE_CONFIGURE mode - processing action");
            
        }
        
        // Handle web server requests
        if (webServer)
        {
            webServer->loop();
        }
    }
};

#endif // LOOP_SERVICE_RE_CONFIGURE_H
