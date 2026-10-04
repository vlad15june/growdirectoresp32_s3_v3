#ifndef REPUBLISH_MODULE_STRUCTURE_USE_CASE_H
#define REPUBLISH_MODULE_STRUCTURE_USE_CASE_H

#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Interfaces/UseCases/IStoredSensorDescriber.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/UseCases/IRepublishModuleStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ISensorStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Boot/IBootGates.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

/**
 * @brief Rebuild the sensor records of the module structure and send it again, right now.
 *
 * Cheap on purpose: no port scan, no ModBus traffic, no module setup - only the records are built
 * from the repository and the structure goes out. Needed when the sensor type catalog moved the
 * number of a computed channel: until the backend has the new uid, readings arrive under a uid it
 * does not know and are dropped silently (GDV1-3782, INV-10).
 *
 * A separate use case and not a method of ModuleSetupService (review of PR #17, 2026-08-13):
 * updating the structure is its own job, and it needs nothing that setup owns. The record builder
 * arrives as ISensorStructureUseCase - the same instance the setup flow uses, created once in the
 * composition root.
 *
 * ### Who makes the call
 *
 * SyncSensorTypesUseCase, whenever a catalog it applied changed the channels of a stored sensor -
 * from the control loop, applying a stored catalog in onCatalogWriteCompletion(). That is the
 * reason the POST goes to the outbox, like every other outgoing request in this firmware: what
 * stays here is deciding *what* to send - reading the sensor repository and rebuilding the node's
 * records, loop-owned state and microseconds of work - and the network task decides *when*. The
 * outcome comes back through onOutboxCompletion() on the loop, so a structure the backend never
 * took still says so in the log.
 *
 * Without an outbox this use case makes the call itself, with the
 * retry budget below. globals.h wires the outbox to it in initDependencies(), before the loop
 * runs, so that branch is not a device path - it is what a caller assembled by hand, such
 * as a test, still gets.
 */
class RepublishModuleStructureUseCase : public IRepublishModuleStructureUseCase
{
public:
    /**
     * @brief Attempts this use case makes at its POST when it has no outbox.
     *
     * Three, at this call site rather than in the transport, because the transport makes one
     * attempt per call: the outbox has a retry budget of its own and retries inside the transport
     * would multiply with it. This path exists
     * for a caller wired without one.
     */
    static const uint8_t MAX_ATTEMPTS = 3;

    /** Longest reason tag carried through to the completion log; longer ones are truncated. */
    static const size_t REASON_BUFFER_CHARS = 32;

private:
    ILogger *logger;
    IDebugSystem *debugSystem;
    ModuleStructure *moduleStructure;
    ISensorRepository *sensorRepository;
    IHybridAPIService *hybridApiService;
    ISensorStructureUseCase *sensorStructure;
    IArduinoAdapter *arduinoAdapter;

    IOutbox *outbox = nullptr;
    IStoredSensorDescriber *storedSensorDescriber = nullptr;
    IBootGates *bootGates = nullptr;
    uint32_t pendingTicket = IOutbox::kRejected;
    /**
     * The reason of the structure in flight, so the completion can be logged with the same tag the
     * submission carried. A fixed buffer: this is written on the control loop and must not be a
     * heap allocation per republish.
     */
    char pendingReason[REASON_BUFFER_CHARS];

    uint32_t submittedCounter = 0;
    uint32_t refusedCounter = 0;
    uint32_t deliveredCounter = 0;
    uint32_t undeliveredCounter = 0;

    void log(const char *format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature *feature = debugSystem->getFeature(DebugFeatures::SETUP_DEBUG);
            if (feature)
            {
                logger->println(DebugFeatures::SETUP_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                logger->println(DebugFeatures::SETUP_DEBUG, buffer);
            }
        }
    }

public:
    RepublishModuleStructureUseCase(ModuleStructure *moduleStructure = nullptr,
                                    ISensorRepository *sensorRepository = nullptr,
                                    IHybridAPIService *hybridApiService = nullptr,
                                    ISensorStructureUseCase *sensorStructure = nullptr,
                                    ILogger *logger = nullptr,
                                    IDebugSystem *debugSystem = nullptr,
                                    IArduinoAdapter *arduinoAdapter = nullptr)
        : logger(logger),
          debugSystem(debugSystem),
          moduleStructure(moduleStructure),
          sensorRepository(sensorRepository),
          hybridApiService(hybridApiService),
          sensorStructure(sensorStructure),
          arduinoAdapter(arduinoAdapter)
    {
        pendingReason[0] = '\0';
    }

    /**
     * @brief The queue this use case hands its structure to instead of posting it itself.
     */
    void setOutbox(IOutbox *outboxToUse) { outbox = outboxToUse; }

    /**
     * @brief Who decides how a stored sensor is described.
     *
     * Rebuilding the records here from `sensorStructure->addSensor()` alone would make this the
     * second answer to that question, and the two would disagree: a stored sensor the module asked
     * the line about and got nothing back from is an empty port, and only the discovery knows
     * that. This use case is the single channel by which a settled structure reaches the backend,
     * so it must not be the one that erases the difference.
     *
     * Without one the records are built as before, straight from the registry - which is what a
     * caller assembled by hand, such as a test, still gets.
     */
    void setStoredSensorDescriber(IStoredSensorDescriber *describer) { storedSensorDescriber = describer; }

    /**
     * @brief The boot sequence, whose STRUCTURE_REPUBLISH gate stays shut until the boot's own
     *        structure has settled.
     *
     * Until then a structure counts as on its way - isSendPending() answers true - so whoever asks
     * before sending waits: the module reports itself, and its address, before anything it found
     * on its lines. Optional: without it only a structure really in flight counts.
     */
    void setBootGates(IBootGates *gates) { bootGates = gates; }

    // --- Observation ------------------------------------------------------------------------

    /** @brief Structures this use case handed to the outbox. */
    uint32_t submittedCount() const { return submittedCounter; }

    /** @brief Structures the outbox had no room for. */
    uint32_t refusedCount() const { return refusedCounter; }

    /** @brief Structures the backend took. */
    uint32_t deliveredCount() const { return deliveredCounter; }

    /** @brief Structures the outbox gave up on after its attempts. */
    uint32_t undeliveredCount() const { return undeliveredCounter; }

    /** @brief True while a structure is on its way to the server, or the boot's has not settled. */
    bool isSendPending() const override
    {
        return pendingTicket != IOutbox::kRejected ||
               (bootGates != nullptr && !bootGates->allows(BootGate::STRUCTURE_REPUBLISH));
    }

    /**
     * @brief Rebuild the sensor records and send the structure
     * @param reason short tag that ends up in the log, e.g. "vpd_channel_shift"
     * @return whether the structure was accepted for sending
     */
    bool execute(const char *reason) override
    {
        if (!moduleStructure || moduleStructure->nodes.empty() || !sensorRepository ||
            !sensorStructure || (!hybridApiService && !outbox))
        {
            log("SETUP: STRUCTURE_REPUBLISH reason=%s sent=0 sensors=0 (republish_fail: prerequisites missing)",
                reason ? reason : "unknown");
            return false;
        }

        Node &node = moduleStructure->nodes[0];
        node.sensors.clear();

        if (storedSensorDescriber != nullptr)
        {
            storedSensorDescriber->describeStoredSensors(node);
        }
        else
        {
            auto sensorEntities = sensorRepository->getAll();
            for (const auto &sensorEntity : sensorEntities)
            {
                sensorStructure->addSensor(node, sensorEntity);
            }
        }

        if (outbox != nullptr)
        {
            return submitThroughOutbox(reason, node.sensors.size());
        }

        bool sent = false;
        for (uint8_t attempt = 1; attempt <= MAX_ATTEMPTS && !sent; ++attempt)
        {
            // This blocks its caller, and the caller vouches for itself between the attempts: one
            // attempt is a bounded legal pause for the task watchdog (the API lock plus one ~35 s
            // call), three back to back would not be.
            if (attempt > 1 && arduinoAdapter)
            {
                arduinoAdapter->feedTaskWatchdog();
            }
            sent = hybridApiService->sendToServerModuleStructure(*moduleStructure, ApiPriority::BOTH);
        }

        log("SETUP: STRUCTURE_REPUBLISH reason=%s sent=%d sensors=%zu",
            reason ? reason : "unknown", sent ? 1 : 0, node.sensors.size());

        if (!sent)
        {
            log("SETUP: STRUCTURE_REPUBLISH republish_fail reason=%s", reason ? reason : "unknown");
        }

        tellDescriberTheOutcome(sent);
        return sent;
    }

    void onOutboxCompletion(const OutboxCompletion &completion) override
    {
        if (completion.kind != OutboxKind::MODULE_STRUCTURE)
        {
            return;
        }

        // Structures are submitted by more than one producer - the local-IP change is the other
        // one - so the ticket, not the kind, is what says this completion is ours.
        if (pendingTicket == IOutbox::kRejected || completion.ticket != pendingTicket)
        {
            return;
        }

        pendingTicket = IOutbox::kRejected;
        tellDescriberTheOutcome(completion.delivered);

        if (completion.delivered)
        {
            deliveredCounter++;
            log("SETUP: STRUCTURE_REPUBLISH reason=%s sent=1 attempts=%u",
                pendingReason, (unsigned)completion.attempts);
            return;
        }

        undeliveredCounter++;
        log("SETUP: STRUCTURE_REPUBLISH reason=%s sent=0 attempts=%u",
            pendingReason, (unsigned)completion.attempts);
        log("SETUP: STRUCTURE_REPUBLISH republish_fail reason=%s", pendingReason);
    }

private:
    /**
     * @brief Queue the rebuilt structure and return without waiting.
     *
     * The submission carries the structure by value, so what the backend eventually sees is the
     * snapshot taken here - the same bytes the blocking call would have sent from this line.
     *
     * A republish while one is still in flight replaces the ticket rather than being skipped: the
     * structure in flight was built before this catalog change and describes channels that have
     * since moved, which is the very thing this use case exists to correct. The displaced request
     * is still sent - the outbox never drops an event - only its outcome is no longer awaited
     * here, and that is said in the log rather than left to be inferred.
     */
    /**
     * @brief Tell the describer whether the structure it wrote reached the server.
     *
     * Only when there is a describer: without one the records here are built straight from the
     * registry and nobody is keeping track of what the server was told.
     *
     * A structure that supersedes one still in flight leaves that one's outcome unmatched by
     * ticket, so the describer hears about the last structure it wrote and not about the one its
     * records have already replaced.
     */
    void tellDescriberTheOutcome(bool delivered)
    {
        if (storedSensorDescriber != nullptr)
        {
            storedSensorDescriber->onDescribedStructureSettled(delivered);
        }
    }

    bool submitThroughOutbox(const char *reason, size_t sensorCount)
    {
        const char *tag = (reason != nullptr) ? reason : "unknown";

        if (pendingTicket != IOutbox::kRejected)
        {
            log("SETUP: STRUCTURE_REPUBLISH reason=%s superseding the structure of reason=%s still in flight",
                tag, pendingReason);
        }

        const uint32_t ticket = outbox->submitModuleStructure(*moduleStructure, ApiPriority::BOTH);

        if (ticket == IOutbox::kRejected)
        {
            refusedCounter++;
            log("SETUP: STRUCTURE_REPUBLISH reason=%s queued=0 sensors=%zu", tag, sensorCount);
            log("SETUP: STRUCTURE_REPUBLISH republish_fail reason=%s", tag);
            tellDescriberTheOutcome(false);
            return false;
        }

        pendingTicket = ticket;
        submittedCounter++;
        snprintf(pendingReason, sizeof(pendingReason), "%s", tag);

        log("SETUP: STRUCTURE_REPUBLISH reason=%s queued=1 sensors=%zu ticket=%u",
            tag, sensorCount, (unsigned)ticket);
        return true;
    }
};

#endif // REPUBLISH_MODULE_STRUCTURE_USE_CASE_H
