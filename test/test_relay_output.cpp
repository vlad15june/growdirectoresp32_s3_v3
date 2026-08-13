#include <unity.h>
#include <string>

#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCase.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessAnalogDeviceStateUseCase.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCaseFactory.h"
#include "Core/Domain/UseCases/DeviceControl/DeviceCommandExecutionUseCase.h"
#include "Core/Domain/Data/Models/DeviceModel.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"

#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"

static DeviceDigitalOut* makeDigital(const char* uid, DeviceStateEnum state)
{
    DeviceDigitalOut* device = new DeviceDigitalOut();
    device->deviceUid = uid;
    device->deviceType = DEVICE_TYPE_DIGITAL;
    device->state = state;
    device->isError = false;
    device->needToSwitchBack = false;
    device->isResquestOn = false;
    device->ruleUid = "rule-1";
    return device;
}

static void test_rule_on_energizes_and_off_deenergizes(void)
{
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
    moduleConfig.setDeviceCount(1);

    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* device = makeDigital("relay-0", DEVICE_STATE_ON);
    deviceModel.addDevice(device);

    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    ProcessDeviceStateUseCase uc(&logger, &debug, &moduleConfig, &arduino, &deviceModel);

    uc.loop();
    TEST_ASSERT_EQUAL_UINT8(1, arduino.digitalOutputs[1]);

    device->state = DEVICE_STATE_OFF;
    uc.loop();
    TEST_ASSERT_EQUAL_UINT8(0, arduino.digitalOutputs[1]);
}

static void test_on_held_writes_pin_once(void)
{
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
    moduleConfig.setDeviceCount(1);

    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* device = makeDigital("relay-0", DEVICE_STATE_ON);
    deviceModel.addDevice(device);

    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    ProcessDeviceStateUseCase uc(&logger, &debug, &moduleConfig, &arduino, &deviceModel);

    uc.loop();
    uc.loop();
    uc.loop();

    TEST_ASSERT_EQUAL_INT(1, arduino.digitalWriteCounts[1]);
}

static void test_default_state_off_keeps_pin_inactive(void)
{
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
    moduleConfig.setDeviceCount(1);

    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* device = makeDigital("relay-0", DEVICE_STATE_OFF);
    deviceModel.addDevice(device);

    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    ProcessDeviceStateUseCase uc(&logger, &debug, &moduleConfig, &arduino, &deviceModel);

    uc.loop();

    TEST_ASSERT_EQUAL_UINT8(0, arduino.digitalOutputs[1]);
    TEST_ASSERT_NOT_EQUAL(1, arduino.digitalOutputs[1]);
}

static void test_hydro_admits_only_one_channel(void)
{
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_HYDRO);
    moduleConfig.setDeviceCount(2);

    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* ch0 = makeDigital("pump-0", DEVICE_STATE_REQUEST_ON);
    DeviceDigitalOut* ch1 = makeDigital("pump-1", DEVICE_STATE_OFF);
    deviceModel.addDevice(ch0);
    deviceModel.addDevice(ch1);

    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    ProcessDeviceStateUseCase uc(&logger, &debug, &moduleConfig, &arduino, &deviceModel);

    uc.loop();

    TEST_ASSERT_EQUAL(DEVICE_STATE_ON, ch0->state);
    TEST_ASSERT_EQUAL_UINT8(1, arduino.digitalOutputs[9]);

    ch1->state = DEVICE_STATE_REQUEST_ON;
    uc.loop();

    TEST_ASSERT_EQUAL(DEVICE_STATE_REQUEST_ON, ch1->state);
    TEST_ASSERT_EQUAL_INT(0, arduino.digitalWriteCounts[10]);
}

static void test_out_of_range_channel_does_not_write(void)
{
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
    moduleConfig.setDeviceCount(1);

    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* ch0 = makeDigital("relay-0", DEVICE_STATE_OFF);
    DeviceDigitalOut* ch1 = makeDigital("relay-1", DEVICE_STATE_ON);
    deviceModel.addDevice(ch0);
    deviceModel.addDevice(ch1);

    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    ProcessDeviceStateUseCase uc(&logger, &debug, &moduleConfig, &arduino, &deviceModel);

    uc.loop();

    TEST_ASSERT_EQUAL_INT(0, arduino.digitalWriteCounts[2]);
}

static void test_manual_command_targets_same_pin_as_rule(void)
{
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
    moduleConfig.setDeviceCount(1);

    DeviceModel ruleModel(&moduleConfig, &ms);
    DeviceDigitalOut* ruleDevice = makeDigital("relay-0", DEVICE_STATE_ON);
    ruleModel.addDevice(ruleDevice);

    MockArduinoAdapter ruleArduino;
    MockLogger logger;
    MockDebugSystem debug;

    ProcessDeviceStateUseCase ruleUc(&logger, &debug, &moduleConfig, &ruleArduino, &ruleModel);
    ruleUc.loop();

    DeviceModel manualModel(&moduleConfig, &ms);
    DeviceDigitalOut* manualDevice = makeDigital("relay-0", DEVICE_STATE_OFF);
    manualModel.addDevice(manualDevice);

    MockArduinoAdapter manualArduino;
    DeviceCommandExecutionUseCase manualUc(&manualModel, &manualArduino, &ms, &logger, &debug, &moduleConfig);
    bool ok = manualUc.execute("relay-0", "on", 1);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(1, ruleArduino.digitalOutputs[1]);
    TEST_ASSERT_EQUAL_UINT8(1, manualArduino.digitalOutputs[1]);
}

static void test_factory_routes_hydro_to_digital_use_case(void)
{
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    DeviceModel deviceModel(&moduleConfig, &ms);
    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    ProcessDeviceStateUseCaseFactory factory(&logger, &debug, &moduleConfig, &arduino, &deviceModel);

    IProcessDeviceStateUseCase* device = factory.create(MODULE_TYPE_DEVICE);
    IProcessDeviceStateUseCase* hydro = factory.create(MODULE_TYPE_HYDRO);
    IProcessDeviceStateUseCase* analog = factory.create(MODULE_TYPE_ANALOG_INPUT_DEVICE);

    TEST_ASSERT_NOT_NULL(dynamic_cast<ProcessDeviceStateUseCase*>(device));
    TEST_ASSERT_NOT_NULL(dynamic_cast<ProcessDeviceStateUseCase*>(hydro));
    TEST_ASSERT_NOT_NULL(dynamic_cast<ProcessAnalogDeviceStateUseCase*>(analog));

    delete device;
    delete hydro;
    delete analog;
}

static void test_hydro_pump_drives_pump_pin(void)
{
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_HYDRO);
    moduleConfig.setDeviceCount(1);

    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* pump = makeDigital("pump-0", DEVICE_STATE_ON);
    pump->deviceType = DEVICE_TYPE_PUMP;
    deviceModel.addDevice(pump);

    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    ProcessDeviceStateUseCase uc(&logger, &debug, &moduleConfig, &arduino, &deviceModel);
    uc.loop();

    TEST_ASSERT_EQUAL_UINT8(1, arduino.digitalOutputs[9]);
}

static void test_switch_back_sets_time_reach_reason(void)
{
    ModuleStructure ms;
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
    moduleConfig.setDeviceCount(1);

    DeviceModel deviceModel(&moduleConfig, &ms);
    DeviceDigitalOut* device = makeDigital("relay-0", DEVICE_STATE_ON);
    device->needToSwitchBack = true;
    device->timeOn = 0;
    device->durationOn = 1000;
    device->isRuleTurnOnDevice = true;
    device->ruleStopReason = nullptr;
    deviceModel.addDevice(device);

    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debug;

    ProcessDeviceStateUseCase uc(&logger, &debug, &moduleConfig, &arduino, &deviceModel);

    arduino.setMillis(2000);
    uc.loop();

    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_OFF, device->state);
    TEST_ASSERT_FALSE(device->isRuleTurnOnDevice);
    TEST_ASSERT_NOT_NULL(device->ruleStopReason);
    TEST_ASSERT_EQUAL_STRING("time_reach", device->ruleStopReason);
}

extern "C" void run_relay_output_tests(void)
{
    RUN_TEST(test_rule_on_energizes_and_off_deenergizes);
    RUN_TEST(test_on_held_writes_pin_once);
    RUN_TEST(test_default_state_off_keeps_pin_inactive);
    RUN_TEST(test_hydro_admits_only_one_channel);
    RUN_TEST(test_hydro_pump_drives_pump_pin);
    RUN_TEST(test_factory_routes_hydro_to_digital_use_case);
    RUN_TEST(test_out_of_range_channel_does_not_write);
    RUN_TEST(test_manual_command_targets_same_pin_as_rule);
    RUN_TEST(test_switch_back_sets_time_reach_reason);
}
