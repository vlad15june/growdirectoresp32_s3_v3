// What an analog output module actually does with its channels.
//
// ProcessAnalogDeviceStateUseCase is the state machine a module of type
// MODULE_TYPE_ANALOG_INPUT_DEVICE runs on every control-loop iteration: it walks the device list,
// picks out the analog outputs and writes each one's percentage to the DAC as a 0..255 duty, plus
// the enable line beside it. The factory routing test builds one and checks its type - which
// compiles every line inside it and runs none of it; the tests here run the
// class itself.
//
// The two tests here run it: one on a list of mixed device types, so the walk over the list and
// the duty it produces are observed rather than assumed, and one on a channel with a run time,
// which is the case that matters on a dosing pump - the channel has to stop by itself when the
// time is up.
//
// Note on pins: the use case has no pin configuration yet (`int pin = 0; int analogPin = 0;` with
// a TODO in the source), so every channel drives pin 0. The tests assert the sequence of writes
// rather than a pin per channel; the sequence is what shows that each analog device in the list
// was visited, in order, and that a non-analog device between them was stepped over.

#include <unity.h>

#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/DeviceModel.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCaseFactory.h"

#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"

#include <cstdio>
#include <vector>

namespace {

/** MockArduinoAdapter keeps digital writes but throws analog ones away; this keeps them in order. */
class AnalogRecordingArduino : public MockArduinoAdapter {
public:
    struct AnalogWrite {
        uint8_t pin;
        int value;
    };

    std::vector<AnalogWrite> analogWrites;

    void analogWrite(uint8_t pin, int value) override
    {
        MockArduinoAdapter::analogWrite(pin, value);
        AnalogWrite write;
        write.pin = pin;
        write.value = value;
        analogWrites.push_back(write);
    }
};

/** An analog output as the module structure builds it, with every field set. */
struct AnalogChannel {
    DeviceAnalogOut device{};

    AnalogChannel(const char* uid, DeviceStateEnum state, float percent)
    {
        device.deviceUid = uid;
        device.deviceType = DEVICE_TYPE_ANALOG;
        device.state = state;
        device.isError = false;
        device.timeOn = 0;
        device.timeOff = 0;
        device.durationOn = 0;
        device.durationOff = 0;
        device.createdDate = "2026-08-22T07:00:00Z";
        device.switchedAt = "2026-08-22T08:00:00Z";
        device.isAuto = true;
        device.isManual = false;
        device.sensorUid = nullptr;
        device.sensorValue = 0.0f;
        device.sensorValueTime = 0;
        device.valueTarget = 0.0f;
        device.valueLow = 0.0f;
        device.valueHigth = 0.0f;
        // Both halves of the state machine sit inside `if (direction == UP)`; a channel with any
        // other direction is not driven at all.
        device.direction = RuleDirection::UP;
        device.range = false;
        device.ruleUid = "8d2b5a71-3ea4-4c29-a063-1b7f94d82e35";
        device.ruleType = RuleType::SENSOR;
        device.ruleVersion = 0;
        device.workflowUid = nullptr;
        device.workflowVersion = 0;
        device.sensorValueLow = 0.0f;
        device.sensorValueHigh = 0.0f;
        device.ruleScheduleStartAt = nullptr;
        device.ruleScheduleDuration = 0;
        device.workByRule = false;
        device.commandId = 0;
        device.workManual = false;
        device.needReportingRuleUid = false;
        device.needToSwitchBack = false;
        device.mixTime = 0;
        device.ruleSensorStartAt = nullptr;
        device.ruleSensorValueStart = 0.0f;
        device.ruleSensorStopAt = nullptr;
        device.ruleSensorValueStop = 0.0f;
        device.ruleStopReason = nullptr;
        device.analogPinValue = percent;
        device.analogPinValueWhen5pers = 0.0f;
        device.analogPinValueWhen10pers = 0.0f;
        device.analogPinValueWhenTarget = 0.0f;
    }
};

/** A digital output, present only to be stepped over by the analog walk. */
struct DigitalChannel {
    DeviceDigitalOut device{};

    DigitalChannel(const char* uid, DeviceStateEnum state)
    {
        device.deviceUid = uid;
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = state;
        device.isError = false;
        device.timeOn = 0;
        device.timeOff = 0;
        device.durationOn = 0;
        device.durationOff = 0;
        device.createdDate = "2026-08-22T07:00:00Z";
        device.switchedAt = "2026-08-22T08:00:00Z";
        device.isAuto = false;
        device.isManual = false;
        device.sensorUid = nullptr;
        device.direction = RuleDirection::UP;
        device.ruleUid = nullptr;
        device.ruleType = RuleType::UNKNOWN;
        device.workflowUid = nullptr;
        device.workByRule = false;
        device.commandId = 0;
        device.workManual = false;
        device.needToSwitchBack = false;
        device.mixTime = 0;
        device.ruleSensorStartAt = nullptr;
        device.ruleSensorStopAt = nullptr;
        device.ruleStopReason = nullptr;
        device.ruleScheduleStartAt = nullptr;
        device.isRuleTurnOnDevice = false;
    }
};

// 40% of full scale. The use case scales with 255/100, so this is the duty the DAC must be given.
const float kPercentOn = 40.0f;
const int kDutyAtPercentOn = 102;

} // namespace

static void test_every_analog_device_in_the_list_is_driven(void)
{
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_ANALOG_INPUT_DEVICE);
    MockDeviceModel deviceModel;
    AnalogRecordingArduino arduino;
    MockLogger logger;
    MockDebugSystem debug;
    debug.featureEnabled = false;

    // Running channel, a digital output in between, and a channel that is off.
    AnalogChannel running("2c6e1f40-75d3-6b80-be59-af43a7c2d693", DEVICE_STATE_ON, kPercentOn);
    DigitalChannel relay("3d7f2a51-86e4-7c91-cf6a-ba54b8d3e7a4", DEVICE_STATE_ON);
    AnalogChannel idle("6a0c5d84-b9a7-af04-fc9d-ed87eba6bad7", DEVICE_STATE_OFF, kPercentOn);
    deviceModel.addDevice(&running.device);
    deviceModel.addDevice(&relay.device);
    deviceModel.addDevice(&idle.device);

    // Through the factory, because that is how a module of this type gets its state machine.
    ProcessDeviceStateUseCaseFactory factory(&logger, &debug, &moduleConfig, &arduino, &deviceModel);
    IProcessDeviceStateUseCase* useCase = factory.create(MODULE_TYPE_ANALOG_INPUT_DEVICE);
    TEST_ASSERT_NOT_NULL_MESSAGE(useCase, "the factory gave this module type no state machine");

    useCase->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)arduino.analogWrites.size(),
                                  "the analog outputs of the module were not driven once each: "
                                  "the walk over the device list did not visit them all, or it "
                                  "did not step over the digital one");
    TEST_ASSERT_EQUAL_INT_MESSAGE(kDutyAtPercentOn, arduino.analogWrites[0].value,
                                  "a running channel at 40% was not given the matching duty");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, arduino.analogWrites[1].value,
                                  "a channel that is off was not given a zero duty");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, arduino.digitalWriteCounts[0],
                                  "the enable line was not driven once per analog channel");

    // The state machine takes a channel it has driven out of auto/manual, which is what tells the
    // rest of the module the command has been carried out.
    TEST_ASSERT_FALSE_MESSAGE(running.device.isAuto,
                              "a driven channel was left in auto, so its command reads as pending");
    TEST_ASSERT_FALSE_MESSAGE(idle.device.isAuto,
                              "a channel switched off was left in auto");

    delete useCase;
}

static void test_an_analog_channel_stops_when_its_run_time_is_up(void)
{
    MockModuleConfig moduleConfig;
    moduleConfig.setModuleType(MODULE_TYPE_ANALOG_INPUT_DEVICE);
    MockDeviceModel deviceModel;
    AnalogRecordingArduino arduino;
    MockLogger logger;
    MockDebugSystem debug;
    debug.featureEnabled = false;

    // A dosing channel switched on for one second.
    AnalogChannel dosing("2c6e1f40-75d3-6b80-be59-af43a7c2d693", DEVICE_STATE_ON, kPercentOn);
    dosing.device.needToSwitchBack = true;
    dosing.device.timeOn = 0;
    dosing.device.durationOn = 1000;
    deviceModel.addDevice(&dosing.device);

    ProcessDeviceStateUseCaseFactory factory(&logger, &debug, &moduleConfig, &arduino, &deviceModel);
    IProcessDeviceStateUseCase* useCase = factory.create(MODULE_TYPE_ANALOG_INPUT_DEVICE);

    arduino.setMillis(500);
    useCase->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)arduino.analogWrites.size(),
                                  "the channel was not driven while its run time was still going");
    TEST_ASSERT_EQUAL_INT_MESSAGE(kDutyAtPercentOn, arduino.analogWrites[0].value,
                                  "the running channel was not given its duty");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, arduino.digitalOutputs[0],
                                    "the enable line was not raised while the channel was running");

    arduino.setMillis(2000);
    useCase->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)arduino.analogWrites.size(),
                                  "the channel was not written to after its run time expired");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, arduino.analogWrites[1].value,
                                  "the channel kept its duty after its run time expired: on a "
                                  "dosing pump this is the run that never stops");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, arduino.digitalOutputs[0],
                                    "the enable line stayed up after the run time expired");
    TEST_ASSERT_FALSE_MESSAGE(dosing.device.isAuto,
                              "the channel was left in auto after its run finished");
    TEST_ASSERT_FALSE_MESSAGE(dosing.device.isManual,
                              "the channel was left in manual after its run finished");

    delete useCase;
}

extern "C" void run_analog_device_output_tests(void)
{
    printf("\n----- Running Analog Device Output Tests -----\n");

    RUN_TEST(test_every_analog_device_in_the_list_is_driven);
    RUN_TEST(test_an_analog_channel_stops_when_its_run_time_is_up);

    printf("\n----- Analog Device Output Tests Complete -----\n");
}
