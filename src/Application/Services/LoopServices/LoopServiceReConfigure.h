#ifndef LOOP_SERVICE_RE_CONFIGURE_H
#define LOOP_SERVICE_RE_CONFIGURE_H

#include "LoopServiceBase.h"

class LoopServiceReConfigure : public LoopServiceBase {
public:
    LoopServiceReConfigure() = default;
    ~LoopServiceReConfigure() override = default;

    void loop() override {
        // Implementation for USER_CAN_RE_CONFIGURE mode
    }
};

#endif // LOOP_SERVICE_RE_CONFIGURE_H
