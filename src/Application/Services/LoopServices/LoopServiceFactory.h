#ifndef LOOP_SERVICE_FACTORY_H
#define LOOP_SERVICE_FACTORY_H

#include "../../Interfaces/LoopServices/ILoopServiceFactory.h"
#include "LoopServiceCanConfigure.h"
#include "LoopServiceReConfigure.h"
#include "LoopServiceTestMode.h"
#include "LoopServiceConfigureDone.h"
#include <stdexcept>

class LoopServiceFactory : public ILoopServiceFactory {
public:
    LoopServiceFactory() = default;
    ~LoopServiceFactory() override = default;

    std::unique_ptr<ILoopService> createLoopService(ModuleModeEnum moduleMode) override {
        switch (moduleMode) {
            case ModuleModeEnum::USER_CAN_CONFIGURE:
                return std::unique_ptr<ILoopService>(new LoopServiceCanConfigure());
            case ModuleModeEnum::USER_CAN_RE_CONFIGURE:
                return std::unique_ptr<ILoopService>(new LoopServiceReConfigure());
            case ModuleModeEnum::AP_TEST_MODE:
                return std::unique_ptr<ILoopService>(new LoopServiceTestMode());
            case ModuleModeEnum::CONFIGURED_DONE:
                return std::unique_ptr<ILoopService>(new LoopServiceConfigureDone());
            default:
                throw std::invalid_argument("Unknown module mode");
        }
    }
};

#endif // LOOP_SERVICE_FACTORY_H
