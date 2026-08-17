// GDV1-3834. A firmware update can fail because the module cannot open a connection at all
// (`GET code=-1`), and it keeps failing that way until it is rebooted: on 2026-08-16 module
// 9C:13:9E:99:41:6C answered three attempts with `Failed to start firmware update` in 134-167 ms,
// while five other modules downloaded the same image the same morning. The module has to restart
// itself and finish the update afterwards, without the user pressing Update a second time.

#include <unity.h>
#include <string>
#include <vector>
#include "Mock/ArduinoMock.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/CheckFirmwareUpdateOnStartupUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/FirmwareUpdateRecoveryUseCase.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/FirmwareUpdateStateEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Mock/Core/Domain/MockFirmwareRemoteUpdate.h"
#include "Mock/Core/Domain/MockNetworkStatusProvider.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Application/DTOs/ModuleCommandDto.h"

static const char *UPDATE_URL =
    "https://api-test.growdirector.com/api/v1/Firmware/file/39d4d2a7ea7f4d78a46d1e4159624a5f";
static const char *UPDATE_VERSION = "2.18.6";
static const char *COMMAND_UID = "48cba456-5a01-4e6c-a9bb-970544fd9f71";

namespace
{
// One module worth of collaborators, so every test drives the real use cases and only the edges
// (network state, HTTP result, server) are mocks.
struct UpdateFixture
{
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockFirmwareRemoteUpdate firmwareUpdater;
    MockNetworkStatusProvider networkStatus;
    MockHybridAPIService hybridApi;
    FirmwareUpdateRecoveryUseCase recovery;
    ModuleCommandExecutionUseCase commandUseCase;
    CheckFirmwareUpdateOnStartupUseCase startupCheck;

    UpdateFixture()
        : recovery(&networkStatus, &moduleConfig, &settings, &logger, &debugSystem),
          commandUseCase(&logger, &debugSystem, nullptr, &firmwareUpdater,
                         nullptr, nullptr, nullptr, nullptr, &moduleConfig, &settings,
                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr, &recovery),
          startupCheck(&hybridApi, &commandUseCase, &logger, &debugSystem, &recovery)
    {
        moduleConfig.setSettings(&settings);
        moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
        settings.setNeedUpdate(0);
        settings.setBootType(BOOT_TYPE_POWER_ON);
        settings.setNoScanSensors(0);
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

std::string keptUrl(MockSettings &settings)
{
    char buffer[UPDATE_LINK_BUFSIZE];
    buffer[0] = '\0';
    settings.getUpdateLink(buffer, UPDATE_LINK_BUFSIZE);
    return std::string(buffer);
}

std::string keptCommandUid(MockSettings &settings)
{
    char buffer[BUFSIZE];
    buffer[0] = '\0';
    settings.getModuleCommandUid(buffer, BUFSIZE);
    return std::string(buffer);
}
} // namespace

// Requirement 1: the connection state the module already reports is what decides whether the
// update may start at all. `httpDown` is the flag from the OFFLINE_INDICATION line.
void test_update_with_http_down_restarts_before_downloading(void)
{
    UpdateFixture f;
    f.networkStatus.httpDown = true;

    auto results = f.commandUseCase.executeCommands({updateFirmwareCommand()});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("InProgress", results[0].status.c_str());
    TEST_ASSERT_FALSE(f.firmwareUpdater.wasUpdateFromURLCalled());
    TEST_ASSERT_EQUAL(CONFIGURED_NEED_RESTART, f.moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_FIRMWARE_UPDATE, f.settings.getBootType());
    TEST_ASSERT_EQUAL_UINT8(1, f.settings.getNeedUpdate());
    TEST_ASSERT_EQUAL_UINT8(FIRMWARE_UPDATE_STATE_IN_PROGRESS, f.settings.getUpdateStatus());
    TEST_ASSERT_EQUAL_STRING(UPDATE_URL, keptUrl(f.settings).c_str());
    TEST_ASSERT_EQUAL_STRING(COMMAND_UID, keptCommandUid(f.settings).c_str());
}

// A usable link is left alone: no restart, the download starts right away.
void test_update_with_http_up_downloads_without_restart(void)
{
    UpdateFixture f;
    f.networkStatus.httpDown = false;
    f.firmwareUpdater.setUpdateFromURLResult(true);

    auto results = f.commandUseCase.executeCommands({updateFirmwareCommand()});

    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_TRUE(f.firmwareUpdater.wasUpdateFromURLCalled());
    TEST_ASSERT_EQUAL_UINT8(0, f.settings.getNeedUpdate());
}

// Requirement 2: the link can also die on the request itself. code=-1 means the connection was
// never opened - the failure a restart has always cured.
void test_connection_refused_keeps_the_update_and_restarts(void)
{
    UpdateFixture f;
    f.firmwareUpdater.setUpdateFromURLResult(false);
    f.firmwareUpdater.setLastHttpCode(FIRMWARE_UPDATE_HTTP_NOT_CONNECTED);
    f.firmwareUpdater.setLastErrorMessage("HTTP code=-1");

    auto results = f.commandUseCase.executeCommands({updateFirmwareCommand()});

    TEST_ASSERT_EQUAL_STRING("InProgress", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("", results[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(CONFIGURED_NEED_RESTART, f.moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(1, f.settings.getNeedUpdate());
    TEST_ASSERT_EQUAL_STRING(UPDATE_URL, keptUrl(f.settings).c_str());
}

// An answer from the server is not a broken link: a restart would change nothing, so the failure
// is reported as before and the module keeps running.
void test_server_error_is_reported_and_no_restart_is_asked(void)
{
    UpdateFixture f;
    f.firmwareUpdater.setUpdateFromURLResult(false);
    f.firmwareUpdater.setLastHttpCode(404);
    f.firmwareUpdater.setLastErrorMessage("HTTP code=404");

    auto results = f.commandUseCase.executeCommands({updateFirmwareCommand()});

    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Failed to start firmware update: HTTP code=404",
                             results[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, f.moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, f.settings.getNeedUpdate());
}

// The whole point of the ticket: after the restart the module finishes the update by itself, and
// the server sees the same command uid reach Done - nobody presses Update again.
void test_kept_update_is_finished_on_the_next_boot(void)
{
    UpdateFixture f;
    f.firmwareUpdater.setUpdateFromURLResult(false);
    f.firmwareUpdater.setLastHttpCode(FIRMWARE_UPDATE_HTTP_NOT_CONNECTED);
    f.commandUseCase.executeCommands({updateFirmwareCommand()});

    // Next boot: the server has nothing New left, the update comes from the module itself.
    f.hybridApi.reset();
    f.hybridApi.moduleCommandsReturnValue.clear();
    f.firmwareUpdater.reset();
    f.firmwareUpdater.setUpdateFromURLResult(true);

    bool applied = f.startupCheck.run();

    TEST_ASSERT_TRUE(applied);
    TEST_ASSERT_EQUAL(1, f.firmwareUpdater.getUpdateFromURLCallCount());
    TEST_ASSERT_EQUAL_STRING(UPDATE_URL, f.firmwareUpdater.getLastUrl());
    TEST_ASSERT_TRUE(f.hybridApi.sendModuleCommandExecutionResultCalled);
    TEST_ASSERT_EQUAL(1, static_cast<int>(f.hybridApi.lastModuleCommandUpdates.size()));
    TEST_ASSERT_EQUAL_STRING(COMMAND_UID, f.hybridApi.lastModuleCommandUpdates[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("Done", f.hybridApi.lastModuleCommandUpdates[0].status.c_str());
    TEST_ASSERT_EQUAL_UINT8(0, f.settings.getNeedUpdate());
}

// The bound on restarts: a module with no link at all gets exactly one extra boot per press of
// Update, then the failure is reported like any other.
void test_resumed_update_that_still_cannot_connect_stops_at_one_restart(void)
{
    UpdateFixture f;
    f.firmwareUpdater.setUpdateFromURLResult(false);
    f.firmwareUpdater.setLastHttpCode(FIRMWARE_UPDATE_HTTP_NOT_CONNECTED);
    f.firmwareUpdater.setLastErrorMessage("HTTP code=-1");
    f.commandUseCase.executeCommands({updateFirmwareCommand()});
    f.moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);

    f.hybridApi.reset();
    f.hybridApi.moduleCommandsReturnValue.clear();

    bool applied = f.startupCheck.run();

    TEST_ASSERT_FALSE(applied);
    TEST_ASSERT_EQUAL_STRING("Error", f.hybridApi.lastModuleCommandUpdates[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Failed to start firmware update: HTTP code=-1",
                             f.hybridApi.lastModuleCommandUpdates[0].errorMessage.c_str());
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, f.moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(0, f.settings.getNeedUpdate());
}

// A second update command while one is already being carried must not add a second restart.
void test_second_update_does_not_arm_another_restart(void)
{
    UpdateFixture f;
    f.networkStatus.httpDown = true;

    f.commandUseCase.executeCommands({updateFirmwareCommand()});
    f.moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
    f.firmwareUpdater.setUpdateFromURLResult(false);
    f.firmwareUpdater.setLastHttpCode(FIRMWARE_UPDATE_HTTP_NOT_CONNECTED);

    auto results = f.commandUseCase.executeCommands({updateFirmwareCommand("second-command-uid")});

    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, f.moduleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_STRING(COMMAND_UID, keptCommandUid(f.settings).c_str());
}

// A boot that died in the middle of the resumed download comes back to a status that is no longer
// in progress, and the update is dropped instead of being tried forever.
void test_kept_update_is_taken_once(void)
{
    UpdateFixture f;
    TEST_ASSERT_TRUE(f.recovery.keepForNextBoot(UPDATE_URL, UPDATE_VERSION, COMMAND_UID));

    ModuleCommandDto first;
    TEST_ASSERT_TRUE(f.recovery.takeKeptUpdate(first));
    TEST_ASSERT_EQUAL_STRING("UpdateFirmware", first.command.c_str());
    TEST_ASSERT_EQUAL_STRING(COMMAND_UID, first.uid.c_str());
    TEST_ASSERT_TRUE(first.createdAt.empty());

    ModuleCommandDto second;
    TEST_ASSERT_FALSE(f.recovery.takeKeptUpdate(second));
    TEST_ASSERT_EQUAL_UINT8(0, f.settings.getNeedUpdate());
}

// The url lives in a fixed-size settings field; one that does not fit is not promised a restart.
void test_url_that_does_not_fit_the_settings_is_not_kept(void)
{
    UpdateFixture f;
    std::string longUrl(UPDATE_LINK_BUFSIZE, 'u');

    TEST_ASSERT_FALSE(f.recovery.keepForNextBoot(longUrl, UPDATE_VERSION, COMMAND_UID));
    TEST_ASSERT_EQUAL_UINT8(0, f.settings.getNeedUpdate());
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, f.moduleConfig.getRequestedModuleMode());
}

// Nothing kept - the startup check works exactly as it did, off the commands the server has.
void test_startup_check_without_a_kept_update_uses_the_server(void)
{
    UpdateFixture f;
    f.hybridApi.moduleCommandsReturnValue = {updateFirmwareCommand("server-command-uid")};
    f.firmwareUpdater.setUpdateFromURLResult(true);

    bool applied = f.startupCheck.run();

    TEST_ASSERT_TRUE(applied);
    TEST_ASSERT_EQUAL_STRING("server-command-uid",
                             f.hybridApi.lastModuleCommandUpdates[0].uid.c_str());
}

extern "C" int run_firmware_update_recovery_tests(void)
{
    RUN_TEST(test_update_with_http_down_restarts_before_downloading);
    RUN_TEST(test_update_with_http_up_downloads_without_restart);
    RUN_TEST(test_connection_refused_keeps_the_update_and_restarts);
    RUN_TEST(test_server_error_is_reported_and_no_restart_is_asked);
    RUN_TEST(test_kept_update_is_finished_on_the_next_boot);
    RUN_TEST(test_resumed_update_that_still_cannot_connect_stops_at_one_restart);
    RUN_TEST(test_second_update_does_not_arm_another_restart);
    RUN_TEST(test_kept_update_is_taken_once);
    RUN_TEST(test_url_that_does_not_fit_the_settings_is_not_kept);
    RUN_TEST(test_startup_check_without_a_kept_update_uses_the_server);
    return 0;
}
