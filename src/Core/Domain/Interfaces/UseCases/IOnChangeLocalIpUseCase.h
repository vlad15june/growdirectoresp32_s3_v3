#ifndef I_ON_CHANGE_LOCAL_IP_USE_CASE_H
#define I_ON_CHANGE_LOCAL_IP_USE_CASE_H

#include "Application/Interfaces/IOutbox.h"

class IOnChangeLocalIpUseCase {
public:
    virtual ~IOnChangeLocalIpUseCase() = default;

    // Perform the local IP change operation
    virtual void perform() = 0;

    /**
     * Told, on the control loop, how a request submitted to the outbox ended.
     *
     * This is where the outcome of the structure upload arrives, and the outcome is what decides
     * two things perform() cannot decide: whether the new address counts as reported, and whether
     * a module that could not report it has to restart.
     *
     * Default: nothing, for implementations that submit nothing.
     */
    virtual void onOutboxCompletion(const OutboxCompletion& completion) { (void)completion; }

    /**
     * The address the server has confirmed; empty until it has confirmed one.
     *
     * Default: empty, for implementations that report nothing.
     */
    virtual const char* reportedIp() const { return ""; }

    /**
     * How many structures this use case handed over have settled - delivered or given up on.
     *
     * A count and not a flag, so that whoever waits for "the structure I saw go out has settled"
     * reads it before and after and needs nothing reset. Control loop only.
     *
     * Default: none, for implementations that hand nothing over.
     */
    virtual uint32_t structuresSettled() const { return 0; }
};

#endif // I_ON_CHANGE_LOCAL_IP_USE_CASE_H
