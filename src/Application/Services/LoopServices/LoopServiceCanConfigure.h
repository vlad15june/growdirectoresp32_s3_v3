#ifndef LOOP_SERVICE_CAN_CONFIGURE_H
#define LOOP_SERVICE_CAN_CONFIGURE_H

#include "LoopServiceBase.h"

class LoopServiceCanConfigure : public LoopServiceBase
{
public:
    LoopServiceCanConfigure() : LoopServiceBase() {}
    ~LoopServiceCanConfigure() override = default;

    void executeLoopImplementation() override
    {
        log("Running loop for USER_CAN_CONFIGURE mode");

        // Handle web server requests
        if (webServer)
        {
            webServer->loop();
        }
    }
};

#endif // LOOP_SERVICE_CAN_CONFIGURE_H
