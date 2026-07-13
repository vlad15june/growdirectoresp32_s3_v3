#ifndef CHECK_FIRMWARE_UPDATE_ON_STARTUP_USE_CASE_H
#define CHECK_FIRMWARE_UPDATE_ON_STARTUP_USE_CASE_H

#include <cstdarg>
#include <cstdio>
#include <vector>
#include <string>
#include "Core/Domain/Interfaces/UseCases/ICheckFirmwareUpdateOnStartupUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"

class CheckFirmwareUpdateOnStartupUseCase : public ICheckFirmwareUpdateOnStartupUseCase
{
private:
    IHybridAPIService *_hybridApiService;
    IModuleCommandExecutionUseCase *_moduleCommandUseCase;
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
    CheckFirmwareUpdateOnStartupUseCase(
        IHybridAPIService *hybridApiService = nullptr,
        IModuleCommandExecutionUseCase *moduleCommandUseCase = nullptr,
        ILogger *logger = nullptr,
        IDebugSystem *debugSystem = nullptr)
        : _hybridApiService(hybridApiService),
          _moduleCommandUseCase(moduleCommandUseCase),
          _logger(logger),
          _debugSystem(debugSystem)
    {
    }

    virtual ~CheckFirmwareUpdateOnStartupUseCase() = default;

    bool run() override
    {
        log("BOOT_UPDATE: pull commands on startup");

        if (_hybridApiService == nullptr || _moduleCommandUseCase == nullptr)
        {
            log("BOOT_UPDATE: dependencies unavailable, continue to deletion check");
            return false;
        }

        _hybridApiService->init();

        std::vector<ModuleCommandDto> commands = _hybridApiService->getModuleCommand();

        if (commands.empty())
        {
            log("BOOT_UPDATE: no commands, continue to deletion check");
            return false;
        }

        std::vector<ModuleCommandDto> updateCommands;
        for (const auto &command : commands)
        {
            if (getModuleCommandEnum(command.command) == ModuleCommandEnum::UPDATE_FIRMWARE)
            {
                log("BOOT_UPDATE: UpdateFirmware found uid=%s createdAt=%s",
                    command.uid.c_str(), command.createdAt.c_str());
                updateCommands.push_back(command);
            }
        }

        if (updateCommands.empty())
        {
            log("BOOT_UPDATE: no UpdateFirmware, continue to deletion check");
            return false;
        }

        std::vector<ModuleCommandExecutionResult> results =
            _moduleCommandUseCase->executeCommands(updateCommands);

        std::vector<ModuleCommandUpdateDto> updates;
        bool updateApplied = false;
        for (const auto &result : results)
        {
            ModuleCommandUpdateDto updateDto;
            updateDto.uid = result.uid;
            updateDto.status = result.status;
            updateDto.errorMessage = result.errorMessage;
            updateDto.result = result.result;
            updates.push_back(updateDto);

            if (getModuleCommandStatus(result.status) == ModuleCommandStatus::DONE)
            {
                updateApplied = true;
            }
        }

        if (!updates.empty())
        {
            _hybridApiService->sendModuleCommandExecutionResult(updates);
        }

        log("BOOT_UPDATE: applied=%d", updateApplied ? 1 : 0);
        return updateApplied;
    }
};

#endif // CHECK_FIRMWARE_UPDATE_ON_STARTUP_USE_CASE_H
