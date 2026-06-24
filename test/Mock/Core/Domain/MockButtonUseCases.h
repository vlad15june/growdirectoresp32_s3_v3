#ifndef MOCK_BUTTON_USE_CASES_H
#define MOCK_BUTTON_USE_CASES_H

#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"

class MockButtonUseCases : public IButtonUseCases {
public:
    bool checkButtonPressedWhenOnCalled = false;
    bool checkStartupFactoryResetCalled = false;
    bool isButtonPressedWhenOnCalled = false;
    bool isButtonHeldForDurationCalled = false;
    bool isTestModeRequestedCalled = false;
    bool isFactoryResetRequestedCalled = false;
    bool isAddModeRequestedCalled = false;
    bool clearSavedButtonStateCalled = false;
    
    // Return values for the mock methods
    bool checkButtonResult = true;
    bool buttonPressedResult = false;
    bool buttonHeldResult = false;
    bool testModeRequestedResult = false;
    bool factoryResetRequestedResult = false;
    bool addModeRequestedResult = false;
    
    void reset() {
        checkButtonPressedWhenOnCalled = false;
        checkStartupFactoryResetCalled = false;
        isButtonPressedWhenOnCalled = false;
        isButtonHeldForDurationCalled = false;
        isTestModeRequestedCalled = false;
        isFactoryResetRequestedCalled = false;
        isAddModeRequestedCalled = false;
        clearSavedButtonStateCalled = false;
        
        checkButtonResult = true;
        buttonPressedResult = false;
        buttonHeldResult = false;
        testModeRequestedResult = false;
        factoryResetRequestedResult = false;
        addModeRequestedResult = false;
    }
    
    bool checkButtonPressedWhenOn() override {
        checkButtonPressedWhenOnCalled = true;
        return checkButtonResult;
    }

    void checkStartupFactoryReset() override {
        checkStartupFactoryResetCalled = true;
    }

    bool isButtonPressedWhenOn() override {
        isButtonPressedWhenOnCalled = true;
        return buttonPressedResult;
    }
    
    bool isButtonHeldForDuration() override {
        isButtonHeldForDurationCalled = true;
        return buttonHeldResult;
    }
    
    bool isTestModeRequested() override {
        isTestModeRequestedCalled = true;
        return testModeRequestedResult;
    }
    
    bool isFactoryResetRequested() override {
        isFactoryResetRequestedCalled = true;
        return factoryResetRequestedResult;
    }
    
    bool isAddModeRequested() override {
        isAddModeRequestedCalled = true;
        return addModeRequestedResult;
    }
    
    void clearSavedButtonState() override {
        clearSavedButtonStateCalled = true;
    }

    void loop() override {
        // Do nothing in mock
    }
};

#endif // MOCK_BUTTON_USE_CASES_H
