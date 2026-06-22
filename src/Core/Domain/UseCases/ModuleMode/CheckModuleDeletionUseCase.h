#ifndef CHECK_MODULE_DELETION_USE_CASE_H
#define CHECK_MODULE_DELETION_USE_CASE_H

#include <cstdarg>
#include <cstdio>
#include "Core/Domain/Interfaces/UseCases/ICheckModuleDeletionUseCase.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/ModuleDeletionStatusEnum.h"
#include "Core/Domain/Data/Models/ModuleDeletionQueryResult.h"

class CheckModuleDeletionUseCase : public ICheckModuleDeletionUseCase
{
private:
    IHybridAPIService *_hybridApiService;
    ILogger *_logger;
    IDebugSystem *_debugSystem;

    void log(const char *format, ...)
    {
        if (_logger != nullptr && _debugSystem != nullptr &&
            _debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature *debugFeature = _debugSystem->getFeature(DebugFeatures::SETUP_DEBUG);
            if (debugFeature)
            {
                _logger->println(DebugFeatures::SETUP_DEBUG, buffer, debugFeature->getLogChannel());
            }
            else
            {
                _logger->println(DebugFeatures::SETUP_DEBUG, buffer);
            }
        }
    }

public:
    CheckModuleDeletionUseCase(
        IHybridAPIService *hybridApiService = nullptr,
        ILogger *logger = nullptr,
        IDebugSystem *debugSystem = nullptr)
        : _hybridApiService(hybridApiService),
          _logger(logger),
          _debugSystem(debugSystem)
    {
    }

    virtual ~CheckModuleDeletionUseCase() = default;

    ModuleDeletionStatusEnum evaluate() override
    {
        if (_hybridApiService == nullptr)
        {
            log("IS_DELETED: verdict=UNKNOWN api_unavailable");
            return ModuleDeletionStatusEnum::UNKNOWN;
        }

        ModuleDeletionQueryResult r = _hybridApiService->queryModuleDeletion();

        if (r.received200 && r.isDeletedFlag == 1)
        {
            log("IS_DELETED: verdict=DELETED via 200 isDeletedFlag=1");
            return ModuleDeletionStatusEnum::DELETED;
        }

        if (r.received200 && r.isDeletedFlag == 0)
        {
            log("IS_DELETED: verdict=NOT_DELETED via 200 isDeletedFlag=0");
            return ModuleDeletionStatusEnum::NOT_DELETED;
        }

        if (!r.received200 && r.allTries403)
        {
            log("IS_DELETED: verdict=DELETED via sustained 403 (forbidden=3/3)");
            return ModuleDeletionStatusEnum::DELETED;
        }

        log("IS_DELETED: verdict=UNKNOWN lastCode=%d received200=%d queryFailed=%d",
            r.finalHttpCode, r.received200 ? 1 : 0, r.queryFailed ? 1 : 0);
        return ModuleDeletionStatusEnum::UNKNOWN;
    }
};

#endif // CHECK_MODULE_DELETION_USE_CASE_H
