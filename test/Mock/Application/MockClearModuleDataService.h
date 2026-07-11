#ifndef MOCK_CLEAR_MODULE_DATA_SERVICE_H
#define MOCK_CLEAR_MODULE_DATA_SERVICE_H

#include "Core/Domain/Interfaces/IClearModuleDataService.h"

class MockClearModuleDataService : public IClearModuleDataService {
public:
    bool clearAllDataCalled = false;
    bool clearAllDataReturnValue = true;

    void reset() {
        clearAllDataCalled = false;
        clearAllDataReturnValue = true;
    }

    bool clearAllData() override {
        clearAllDataCalled = true;
        return clearAllDataReturnValue;
    }
};

#endif // MOCK_CLEAR_MODULE_DATA_SERVICE_H
