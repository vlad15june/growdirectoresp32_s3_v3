#pragma once

#include <cstdint>
#include <vector>

#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"

/**
 * @brief One finished network command, as the control loop collects it.
 *
 * The work travels back with the outcome because the control loop needs what it prepared -
 * the command uid, the options - to apply the outcome and report it.
 */
struct NetworkCommandCompletion
{
    uint32_t ticket = 0;
    ComplexCommandWork work;
    ComplexCommandOutcome outcome;
};

/**
 * @brief The task that waits on the network for module commands, seen from the control loop.
 *
 * UpdateFirmware downloads an image with no time limit of its own; DeleteModule asks the server
 * whether the deletion is real (three 10 s attempts). Run inline, either would park the
 * controller for as long as the network takes. The loop submits the work here and collects the
 * outcome later; neither call waits.
 */
class INetworkCommandRunner
{
public:
    /** @brief Ticket value returned when a command could not be queued. Real tickets start at 1. */
    static const uint32_t kRejected = 0;

    /**
     * @brief Cap on commands waiting to run.
     *
     * Sized to the largest command list the report queue can carry (CommandService's
     * MAX_PENDING_REPORTS is 24): a fetched list is routed here slot by slot, and a submission
     * refused mid-list would turn one server answer into a mix of executed and refused commands
     * for no reason but a short queue.
     */
    static const size_t MAX_PENDING_COMMANDS = 24;

    /**
     * @brief Up-front capacity of the result buffers: the runner's vector and the drain buffer
     *        the consumer swaps with it.
     *
     * What actually bounds a burst is MAX_PENDING_COMMANDS: completions outstanding can never
     * exceed the commands in flight. The reservation is deliberately smaller, sized to the
     * realistic burst instead - results are collected by the control loop every iteration while
     * completions arrive at the pace of network operations, so eight covers what a drain
     * plausibly finds. A rarer, larger burst still fits: the vector grows past the reservation
     * and the extra capacity then circulates in the swap pair.
     */
    static const size_t RESULT_BUFFER_RESERVE = 8;
    static_assert(RESULT_BUFFER_RESERVE <= MAX_PENDING_COMMANDS,
                  "a result buffer reservation larger than the command queue could never fill");

    virtual ~INetworkCommandRunner() = default;

    /**
     * @brief Queue the network part of a complex module command and return without waiting.
     * @param work Prepared on the control loop by IModuleCommandExecutionUseCase::prepareComplexCommand()
     * @return a ticket, or kRejected when the queue is full or no task could be created
     */
    virtual uint32_t submit(const ComplexCommandWork& work) = 0;

    /**
     * @brief Take every network command that has finished since the last call.
     * @return true if there was anything to take
     */
    virtual bool drainResults(std::vector<NetworkCommandCompletion>& out) = 0;
};
