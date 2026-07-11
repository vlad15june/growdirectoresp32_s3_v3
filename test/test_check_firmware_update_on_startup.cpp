#include <unity.h>
#include <string>
#include <vector>
#include "Mock/ArduinoMock.h"
#include "Core/Domain/UseCases/ModuleMode/CheckFirmwareUpdateOnStartupUseCase.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Core/Domain/MockLedUseCases.h"
#include "Mock/UseCases/ModuleCommand/MockModuleCommandExecutionUseCase.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"

static MockHybridAPIService g_mockHybrid;
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockModuleCommandExecutionUseCase g_mockModuleCommand;
static CheckFirmwareUpdateOnStartupUseCase* g_useCase;

static ModuleCommandDto makeCommand(const std::string& uid, const std::string& command) {
    ModuleCommandDto dto;
    dto.uid = uid;
    dto.command = command;
    dto.options = "{\"url\":\"http://x/fw.bin\",\"version\":\"2.0.0\"}";
    dto.createdAt = "2021-01-01T00:00:00Z";
    return dto;
}

static void firmware_update_setup(void) {
    g_mockHybrid.reset();
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockDebugSystem.featureEnabled = true;
    g_mockModuleCommand.reset();
    g_useCase = new CheckFirmwareUpdateOnStartupUseCase(
        &g_mockHybrid, &g_mockModuleCommand, &g_mockLogger, &g_mockDebugSystem);
}

static void firmware_update_teardown(void) {
    delete g_useCase;
    g_useCase = nullptr;
}

void test_run_fresh_update_firmware_applied(void) {
    firmware_update_setup();

    g_mockHybrid.moduleCommandsReturnValue = { makeCommand("cmd-1", "UpdateFirmware") };
    g_mockModuleCommand.executeCommandsReturnValue = {
        ModuleCommandExecutionResult("cmd-1", "Done", "")
    };

    bool applied = g_useCase->run();

    TEST_ASSERT_TRUE(applied);
    TEST_ASSERT_TRUE(g_mockModuleCommand.executeCommandsCalled);
    TEST_ASSERT_EQUAL(1, static_cast<int>(g_mockModuleCommand.lastExecutedCommands.size()));
    TEST_ASSERT_EQUAL_STRING("UpdateFirmware", g_mockModuleCommand.lastExecutedCommands[0].command.c_str());
    TEST_ASSERT_TRUE(g_mockHybrid.sendModuleCommandExecutionResultCalled);
    TEST_ASSERT_EQUAL(1, static_cast<int>(g_mockHybrid.lastModuleCommandUpdates.size()));
    TEST_ASSERT_EQUAL_STRING("Done", g_mockHybrid.lastModuleCommandUpdates[0].status.c_str());

    firmware_update_teardown();
}

void test_run_no_update_firmware(void) {
    firmware_update_setup();

    g_mockHybrid.moduleCommandsReturnValue = { makeCommand("cmd-2", "BlinkingStart") };

    bool applied = g_useCase->run();

    TEST_ASSERT_FALSE(applied);
    TEST_ASSERT_FALSE(g_mockModuleCommand.executeCommandsCalled);
    TEST_ASSERT_FALSE(g_mockHybrid.sendModuleCommandExecutionResultCalled);

    firmware_update_teardown();
}

void test_run_empty_command_list(void) {
    firmware_update_setup();

    g_mockHybrid.moduleCommandsReturnValue.clear();

    bool applied = g_useCase->run();

    TEST_ASSERT_FALSE(applied);
    TEST_ASSERT_FALSE(g_mockModuleCommand.executeCommandsCalled);

    firmware_update_teardown();
}

void test_run_expired_update_firmware(void) {
    firmware_update_setup();

    g_mockHybrid.moduleCommandsReturnValue = { makeCommand("cmd-3", "UpdateFirmware") };
    g_mockModuleCommand.executeCommandsReturnValue = {
        ModuleCommandExecutionResult("cmd-3", "Error", "Command is expired")
    };

    bool applied = g_useCase->run();

    TEST_ASSERT_FALSE(applied);
    TEST_ASSERT_TRUE(g_mockModuleCommand.executeCommandsCalled);
    TEST_ASSERT_TRUE(g_mockHybrid.sendModuleCommandExecutionResultCalled);
    TEST_ASSERT_EQUAL_STRING("Error", g_mockHybrid.lastModuleCommandUpdates[0].status.c_str());

    firmware_update_teardown();
}

void test_run_null_dependencies(void) {
    CheckFirmwareUpdateOnStartupUseCase useCase(nullptr, nullptr, &g_mockLogger, &g_mockDebugSystem);
    TEST_ASSERT_FALSE(useCase.run());
}

void test_module_command_expired_dropped(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockNTPTimeAdapter timeAdapter;
    MockLedUseCases ledUseCases;
    timeAdapter.setCommandTimeValid(false);

    ModuleCommandExecutionUseCase useCase(
        &logger, &debugSystem, &timeAdapter,
        nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, &ledUseCases);

    ModuleCommandDto cmd;
    cmd.uid = "cmd-exp";
    cmd.command = "BlinkingStart";
    cmd.options = "";
    cmd.createdAt = "2021-01-01T00:00:00Z";

    auto results = useCase.executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_EQUAL_STRING("Command is expired", results[0].errorMessage.c_str());
    TEST_ASSERT_TRUE(timeAdapter.wasCommandTimeValidCalled());
    TEST_ASSERT_EQUAL(300, static_cast<int>(timeAdapter.getLastCommandTimeMaxAge()));

    TEST_ASSERT_FALSE(ledUseCases.setBlinkingCalled);
}

void test_module_command_fresh_passes(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockNTPTimeAdapter timeAdapter;
    MockLedUseCases ledUseCases;
    timeAdapter.setCommandTimeValid(true);

    ModuleCommandExecutionUseCase useCase(
        &logger, &debugSystem, &timeAdapter,
        nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, &ledUseCases);

    ModuleCommandDto cmd;
    cmd.uid = "cmd-fresh";
    cmd.command = "BlinkingStart";
    cmd.options = "";
    cmd.createdAt = "2021-01-01T00:00:00Z";

    auto results = useCase.executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_TRUE(timeAdapter.wasCommandTimeValidCalled());
    TEST_ASSERT_TRUE(ledUseCases.setBlinkingCalled);
}

extern "C" int run_check_firmware_update_on_startup_tests(void) {
    RUN_TEST(test_run_fresh_update_firmware_applied);
    RUN_TEST(test_run_no_update_firmware);
    RUN_TEST(test_run_empty_command_list);
    RUN_TEST(test_run_expired_update_firmware);
    RUN_TEST(test_run_null_dependencies);
    RUN_TEST(test_module_command_expired_dropped);
    RUN_TEST(test_module_command_fresh_passes);
    return 0;
}
