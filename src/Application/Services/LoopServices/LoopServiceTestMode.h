#ifndef LOOP_SERVICE_TEST_MODE_H
#define LOOP_SERVICE_TEST_MODE_H

#include "LoopServiceBase.h"

class LoopServiceTestMode : public LoopServiceBase {
public:
    LoopServiceTestMode() = default;
    ~LoopServiceTestMode() override = default;

    void loop() override {
        // Implementation for AP_TEST_MODE
    }
};

#endif // LOOP_SERVICE_TEST_MODE_H
