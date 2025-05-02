#ifndef MOCK_BUTTON_USE_CASES_H
#define MOCK_BUTTON_USE_CASES_H

#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"

class MockButtonUseCases : public IButtonUseCases {
public:
    bool checkButtonPressedWhenOnCalled = false;
    bool isButtonPressedWhenOnCalled = false;
    bool isButtonHeldForDurationCalled = false;
    
    // Return values for the mock methods
    bool checkButtonResult = true;
    bool buttonPressedResult = false;
    bool buttonHeldResult = false;
    
    void reset() {
        checkButtonPressedWhenOnCalled = false;
        isButtonPressedWhenOnCalled = false;
        isButtonHeldForDurationCalled = false;
        
        checkButtonResult = true;
        buttonPressedResult = false;
        buttonHeldResult = false;
    }
    
    bool checkButtonPressedWhenOn() override {
        checkButtonPressedWhenOnCalled = true;
        return checkButtonResult;
    }
    
    bool isButtonPressedWhenOn() override {
        isButtonPressedWhenOnCalled = true;
        return buttonPressedResult;
    }
    
    bool isButtonHeldForDuration() override {
        isButtonHeldForDurationCalled = true;
        return buttonHeldResult;
    }
};

#endif // MOCK_BUTTON_USE_CASES_H
