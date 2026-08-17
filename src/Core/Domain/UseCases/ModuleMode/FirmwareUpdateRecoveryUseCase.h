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

    // One restart per press of Update: once an update has been kept or resumed in this boot, the
    // module does not arm another one, so a module with no link at all cannot reboot in a loop.
    bool _updateHandledThisBoot = false;

    // UPDATE_FIRMWARE_DEBUG is registered SerialOnly, so the OTA log is the only channel on which
    // the reason for the reboot reaches MQTT after the restart.
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
        IOtaUpdateLog *otaUpdateLog = nullptr)
        : _networkStatusProvider(networkStatusProvider),
          _moduleConfig(moduleConfig),
          _settings(settings),
          _pendingUpdateStore(pendingUpdateStore),
          _logger(logger),
          _debugSystem(debugSystem),
          _otaUpdateLog(otaUpdateLog)
    {
    }

    virtual ~FirmwareUpdateRecoveryUseCase() = default;

    bool isHttpConnectionDown() override
    {
        return _networkStatusProvider != nullptr && _networkStatusProvider->isHttpConnectionDown();
    }

    bool keepForNextBoot(const std::string &url,
                         const std::string &version,
                         const std::string &commandUid) override
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

        ModuleCommandDto command;
        command.uid = commandUid;
        command.command = getModuleCommandName(ModuleCommandEnum::UPDATE_FIRMWARE);
        command.options = std::string("{\"url\":\"") + url + "\",\"version\":\"" + version + "\"}";
        // Left empty on purpose: the age check is about how long a command waited on the server,
        // and this one must not expire because of the restart it was asked to make.
        command.createdAt = "";

        if (!_pendingUpdateStore->save(command))
        {
            log("UPDATE_RECOVERY: could not store the update, reporting the failure instead");
            return false;
        }

        _updateHandledThisBoot = true;

        _settings->setNoScanSensors(1);
        _settings->setBootType(BOOT_TYPE_FIRMWARE_UPDATE);
        _moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);

        char kept[256];
        snprintf(kept, sizeof(kept),
                 "UPDATE_RECOVERY: kept update uid=%s version=%s, restarting first",
                 commandUid.c_str(), version.c_str());
        log("%s", kept);
        writeOtaLog(kept);
        return true;
    }

    bool takeKeptUpdate(ModuleCommandDto &command) override
    {
        if (_pendingUpdateStore == nullptr || !_pendingUpdateStore->load(command))
        {
            return false;
        }

        // Dropped before the attempt, not after: a boot that dies in the middle of the download
        // (watchdog, power) comes back to an empty store and stops instead of looping.
        _pendingUpdateStore->clear();

        if (command.uid.empty() || command.options.empty())
        {
            log("UPDATE_RECOVERY: kept update is incomplete, dropping it");
            return false;
        }

        _updateHandledThisBoot = true;
        log("UPDATE_RECOVERY: resuming update uid=%s", command.uid.c_str());
        return true;
    }
};

#endif // FIRMWARE_UPDATE_RECOVERY_USE_CASE_H
