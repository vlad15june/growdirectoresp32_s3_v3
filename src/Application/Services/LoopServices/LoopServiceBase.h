#ifndef LOOP_SERVICE_BASE_H
#define LOOP_SERVICE_BASE_H

#include "../../Interfaces/LoopServices/ILoopService.h"

class LoopServiceBase : public ILoopService {
protected:
    LoopServiceBase() = default;

public:
    virtual ~LoopServiceBase() = default;
    // Derived classes will implement the loop method
};

#endif // LOOP_SERVICE_BASE_H
