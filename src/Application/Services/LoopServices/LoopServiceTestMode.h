#ifndef LOOP_SERVICE_TEST_MODE_H
#define LOOP_SERVICE_TEST_MODE_H

#include "LoopServiceBase.h"

class LoopServiceTestMode : public LoopServiceBase {
private:
    unsigned long lastTestTime = 0;
    const unsigned long TEST_INTERVAL_MS = 5000; // 5 seconds

public:
    LoopServiceTestMode() : LoopServiceBase() {}
    ~LoopServiceTestMode() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for AP_TEST_MODE");
        
        // Test mode specific operations
        unsigned long currentTime = millis();
        
        // Run test sequence periodically
        if (currentTime - lastTestTime >= TEST_INTERVAL_MS) {
            lastTestTime = currentTime;
            runTestSequence();
        }
        
        // Handle any test mode button inputs
        if (buttonUseCases && buttonUseCases->isButtonPressedWhenOn()) {
            log("Button press detected in TEST mode");
            // Special test operation on button press
        }
    }
    
private:
    void runTestSequence() {
        log("Running test sequence...");
        
        // Example: Cycle through LED patterns for testing
        if (ledUseCases) {
            ledUseCases->ledBlinkGreen2Times();
            delay(500);
            ledUseCases->ledBlinkYellow2Times();
        }
        
        // Could add more test routines:
        // - Network connectivity tests
        // - Sensor readings
        // - Actuator tests
        
        log("Test sequence completed");
    }
};

#endif // LOOP_SERVICE_TEST_MODE_H
