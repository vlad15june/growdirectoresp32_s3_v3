#ifndef MOCK_LED_USE_CASES_H
#define MOCK_LED_USE_CASES_H

#include "Core/Domain/Interfaces/ILedUseCases.h"

class MockLedUseCases : public ILedUseCases {
public:
    bool onLoadCalled = false;
    bool offLoadCalled = false;
    bool blinkGreen2TimesCalled = false;
    bool blinkYellow2TimesCalled = false;
    
    // Reset all state for fresh test
    void reset() {
        onLoadCalled = false;
        offLoadCalled = false;
        blinkGreen2TimesCalled = false;
        blinkYellow2TimesCalled = false;
    }
    
    // Implementations of the interface methods
    void ledOnLoad() override {
        onLoadCalled = true;
    }
    
    void ledOffLoad() override {
        offLoadCalled = true;
    }
    
    void ledBlinkGreen2Times() override {
        blinkGreen2TimesCalled = true;
    }
    
    void ledBlinkYellow2Times() override {
        blinkYellow2TimesCalled = true;
    }
};

#endif // MOCK_LED_USE_CASES_H
