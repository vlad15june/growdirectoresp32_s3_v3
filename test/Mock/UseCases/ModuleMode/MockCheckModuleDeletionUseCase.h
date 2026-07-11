#ifndef MOCK_CHECK_MODULE_DELETION_USE_CASE_H
#define MOCK_CHECK_MODULE_DELETION_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/ICheckModuleDeletionUseCase.h"

class MockCheckModuleDeletionUseCase : public ICheckModuleDeletionUseCase {
public:
    bool evaluateCalled = false;
    ModuleDeletionStatusEnum evaluateReturnValue = ModuleDeletionStatusEnum::NOT_DELETED;

    void reset() {
        evaluateCalled = false;
        evaluateReturnValue = ModuleDeletionStatusEnum::NOT_DELETED;
    }

    ModuleDeletionStatusEnum evaluate() override {
        evaluateCalled = true;
        return evaluateReturnValue;
    }
};

#endif // MOCK_CHECK_MODULE_DELETION_USE_CASE_H
