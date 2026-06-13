#include <unity.h>
#include <string>

#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/UpdateDto.h"
#include "Core/Domain/UseCases/DeviceControl/DeviceCommandExecutionUseCase.h"
#include "Application/Services/Devices/DeviceService.cpp"
#include "Core/Domain/Data/Models/DeviceModel.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"

#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"

static const char* npos_safe(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos ? "1" : "0";
}

static void test_device_command_ack_is_array_with_timestamp(void) {
    JsonConverter conv;

    DeviceCommandUpdateDto dto("9C:13:9E:99:47:38.00", "done", "", "2026-06-06T05:56:22.000Z");
    std::string json = conv.deviceCommandUpdateToJson(dto);

    TEST_ASSERT_TRUE(json.size() >= 2);
    TEST_ASSERT_EQUAL('[', json.front());
    TEST_ASSERT_EQUAL(']', json.back());
    TEST_ASSERT_EQUAL_STRING("1", npos_safe(json, "\"uid\":\"9C:13:9E:99:47:38.00\""));
    TEST_ASSERT_EQUAL_STRING("1", npos_safe(json, "\"status\":\"done\""));
    TEST_ASSERT_EQUAL_STRING("1", npos_safe(json, "\"timestamp\":\"2026-06-06T05:56:22.000Z\""));
}

static void test_device_command_ack_omits_empty_timestamp(void) {
    JsonConverter conv;

    DeviceCommandUpdateDto dto("uid-1", "error", "boom");
    std::string json = conv.deviceCommandUpdateToJson(dto);

    TEST_ASSERT_EQUAL('[', json.front());
    TEST_ASSERT_EQUAL(']', json.back());
    TEST_ASSERT_EQUAL_STRING("1", npos_safe(json, "\"errorMessage\":\"boom\""));
    TEST_ASSERT_EQUAL_STRING("0", npos_safe(json, "timestamp"));
    TEST_ASSERT_EQUAL_STRING("0", npos_safe(json, "accountUid"));
}

static void test_execute_sets_switchback_duration(void) {
    ModuleStructure ms;
    Node node;
    node.relays.push_back(Relay("9C:13:9E:99:47:38.00", 1));
    ms.nodes.push_back(node);

    MockModuleConfig moduleConfig;
    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* device = new DeviceDigitalOut();
    device->deviceUid = "9C:13:9E:99:47:38.00";
    device->deviceType = DEVICE_TYPE_DIGITAL;
    device->durationOn = 0;
    device->needToSwitchBack = false;
    device->workManual = false;
    deviceModel.addDevice(device);

    MockArduinoAdapter arduino;
    arduino.currentMillis = 1000;
    MockLogger logger;
    MockDebugSystem debug;

    DeviceCommandExecutionUseCase uc(&deviceModel, &arduino, &ms, &logger, &debug, &moduleConfig);

    bool ok = uc.execute("9C:13:9E:99:47:38.00", "on", 4410, true, 60, "2026-06-06T14:31:26.000Z");

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(60000, device->durationOn);
    TEST_ASSERT_TRUE(device->needToSwitchBack);
    TEST_ASSERT_TRUE(device->workManual);
    TEST_ASSERT_TRUE(device->isManual);
    TEST_ASSERT_EQUAL(DEVICE_STATE_ON, device->state);
    TEST_ASSERT_EQUAL_INT(4410, device->commandId);
    TEST_ASSERT_EQUAL_UINT32(1000, device->timeOn);
    TEST_ASSERT_NOT_NULL(device->switchedAt);
    TEST_ASSERT_EQUAL_STRING("2026-06-06T14:31:26.000Z", device->switchedAt);
}

static void test_execute_no_switchback_keeps_zero_duration(void) {
    ModuleStructure ms;
    Node node;
    node.relays.push_back(Relay("9C:13:9E:99:47:38.01", 2));
    ms.nodes.push_back(node);

    MockModuleConfig moduleConfig;
    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* device = new DeviceDigitalOut();
    device->deviceUid = "9C:13:9E:99:47:38.01";
    device->deviceType = DEVICE_TYPE_DIGITAL;
    device->durationOn = 12345;
    deviceModel.addDevice(device);

    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    DeviceCommandExecutionUseCase uc(&deviceModel, &arduino, &ms, &logger, &debug, &moduleConfig);

    bool ok = uc.execute("9C:13:9E:99:47:38.01", "on", 5000, false, 0);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(0, device->durationOn);
    TEST_ASSERT_FALSE(device->needToSwitchBack);
}

static void test_device_update_carries_switchback_time(void) {
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    DeviceModel deviceModel(&moduleConfig, &ms);

    DeviceDigitalOut* device = new DeviceDigitalOut();
    device->deviceUid = "9C:13:9E:99:47:38.00";
    device->deviceType = DEVICE_TYPE_DIGITAL;
    device->state = DEVICE_STATE_ON;
    device->switchedAt = "2026-06-06T14:31:26.000Z";
    device->isAuto = false;
    device->isManual = true;
    device->workByRule = false;
    device->workManual = true;
    device->needToSwitchBack = true;
    device->durationOn = 60000;
    device->timeOn = 30000;
    device->commandId = 4410;
    device->mixTime = 0;
    deviceModel.addDevice(device);

    MockNTPTimeAdapter timeAdapter;
    MockArduinoAdapter arduino;
    arduino.currentMillis = 40000;
    MockHybridAPIService hybrid;
    MockLogger logger;
    MockDebugSystem debug;

    DeviceService svc(&moduleConfig, &ms, &deviceModel, &timeAdapter, &arduino,
                      nullptr, &hybrid, &logger, &debug);

    svc.loop();

    TEST_ASSERT_TRUE(hybrid.sendModuleUpdateDataCalled);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(hybrid.lastUpdateData.devices.size()));
    const DeviceDto& d = hybrid.lastUpdateData.devices[0];
    TEST_ASSERT_EQUAL_UINT32(50, d.secondsToSwitchBack);
    TEST_ASSERT_EQUAL_UINT32(60, d.secondsEnabledFor);
    TEST_ASSERT_EQUAL_STRING("2026-06-06T14:31:26.000Z", d.switchedAt.c_str());
    TEST_ASSERT_EQUAL_INT(4410, d.commandId);
}

static void test_device_update_sent_immediately_on_state_change(void) {
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    DeviceModel deviceModel(&moduleConfig, &ms);

    DeviceDigitalOut* device = new DeviceDigitalOut();
    device->deviceUid = "9C:13:9E:99:47:38.00";
    device->deviceType = DEVICE_TYPE_DIGITAL;
    device->state = DEVICE_STATE_OFF;
    device->switchedAt = "";
    device->isAuto = false;
    device->isManual = false;
    device->workByRule = false;
    device->workManual = false;
    device->commandId = 0;
    deviceModel.addDevice(device);

    MockNTPTimeAdapter timeAdapter;
    MockArduinoAdapter arduino;
    arduino.currentMillis = 1000;
    MockHybridAPIService hybrid;
    MockLogger logger;
    MockDebugSystem debug;

    DeviceService svc(&moduleConfig, &ms, &deviceModel, &timeAdapter, &arduino,
                      nullptr, &hybrid, &logger, &debug);

    svc.loop();
    TEST_ASSERT_TRUE(hybrid.sendModuleUpdateDataCalled);

    hybrid.sendModuleUpdateDataCalled = false;
    arduino.currentMillis = 6000;
    svc.loop();
    TEST_ASSERT_FALSE(hybrid.sendModuleUpdateDataCalled);

    device->state = DEVICE_STATE_ON;
    device->isManual = true;
    device->workManual = true;
    device->commandId = 4421;
    device->switchedAt = "2026-06-06T14:31:26.000Z";
    arduino.currentMillis = 7000;
    svc.loop();
    TEST_ASSERT_TRUE(hybrid.sendModuleUpdateDataCalled);
}

static void test_manual_device_update_json_has_switchedat_and_numeric_commandid(void) {
    JsonConverter conv;

    UpdateDto update;
    update.growdirectorUid = "9C:13:9E:99:47:38";
    DeviceDto d;
    d.uid = "9C:13:9E:99:47:38.00";
    d.state = "on";
    d.state_hw = "on";
    d.isManual = true;
    d.isAuto = false;
    d.switchedAt = "2026-06-06T14:31:26.000Z";
    d.commandId = 4421;
    d.secondsToSwitchBack = 50;
    d.secondsEnabledFor = 60;
    update.devices.push_back(d);

    std::string json = conv.updateToJson(update);

    TEST_ASSERT_EQUAL_STRING("1", npos_safe(json, "\"switchedAt\":\"2026-06-06T14:31:26.000Z\""));
    TEST_ASSERT_EQUAL_STRING("1", npos_safe(json, "\"commandId\":4421"));
    TEST_ASSERT_EQUAL_STRING("0", npos_safe(json, "\"commandId\":\"4421\""));
}

static void test_switchedat_refreshes_on_loop_phase_transitions(void) {
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    DeviceModel deviceModel(&moduleConfig, &ms);

    DeviceDigitalOut* device = new DeviceDigitalOut();
    device->deviceUid = "9C:13:9E:99:47:38.00";
    device->deviceType = DEVICE_TYPE_DIGITAL;
    device->state = DEVICE_STATE_REQUEST_STEP_ON;
    device->switchedAt = "T0";
    device->isAuto = true;
    device->workByRule = true;
    device->ruleType = RuleType::LOOP;
    device->durationOn = 20000;
    device->mixTime = 10000;
    device->needToSwitchBack = true;
    deviceModel.addDevice(device);

    MockNTPTimeAdapter timeAdapter;
    MockArduinoAdapter arduino;
    MockHybridAPIService hybrid;
    MockLogger logger;
    MockDebugSystem debug;

    DeviceService svc(&moduleConfig, &ms, &deviceModel, &timeAdapter, &arduino,
                      nullptr, &hybrid, &logger, &debug);

    timeAdapter.setFormattedTime("T0");
    arduino.currentMillis = 1000;
    svc.loop();
    TEST_ASSERT_EQUAL_STRING("T0", hybrid.lastUpdateData.devices[0].switchedAt.c_str());

    device->state = DEVICE_STATE_STEP_ON;
    timeAdapter.setFormattedTime("T1");
    arduino.currentMillis = 2000;
    svc.loop();
    TEST_ASSERT_EQUAL_STRING("T1", hybrid.lastUpdateData.devices[0].switchedAt.c_str());

    device->state = DEVICE_STATE_STEP_WAITING;
    timeAdapter.setFormattedTime("T2");
    arduino.currentMillis = 3000;
    svc.loop();
    TEST_ASSERT_EQUAL_STRING("T2", hybrid.lastUpdateData.devices[0].switchedAt.c_str());
}

extern "C" void run_device_command_tests(void) {
    RUN_TEST(test_device_command_ack_is_array_with_timestamp);
    RUN_TEST(test_device_command_ack_omits_empty_timestamp);
    RUN_TEST(test_execute_sets_switchback_duration);
    RUN_TEST(test_execute_no_switchback_keeps_zero_duration);
    RUN_TEST(test_device_update_carries_switchback_time);
    RUN_TEST(test_device_update_sent_immediately_on_state_change);
    RUN_TEST(test_manual_device_update_json_has_switchedat_and_numeric_commandid);
    RUN_TEST(test_switchedat_refreshes_on_loop_phase_transitions);
}
