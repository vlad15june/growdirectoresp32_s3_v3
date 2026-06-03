#include <unity.h>
#include "Mock/ArduinoMock.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Infrastructure/Configuration/ModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"

static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockSettings g_mockSettings;
static MockNTPTimeAdapter g_mockTimeAdapter;
static ModuleConfig* g_moduleConfig;

static void module_config_setup(void) {
    g_mockLogger.printCalled = false;
    g_mockDebugSystem.featureEnabled = true;
    g_mockSettings.reset();
    g_mockTimeAdapter.reset();
    g_moduleConfig = new ModuleConfig(&g_mockLogger, &g_mockDebugSystem, &g_mockSettings, &g_mockTimeAdapter);
}

static void module_config_teardown(void) {
    delete g_moduleConfig;
    g_moduleConfig = nullptr;
}

static void test_default_module_types(void) {
    module_config_setup();
    TEST_ASSERT_EQUAL(MODULE_TYPE_DEVICE, g_moduleConfig->getModuleType());
    TEST_ASSERT_EQUAL(MODULE_SUBTYPE_RELAY, g_moduleConfig->getModuleSubType());
    module_config_teardown();
}

static void test_default_values(void) {
    module_config_setup();
    TEST_ASSERT_EQUAL(MODULE_TYPE_DEVICE, g_moduleConfig->getModuleType());
    TEST_ASSERT_EQUAL(MODULE_SUBTYPE_RELAY, g_moduleConfig->getModuleSubType());
    TEST_ASSERT_EQUAL(4, g_moduleConfig->getDeviceCount());
    TEST_ASSERT_EQUAL(4, g_moduleConfig->getLedsCount());
    module_config_teardown();
}

static void test_init_calls_dependencies(void) {
    module_config_setup();
    g_moduleConfig->init();
    TEST_ASSERT_TRUE(g_mockTimeAdapter.wasSetupCalled());
    TEST_ASSERT_TRUE(g_mockTimeAdapter.wasTimeRestored());
    module_config_teardown();
}

static void test_get_settings(void) {
    module_config_setup();
    ISettings* settings = g_moduleConfig->getSettings();
    TEST_ASSERT_EQUAL(&g_mockSettings, settings);
    module_config_teardown();
}

static void test_setters(void) {
    module_config_setup();
    MockLogger newLogger;
    MockSettings newSettings;
    MockNTPTimeAdapter newTimeAdapter;
    g_moduleConfig->setLogger(&newLogger);
    g_moduleConfig->setSettings(&newSettings);
    g_moduleConfig->setTimeAdapter(&newTimeAdapter);
    g_moduleConfig->init();
    TEST_ASSERT_TRUE(newTimeAdapter.wasSetupCalled());
    TEST_ASSERT_TRUE(newTimeAdapter.wasTimeRestored());
    TEST_ASSERT_EQUAL(&newSettings, g_moduleConfig->getSettings());
    TEST_ASSERT_EQUAL(&newTimeAdapter, g_moduleConfig->getTimeAdapter());
    module_config_teardown();
}

static void test_led_getters(void) {
    module_config_setup();
    TEST_ASSERT_NULL(g_moduleConfig->getRedLeds());
    TEST_ASSERT_NULL(g_moduleConfig->getGreenLeds());
    TEST_ASSERT_EQUAL(4, g_moduleConfig->getLedsCount());
    g_moduleConfig->init();
    
    // After initialization, LED arrays should be initialized
    TEST_ASSERT_NOT_NULL(g_moduleConfig->getRedLeds());
    TEST_ASSERT_NOT_NULL(g_moduleConfig->getGreenLeds());
    TEST_ASSERT_EQUAL(4, g_moduleConfig->getLedsCount());
    
    // For default config (MODULE_TYPE_DEVICE, MODULE_SUBTYPE_RELAY), 
    // verify the LED pin values match the static definitions
    uint8_t* redLeds = g_moduleConfig->getRedLeds();
    uint8_t* greenLeds = g_moduleConfig->getGreenLeds();
    
    // Expected values from ModuleConfig.h static arrays
    // uint8_t ModuleConfig::LED_R_RELAY[4] = {15, 14, 13, 12};
    // uint8_t ModuleConfig::LED_G_RELAY[4] = {35, 36, 37, 38};
    TEST_ASSERT_EQUAL(15, redLeds[0]);
    TEST_ASSERT_EQUAL(14, redLeds[1]);
    TEST_ASSERT_EQUAL(13, redLeds[2]);
    TEST_ASSERT_EQUAL(12, redLeds[3]);

    TEST_ASSERT_EQUAL(35, greenLeds[0]);
    TEST_ASSERT_EQUAL(36, greenLeds[1]);
    TEST_ASSERT_EQUAL(37, greenLeds[2]);
    TEST_ASSERT_EQUAL(38, greenLeds[3]);
    
    module_config_teardown();
}

static void test_all_module_types(void) {
    struct TestCase {
        int cid0Value;
        int cid1Value;
        int cid2Value;
        int expectedModuleType;
        int expectedModuleSubType;
        int expectedDeviceCount;
        int expectedLedsCount;
        uint8_t* expectedRedLeds;
        uint8_t* expectedGreenLeds;
    };
    
    // Pin values for different module types - based on the if conditions in configureModuleType()
    // code 1: MODULE_TYPE_DEVICE, MODULE_SUBTYPE_RELAY
    // code 2: MODULE_TYPE_DEVICE, MODULE_SUBTYPE_SOKET
    // code 3: MODULE_TYPE_SENSOR, MODULE_SUBTYPE_NONE
    // code 4: MODULE_TYPE_HYDRO, MODULE_SUBTYPE_NONE
    // code 5: MODULE_TYPE_ANALOG_INPUT_DEVICE, MODULE_SUBTYPE_DIMMER
    
    // Expected LED values from static definitions in ModuleConfig.h
    uint8_t expectedRelayRed[4] = {15, 14, 13, 12};
    uint8_t expectedRelayGreen[4] = {35, 36, 37, 38};
    uint8_t expectedSktRed[2] = {38, 37};
    uint8_t expectedSktGreen[2] = {40, 39};
    uint8_t expectedHydroRed[3] = {16, 8, 34};
    uint8_t expectedHydroGreen[3] = {13, 14, 12};
    uint8_t expectedDimmerRed[2] = {42, 41};
    uint8_t expectedDimmerGreen[2] = {40, 39};
    TestCase testCases[] = {
        {0, 0, 0, MODULE_TYPE_DEVICE, MODULE_SUBTYPE_RELAY, 4, 4, expectedRelayRed, expectedRelayGreen},
        {1, 0, 0, MODULE_TYPE_DEVICE, MODULE_SUBTYPE_RELAY, 4, 4, expectedRelayRed, expectedRelayGreen},
        {0, 1, 0, MODULE_TYPE_DEVICE, MODULE_SUBTYPE_SOKET, 2, 2, expectedSktRed, expectedSktGreen},
        {1, 1, 0, MODULE_TYPE_SENSOR, MODULE_SUBTYPE_NONE, 0, 1, nullptr, nullptr},
        {0, 0, 1, MODULE_TYPE_HYDRO, MODULE_SUBTYPE_NONE, 3, 3, expectedHydroRed, expectedHydroGreen},
        {1, 0, 1, MODULE_TYPE_ANALOG_INPUT_DEVICE, MODULE_SUBTYPE_DIMMER, 2, 2, expectedDimmerRed, expectedDimmerGreen}
    };
    for (const auto& testCase : testCases) {
        module_config_setup();
        
        // Set mock digital read values for CID pins
        // Code is calculated as: digitalRead(CID_0) + digitalRead(CID_1) * 2 + digitalRead(CID_2) * 4
        setDigitalReadMockValue(CID_0, testCase.cid0Value);
        setDigitalReadMockValue(CID_1, testCase.cid1Value);
        setDigitalReadMockValue(CID_2, testCase.cid2Value);
        int expectedCode = testCase.cid0Value + testCase.cid1Value * 2 + testCase.cid2Value * 4;
        g_moduleConfig->init();
        char errorMsg[100];
        snprintf(errorMsg, sizeof(errorMsg), "Failed for code %d", expectedCode);
        TEST_ASSERT_EQUAL_MESSAGE(testCase.expectedModuleType, g_moduleConfig->getModuleType(), errorMsg);
        TEST_ASSERT_EQUAL_MESSAGE(testCase.expectedModuleSubType, g_moduleConfig->getModuleSubType(), errorMsg);
        TEST_ASSERT_EQUAL_MESSAGE(testCase.expectedDeviceCount, g_moduleConfig->getDeviceCount(), errorMsg);
        TEST_ASSERT_EQUAL_MESSAGE(testCase.expectedLedsCount, g_moduleConfig->getLedsCount(), errorMsg);
        TEST_ASSERT_NOT_NULL_MESSAGE(g_moduleConfig->getRedLeds(), errorMsg);
        TEST_ASSERT_NOT_NULL_MESSAGE(g_moduleConfig->getGreenLeds(), errorMsg);
        if (testCase.expectedModuleType == MODULE_TYPE_SENSOR) {
            TEST_ASSERT_EQUAL_MESSAGE(12, g_moduleConfig->getRedLeds()[0], errorMsg);  // LED_R_Sensor
            TEST_ASSERT_EQUAL_MESSAGE(13, g_moduleConfig->getGreenLeds()[0], errorMsg); // LED_G_Sensor
        } 
        // For other types, verify all LED values
        else if (testCase.expectedRedLeds != nullptr && testCase.expectedGreenLeds != nullptr) {
            uint8_t* actualRedLeds = g_moduleConfig->getRedLeds();
            uint8_t* actualGreenLeds = g_moduleConfig->getGreenLeds();
            
            for (int i = 0; i < testCase.expectedLedsCount; i++) {
                char pinErrorMsg[120];
                snprintf(pinErrorMsg, sizeof(pinErrorMsg), "Red LED[%d] mismatch for code %d", i, expectedCode);
                TEST_ASSERT_EQUAL_MESSAGE(testCase.expectedRedLeds[i], actualRedLeds[i], pinErrorMsg);
                
                snprintf(pinErrorMsg, sizeof(pinErrorMsg), "Green LED[%d] mismatch for code %d", i, expectedCode);
                TEST_ASSERT_EQUAL_MESSAGE(testCase.expectedGreenLeds[i], actualGreenLeds[i], pinErrorMsg);
            }
        }
        module_config_teardown();
    }
}

static void test_module_mode(void) {
    module_config_setup();
    
    // Test requested module mode
    ModuleModeEnum defaultMode = g_moduleConfig->getRequestedModuleMode();
    g_moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_CONFIGURE);
    TEST_ASSERT_EQUAL(ModuleModeEnum::USER_CAN_CONFIGURE, g_moduleConfig->getRequestedModuleMode());
    g_moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
    TEST_ASSERT_EQUAL(ModuleModeEnum::USER_CAN_RE_CONFIGURE, g_moduleConfig->getRequestedModuleMode());
    g_moduleConfig->setRequestedModuleMode(ModuleModeEnum::AP_TEST_MODE);
    TEST_ASSERT_EQUAL(ModuleModeEnum::AP_TEST_MODE, g_moduleConfig->getRequestedModuleMode());
    g_moduleConfig->setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, g_moduleConfig->getRequestedModuleMode());
    g_moduleConfig->setRequestedModuleMode(ModuleModeEnum::CONFIGURED_NEED_RESTART);
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_NEED_RESTART, g_moduleConfig->getRequestedModuleMode());
    
    // Test current module mode
    ModuleModeEnum defaultCurrentMode = g_moduleConfig->getCurrentModuleMode();
    g_moduleConfig->setCurrentModuleMode(ModuleModeEnum::USER_CAN_CONFIGURE);
    TEST_ASSERT_EQUAL(ModuleModeEnum::USER_CAN_CONFIGURE, g_moduleConfig->getCurrentModuleMode());
    g_moduleConfig->setCurrentModuleMode(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
    TEST_ASSERT_EQUAL(ModuleModeEnum::USER_CAN_RE_CONFIGURE, g_moduleConfig->getCurrentModuleMode());
    g_moduleConfig->setCurrentModuleMode(ModuleModeEnum::AP_TEST_MODE);
    TEST_ASSERT_EQUAL(ModuleModeEnum::AP_TEST_MODE, g_moduleConfig->getCurrentModuleMode());
    g_moduleConfig->setCurrentModuleMode(ModuleModeEnum::CONFIGURED_DONE);
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_DONE, g_moduleConfig->getCurrentModuleMode());
    g_moduleConfig->setCurrentModuleMode(ModuleModeEnum::CONFIGURED_NEED_RESTART);
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_NEED_RESTART, g_moduleConfig->getCurrentModuleMode());
    
    module_config_teardown();
}

static void test_sensor_pin_count(void) {
    module_config_setup();
    TEST_ASSERT_EQUAL(0, g_moduleConfig->getSensorPinCount());
    setDigitalReadMockValue(CID_0, 1);
    setDigitalReadMockValue(CID_1, 1);
    setDigitalReadMockValue(CID_2, 0);
    g_moduleConfig->init();
    TEST_ASSERT_EQUAL(MODULE_TYPE_SENSOR, g_moduleConfig->getModuleType());
    TEST_ASSERT_EQUAL(SENSOR_PIN_COUNT, g_moduleConfig->getSensorPinCount());
    module_config_teardown();
    module_config_setup();
    setDigitalReadMockValue(CID_0, 0);
    setDigitalReadMockValue(CID_1, 0);
    setDigitalReadMockValue(CID_2, 1);
    g_moduleConfig->init();
    TEST_ASSERT_EQUAL(MODULE_TYPE_HYDRO, g_moduleConfig->getModuleType());
    TEST_ASSERT_EQUAL(SENSOR_PIN_COUNT, g_moduleConfig->getSensorPinCount());
    module_config_teardown();
}

static void test_firmware_version(void) {
    module_config_setup();
    const char* firmwareVersion = g_moduleConfig->getFirmwareVersion();
    TEST_ASSERT_NOT_NULL(firmwareVersion);
    TEST_ASSERT_TRUE(strlen(firmwareVersion) > 0);
    TEST_ASSERT_EQUAL_STRING(FIRMWARE_VERSION, firmwareVersion);
    module_config_teardown();
}

static void test_module_uid(void) {
    module_config_setup();
    const char* defaultUid = g_moduleConfig->getModuleUid();
    TEST_ASSERT_EQUAL_STRING("default_uid", defaultUid);
    module_config_teardown();
}

extern "C" int run_module_config_tests(void) {
    printf("\n----- Running ModuleConfig Tests -----\n");
    RUN_TEST(test_default_module_types);
    RUN_TEST(test_default_values);
    RUN_TEST(test_init_calls_dependencies);
    RUN_TEST(test_get_settings);
    RUN_TEST(test_setters);
    RUN_TEST(test_led_getters);
    RUN_TEST(test_all_module_types);
    RUN_TEST(test_module_mode);
    RUN_TEST(test_sensor_pin_count);
    RUN_TEST(test_firmware_version);
    RUN_TEST(test_module_uid);
    printf("\n----- ModuleConfig Tests Complete -----\n");
    return 0;
}
