#pragma once

#include "Application/DTOs/CommandDto.h"
#include "Application/Interfaces/IOutbox.h"

/**
 * @brief Interface for command processing service
 *
 * Handling a command is not one call. `process()` accepts the MQTT signal and returns
 * within microseconds; the authoritative list comes back later through the outbox, the raw bus
 * transactions come back later through the acquisition task, and both are picked up on the
 * control loop by the two methods below. That split is what keeps the control loop's worst
 * iteration independent of how slow the server or a silent Modbus device is.
 */
class ICommandService
{
public:
    virtual ~ICommandService() = default;

    /**
     * @brief Accept a command DTO
     *
     * Does not execute anything that can wait on the network or on the bus - it starts the work
     * and returns.
     *
     * @param command Command to process
     * @return true if the command was accepted, false if it could not be started
     */
    virtual bool process(const CommandDto &command) = 0;

    /**
     * @brief Apply one finished outbox request, if it is one of this service's.
     *
     * Called by the control loop while it drains the completion queue. Ignores every kind it did
     * not ask for.
     */
    virtual void onOutboxCompletion(const OutboxCompletion &completion) = 0;

    /**
     * @brief One control-loop iteration's worth of follow-up work.
     *
     * Takes the raw bus commands the acquisition task finished and reports them, and re-offers any
     * report the outbox had no room for. Never touches the network or the bus itself.
     */
    virtual void loop() = 0;

    /**
     * @brief Ask the server for the module commands waiting for this module, as a signal would.
     *
     * The boot's own ask, once its structure has settled: a command left for the module while it
     * was off has no signal coming for it. The list is executed as every fetched list is.
     */
    virtual void requestModuleCommandFetch() = 0;

    /**
     * @brief Carry out the firmware update the previous boot kept for this one, if there is one.
     *
     * Asked once the boot has a link and its structure has settled: until then the update stays
     * in its file, so a boot that never gets a link loses nothing.
     */
    virtual void resumeKeptUpdate() = 0;

    /**
     * @brief Ask the server whether it has deleted the module, in case its DeleteGd was lost.
     *
     * Asked once a boot, on its first STEADY, of a module the server had registered when the boot
     * began. A DELETED verdict is a factory reset, as DeleteGd's is; any other is nothing, and
     * nothing is reported - no command asked for this.
     */
    virtual void checkModuleDeletion() = 0;
};
