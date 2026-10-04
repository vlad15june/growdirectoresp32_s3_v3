#ifndef MOCK_LED_USE_CASES_H
#define MOCK_LED_USE_CASES_H

#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"

#include <vector>

class MockLedUseCases : public ILedUseCases {
public:
    bool onLoadCalled = false;
    bool offLoadCalled = false;
    bool blinkGreenCalled = false;
    bool blinkYellowCalled = false;
    bool loopCalled = false;
    int blinkGreenCount = 0;
    int blinkYellowCount = 0;
    int loopCount = 0;

    bool setBlinkingCalled = false;
    bool setBlinkingValue = false;
    int setBlinkingCount = 0;
    bool blinkingState = false;

    LedBootStageEnum stage = LED_BOOT_STAGE_RUNNING;
    std::vector<LedBootStageEnum> stages;

    // Reset all state for fresh test
    void reset() {
        stage = LED_BOOT_STAGE_RUNNING;
        stages.clear();
        onLoadCalled = false;
        offLoadCalled = false;
        blinkGreenCalled = false;
        blinkYellowCalled = false;
        loopCalled = false;
        blinkGreenCount = 0;
        blinkYellowCount = 0;
        loopCount = 0;
        setBlinkingCalled = false;
        setBlinkingValue = false;
        setBlinkingCount = 0;
        blinkingState = false;
    }

    // Implementations of the interface methods
    void ledOnLoad() override {
        onLoadCalled = true;
    }

    void ledOffLoad() override {
        offLoadCalled = true;
    }

    void ledBlinkGreen2Times() override {
        blinkGreenCalled = true;
        blinkGreenCount++;
    }

    void ledBlinkYellow2Times() override {
        blinkYellowCalled = true;
        blinkYellowCount++;
    }
    
    void loop() override {
        loopCalled = true;
        loopCount++;
    }

    void setBlinking(bool blinking) override {
        setBlinkingCalled = true;
        setBlinkingValue = blinking;
        setBlinkingCount++;
        blinkingState = blinking;
    }

    bool isBlinking() const override {
        return blinkingState;
    }

    void setBootStage(LedBootStageEnum newStage) override {
        stage = newStage;
        stages.push_back(newStage);
    }

    LedBootStageEnum bootStage() const override {
        return stage;
    }
};

#endif // MOCK_LED_USE_CASES_H
