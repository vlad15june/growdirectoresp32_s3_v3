#include <unity.h>
#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/DeviceDto.h"
#include "Application/DTOs/RuleStateUpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Application/Services/Rules/RuleValidator.h"
#include "Application/Services/CommandService.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Core/Domain/MockRemoteSensorReceiver.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"

static const char *kWorkflowRuleJson =
    R"({"uid":"wfr-1","version":2,"workflowUid":"wf-1","workflowVersion":4,"workflowDisplayOrder":2,"workflowPriority":5,"dependsOnRuleUid":"pred-1","deviceUid":"dev-1","ruleType":"sensor","sensorUid":"s1","valueLow":10,"valueHigh":20,"direction":"down","mode":"On","state":"Synchronized","daysOfWeek":[],"daysOfMonth":[],"conditions":[]})";

static const char *kStandaloneRuleJson =
    R"({"uid":"std-1","version":1,"deviceUid":"dev-1","ruleType":"sensor","sensorUid":"s1","valueLow":10,"valueHigh":20,"direction":"down","mode":"On","state":"Synchronized","daysOfWeek":[],"daysOfMonth":[],"conditions":[]})";

static bool contains(const std::string &s, const std::string &needle)
{
    return s.find(needle) != std::string::npos;
}

class WfDeviceModel : public IDeviceModel {
public:
    Device* device = nullptr;
    bool init() override { return true; }
    Device* getDeviceByUid(const char* uid) override {
        if (device && device->deviceUid && uid && std::string(device->deviceUid) == uid) {
            return device;
        }
        return nullptr;
    }
    std::vector<Device*> getAllDevices() override {
        std::vector<Device*> v;
        if (device) v.push_back(device);
        return v;
    }
    int getDeviceCount() const override { return device ? 1 : 0; }
    bool addDevice(Device*) override { return true; }
    bool removeDevice(const char*) override { return true; }
    void clearDevices() override {}
};

static void test_workflow_fields_parsed(void)
{
    JsonConverter c;
    Rule rule;
    TEST_ASSERT_TRUE(c.ruleFromJson(kWorkflowRuleJson, rule));

    TEST_ASSERT_EQUAL_STRING("wf-1", rule.workflowUid.c_str());
    TEST_ASSERT_EQUAL_INT(4, rule.workflowVersion);
    TEST_ASSERT_EQUAL_INT(2, rule.workflowDisplayOrder);
    TEST_ASSERT_EQUAL_INT(5, rule.workflowPriority);
    TEST_ASSERT_EQUAL_STRING("pred-1", rule.dependsOnRuleUid.c_str());
}

static void test_standalone_defaults_when_absent(void)
{
    JsonConverter c;
    Rule rule;
    TEST_ASSERT_TRUE(c.ruleFromJson(kStandaloneRuleJson, rule));

    TEST_ASSERT_TRUE(rule.workflowUid.empty());
    TEST_ASSERT_EQUAL_INT(0, rule.workflowVersion);
    TEST_ASSERT_EQUAL_INT(0, rule.workflowDisplayOrder);
    TEST_ASSERT_EQUAL_INT(0, rule.workflowPriority);
    TEST_ASSERT_TRUE(rule.dependsOnRuleUid.empty());
}

static void test_workflow_fields_roundtrip(void)
{
    JsonConverter c;
    Rule rule;
    TEST_ASSERT_TRUE(c.ruleFromJson(kWorkflowRuleJson, rule));

    std::string json = c.ruleToJson(rule);

    Rule rule2;
    TEST_ASSERT_TRUE(c.ruleFromJson(json, rule2));

    TEST_ASSERT_EQUAL_STRING("wf-1", rule2.workflowUid.c_str());
    TEST_ASSERT_EQUAL_INT(4, rule2.workflowVersion);
    TEST_ASSERT_EQUAL_INT(2, rule2.workflowDisplayOrder);
    TEST_ASSERT_EQUAL_INT(5, rule2.workflowPriority);
    TEST_ASSERT_EQUAL_STRING("pred-1", rule2.dependsOnRuleUid.c_str());
}

static Rule makeWorkflowSensorRule()
{
    Rule r;
    r.uid = "wfr-1";
    r.version = 2;
    r.workflowUid = "wf-1";
    r.workflowVersion = 4;
    r.workflowPriority = 5;
    r.dependsOnRuleUid = "pred-1";
    r.ruleType = RuleType::SENSOR;
    r.deviceUid = "dev-1";
    r.sensorUid = "s1";
    r.mode = RuleMode::ON;
    r.state = RuleState::SYNCHRONIZED;
    r.direction = RuleDirection::DOWN;
    r.valueLow = 10;
    r.valueHigh = 20;
    return r;
}

static void test_attribution_propagates_to_device(void)
{
    MockRuleRepository repo;
    DeviceDigitalOut device;
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.workByRule = false;

    WfDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 25.0f;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    Rule rule = makeWorkflowSensorRule();
    uc.executeRuleActions(rule);

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
    TEST_ASSERT_TRUE(device.workByRule);
    TEST_ASSERT_NOT_NULL(device.workflowUid);
    TEST_ASSERT_EQUAL_STRING("wf-1", device.workflowUid);
    TEST_ASSERT_EQUAL_INT(4, device.workflowVersion);
}

static void test_predecessor_and_priority_do_not_gate(void)
{
    MockRuleRepository repo;
    DeviceDigitalOut device;
    device.deviceUid = "dev-1";
    device.deviceType = DEVICE_TYPE_DIGITAL;
    device.state = DEVICE_STATE_OFF;
    device.workByRule = false;

    WfDeviceModel deviceModel;
    deviceModel.device = &device;

    MockSensorValueModel sensors;
    sensors.values["s1"] = 25.0f;
    MockArduinoAdapter arduino;
    MockNTPTimeAdapter ntp;
    ntp.setEpochTime(1700000000);

    EvaluateRulesUseCase uc(&deviceModel, &repo, &arduino, &ntp, &sensors);

    Rule rule = makeWorkflowSensorRule();
    rule.dependsOnRuleUid = "missing-predecessor";
    rule.workflowPriority = 99;
    uc.executeRuleActions(rule);

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_REQUEST_ON, device.state);
}

static void test_updateToJson_emits_for_workflow_device(void)
{
    JsonConverter c;
    UpdateDto u;

    DeviceDto d;
    d.uid = "dev-1";
    d.state = "on";
    d.isAuto = true;
    d.ruleUid = "wfr-1";
    d.ruleType = "sensor";
    d.workflowUid = "wf-1";
    d.workflowVersion = 4;
    u.devices.push_back(d);

    std::string json = c.updateToJson(u);
    TEST_ASSERT_TRUE(contains(json, "\"workflowUid\":\"wf-1\""));
    TEST_ASSERT_TRUE(contains(json, "\"workflowVersion\":4"));
}

static void test_updateToJson_omits_for_standalone_device(void)
{
    JsonConverter c;
    UpdateDto u;

    DeviceDto d;
    d.uid = "dev-2";
    d.state = "on";
    d.isAuto = true;
    d.ruleUid = "std-1";
    d.ruleType = "sensor";
    u.devices.push_back(d);

    std::string json = c.updateToJson(u);
    TEST_ASSERT_FALSE(contains(json, "workflowUid"));
    TEST_ASSERT_FALSE(contains(json, "workflowVersion"));
}

static Rule makeValidatableSensorRule()
{
    Rule r;
    r.uid = "v-1";
    r.deviceUid = "dev-1";
    r.ruleType = RuleType::SENSOR;
    r.sensorUid = "s1";
    r.valueLow = 10;
    r.valueHigh = 20;
    r.direction = RuleDirection::DOWN;
    r.state = RuleState::SYNCHRONIZED;
    return r;
}

static void test_validator_workflow_version_missing_is_wrong_format(void)
{
    RuleValidator validator;
    Rule r = makeValidatableSensorRule();
    r.workflowUid = "wf-1";
    r.workflowVersion = 0;

    RuleValidationResult res = validator.validateRule(r);
    TEST_ASSERT_FALSE(res.isValid);
    TEST_ASSERT_EQUAL_STRING("wrong_format", res.errorType.c_str());
}

static void test_validator_workflow_pair_valid(void)
{
    RuleValidator validator;
    Rule r = makeValidatableSensorRule();
    r.workflowUid = "wf-1";
    r.workflowVersion = 4;

    RuleValidationResult res = validator.validateRule(r);
    TEST_ASSERT_TRUE(res.isValid);
}

static void test_validator_standalone_unaffected(void)
{
    RuleValidator validator;
    Rule r = makeValidatableSensorRule();

    RuleValidationResult res = validator.validateRule(r);
    TEST_ASSERT_TRUE(res.isValid);
}

extern "C" void run_rule_workflow_phase1_tests(void)
{
    RUN_TEST(test_workflow_fields_parsed);
    RUN_TEST(test_standalone_defaults_when_absent);
    RUN_TEST(test_workflow_fields_roundtrip);
    RUN_TEST(test_attribution_propagates_to_device);
    RUN_TEST(test_predecessor_and_priority_do_not_gate);
    RUN_TEST(test_updateToJson_emits_for_workflow_device);
    RUN_TEST(test_updateToJson_omits_for_standalone_device);
    RUN_TEST(test_validator_workflow_version_missing_is_wrong_format);
    RUN_TEST(test_validator_workflow_pair_valid);
    RUN_TEST(test_validator_standalone_unaffected);
}
