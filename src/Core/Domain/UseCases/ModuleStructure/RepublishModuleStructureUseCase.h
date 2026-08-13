#ifndef REPUBLISH_MODULE_STRUCTURE_USE_CASE_H
#define REPUBLISH_MODULE_STRUCTURE_USE_CASE_H

#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/UseCases/IRepublishModuleStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ISensorStructureUseCase.h"
#include <cstdarg>
#include <cstdio>

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
 */
class RepublishModuleStructureUseCase : public IRepublishModuleStructureUseCase
{
private:
    ILogger *logger;
    IDebugSystem *debugSystem;
    ModuleStructure *moduleStructure;
    ISensorRepository *sensorRepository;
    IHybridAPIService *hybridApiService;
    ISensorStructureUseCase *sensorStructure;

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
                                    IDebugSystem *debugSystem = nullptr)
        : logger(logger),
          debugSystem(debugSystem),
          moduleStructure(moduleStructure),
          sensorRepository(sensorRepository),
          hybridApiService(hybridApiService),
          sensorStructure(sensorStructure)
    {
    }

    /**
     * @brief Rebuild the sensor records and send the structure
     * @param reason short tag that ends up in the log, e.g. "vpd_channel_shift"
     * @return whether the structure was accepted for sending
     */
    bool execute(const char *reason) override
    {
        if (!moduleStructure || moduleStructure->nodes.empty() || !sensorRepository ||
            !hybridApiService || !sensorStructure)
        {
            log("SETUP: STRUCTURE_REPUBLISH reason=%s sent=0 sensors=0 (republish_fail: prerequisites missing)",
                reason ? reason : "unknown");
            return false;
        }

        Node &node = moduleStructure->nodes[0];
        node.sensors.clear();

        auto sensorEntities = sensorRepository->getAll();
        for (const auto &sensorEntity : sensorEntities)
        {
            sensorStructure->addSensor(node, sensorEntity);
        }

        bool sent = hybridApiService->sendToServerModuleStructure(*moduleStructure, ApiPriority::BOTH);

        log("SETUP: STRUCTURE_REPUBLISH reason=%s sent=%d sensors=%zu",
            reason ? reason : "unknown", sent ? 1 : 0, node.sensors.size());

        if (!sent)
        {
            log("SETUP: STRUCTURE_REPUBLISH republish_fail reason=%s", reason ? reason : "unknown");
        }

        return sent;
    }
};

#endif // REPUBLISH_MODULE_STRUCTURE_USE_CASE_H
