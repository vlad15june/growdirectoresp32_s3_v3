#include <unity.h>
#include "Application/DTOs/UpdateDto.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"

// Sample update JSON for testing
static const char* test_update_json = R"({
    "GrowdirectorUid":"58:CF:79:A4:77:32",
    "email":"david@raiz.farm",
    "accountUid":"1d1e2e92-c30f-62bb-8e55-8032f8315edf",
    "time":"2025-05-11T19:49:52.632Z",
    "millis":555894690,
    "devices":[
        {
            "uid":"58:CF:79:A4:77:32.pm.00",
            "switchedAt":"2025-05-07T06:33:35.658Z",
            "state":"off",
            "state_hw":"off",
            "isAuto":true,
            "isManual":false,
            "isRuleTurnOnDevice":false,
            "ruleSensorStopAt":"2025-05-07T06:29:20.979Z",
            "ruleSensorValueStop":1743.853,
            "ruleStopReason":"setpoint_reach",
            "ruleSensorStartAt":"2025-05-07T06:29:12.561Z",
            "ruleUid":"fbe121d1-f99e-4086-9c5e-ababd412d04a",
            "ruleType":"ec",
            "ruleVersion":5,
            "sensorUid":"58:CF:79:A4:77:32.01",
            "sensorValueLow":1722.586,
            "sensorValueHigh":1799.633,
            "sensorValue":1743.853,
            "ruleSensorValueStart":1720.208,
            "secondsToSwitchBack":0,
            "secondsDisabledFor":90,
            "secondsEnabledFor":0
        },
        {
            "uid":"58:CF:79:A4:77:32.pm.01",
            "switchedAt":"2025-05-07T06:37:44.286Z",
            "state":"off",
            "state_hw":"off",
            "isAuto":true,
            "isManual":false,
            "isRuleTurnOnDevice":false,
            "ruleSensorStopAt":"2025-05-07T06:29:20.996Z",
            "ruleSensorValueStop":1743.853,
            "ruleStopReason":"setpoint_reach",
            "ruleSensorStartAt":"2025-05-07T06:29:12.577Z",
            "ruleUid":"26332b8e-20ff-4134-9382-218e1957b452",
            "ruleType":"ec",
            "ruleVersion":5,
            "sensorUid":"58:CF:79:A4:77:32.01",
            "sensorValueLow":1722.586,
            "sensorValueHigh":1799.633,
            "sensorValue":1743.853,
            "ruleSensorValueStart":1720.208,
            "secondsToSwitchBack":0,
            "secondsDisabledFor":90,
            "secondsEnabledFor":0
        },
        {
            "uid":"58:CF:79:A4:77:32.pm.02",
            "switchedAt":"2025-05-04T05:02:36.247Z",
            "state":"offByPowerOn",
            "state_hw":"off",
            "isAuto":false,
            "isManual":false
        }
    ]
})";

// Test constructing an UpdateDto manually and serializing it to JSON
void test_serialize_update_dto() {
    // Create a sample UpdateDto with some devices
    UpdateDto updateDto;
    updateDto.growdirectorUid = "58:CF:79:A4:77:32";
    updateDto.email = "david@raiz.farm";
    updateDto.accountUid = "1d1e2e92-c30f-62bb-8e55-8032f8315edf";
    updateDto.time = "2025-05-11T19:49:52.632Z";
    updateDto.millis = 555894690;
    
    // Add a simple device
    DeviceDto device1;
    device1.uid = "58:CF:79:A4:77:32.pm.00";
    device1.switchedAt = "2025-05-07T06:33:35.658Z";
    device1.state = "off";
    device1.state_hw = "off";
    device1.isAuto = true;
    device1.isManual = false;
    device1.ruleUid = "fbe121d1-f99e-4086-9c5e-ababd412d04a";
    device1.ruleType = "ec";
    updateDto.devices.push_back(device1);
    
    // Add a minimal device
    DeviceDto device2;
    device2.uid = "58:CF:79:A4:77:32.pm.02";
    device2.state = "offByPowerOn";
    device2.state_hw = "off";
    updateDto.devices.push_back(device2);
    
    // Test with ArduinoJsonConverter
    ArduinoJsonConverter jsonConverter;
    std::string json = jsonConverter.updateToJson(updateDto);
    
    // Verify the serialized JSON contains expected elements
    TEST_ASSERT_NOT_NULL(json.c_str());
    TEST_ASSERT_TRUE(json.length() > 0);
    TEST_ASSERT_TRUE(json.find("\"GrowdirectorUid\":\"58:CF:79:A4:77:32\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"email\":\"david@raiz.farm\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"accountUid\":\"1d1e2e92-c30f-62bb-8e55-8032f8315edf\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"time\":\"2025-05-11T19:49:52.632Z\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"millis\":555894690") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"devices\":[") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"uid\":\"58:CF:79:A4:77:32.pm.00\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"uid\":\"58:CF:79:A4:77:32.pm.02\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"ruleType\":\"ec\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"state\":\"off\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"state_hw\":\"off\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"isAuto\":true") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"isManual\":false") != std::string::npos);
}

// Helper function to test update DTO serialization with all fields populated
void test_serialize_full_update_dto() {
    // Create a sample UpdateDto with all fields populated
    UpdateDto updateDto;
    updateDto.growdirectorUid = "58:CF:79:A4:77:32";
    updateDto.email = "david@raiz.farm";
    updateDto.accountUid = "1d1e2e92-c30f-62bb-8e55-8032f8315edf";
    updateDto.time = "2025-05-11T19:49:52.632Z";
    updateDto.millis = 555894690;
    
    // Create a fully populated device DTO
    DeviceDto device;
    device.uid = "58:CF:79:A4:77:32.pm.00";
    device.switchedAt = "2025-05-07T06:33:35.658Z";
    device.state = "off";
    device.state_hw = "off";
    device.isAuto = true;
    device.isManual = false;
    device.isRuleTurnOnDevice = false;
    device.ruleSensorStopAt = "2025-05-07T06:29:20.979Z";
    device.ruleSensorValueStop = 1743.853;
    device.ruleStopReason = "setpoint_reach";
    device.ruleSensorStartAt = "2025-05-07T06:29:12.561Z";
    device.ruleUid = "fbe121d1-f99e-4086-9c5e-ababd412d04a";
    device.ruleType = "ec";
    device.ruleVersion = 5;
    device.sensorUid = "58:CF:79:A4:77:32.01";
    device.sensorValueLow = 1722.586;
    device.sensorValueHigh = 1799.633;
    device.sensorValue = 1743.853;
    device.ruleSensorValueStart = 1720.208;
    device.secondsToSwitchBack = 0;
    device.secondsDisabledFor = 90;
    device.secondsEnabledFor = 0;
    
    updateDto.devices.push_back(device);
    
    // Test with ArduinoJsonConverter
    ArduinoJsonConverter jsonConverter;
    std::string json = jsonConverter.updateToJson(updateDto);
    
    // Basic validation
    TEST_ASSERT_NOT_NULL(json.c_str());
    TEST_ASSERT_TRUE(json.length() > 0);
    
    // Verify specific detailed fields are included
    TEST_ASSERT_TRUE(json.find("\"GrowdirectorUid\":\"58:CF:79:A4:77:32\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"email\":\"david@raiz.farm\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"accountUid\":\"1d1e2e92-c30f-62bb-8e55-8032f8315edf\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"time\":\"2025-05-11T19:49:52.632Z\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"millis\":555894690") != std::string::npos);
    
    // Device fields
    TEST_ASSERT_TRUE(json.find("\"uid\":\"58:CF:79:A4:77:32.pm.00\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"switchedAt\":\"2025-05-07T06:33:35.658Z\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"state\":\"off\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"state_hw\":\"off\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"isAuto\":true") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"isManual\":false") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"isRuleTurnOnDevice\":false") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"ruleSensorStopAt\":\"2025-05-07T06:29:20.979Z\"") != std::string::npos);
    
    // Check for floating point values with partial matches to avoid precision issues
    TEST_ASSERT_TRUE(json.find("\"ruleSensorValueStop\":") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("1743.8") != std::string::npos);
    
    TEST_ASSERT_TRUE(json.find("\"ruleStopReason\":\"setpoint_reach\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"ruleSensorStartAt\":\"2025-05-07T06:29:12.561Z\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"ruleUid\":\"fbe121d1-f99e-4086-9c5e-ababd412d04a\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"ruleType\":\"ec\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"ruleVersion\":5") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"sensorUid\":\"58:CF:79:A4:77:32.01\"") != std::string::npos);
    
    // Check for floating point values with partial matches
    TEST_ASSERT_TRUE(json.find("\"sensorValueLow\":") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("1722.5") != std::string::npos);
    
    TEST_ASSERT_TRUE(json.find("\"sensorValueHigh\":") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("1799.6") != std::string::npos);
    
    TEST_ASSERT_TRUE(json.find("\"sensorValue\":") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("1743.8") != std::string::npos);
    
    TEST_ASSERT_TRUE(json.find("\"ruleSensorValueStart\":") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("1720.2") != std::string::npos);
    
    TEST_ASSERT_TRUE(json.find("\"secondsDisabledFor\":90") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"secondsEnabledFor\":0") != std::string::npos);
    
    // Print the actual JSON for debugging
    printf("JSON: %s\n", json.c_str());
}

// Test various edge cases in the update DTO serialization
void test_update_dto_edge_cases() {
    // Test empty devices array
    UpdateDto emptyDevices;
    emptyDevices.growdirectorUid = "58:CF:79:A4:77:32";
    emptyDevices.email = "test@example.com";
    emptyDevices.time = "2025-05-11T00:00:00.000Z";
    emptyDevices.millis = 1000;
    
    ArduinoJsonConverter jsonConverter;
    std::string emptyDevicesJson = jsonConverter.updateToJson(emptyDevices);
    
    TEST_ASSERT_NOT_NULL(emptyDevicesJson.c_str());
    TEST_ASSERT_TRUE(emptyDevicesJson.length() > 0);
    TEST_ASSERT_TRUE(emptyDevicesJson.find("\"devices\":[]") != std::string::npos);
    
    // Test device with minimal fields (just UID and states)
    UpdateDto minimalDto;
    minimalDto.growdirectorUid = "58:CF:79:A4:77:32";
    
    DeviceDto minimalDevice;
    minimalDevice.uid = "test.device";
    minimalDevice.state = "off";
    minimalDevice.state_hw = "off";
    minimalDto.devices.push_back(minimalDevice);
    
    std::string minimalJson = jsonConverter.updateToJson(minimalDto);
    
    TEST_ASSERT_NOT_NULL(minimalJson.c_str());
    TEST_ASSERT_TRUE(minimalJson.length() > 0);
    TEST_ASSERT_TRUE(minimalJson.find("\"uid\":\"test.device\"") != std::string::npos);
    TEST_ASSERT_TRUE(minimalJson.find("\"state\":\"off\"") != std::string::npos);
    TEST_ASSERT_TRUE(minimalJson.find("\"state_hw\":\"off\"") != std::string::npos);
    TEST_ASSERT_FALSE(minimalJson.find("\"ruleUid\"") != std::string::npos); // This field should not be present
    
    // Test with a JSON string representation
    std::string json = jsonConverter.updateToJson(minimalDto);
    printf("JSON: %s\n", json.c_str()); // Print for debugging
    
    // Validate JSON syntax - check for opening and closing braces
    TEST_ASSERT_TRUE(json[0] == '{');
    TEST_ASSERT_TRUE(json[json.length()-1] == '}');
}

// Run all the tests
extern "C" void run_update_dto_converter_tests(void) {
    RUN_TEST(test_serialize_update_dto);
    RUN_TEST(test_serialize_full_update_dto);
    RUN_TEST(test_update_dto_edge_cases);
}
