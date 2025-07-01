#ifndef I_LOOP_SERVICE_FACTORY_H
#define I_LOOP_SERVICE_FACTORY_H

#include "../../../Core/Domain/Enums/ModuleModeEnum.h"
#include "ILoopService.h"

class ILoopServiceFactory {
public:
    virtual ~ILoopServiceFactory() = default;
    virtual std::unique_ptr<ILoopService> createLoopService(ModuleModeEnum moduleMode) = 0;
};

#endif // I_LOOP_SERVICE_FACTORY_H
