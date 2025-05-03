#ifndef LOOP_SERVICE_CAN_CONFIGURE_H
#define LOOP_SERVICE_CAN_CONFIGURE_H

#include "LoopServiceBase.h"

class LoopServiceCanConfigure : public LoopServiceBase {
public:
    LoopServiceCanConfigure() = default;
    ~LoopServiceCanConfigure() override = default;

    void loop() override {
        // Implementation for USER_CAN_CONFIGURE mode
    }
};

#endif // LOOP_SERVICE_CAN_CONFIGURE_H
