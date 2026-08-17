#include <unity.h>
#include <string>
#include <vector>
#include "Mock/ArduinoMock.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/CheckFirmwareUpdateOnStartupUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/FirmwareUpdateRecoveryUseCase.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Configuration/PendingFirmwareUpdateStore.h"
#include "Mock/Core/Domain/MockFirmwareRemoteUpdate.h"
#include "Mock/Core/Domain/MockNetworkStatusProvider.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Application/DTOs/ModuleCommandDto.h"

static const char *UPDATE_URL =
    "https://api-test.growdirector.com/api/v1/Firmware/file/39d4d2a7ea7f4d78a46d1e4159624a5f";
static const char *UPDATE_VERSION = "2.18.6";
static const char *COMMAND_UID = "48cba456-5a01-4e6c-a9bb-970544fd9f71";

namespace
{
struct ModuleState
{
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockFirmwareRemoteUpdate firmwareUpdater;
    MockNetworkStatusProvider networkStatus;
    MockHybridAPIService hybridApi;
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    PendingFirmwareUpdateStore pendingUpdateStore;

    ModuleState() : pendingUpdateStore(&spiffs, &jsonConverter)
    {
        moduleConfig.setSettings(&settings);
        moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
        settings.setBootType(BOOT_TYPE_POWER_ON);
        settings.setNoScanSensors(0);
    }

    bool storedUpdate(ModuleCommandDto &command) { return pendingUpdateStore.load(command); }
};

struct Boot
{
    FirmwareUpdateRecoveryUseCase recovery;
    ModuleCommandExecutionUseCase commandUseCase;
    CheckFirmwareUpdateOnStartupUseCase startupCheck;

    explicit Boot(ModuleState &m)
        : recovery(&m.networkStatus, &m.moduleConfig, &m.settings, &m.pendingUpdateStore,
                   &m.logger, &m.debugSystem),
          commandUseCase(&m.logger, &m.debugSystem, nullptr, &m.firmwareUpdater,
                         nullptr, nullptr, nullptr, nullptr, &m.moduleConfig, &m.settings,
                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr, &recovery),
          startupCheck(&m.hybridApi, &commandUseCase, &m.logger, &m.debugSystem, &recovery)
    {
    }
};

ModuleCommandDto updateFirmwareCommand(const char *uid = COMMAND_UID)
{
    ModuleCommandDto command;
    command.uid = uid;
    command.command = "UpdateFirmware";
    command.options =
        std::string("{\"url\":\"") + UPDATE_URL + "\",\"version\":\"" + UPDATE_VERSION + "\"}";
    command.createdAt = "";
    return command;
}
}

void test_update_with_http_down_restarts_before_downloading(void)
{
    ModuleState m;
    Boot boot(m);
    m.networkStatus.httpDown = true;

    auto results = boot.commandUseCase.executeCommands({updateFirmwareCommand()});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("InProgress", results[0].status.c_str());
    TEST_ASSERT_FALSE(m.firmwareUpdater.wasUpdateFromURLCalled());
    TEST_ASSERT_EQUAL(CONFIGURED_NEED_RESTART, m.moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_FIRMWARE_UPDATE, m.settings.getBootType());

    ModuleCommandDto kept;
    TEST_ASSERT_TRUE(m.storedUpdate(kept));
    TEST_ASSERT_EQUAL_STRING(COMMAND_UID, kept.uid.c_str());
    TEST_ASSERT_EQUAL_STRING("UpdateFirmware", kept.command.c_str());
    TEST_ASSERT_TRUE(kept.options.find(UPDATE_URL) != std::string::npos);
    TEST_ASSERT_TRUE(kept.options.find(UPDATE_VERSION) != std::string::npos);
}

void test_kept_update_does_not_touch_the_settings(void)
{
    ModuleState m;
    Boot boot(m);
    m.networkStatus.httpDown = true;

    boot.commandUseCase.executeCommands({updateFirmwareCommand()});

    TEST_ASSERT_EQUAL_UINT8(0, m.settings.getNeedUpdate());
    char link[BUFSIZE];
    link[0] = '\0';
    m.settings.getUpdateLink(link, BUFSIZE);
    TEST_ASSERT_EQUAL_STRING("http://example.com/update", link);
}

void test_update_with_http_up_downloads_without_restart(void)
{
    ModuleState m;
    Boot boot(m);
    m.networkStatus.httpDown = false;
    m.firmwareUpdater.setUpdateFromURLResult(true);

    auto results = boot.commandUseCase.executeCommands({updateFirmwareCommand()});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_TRUE(m.firmwareUpdater.wasUpdateFromURLCalled());

    ModuleCommandDto kept;
    TEST_ASSERT_FALSE(m.storedUpdate(kept));
}

void test_connection_refused_keeps_the_update_and_restarts(void)
{
    ModuleState m;
    Boot boot(m);
    m.firmwareUpdater.setUpdateFromURLResult(false);
    m.firmwareUpdater.setLastHttpCode(FIRMWARE_UPDATE_HTTP_NOT_CONNECTED);
    m.firmwareUpdater.setLastErrorMessage("HTTP code=-1");

    auto results = boot.commandUseCase.executeCommands({updateFirmwareCommand()});

    TEST_ASSERT_EQUAL_STRING("InProgress", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("", results[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(CONFIGURED_NEED_RESTART, m.moduleConfig.getRequestedModuleMode());

    ModuleCommandDto kept;
    TEST_ASSERT_TRUE(m.storedUpdate(kept));
    TEST_ASSERT_EQUAL_STRING(COMMAND_UID, kept.uid.c_str());
}

void test_server_error_is_reported_and_no_restart_is_asked(void)
{
    ModuleState m;
    Boot boot(m);
    m.firmwareUpdater.setUpdateFromURLResult(false);
    m.firmwareUpdater.setLastHttpCode(404);
    m.firmwareUpdater.setLastErrorMessage("HTTP code=404");

    auto results = boot.commandUseCase.executeCommands({updateFirmwareCommand()});

    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Failed to start firmware update: HTTP code=404",
                             results[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, m.moduleConfig.getRequestedModuleMode());

    ModuleCommandDto kept;
    TEST_ASSERT_FALSE(m.storedUpdate(kept));
}

void test_kept_update_is_finished_on_the_next_boot(void)
{
    ModuleState m;
    Boot boot(m);
    m.firmwareUpdater.setUpdateFromURLResult(false);
    m.firmwareUpdater.setLastHttpCode(FIRMWARE_UPDATE_HTTP_NOT_CONNECTED);
    boot.commandUseCase.executeCommands({updateFirmwareCommand()});

    m.hybridApi.reset();
    m.hybridApi.moduleCommandsReturnValue.clear();
    m.firmwareUpdater.reset();
    m.firmwareUpdater.setUpdateFromURLResult(true);

    Boot nextBoot(m);
    bool applied = nextBoot.startupCheck.run();

    TEST_ASSERT_TRUE(applied);
    TEST_ASSERT_EQUAL(1, m.firmwareUpdater.getUpdateFromURLCallCount());
    TEST_ASSERT_EQUAL_STRING(UPDATE_URL, m.firmwareUpdater.getLastUrl());
    TEST_ASSERT_TRUE(m.hybridApi.sendModuleCommandExecutionResultCalled);
    TEST_ASSERT_EQUAL(1, static_cast<int>(m.hybridApi.lastModuleCommandUpdates.size()));
    TEST_ASSERT_EQUAL_STRING(COMMAND_UID, m.hybridApi.lastModuleCommandUpdates[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("Done", m.hybridApi.lastModuleCommandUpdates[0].status.c_str());

    ModuleCommandDto kept;
    TEST_ASSERT_FALSE(m.storedUpdate(kept));
}

void test_resumed_update_that_still_cannot_connect_stops_at_one_restart(void)
{
    ModuleState m;
    Boot boot(m);
    m.firmwareUpdater.setUpdateFromURLResult(false);
    m.firmwareUpdater.setLastHttpCode(FIRMWARE_UPDATE_HTTP_NOT_CONNECTED);
    m.firmwareUpdater.setLastErrorMessage("HTTP code=-1");
    boot.commandUseCase.executeCommands({updateFirmwareCommand()});
    m.moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);

    m.hybridApi.reset();
    m.hybridApi.moduleCommandsReturnValue.clear();

    Boot nextBoot(m);
    bool applied = nextBoot.startupCheck.run();

    TEST_ASSERT_FALSE(applied);
    TEST_ASSERT_EQUAL_STRING("Error", m.hybridApi.lastModuleCommandUpdates[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Failed to start firmware update: HTTP code=-1",
                             m.hybridApi.lastModuleCommandUpdates[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, m.moduleConfig.getRequestedModuleMode());

    ModuleCommandDto kept;
    TEST_ASSERT_FALSE(m.storedUpdate(kept));
}

void test_second_update_does_not_arm_another_restart(void)
{
    ModuleState m;
    Boot boot(m);
    m.networkStatus.httpDown = true;

    boot.commandUseCase.executeCommands({updateFirmwareCommand()});
    m.moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
    m.firmwareUpdater.setUpdateFromURLResult(false);
    m.firmwareUpdater.setLastHttpCode(FIRMWARE_UPDATE_HTTP_NOT_CONNECTED);

    auto results = boot.commandUseCase.executeCommands({updateFirmwareCommand("second-command-uid")});

    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, m.moduleConfig.getRequestedModuleMode());

    ModuleCommandDto kept;
    TEST_ASSERT_TRUE(m.storedUpdate(kept));
    TEST_ASSERT_EQUAL_STRING(COMMAND_UID, kept.uid.c_str());
}

void test_kept_update_is_taken_once(void)
{
    ModuleState m;
    Boot boot(m);
    TEST_ASSERT_TRUE(boot.recovery.keepForNextBoot(UPDATE_URL, UPDATE_VERSION, COMMAND_UID));

    Boot afterRestart(m);
    ModuleCommandDto first;
    TEST_ASSERT_TRUE(afterRestart.recovery.takeKeptUpdate(first));
    TEST_ASSERT_EQUAL_STRING("UpdateFirmware", first.command.c_str());
    TEST_ASSERT_EQUAL_STRING(COMMAND_UID, first.uid.c_str());
    TEST_ASSERT_TRUE(first.createdAt.empty());

    Boot afterSecondRestart(m);
    ModuleCommandDto second;
    TEST_ASSERT_FALSE(afterSecondRestart.recovery.takeKeptUpdate(second));
}

void test_update_that_cannot_be_stored_is_not_kept(void)
{
    ModuleState m;
    Boot boot(m);
    m.spiffs.setShouldFail(true);

    TEST_ASSERT_FALSE(boot.recovery.keepForNextBoot(UPDATE_URL, UPDATE_VERSION, COMMAND_UID));
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, m.moduleConfig.getRequestedModuleMode());
}

void test_startup_check_without_a_kept_update_uses_the_server(void)
{
    ModuleState m;
    Boot boot(m);
    m.hybridApi.moduleCommandsReturnValue = {updateFirmwareCommand("server-command-uid")};
    m.firmwareUpdater.setUpdateFromURLResult(true);

    bool applied = boot.startupCheck.run();

    TEST_ASSERT_TRUE(applied);
    TEST_ASSERT_EQUAL_STRING("server-command-uid",
                             m.hybridApi.lastModuleCommandUpdates[0].uid.c_str());
}

extern "C" int run_firmware_update_recovery_tests(void)
{
    RUN_TEST(test_update_with_http_down_restarts_before_downloading);
    RUN_TEST(test_kept_update_does_not_touch_the_settings);
    RUN_TEST(test_update_with_http_up_downloads_without_restart);
    RUN_TEST(test_connection_refused_keeps_the_update_and_restarts);
    RUN_TEST(test_server_error_is_reported_and_no_restart_is_asked);
    RUN_TEST(test_kept_update_is_finished_on_the_next_boot);
    RUN_TEST(test_resumed_update_that_still_cannot_connect_stops_at_one_restart);
    RUN_TEST(test_second_update_does_not_arm_another_restart);
    RUN_TEST(test_kept_update_is_taken_once);
    RUN_TEST(test_update_that_cannot_be_stored_is_not_kept);
    RUN_TEST(test_startup_check_without_a_kept_update_uses_the_server);
    return 0;
}
