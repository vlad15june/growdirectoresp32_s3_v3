#include <unity.h>
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include <string.h>

// Rename the file-level variables to avoid potential conflicts
static MockSettings g_mockSettings;
static char g_buffer[256];

// These test functions will only be used in this file
static void test_default_values(void) {
    g_mockSettings.reset(); // Reset before test
    
    // Test default values for configuration status
    TEST_ASSERT_EQUAL(0, g_mockSettings.getIfConfigured());
    TEST_ASSERT_EQUAL(0, g_mockSettings.getNeedUpdate());
    TEST_ASSERT_EQUAL(0, g_mockSettings.getUpdateStatus());
    TEST_ASSERT_EQUAL(0, g_mockSettings.getBootType());
    
    // Test default values for rule and sensor configuration
    TEST_ASSERT_EQUAL(0, g_mockSettings.getRuleState());
    TEST_ASSERT_EQUAL(0, g_mockSettings.getSensorTypeDoOrp());
    TEST_ASSERT_EQUAL(NOT_CONNECTED, g_mockSettings.getSensorType(0));
    TEST_ASSERT_EQUAL(NOT_CONNECTED, g_mockSettings.getSensorType(1));
    TEST_ASSERT_EQUAL(NOT_CONNECTED, g_mockSettings.getSensorType(2));
    TEST_ASSERT_EQUAL(NOT_CONNECTED, g_mockSettings.getSensorType(3));
    TEST_ASSERT_EQUAL(0, g_mockSettings.getKeyTestMode());
    TEST_ASSERT_EQUAL(0, g_mockSettings.getTempCoefVersion());
    
    // Test default location values
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, g_mockSettings.getLatitude());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, g_mockSettings.getLongitude());
    
    // Test default string values
    g_mockSettings.getSsid(g_buffer);
    TEST_ASSERT_EQUAL_STRING("MockSSID", g_buffer);
    
    g_mockSettings.getPassword(g_buffer);
    TEST_ASSERT_EQUAL_STRING("MockPassword", g_buffer);
    
    g_mockSettings.getUser(g_buffer);
    TEST_ASSERT_EQUAL_STRING("MockUser", g_buffer);
    
    g_mockSettings.getToken(g_buffer);
    TEST_ASSERT_EQUAL_STRING("MockToken", g_buffer);
    
    g_mockSettings.getUpdateLink(g_buffer);
    TEST_ASSERT_EQUAL_STRING("http://example.com/update", g_buffer);
    
    g_mockSettings.getModuleCommandUid(g_buffer);
    TEST_ASSERT_EQUAL_STRING("mock-command-uid", g_buffer);
    
    g_mockSettings.getAccountUid(g_buffer);
    TEST_ASSERT_EQUAL_STRING("mock-account-uid", g_buffer);
    
    g_mockSettings.getServerUrl(g_buffer);
    TEST_ASSERT_EQUAL_STRING("mqtt.example.com", g_buffer);
}

static void test_set_and_get_byte_values(void) {
    g_mockSettings.reset(); // Reset before test
    
    // Test setting and getting configuration values
    g_mockSettings.setConfigured(1);
    TEST_ASSERT_EQUAL(1, g_mockSettings.getIfConfigured());
    
    g_mockSettings.setNeedUpdate(2);
    TEST_ASSERT_EQUAL(2, g_mockSettings.getNeedUpdate());
    
    g_mockSettings.setUpdateStatus(3);
    TEST_ASSERT_EQUAL(3, g_mockSettings.getUpdateStatus());
    
    g_mockSettings.setBootType(4);
    TEST_ASSERT_EQUAL(4, g_mockSettings.getBootType());
    
    g_mockSettings.setRuleState(5);
    TEST_ASSERT_EQUAL(5, g_mockSettings.getRuleState());
    
    g_mockSettings.setSensorTypeDoOrp(6);
    TEST_ASSERT_EQUAL(6, g_mockSettings.getSensorTypeDoOrp());
    
    g_mockSettings.setKeyTestMode(7);
    TEST_ASSERT_EQUAL(7, g_mockSettings.getKeyTestMode());
    
    g_mockSettings.setTempCoefVersion(8);
    TEST_ASSERT_EQUAL(8, g_mockSettings.getTempCoefVersion());
}

static void test_set_and_get_sensor_types(void) {
    g_mockSettings.reset(); // Reset before test
    
    // Test setting and getting sensor types
    g_mockSettings.setSensorType(1, 0);
    g_mockSettings.setSensorType(2, 1);
    g_mockSettings.setSensorType(3, 2);
    g_mockSettings.setSensorType(4, 3);
    
    TEST_ASSERT_EQUAL(1, g_mockSettings.getSensorType(0));
    TEST_ASSERT_EQUAL(2, g_mockSettings.getSensorType(1));
    TEST_ASSERT_EQUAL(3, g_mockSettings.getSensorType(2));
    TEST_ASSERT_EQUAL(4, g_mockSettings.getSensorType(3));
    
    // Test invalid channel
    TEST_ASSERT_EQUAL(NOT_CONNECTED, g_mockSettings.getSensorType(4));
}

static void test_set_and_get_location(void) {
    g_mockSettings.reset(); // Reset before test
    
    // Test setting and getting location values
    g_mockSettings.setLatitude(12.345f);
    g_mockSettings.setLongitude(67.890f);
    
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.345f, g_mockSettings.getLatitude());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 67.890f, g_mockSettings.getLongitude());
}

static void test_set_and_get_strings(void) {
    g_mockSettings.reset(); // Reset before test
    
    // Test setting and getting string values
    g_mockSettings.setSsid("TestSSID");
    g_mockSettings.getSsid(g_buffer);
    TEST_ASSERT_EQUAL_STRING("TestSSID", g_buffer);
    
    g_mockSettings.setPassword("TestPassword");
    g_mockSettings.getPassword(g_buffer);
    TEST_ASSERT_EQUAL_STRING("TestPassword", g_buffer);
    
    g_mockSettings.setUser("TestUser");
    g_mockSettings.getUser(g_buffer);
    TEST_ASSERT_EQUAL_STRING("TestUser", g_buffer);
    
    g_mockSettings.setToken("TestToken");
    g_mockSettings.getToken(g_buffer);
    TEST_ASSERT_EQUAL_STRING("TestToken", g_buffer);
    
    g_mockSettings.setUpdateLink("https://test.com/update");
    g_mockSettings.getUpdateLink(g_buffer);
    TEST_ASSERT_EQUAL_STRING("https://test.com/update", g_buffer);
    
    g_mockSettings.setModuleCommandUid("test-command-uid");
    g_mockSettings.getModuleCommandUid(g_buffer);
    TEST_ASSERT_EQUAL_STRING("test-command-uid", g_buffer);
    
    g_mockSettings.setAccountUid("test-account-uid");
    g_mockSettings.getAccountUid(g_buffer);
    TEST_ASSERT_EQUAL_STRING("test-account-uid", g_buffer);
    
    g_mockSettings.setServerUrl("mqtt.test.com");
    g_mockSettings.getServerUrl(g_buffer);
    TEST_ASSERT_EQUAL_STRING("mqtt.test.com", g_buffer);
}

static void test_local_time(void) {
    g_mockSettings.reset(); // Reset before test
    
    // Test setting and getting local time
    struct tm testTime;
    memset(&testTime, 0, sizeof(struct tm));
    testTime.tm_year = 123;  // 2023 (year since 1900)
    testTime.tm_mon = 11;    // December (0-based)
    testTime.tm_mday = 31;
    testTime.tm_hour = 23;
    testTime.tm_min = 59;
    testTime.tm_sec = 59;
    
    g_mockSettings.setLocalTime(testTime);
    struct tm resultTime = g_mockSettings.getLocalTime();
    
    TEST_ASSERT_EQUAL(123, resultTime.tm_year);
    TEST_ASSERT_EQUAL(11, resultTime.tm_mon);
    TEST_ASSERT_EQUAL(31, resultTime.tm_mday);
    TEST_ASSERT_EQUAL(23, resultTime.tm_hour);
    TEST_ASSERT_EQUAL(59, resultTime.tm_min);
    TEST_ASSERT_EQUAL(59, resultTime.tm_sec);
}

static void test_was_modified(void) {
    g_mockSettings.reset(); // Reset before test
    
    // Initially nothing should be modified
    TEST_ASSERT_FALSE(g_mockSettings.wasModified("isConfigured"));
    TEST_ASSERT_FALSE(g_mockSettings.wasModified("sensorType0"));
    TEST_ASSERT_FALSE(g_mockSettings.wasModified("latitude"));
    TEST_ASSERT_FALSE(g_mockSettings.wasModified("ssid"));
    
    // Modify values
    g_mockSettings.setConfigured(1);
    g_mockSettings.setSensorType(1, 0);
    g_mockSettings.setLatitude(12.345f);
    g_mockSettings.setSsid("TestSSID");
    
    // Check if modified
    TEST_ASSERT_TRUE(g_mockSettings.wasModified("isConfigured"));
    TEST_ASSERT_TRUE(g_mockSettings.wasModified("sensorType0"));
    TEST_ASSERT_TRUE(g_mockSettings.wasModified("latitude"));
    TEST_ASSERT_TRUE(g_mockSettings.wasModified("ssid"));
    
    // Reset should clear modifications
    g_mockSettings.reset();
    TEST_ASSERT_FALSE(g_mockSettings.wasModified("isConfigured"));
    TEST_ASSERT_FALSE(g_mockSettings.wasModified("sensorType0"));
    TEST_ASSERT_FALSE(g_mockSettings.wasModified("latitude"));
    TEST_ASSERT_FALSE(g_mockSettings.wasModified("ssid"));
}

// This function will be exported to be called from test_main.cpp
extern "C" void run_mock_settings_tests(void) {
    printf("\n----- Running MockSettings Tests -----\n");
    
    // Run all the MockSettings tests
    RUN_TEST(test_default_values);
    RUN_TEST(test_set_and_get_byte_values);
    RUN_TEST(test_set_and_get_sensor_types);
    RUN_TEST(test_set_and_get_location);
    RUN_TEST(test_set_and_get_strings);
    RUN_TEST(test_local_time);
    RUN_TEST(test_was_modified);
    
    printf("\n----- MockSettings Tests Complete -----\n");
}
