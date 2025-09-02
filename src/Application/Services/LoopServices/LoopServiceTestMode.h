#ifndef LOOP_SERVICE_TEST_MODE_H
#define LOOP_SERVICE_TEST_MODE_H

#include "LoopServiceBase.h"

class LoopServiceTestMode : public LoopServiceBase {
public:
    LoopServiceTestMode() : LoopServiceBase() {}
    ~LoopServiceTestMode() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for AP_TEST_MODE");
        
        // Handle test mode web server requests
        if (webServer) {
            webServer->loop();
        }
        
        if (boardTest && boardTest->hasTestCommandsAvailable()) {
            log("Processing test commands");
            boardTest->processTestCommands();
        }
        
        if (buttonUseCases && buttonUseCases->isButtonPressedWhenOn()) {
            log("Button press detected in TEST mode");
        }
        
        if (arduinoAdapter) {
            arduinoAdapter->delay(10);
        }
    }
};

#endif // LOOP_SERVICE_TEST_MODE_H
