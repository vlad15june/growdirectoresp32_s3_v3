#include <unity.h>
#include <string>
#include <vector>
#include "Mock/ArduinoMock.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"

static MockWiFiManager* mockWiFiManager;
static MockLogger* mockLogger;
static MockDebugSystem* mockDebugSystem;
static ModuleCommandExecutionUseCase* useCase;

void setUp_module_command_execution_use_case(void) {
    mockWiFiManager = new MockWiFiManager();
    mockLogger = new MockLogger();
    mockDebugSystem = new MockDebugSystem();

    mockDebugSystem->registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);
    mockDebugSystem->setDebugFeatureState(DebugFeatures::MODULE_COMMAND_DEBUG, true);

    useCase = new ModuleCommandExecutionUseCase(
        mockLogger,      // logger
        mockDebugSystem, // debugSystem
        nullptr,         // timeAdapter
        nullptr,         // firmwareUpdater
        nullptr,         // arduinoAdapter
        nullptr,         // sensorRepository
        nullptr,         // sensorTypeRepository
        nullptr,         // sensorReader
        nullptr,         // moduleConfig
        nullptr,         // settings
        nullptr,         // hybridApiService
        nullptr,         // clearModuleDataService
        nullptr,         // moduleStructure
        nullptr,         // sensorValueModel
        mockWiFiManager  // wifiManager
    );
}

void tearDown_module_command_execution_use_case(void) {
    delete useCase;
    delete mockDebugSystem;
    delete mockLogger;
    delete mockWiFiManager;

    useCase = nullptr;
    mockDebugSystem = nullptr;
    mockLogger = nullptr;
    mockWiFiManager = nullptr;
}

// addWifiNetworkSuccess = true -> status DONE, mock was called with correct credentials
void test_add_wifi_success(void) {
    setUp_module_command_execution_use_case();

    mockWiFiManager->addWifiNetworkSuccess = true;

    ModuleCommandDto cmd;
    cmd.uid = "cmd-1";
    cmd.command = "AddWifi";
    cmd.options = "{\"ssid\":\"HomeNet\",\"password\":\"secret123\"}";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Done", results[0].status.c_str());
    TEST_ASSERT_TRUE(mockWiFiManager->addWifiNetworkCalled);
    TEST_ASSERT_EQUAL_STRING("HomeNet", mockWiFiManager->lastSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("secret123", mockWiFiManager->lastPassword.c_str());

    tearDown_module_command_execution_use_case();
}

// addWifiNetworkSuccess = false -> status ERROR
void test_add_wifi_failure(void) {
    setUp_module_command_execution_use_case();

    mockWiFiManager->addWifiNetworkSuccess = false;

    ModuleCommandDto cmd;
    cmd.uid = "cmd-2";
    cmd.command = "AddWifi";
    cmd.options = "{\"ssid\":\"BadNet\",\"password\":\"wrongpass\"}";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_TRUE(mockWiFiManager->addWifiNetworkCalled);

    tearDown_module_command_execution_use_case();
}

// Empty options -> status ERROR, mock not called
void test_add_wifi_missing_options(void) {
    setUp_module_command_execution_use_case();

    ModuleCommandDto cmd;
    cmd.uid = "cmd-3";
    cmd.command = "AddWifi";
    cmd.options = "";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_FALSE(mockWiFiManager->addWifiNetworkCalled);

    tearDown_module_command_execution_use_case();
}

// Options with only password -> status ERROR
void test_add_wifi_missing_ssid(void) {
    setUp_module_command_execution_use_case();

    ModuleCommandDto cmd;
    cmd.uid = "cmd-4";
    cmd.command = "AddWifi";
    cmd.options = "{\"password\":\"secret123\"}";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_FALSE(mockWiFiManager->addWifiNetworkCalled);

    tearDown_module_command_execution_use_case();
}

// Options with only ssid -> status ERROR
void test_add_wifi_missing_password(void) {
    setUp_module_command_execution_use_case();

    ModuleCommandDto cmd;
    cmd.uid = "cmd-5";
    cmd.command = "AddWifi";
    cmd.options = "{\"ssid\":\"HomeNet\"}";
    cmd.createdAt = "";

    auto results = useCase->executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
    TEST_ASSERT_FALSE(mockWiFiManager->addWifiNetworkCalled);

    tearDown_module_command_execution_use_case();
}

// nullptr wifiManager -> status ERROR
void test_add_wifi_no_wifi_manager(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.registerFeature(DebugFeatures::MODULE_COMMAND_DEBUG);

    ModuleCommandExecutionUseCase ucNoWifi(
        &logger, &debugSystem,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr  // wifiManager = nullptr
    );

    ModuleCommandDto cmd;
    cmd.uid = "cmd-6";
    cmd.command = "AddWifi";
    cmd.options = "{\"ssid\":\"HomeNet\",\"password\":\"secret123\"}";
    cmd.createdAt = "";

    auto results = ucNoWifi.executeCommands({cmd});

    TEST_ASSERT_EQUAL(1, static_cast<int>(results.size()));
    TEST_ASSERT_EQUAL_STRING("Error", results[0].status.c_str());
}

extern "C" int run_module_command_execution_use_case_tests(void) {
    RUN_TEST(test_add_wifi_success);
    RUN_TEST(test_add_wifi_failure);
    RUN_TEST(test_add_wifi_missing_options);
    RUN_TEST(test_add_wifi_missing_ssid);
    RUN_TEST(test_add_wifi_missing_password);
    RUN_TEST(test_add_wifi_no_wifi_manager);
    return 0;
}
