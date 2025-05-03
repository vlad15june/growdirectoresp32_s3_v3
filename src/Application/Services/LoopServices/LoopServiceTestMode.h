#ifndef LOOP_SERVICE_TEST_MODE_H
#define LOOP_SERVICE_TEST_MODE_H

#include "LoopServiceBase.h"

class LoopServiceTestMode : public LoopServiceBase {
private:


public:
    LoopServiceTestMode() : LoopServiceBase() {}
    ~LoopServiceTestMode() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for AP_TEST_MODE");
                
        // Handle any test mode button inputs
        if (buttonUseCases && buttonUseCases->isButtonPressedWhenOn()) {
            log("Button press detected in TEST mode");
            // Special test operation on button press
        }
    }
    
private:
    
};

#endif // LOOP_SERVICE_TEST_MODE_H
