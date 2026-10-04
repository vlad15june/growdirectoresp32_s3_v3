#include <unity.h>
#include <string>

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/Services/CommandService.h"
#include "Core/Domain/UseCases/DeviceControl/DeviceCommandExecutionUseCase.h"
#include "Application/Services/Devices/DeviceService.h"
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
#include "Mock/Application/MockOutbox.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"

static const char* npos_safe(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos ? "1" : "0";
}

static void test_device_command_ack_is_array_with_timestamp(void) {
    ArduinoJsonConverter conv;

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
    ArduinoJsonConverter conv;

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
    ArduinoJsonConverter conv;

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

static void test_rule_off_update_carries_stop_reason(void) {
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    DeviceModel deviceModel(&moduleConfig, &ms);

    DeviceDigitalOut* device = new DeviceDigitalOut();
    device->deviceUid = "9C:13:9E:99:47:38.00";
    device->deviceType = DEVICE_TYPE_DIGITAL;
    device->state = DEVICE_STATE_OFF;
    device->isAuto = false;
    device->isManual = false;
    device->workByRule = true;
    device->workManual = false;
    device->isRuleTurnOnDevice = false;
    device->ruleUid = "rule-1";
    device->ruleType = RuleType::SENSOR;
    device->ruleStopReason = "rule_manualy_off";
    device->needReportingRuleUid = true;
    deviceModel.addDevice(device);

    MockNTPTimeAdapter timeAdapter;
    MockArduinoAdapter arduino;
    arduino.currentMillis = 1000;
    MockHybridAPIService hybrid;
    MockLogger logger;
    MockDebugSystem debug;

    DeviceService svc(&moduleConfig, &ms, &deviceModel, &timeAdapter, &arduino,
                      nullptr, &hybrid, &logger, &debug);

    ArduinoJsonConverter converter;

    // Packet 1: rule-stop event carries reason and isAuto=true
    svc.loop();
    TEST_ASSERT_TRUE(hybrid.sendModuleUpdateDataCalled);
    const DeviceDto& d1 = hybrid.lastUpdateData.devices[0];
    TEST_ASSERT_TRUE(d1.isAuto);
    TEST_ASSERT_EQUAL_STRING("rule_manualy_off", d1.ruleStopReason.c_str());
    std::string json1 = converter.updateToJson(hybrid.lastUpdateData);
    TEST_ASSERT_TRUE(json1.find("\"ruleStopReason\":\"rule_manualy_off\"") != std::string::npos);
    TEST_ASSERT_TRUE(json1.find("\"isAuto\":true") != std::string::npos);

    // Context cleared after reporting
    TEST_ASSERT_FALSE(device->workByRule);
    TEST_ASSERT_FALSE(device->needReportingRuleUid);

    // Packet 2: cleared state, isAuto=false, no rule context
    arduino.currentMillis = 2000;
    hybrid.sendModuleUpdateDataCalled = false;
    svc.loop();
    TEST_ASSERT_TRUE(hybrid.sendModuleUpdateDataCalled);
    const DeviceDto& d2 = hybrid.lastUpdateData.devices[0];
    TEST_ASSERT_FALSE(d2.isAuto);
    std::string json2 = converter.updateToJson(hybrid.lastUpdateData);
    TEST_ASSERT_TRUE(json2.find("\"isAuto\":false") != std::string::npos);
    TEST_ASSERT_TRUE(json2.find("ruleStopReason") == std::string::npos);
    TEST_ASSERT_TRUE(json2.find("\"ruleUid\"") == std::string::npos);
}

// The command path is asynchronous: process() only asks, and the answer arrives as
// an outbox completion which the control loop hands back. `delivered == false` is what "the server
// did not answer" looks like from here - the mapping from an HTTP failure to that flag lives in
// Outbox::fetchDeviceCommands() and is tested against the real class in test_outbox.cpp.
static void test_fallback_executes_from_mqtt_when_http_failed(void) {
    ModuleStructure ms;
    Node node;
    node.relays.push_back(Relay("9C:13:9E:99:47:38.00", 1));
    ms.nodes.push_back(node);

    MockModuleConfig moduleConfig;
    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* device = new DeviceDigitalOut();
    device->deviceUid = "9C:13:9E:99:47:38.00";
    device->deviceType = DEVICE_TYPE_DIGITAL;
    device->state = DEVICE_STATE_OFF;
    device->durationOn = 0;
    device->needToSwitchBack = false;
    deviceModel.addDevice(device);

    MockArduinoAdapter arduino;
    arduino.currentMillis = 1000;
    MockLogger logger;
    MockDebugSystem debug;
    MockOutbox outbox;

    DeviceCommandExecutionUseCase uc(&deviceModel, &arduino, &ms, &logger, &debug, &moduleConfig);
    CommandService svc(&uc, nullptr, nullptr, &ms, &logger, &debug);
    svc.setOutbox(&outbox);

    CommandDto cmd;
    cmd.type = "DeviceCommandNotification";
    cmd.deviceUid = "9C:13:9E:99:47:38.00";
    cmd.requestedState = "on";
    cmd.commandId = 4475;
    cmd.needToSwitchBack = true;
    cmd.secondsToSwitchBack = 60;

    bool ok = svc.process(cmd);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(1, outbox.deviceCommandFetches);
    // Nothing has been executed yet: process() only asked.
    TEST_ASSERT_EQUAL(DEVICE_STATE_OFF, device->state);

    // The server did not answer, and there is no response in the slot.
    outbox.pushCompletion(OutboxKind::DEVICE_COMMAND_FETCH, 1, false);
    OutboxCompletion completion;
    while (outbox.pollCompletion(completion)) {
        svc.onOutboxCompletion(completion);
    }

    TEST_ASSERT_EQUAL(DEVICE_STATE_ON, device->state);
    TEST_ASSERT_EQUAL_INT(4475, device->commandId);
    TEST_ASSERT_TRUE(device->needToSwitchBack);
    TEST_ASSERT_EQUAL_UINT32(60000, device->durationOn);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)outbox.deviceCommandResults.size());
}

static void test_no_fallback_when_http_ok_and_empty(void) {
    ModuleStructure ms;
    Node node;
    node.relays.push_back(Relay("9C:13:9E:99:47:38.00", 1));
    ms.nodes.push_back(node);

    MockModuleConfig moduleConfig;
    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* device = new DeviceDigitalOut();
    device->deviceUid = "9C:13:9E:99:47:38.00";
    device->deviceType = DEVICE_TYPE_DIGITAL;
    device->state = DEVICE_STATE_OFF;
    deviceModel.addDevice(device);

    MockArduinoAdapter arduino;
    arduino.currentMillis = 1000;
    MockLogger logger;
    MockDebugSystem debug;
    MockOutbox outbox;

    DeviceCommandExecutionUseCase uc(&deviceModel, &arduino, &ms, &logger, &debug, &moduleConfig);
    CommandService svc(&uc, nullptr, nullptr, &ms, &logger, &debug);
    svc.setOutbox(&outbox);

    CommandDto cmd;
    cmd.type = "DeviceCommandNotification";
    cmd.deviceUid = "9C:13:9E:99:47:38.00";
    cmd.requestedState = "on";
    cmd.commandId = 4475;

    bool ok = svc.process(cmd);
    TEST_ASSERT_TRUE(ok);

    // The server answered, and answered that there is nothing pending: an empty list from a
    // delivered fetch. The MQTT payload must not be executed on the strength of that.
    outbox.deviceCommandResponseReady = true;
    outbox.pushCompletion(OutboxKind::DEVICE_COMMAND_FETCH, 1, true);
    OutboxCompletion completion;
    while (outbox.pollCompletion(completion)) {
        svc.onOutboxCompletion(completion);
    }

    TEST_ASSERT_EQUAL(DEVICE_STATE_OFF, device->state);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)outbox.deviceCommandResults.size());
}

extern "C" void run_device_command_tests(void) {
    RUN_TEST(test_fallback_executes_from_mqtt_when_http_failed);
    RUN_TEST(test_no_fallback_when_http_ok_and_empty);
    RUN_TEST(test_rule_off_update_carries_stop_reason);
    RUN_TEST(test_device_command_ack_is_array_with_timestamp);
    RUN_TEST(test_device_command_ack_omits_empty_timestamp);
    RUN_TEST(test_execute_sets_switchback_duration);
    RUN_TEST(test_execute_no_switchback_keeps_zero_duration);
    RUN_TEST(test_device_update_carries_switchback_time);
    RUN_TEST(test_device_update_sent_immediately_on_state_change);
    RUN_TEST(test_manual_device_update_json_has_switchedat_and_numeric_commandid);
    RUN_TEST(test_switchedat_refreshes_on_loop_phase_transitions);
}
