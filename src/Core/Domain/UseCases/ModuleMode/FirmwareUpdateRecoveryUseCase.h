#ifndef FIRMWARE_UPDATE_RECOVERY_USE_CASE_H
#define FIRMWARE_UPDATE_RECOVERY_USE_CASE_H

#include <cstdarg>
#include <cstdio>
#include <string>
#include "Core/Domain/Interfaces/UseCases/IFirmwareUpdateRecoveryUseCase.h"
#include "Core/Domain/Interfaces/INetworkStatusProvider.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/FirmwareUpdateStateEnum.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"

class FirmwareUpdateRecoveryUseCase : public IFirmwareUpdateRecoveryUseCase
{
private:
    INetworkStatusProvider *_networkStatusProvider;
    IModuleConfig *_moduleConfig;
    ISettings *_settings;
    ILogger *_logger;
    IDebugSystem *_debugSystem;

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
        ILogger *logger = nullptr,
        IDebugSystem *debugSystem = nullptr)
        : _networkStatusProvider(networkStatusProvider),
          _moduleConfig(moduleConfig),
          _settings(settings),
          _logger(logger),
          _debugSystem(debugSystem)
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
        if (_settings == nullptr || _moduleConfig == nullptr)
        {
            log("UPDATE_RECOVERY: cannot keep the update, settings unavailable");
            return false;
        }

        // The bound on restarts, and the whole of it: while an update is being carried the module
        // does not arm another one, so one press of Update costs at most one extra boot. Without
        // it a module with no link at all would answer every failed attempt with a reboot.
        if (_settings->getNeedUpdate() == 1)
        {
            log("UPDATE_RECOVERY: already carrying an update, no second restart");
            return false;
        }

        // Truncated on the way back would mean downloading nothing, or reporting the result
        // against a uid the server does not know.
        if (url.empty() || url.length() >= UPDATE_LINK_BUFSIZE ||
            version.length() >= UPDATE_VERSION_BUFSIZE ||
            commandUid.length() >= BUFSIZE)
        {
            log("UPDATE_RECOVERY: update does not fit the settings, url=%u version=%u uid=%u",
                (unsigned)url.length(), (unsigned)version.length(), (unsigned)commandUid.length());
            return false;
        }

        _settings->setUpdateLink(url.c_str());
        _settings->setUpdateVersion(version.c_str());
        _settings->setModuleCommandUid(commandUid.c_str());
        _settings->setUpdateStatus(FIRMWARE_UPDATE_STATE_IN_PROGRESS);
        _settings->setNeedUpdate(1);

        _settings->setNoScanSensors(1);
        _settings->setBootType(BOOT_TYPE_FIRMWARE_UPDATE);
        _moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);

        log("UPDATE_RECOVERY: kept update uid=%s version=%s, restarting first",
            commandUid.c_str(), version.c_str());
        return true;
    }

    bool takeKeptUpdate(ModuleCommandDto &command) override
    {
        if (_settings == nullptr || _settings->getNeedUpdate() != 1)
        {
            return false;
        }

        // Anything other than in-progress means the previous boot already used its attempt, or
        // died in the middle of one. Either way the update is not tried again.
        if (_settings->getUpdateStatus() != FIRMWARE_UPDATE_STATE_IN_PROGRESS)
        {
            log("UPDATE_RECOVERY: kept update was already attempted (status=%d), dropping it",
                (int)_settings->getUpdateStatus());
            _settings->setNeedUpdate(0);
            return false;
        }

        char url[UPDATE_LINK_BUFSIZE];
        char version[UPDATE_VERSION_BUFSIZE];
        char commandUid[BUFSIZE];
        url[0] = '\0';
        version[0] = '\0';
        commandUid[0] = '\0';
        _settings->getUpdateLink(url, UPDATE_LINK_BUFSIZE);
        _settings->getUpdateVersion(version, UPDATE_VERSION_BUFSIZE);
        _settings->getModuleCommandUid(commandUid, BUFSIZE);

        if (url[0] == '\0' || version[0] == '\0' || commandUid[0] == '\0')
        {
            log("UPDATE_RECOVERY: kept update is incomplete, dropping it");
            _settings->setNeedUpdate(0);
            return false;
        }

        // Spent before the attempt, not after: a module that reboots in the middle of the
        // download (watchdog, power) comes back to a status that is no longer in-progress and
        // stops instead of looping (the same guard S2 has in NormalModeConfigure).
        _settings->setUpdateStatus(FIRMWARE_UPDATE_STATE_ERROR);

        command.uid = commandUid;
        command.command = getModuleCommandName(ModuleCommandEnum::UPDATE_FIRMWARE);
        command.options = std::string("{\"url\":\"") + url + "\",\"version\":\"" + version + "\"}";
        // Left empty on purpose: the age check is about how long a command waited on the server,
        // and this one has been on the module all along - it must not expire because of the
        // restart it was asked to make.
        command.createdAt = "";

        log("UPDATE_RECOVERY: resuming update uid=%s version=%s", commandUid, version);
        return true;
    }

    void finishKeptUpdate() override
    {
        if (_settings == nullptr)
        {
            return;
        }

        _settings->setNeedUpdate(0);
    }
};

#endif // FIRMWARE_UPDATE_RECOVERY_USE_CASE_H
