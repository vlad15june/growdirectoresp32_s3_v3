#ifndef LOOP_SERVICE_RE_CONFIGURE_H
#define LOOP_SERVICE_RE_CONFIGURE_H

#include "LoopServiceBase.h"

class LoopServiceReConfigure : public LoopServiceBase {
public:
    LoopServiceReConfigure() : LoopServiceBase() {}
    ~LoopServiceReConfigure() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for USER_CAN_RE_CONFIGURE mode");
        
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
