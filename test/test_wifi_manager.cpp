#include <unity.h>
#include <string>
#include <vector>
#include "Mock/ArduinoMock.h"
#include "Infrastructure/Adapters/WiFi/WiFiManager.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"

// Test fixtures
static MockSpiffsAdapter* mockSpiffs;
static MockWifiAdapter* mockWifiAdapter;
static MockSettings* mockSettings;
static JsonConverter* jsonConverter;
static WiFiManager* wifiManager;

void setUp_wifi_manager(void) {
    mockSpiffs = new MockSpiffsAdapter();
    mockWifiAdapter = new MockWifiAdapter();
    mockSettings = new MockSettings();
    jsonConverter = new JsonConverter();
    wifiManager = new WiFiManager(mockWifiAdapter, mockSettings, nullptr, nullptr, mockSpiffs, jsonConverter);
}

void tearDown_wifi_manager(void) {
    delete wifiManager;
    delete jsonConverter;
    delete mockSettings;
    delete mockWifiAdapter;
    delete mockSpiffs;

    wifiManager = nullptr;
    jsonConverter = nullptr;
    mockSettings = nullptr;
    mockWifiAdapter = nullptr;
    mockSpiffs = nullptr;
}

// After addWifiNetwork() the SPIFFS file contains the network
void test_add_wifi_saves_to_file(void) {
    setUp_wifi_manager();

    bool result = wifiManager->addWifiNetwork("TestSSID", "TestPass");

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(mockSpiffs->fileExists("/wifiNetworks.json"));
    std::string content = mockSpiffs->readSPIFFSstr("/wifiNetworks.json");
    TEST_ASSERT_TRUE(content.find("TestSSID") != std::string::npos);
    TEST_ASSERT_TRUE(content.find("TestPass") != std::string::npos);

    tearDown_wifi_manager();
}

// Calling addWifiNetwork() twice with the same SSID results in one entry
void test_add_wifi_deduplicates_by_ssid(void) {
    setUp_wifi_manager();

    wifiManager->addWifiNetwork("DupSSID", "Pass1");
    wifiManager->addWifiNetwork("DupSSID", "Pass2");

    std::string content = mockSpiffs->readSPIFFSstr("/wifiNetworks.json");
    // Count occurrences of "DupSSID"
    size_t count = 0;
    size_t pos = 0;
    while ((pos = content.find("DupSSID", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    TEST_ASSERT_EQUAL(1, count);

    tearDown_wifi_manager();
}

// Two different SSIDs accumulate - both end up in the file
void test_add_wifi_accumulates_multiple(void) {
    setUp_wifi_manager();

    wifiManager->addWifiNetwork("SSID_A", "PassA");
    wifiManager->addWifiNetwork("SSID_B", "PassB");

    std::string content = mockSpiffs->readSPIFFSstr("/wifiNetworks.json");
    TEST_ASSERT_TRUE(content.find("SSID_A") != std::string::npos);
    TEST_ASSERT_TRUE(content.find("SSID_B") != std::string::npos);

    tearDown_wifi_manager();
}

// addWifiNetwork() calls MockWifiAdapter::addNetwork once
void test_add_wifi_registers_with_adapter(void) {
    setUp_wifi_manager();

    mockWifiAdapter->reset();
    wifiManager->addWifiNetwork("RegSSID", "RegPass");

    TEST_ASSERT_TRUE(mockWifiAdapter->addNetworkCalled);

    tearDown_wifi_manager();
}

// Pre-populated SPIFFS file: setupWifiConnection() loads all networks into adapter
void test_setup_loads_from_file(void) {
    setUp_wifi_manager();

    // Pre-seed the SPIFFS file with two networks
    const char* fileContent = "[{\"ssid\":\"Net1\",\"password\":\"Pass1\"},{\"ssid\":\"Net2\",\"password\":\"Pass2\"}]";
    mockSpiffs->writeToFile("/wifiNetworks.json", fileContent);

    mockWifiAdapter->reset();
    mockWifiAdapter->connectToNetworksSuccess = true;
    wifiManager->setupWifiConnection();

    // Both networks should have been registered
    TEST_ASSERT_EQUAL(2, static_cast<int>(mockWifiAdapter->savedNetworks.size()));

    tearDown_wifi_manager();
}

// Empty SPIFFS -> fallback to EEPROM ssid/password
void test_setup_falls_back_to_eeprom_when_file_empty(void) {
    setUp_wifi_manager();

    // No file in SPIFFS
    mockWifiAdapter->reset();
    mockWifiAdapter->connectToNetworksSuccess = true;
    wifiManager->setupWifiConnection();

    // Must have called addNetwork exactly once (the EEPROM fallback)
    TEST_ASSERT_EQUAL(1, static_cast<int>(mockWifiAdapter->savedNetworks.size()));
    TEST_ASSERT_EQUAL_STRING("MockSSID", mockWifiAdapter->savedNetworks[0].ssid.c_str());

    tearDown_wifi_manager();
}

extern "C" int run_wifi_manager_tests(void) {
    RUN_TEST(test_add_wifi_saves_to_file);
    RUN_TEST(test_add_wifi_deduplicates_by_ssid);
    RUN_TEST(test_add_wifi_accumulates_multiple);
    RUN_TEST(test_add_wifi_registers_with_adapter);
    RUN_TEST(test_setup_loads_from_file);
    RUN_TEST(test_setup_falls_back_to_eeprom_when_file_empty);
    return 0;
}
