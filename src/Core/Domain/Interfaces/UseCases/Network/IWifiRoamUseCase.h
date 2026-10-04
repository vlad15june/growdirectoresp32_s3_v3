#pragma once

#include "Application/Interfaces/IOutbox.h"

/**
 * @brief Keeps the station on the best access point of its own network, seen from the control loop.
 *
 * Two calls, both from the loop service: one per iteration, and one per completion the network task
 * hands back - the same shape every producer of the outbox has.
 */
class IWifiRoamUseCase
{
public:
    virtual ~IWifiRoamUseCase() = default;

    /** @brief One pass of the control loop: look around if it is time, and decide if a look ended. */
    virtual void onControlLoopIteration() = 0;

    /** @brief What the network task made of the move this use case asked for. */
    virtual void onOutboxCompletion(const OutboxCompletion& completion) = 0;
};
