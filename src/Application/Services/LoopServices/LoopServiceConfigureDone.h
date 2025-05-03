#ifndef LOOP_SERVICE_CONFIGURE_DONE_H
#define LOOP_SERVICE_CONFIGURE_DONE_H

#include "LoopServiceBase.h"

class LoopServiceConfigureDone : public LoopServiceBase {
public:
    LoopServiceConfigureDone() = default;
    ~LoopServiceConfigureDone() override = default;

    void loop() override {
        // Implementation for CONFIGURED_DONE mode
    }
};

#endif // LOOP_SERVICE_CONFIGURE_DONE_H
