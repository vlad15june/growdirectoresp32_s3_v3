#ifndef FIRMWARE_UPDATE_RECOVERY_USE_CASE_H
#define FIRMWARE_UPDATE_RECOVERY_USE_CASE_H

#include <cstdarg>
#include <cstdio>
#include <string>
#include "Core/Domain/Interfaces/UseCases/IFirmwareUpdateRecoveryUseCase.h"
#include "Core/Domain/Interfaces/INetworkStatusProvider.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/IPendingFirmwareUpdateStore.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Core/Domain/Interfaces/Adapters/IOtaPartitionControl.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"

class FirmwareUpdateRecoveryUseCase : public IFirmwareUpdateRecoveryUseCase
{
private:
    INetworkStatusProvider *_networkStatusProvider;
    IModuleConfig *_moduleConfig;
    ISettings *_settings;
    IPendingFirmwareUpdateStore *_pendingUpdateStore;
    ILogger *_logger;
    IDebugSystem *_debugSystem;
    IOtaUpdateLog *_otaUpdateLog;
    IOtaPartitionControl *_otaPartitionControl;

    bool _updateHandledThisBoot = false;

    /** The kept update this boot found but may not start yet, and whether there is one. */
    ModuleCommandDto _deferredUpdate;
    bool _updateDeferred = false;

    /** @brief The command as it will be executed after the restart or after the confirmation. */
    static ModuleCommandDto forStorage(const ModuleCommandDto &moduleCommand)
    {
        // The command as it arrived, options included: whatever the platform put in them is the
        // platform's, and this class is in no position to decide which of it the update needs.
        // The age is the one field that cannot survive - the wait it is put on lasts longer than
        // the command is allowed to live, and an expired command is refused on arrival.
        ModuleCommandDto command = moduleCommand;
        command.command = getModuleCommandName(ModuleCommandEnum::UPDATE_FIRMWARE);
        command.createdAt = "";
        return command;
    }

    /**
     * @brief Remember the update this boot may not start yet, and say so where support reads it.
     *
     * Sets the same "this boot is already carrying an update" flag a kept update sets: nothing
     * else may then overwrite the file it waits in, or ask for the restart that would roll this
     * image back before it is confirmed.
     */
    void hold(const ModuleCommandDto &command)
    {
        _deferredUpdate = command;
        _updateDeferred = true;
        _updateHandledThisBoot = true;

        char held[256];
        snprintf(held, sizeof(held),
                 "UPDATE_RECOVERY: update uid=%s waits, this image is still on probation",
                 command.uid.c_str());
        log("%s", held);
        writeOtaLog(held);
    }

    void writeOtaLog(const char *message)
    {
        if (_otaUpdateLog != nullptr)
        {
            _otaUpdateLog->write(message);
        }
    }

    void log(const char *format, ...)
    {
        if (_logger == nullptr || _debugSystem == nullptr ||
            !_debugSystem->isFeatureEnabled(DebugFeatures::UPDATE_FIRMWARE_DEBUG))
        {
            return;
        }

        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        IDebugFeature *feature = _debugSystem->getFeature(DebugFeatures::UPDATE_FIRMWARE_DEBUG);
        MqttLoggerMode channel = feature ? feature->getLogChannel() : MqttLoggerMode::MqttAndSerial;
        _logger->println(DebugFeatures::UPDATE_FIRMWARE_DEBUG, buffer, channel);
    }

public:
    FirmwareUpdateRecoveryUseCase(
        INetworkStatusProvider *networkStatusProvider = nullptr,
        IModuleConfig *moduleConfig = nullptr,
        ISettings *settings = nullptr,
        IPendingFirmwareUpdateStore *pendingUpdateStore = nullptr,
        ILogger *logger = nullptr,
        IDebugSystem *debugSystem = nullptr,
        IOtaUpdateLog *otaUpdateLog = nullptr,
        IOtaPartitionControl *otaPartitionControl = nullptr)
        : _networkStatusProvider(networkStatusProvider),
          _moduleConfig(moduleConfig),
          _settings(settings),
          _pendingUpdateStore(pendingUpdateStore),
          _logger(logger),
          _debugSystem(debugSystem),
          _otaUpdateLog(otaUpdateLog),
          _otaPartitionControl(otaPartitionControl)
    {
    }

    virtual ~FirmwareUpdateRecoveryUseCase() = default;

    bool isHttpConnectionDown() override
    {
        return _networkStatusProvider != nullptr && _networkStatusProvider->isHttpConnectionDown();
    }

    /**
     * Without the bootloader bookkeeping (no adapter) the answer is "not on probation", which is
     * what this firmware did before the rollback machinery existed.
     */
    bool isRunningImageOnProbation() override
    {
        return _otaPartitionControl != nullptr &&
               _otaPartitionControl->runningImageState() == OtaImageState::PENDING_VERIFY;
    }

    bool keepForNextBoot(const ModuleCommandDto &moduleCommand) override
    {
        if (_settings == nullptr || _moduleConfig == nullptr || _pendingUpdateStore == nullptr)
        {
            log("UPDATE_RECOVERY: cannot keep the update, store unavailable");
            return false;
        }

        if (_updateHandledThisBoot)
        {
            log("UPDATE_RECOVERY: already carrying an update, no second restart");
            return false;
        }

        ModuleCommandDto command = forStorage(moduleCommand);

        if (!_pendingUpdateStore->save(command))
        {
            log("UPDATE_RECOVERY: could not store the update, reporting the failure instead");
            return false;
        }

        // The restart this method exists to ask for is a rollback while the image is on
        // probation: it was never confirmed, so the bootloader would come back on the previous
        // one. The update waits for the confirmation instead and starts from the control loop
        // then - the same wait every other way into this class ends in. The caller reports the
        // update as still running either way, so what the server sees does not change.
        if (isRunningImageOnProbation())
        {
            hold(command);
            return true;
        }

        _updateHandledThisBoot = true;

        _settings->setNoScanSensors(1);
        _settings->setBootType(BOOT_TYPE_FIRMWARE_UPDATE);
        _moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);

        // The options rather than the version alone: they are what the update will be carried out
        // with after the restart, so the log says what was kept and not a summary of it.
        char kept[256];
        snprintf(kept, sizeof(kept),
                 "UPDATE_RECOVERY: kept update uid=%s options=%s, restarting first",
                 command.uid.c_str(), command.options.c_str());
        log("%s", kept);
        writeOtaLog(kept);
        return true;
    }

    bool holdUntilImageConfirmed(const ModuleCommandDto &moduleCommand) override
    {
        if (_updateDeferred)
        {
            // One update is held per boot. The same one offered again - the server still has the
            // command it was answered "in progress" for - is that same update, not a second one,
            // and saying so keeps it from being carried out twice.
            return _deferredUpdate.uid == moduleCommand.uid;
        }

        if (_pendingUpdateStore == nullptr)
        {
            log("UPDATE_RECOVERY: cannot hold the update, store unavailable");
            return false;
        }

        ModuleCommandDto command = forStorage(moduleCommand);
        if (!_pendingUpdateStore->save(command))
        {
            log("UPDATE_RECOVERY: could not store the update, leaving it with the server");
            return false;
        }

        hold(command);
        return true;
    }

    bool takeKeptUpdate(ModuleCommandDto &command) override
    {
        if (_pendingUpdateStore == nullptr || !_pendingUpdateStore->load(command))
        {
            return false;
        }

        if (command.uid.empty() || command.options.empty())
        {
            _pendingUpdateStore->clear();
            log("UPDATE_RECOVERY: kept update is incomplete, dropping it");
            return false;
        }

        if (isRunningImageOnProbation())
        {
            // Held, not dropped: the file stays as it is, so a reset in the meantime - which on a
            // probation image means a rollback - finds the update again on the image it lands on.
            hold(command);
            command = ModuleCommandDto();
            return false;
        }

        _pendingUpdateStore->clear();
        _updateHandledThisBoot = true;
        log("UPDATE_RECOVERY: resuming update uid=%s", command.uid.c_str());
        return true;
    }

    bool takeDeferredUpdate(ModuleCommandDto &command) override
    {
        if (!_updateDeferred || isRunningImageOnProbation())
        {
            return false;
        }

        _updateDeferred = false;
        command = _deferredUpdate;
        _deferredUpdate = ModuleCommandDto();

        if (_pendingUpdateStore != nullptr)
        {
            _pendingUpdateStore->clear();
        }

        // The flag stands for "this boot has already spent its one restart", and waiting spent
        // none: the update is only starting now. Released here so that an attempt which still
        // cannot open a connection gets the restart the recovery path exists to give it -
        // by then the image is confirmed, so that restart is no longer a rollback.
        _updateHandledThisBoot = false;

        char resumed[256];
        snprintf(resumed, sizeof(resumed),
                 "UPDATE_RECOVERY: running image confirmed, starting update uid=%s",
                 command.uid.c_str());
        log("%s", resumed);
        writeOtaLog(resumed);
        return true;
    }
};

#endif // FIRMWARE_UPDATE_RECOVERY_USE_CASE_H
