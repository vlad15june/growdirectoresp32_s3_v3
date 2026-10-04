#pragma once

#include <vector>
#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Application/DTOs/UpdateDto.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"

class IDeviceService {
public:
    virtual ~IDeviceService() = default;

    virtual bool init() = 0;
    virtual void loop() = 0;

    /**
     * @brief Told, on the control loop, how a request this service submitted to the outbox ended.
     *
     * Called from the control loop's own task, so an implementation may touch its own state
     * without any lock. Completions for other kinds arrive here too; ignoring them is correct.
     *
     * Default: nothing, for services that submit nothing and for the mocks in the test suite.
     */
    virtual void onOutboxCompletion(const OutboxCompletion& completion) { (void)completion; }
};
