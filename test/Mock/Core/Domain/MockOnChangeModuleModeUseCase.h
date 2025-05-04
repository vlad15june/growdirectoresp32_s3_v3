#ifndef MOCK_ON_CHANGE_MODULE_MODE_USE_CASE_H
#define MOCK_ON_CHANGE_MODULE_MODE_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/IOnChangeModuleModeUseCase.h"

class MockOnChangeModuleModeUseCase : public IOnChangeModuleModeUseCase {
public:
    bool loopCalled = false;
    int loopCallCount = 0;

    // Reset all state for fresh test
    void reset() {
        loopCalled = false;
        loopCallCount = 0;
    }

    // Implementation of the interface method
    void loop() override {
        loopCalled = true;
        loopCallCount++;
    }
};

#endif // MOCK_ON_CHANGE_MODULE_MODE_USE_CASE_H
