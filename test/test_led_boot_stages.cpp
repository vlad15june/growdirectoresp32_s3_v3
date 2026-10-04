// What the front panel says while a module boots, and while it is looking at its sensors.
//
// A boot of a configured module is a handful of waits in a row - the stored configuration, the
// WiFi association, the first exchanges with the backend - and any of them can be the one that
// never ends. The LED says one thing through all of them, and says it the whole way: green,
// twice a second, until the control loop takes over - a steady light would be indistinguishable
// from a module that has stopped. Which wait a boot is in is a question for the log, which
// names each of them with the milliseconds it took; the front panel answers "is it coming up".
//
// The first tests are that pattern. They drive the real ModuleSetupService through a real boot and
// read the LED writes off the adapter - which is what a person looking at the box sees. What is
// replaced is the outside world: the radio (whether it associates, and after how long), the
// broker, and time.
//
// ### Reproducing the red
//
// The boot pattern: make the boot light the LED steady yellow until setup is over. The boot tests then fail on the first write
// to the LED - a steady yellow - and
// test_the_boot_draws_one_pattern_until_the_loop_takes_over fails on the yellow writes it counts.
//
// One pattern rather than several - to see the red, make renderBootStage() draw
// (currentTime % 2000) < 250 while the module waits on the broker.
// test_the_two_waits_of_a_boot_look_the_same then fails on the gap it measures inside that wait.
//
// The scan entry: keep the whole change and make LedUseCases::scanEntryRunning() return false. The
// overlay tests then fail on the flash pair that never comes.
//
// The working pattern coming back: drop the renderConfiguredDoneOnline() call that closes the
// flash pair's window in LedUseCases::loop(). test_the_working_green_is_put_back_when_the_flash_pair_ends
// then fails: a closed relay reads as open until the working pattern's own next edge.
//
// The refused render task: drop the increment of _taskStartFailureCount.
// test_a_render_task_the_scheduler_refuses_is_counted then fails on the count, and everything the
// module does about the refusal is a log line behind a debug feature that ships disabled.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/ModuleSetupService.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/UseCases/Led/LedUseCases.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"

#include "Mock/Core/Domain/MockButtonUseCases.h"
#include "Mock/Core/Domain/MockNetworkStatusProvider.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

const char* const MODULE_UID = "AA:BB:CC:DD:EE:01";

// --- what a person watching the box sees -------------------------------------------------------

/** @brief Every write the LED use case makes, with the moment it made it. */
struct LedWrite
{
    enum Kind
    {
        YELLOW_ON,
        YELLOW_OFF,
        ALL_YELLOW_ON,
        ALL_YELLOW_OFF,
        GREEN_ON,
        GREEN_OFF,
        ALL_GREEN_ON,
        ALL_GREEN_OFF,
        RED_ON,
        RED_OFF,
        ALL_RED_ON,
        ALL_RED_OFF
    };

    uint64_t atMs = 0;
    Kind kind = YELLOW_ON;
    uint8_t index = 0;
};

/**
 * @brief The LED, as a tape of what was written to it and when.
 *
 * MockLedAdapter answers "was this called"; a pattern is a question about order and spacing, so
 * this one keeps the tape. It replaces the GPIO and nothing else.
 */
class RecordingLedAdapter : public ILedAdapter
{
public:
    explicit RecordingLedAdapter(const VirtualClock* clock = nullptr) : _clock(clock) {}

    std::vector<LedWrite> writes;
    /** Read when no virtual clock is wired - the unit-level tests step time by hand. */
    uint64_t manualNowMs = 0;
    uint8_t ledsCount = 0;

    void clear() { writes.clear(); }

    uint64_t nowMs() const { return (_clock != nullptr) ? _clock->nowMs() : manualNowMs; }

    size_t countBetween(uint64_t fromMs, uint64_t toMs) const
    {
        size_t count = 0;
        for (size_t i = 0; i < writes.size(); i++)
        {
            if (writes[i].atMs >= fromMs && writes[i].atMs <= toMs)
            {
                count++;
            }
        }
        return count;
    }

    size_t countOfKind(LedWrite::Kind kind) const
    {
        size_t count = 0;
        for (size_t i = 0; i < writes.size(); i++)
        {
            if (writes[i].kind == kind)
            {
                count++;
            }
        }
        return count;
    }

    /**
     * @brief What the first green LED was showing at a moment: 1 on, 0 off, -1 never written.
     *
     * The tape is what a person sees, and what a person sees is a level rather than an edge: the
     * question "was the green back" is asked of a moment, not of a write.
     */
    int greenZeroStateAt(uint64_t atMs) const
    {
        int state = -1;
        for (size_t i = 0; i < writes.size() && writes[i].atMs <= atMs; i++)
        {
            if (writes[i].kind == LedWrite::ALL_GREEN_ON ||
                (writes[i].kind == LedWrite::GREEN_ON && writes[i].index == 0))
            {
                state = 1;
            }
            else if (writes[i].kind == LedWrite::ALL_GREEN_OFF ||
                     (writes[i].kind == LedWrite::GREEN_OFF && writes[i].index == 0))
            {
                state = 0;
            }
        }
        return state;
    }

    /** @brief The longest gap between two consecutive writes inside a window, in ms. */
    uint64_t longestGapBetween(uint64_t fromMs, uint64_t toMs) const
    {
        uint64_t previous = fromMs;
        uint64_t longest = 0;
        for (size_t i = 0; i < writes.size(); i++)
        {
            if (writes[i].atMs < fromMs || writes[i].atMs > toMs)
            {
                continue;
            }
            if (writes[i].atMs - previous > longest)
            {
                longest = writes[i].atMs - previous;
            }
            previous = writes[i].atMs;
        }
        if (toMs - previous > longest)
        {
            longest = toMs - previous;
        }
        return longest;
    }

    // --- ILedAdapter ----------------------------------------------------------------------
    void setupLed(uint8_t, uint8_t) override {}
    void setLedOn(uint8_t) override {}
    void setLedOff(uint8_t) override {}
    void toggleLed(uint8_t) override {}

    void setupModuleLeds(const uint8_t*, const uint8_t*, uint8_t count) override
    {
        ledsCount = count;
    }

    uint8_t getGreenLedPin(uint8_t index) override { return index; }
    uint8_t getRedLedPin(uint8_t index) override { return index; }

    void setAllGreenLedOn() override { record(LedWrite::ALL_GREEN_ON, 0); }
    void setAllGreenLedOff() override { record(LedWrite::ALL_GREEN_OFF, 0); }
    void setGreenLedOn(uint8_t index) override { record(LedWrite::GREEN_ON, index); }
    void setGreenLedOff(uint8_t index) override { record(LedWrite::GREEN_OFF, index); }

    void setAllRedLedOn() override { record(LedWrite::ALL_RED_ON, 0); }
    void setAllRedLedOff() override { record(LedWrite::ALL_RED_OFF, 0); }
    void setRedLedOn(uint8_t index) override { record(LedWrite::RED_ON, index); }
    void setRedLedOff(uint8_t index) override { record(LedWrite::RED_OFF, index); }

    void setAllYellowLedOn() override { record(LedWrite::ALL_YELLOW_ON, 0); }
    void setAllYellowLedOff() override { record(LedWrite::ALL_YELLOW_OFF, 0); }
    void setYellowLedOn(uint8_t index) override { record(LedWrite::YELLOW_ON, index); }
    void setYellowLedOff(uint8_t index) override { record(LedWrite::YELLOW_OFF, index); }

private:
    void record(LedWrite::Kind kind, uint8_t index)
    {
        LedWrite write;
        write.atMs = nowMs();
        write.kind = kind;
        write.index = index;
        writes.push_back(write);
    }

    const VirtualClock* _clock;
};

/**
 * @brief The log, with the moment of each line.
 *
 * MockLogger keeps what was written; a line that reports a time has to be checked against the time
 * it was written at, or a number that is right by construction proves nothing. Both overloads that
 * MockLogger::capture() sits behind are timed here, so the two vectors stay in step.
 */
class TimedLogger : public MockLogger
{
public:
    explicit TimedLogger(const VirtualClock& clock) : _clock(clock) {}

    std::vector<uint64_t> times;

    void println(const char* topic, const char* message) override
    {
        times.push_back(_clock.nowMs());
        MockLogger::println(topic, message);
    }

    void println(const char* topic, const char* message, MqttLoggerMode channelOverride) override
    {
        times.push_back(_clock.nowMs());
        MockLogger::println(topic, message, channelOverride);
    }

private:
    const VirtualClock& _clock;
};

// --- the outside world -------------------------------------------------------------------------

/**
 * @brief The networks the boot hands the radio. Setup associates with none of them - the boot's
 *        association is the network task's, once the loop runs - so nothing here waits.
 */
class SimWifiManager : public IWifiManager
{
public:
    SimWifiManager(IArduinoAdapter& arduino, VirtualClock& clock)
        : _arduino(arduino), _clock(clock)
    {
    }

    uint64_t enteredAtMs = 0;
    uint64_t leftAtMs = 0;
    bool wasCalled = false;

    int registerNetworks() override
    {
        wasCalled = true;
        enteredAtMs = _clock.nowMs();
        leftAtMs = _clock.nowMs();
        return 1;
    }

    bool addWifiNetwork(const char*, const char*) override { return true; }

private:
    IArduinoAdapter& _arduino;
    VirtualClock& _clock;
};

/** @brief The broker: how long the connect takes and whether it succeeds. */
class SimMqttManager : public IMqttManager
{
public:
    SimMqttManager(IArduinoAdapter& arduino, VirtualClock& clock)
        : _arduino(arduino), _clock(clock)
    {
    }

    unsigned long connectTakesMs = 0;
    bool connectSucceeds = true;

    uint64_t enteredAtMs = 0;
    uint64_t leftAtMs = 0;

    bool connect() override
    {
        enteredAtMs = _clock.nowMs();
        // The real client blocks on the socket; the wait is modelled in the same 100 ms steps the
        // WiFi association uses, so the render task gets its turns either way.
        while (_clock.nowMs() - enteredAtMs < connectTakesMs)
        {
            _arduino.delay(100);
        }
        leftAtMs = _clock.nowMs();
        return connectSucceeds;
    }

    void disconnect() override {}
    bool isConnected() override { return connectSucceeds; }
    bool subscribe(const char*) override { return true; }
    bool unsubscribe(const char*) override { return true; }
    bool publish(const char*, const char*) override { return true; }
    void loop() override {}
    void setMessageProcessor(IMessageProcessor*) override {}
    void setupMqttTopicHandlers() override {}
    int getMqttFails() const override { return 0; }
    void resetMqttFails() override {}
    uint32_t getInboundOversizeDrops() const override { return 0; }
    const char* getClientId() const override { return "gd-"; }
    void setClientId(const char*) override {}

private:
    IArduinoAdapter& _arduino;
    VirtualClock& _clock;
};

/**
 * @brief The startup button check, which holds the boot for as long as the button is held.
 *
 * ButtonUseCases::checkStartupFactoryReset() polls the button every 50 ms until the factory-reset
 * hold time is up or the button is released, and it runs inside handleModuleMode() before the
 * network is touched. It is the one wait of the storage stage long enough to look at.
 */
class SlowButtonUseCases : public MockButtonUseCases
{
public:
    explicit SlowButtonUseCases(IArduinoAdapter& arduino) : _arduino(arduino) {}

    unsigned long holdsTheBootForMs = 0;

    void checkStartupFactoryReset() override
    {
        MockButtonUseCases::checkStartupFactoryReset();
        if (holdsTheBootForMs != 0)
        {
            _arduino.delay(holdsTheBootForMs);
        }
    }

private:
    IArduinoAdapter& _arduino;
};

/** @brief A discovery state the LED overlay can be pointed at. */
class StubScanEntryStatus : public IScanEntryStatus
{
public:
    bool running = false;
    bool isScanEntryRunning() const override { return running; }
};

/** @brief The relays of a device module, which are what its working indication is about. */
class RelayDeviceModel : public IDeviceModel
{
public:
    std::vector<Device*> devices;

    bool init() override { return true; }
    Device* getDeviceByUid(const char*) override { return nullptr; }
    std::vector<Device*> getAllDevices() override { return devices; }
    int getDeviceCount() const override { return (int)devices.size(); }
    bool addDevice(Device* device) override
    {
        devices.push_back(device);
        return true;
    }
    bool removeDevice(const char*) override { return false; }
    void clearDevices() override
    {
        for (size_t i = 0; i < devices.size(); i++)
        {
            delete devices[i];
        }
        devices.clear();
    }

    void addWithState(DeviceStateEnum state)
    {
        Device* device = new Device();
        device->state = state;
        devices.push_back(device);
    }

    ~RelayDeviceModel() override { clearDevices(); }
};

// --- the module under test ---------------------------------------------------------------------

/**
 * @brief A configured module booting, with the real setup service and the real LED use case.
 *
 * Setup runs on a task of the cooperative scheduler rather than on the test body, because that is
 * where it runs on the device: it is the Arduino loop task, and everything it waits for it waits
 * for with the rest of the system still scheduled around it.
 */
struct BootUnderTest
{
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;

    TimedLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    RecordingLedAdapter ledAdapter;
    SlowButtonUseCases buttonUseCases;
    MockWifiAdapter wifiAdapter;
    SimWifiManager wifiManager;
    SimMqttManager mqttManager;
    ModuleStructure moduleStructure;

    LedUseCases ledUseCases;
    ModuleSetupService setupService;

    bool setupFinished = false;
    uint64_t setupFinishedAtMs = 0;

    BootUnderTest()
        : scheduler(clock),
          arduino(clock, &scheduler),
          logger(clock),
          moduleConfig(&logger, &debugSystem, &settings),
          ledAdapter(&clock),
          buttonUseCases(arduino),
          wifiManager(arduino, clock),
          mqttManager(arduino, clock),
          ledUseCases(&ledAdapter, &moduleConfig, &arduino, &logger, &debugSystem),
          setupService(&logger, &debugSystem, &moduleConfig,
                       &ledAdapter, nullptr, &ledUseCases, &buttonUseCases,
                       &wifiAdapter, &wifiManager, nullptr, nullptr, &mqttManager,
                       &moduleStructure, nullptr, nullptr,
                       nullptr, nullptr, nullptr,
                       nullptr, nullptr, nullptr, nullptr, nullptr,
                       &arduino)
    {
        debugSystem.featureEnabled = false;

        settings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
        settings.setUser("led@example.com");
        settings.setAccountUid("led-account");
        moduleConfig.setSettings(&settings);
        moduleConfig.setWifiAdapter(&wifiAdapter);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
        moduleConfig.setSensorPinCount(4);
        moduleConfig.setLedsCount(1);
        wifiAdapter.setMACAddress(MODULE_UID);
        wifiAdapter.setLocalIP("192.168.1.100");

        ledUseCases.setTaskScheduler(&scheduler);
    }

    /**
     * @brief Put the boot on a task of its own without running anything yet.
     *
     * Separate from bootFor() so that a test can arrange the scheduler between the task the boot
     * runs on and the tasks the boot itself creates.
     */
    void startBoot()
    {
        scheduler.createTask("arduino-loop", 8192, 1, [this]() {
            setupService.setupModule();
            setupFinished = true;
            setupFinishedAtMs = clock.nowMs();
        });
    }

    /** @brief Boot on a task of its own and run until it is over, or until the window ends. */
    void bootFor(uint64_t windowMs)
    {
        startBoot();
        scheduler.runForMs(windowMs);
    }
};

// --- the boot pattern ---------------------------------------------------------------------------

/** @brief Was every write of this tape the boot pattern on the first LED, and nothing else? */
bool everyWriteIsTheBootPattern(const RecordingLedAdapter& adapter, size_t upToIndex)
{
    for (size_t i = 0; i < upToIndex && i < adapter.writes.size(); i++)
    {
        const LedWrite& write = adapter.writes[i];
        const bool green = (write.kind == LedWrite::GREEN_ON || write.kind == LedWrite::GREEN_OFF);
        // The one write that is not a pulse: the boot indication takes the LED over by putting
        // both its colours down before it draws anything.
        const bool takesTheLed = (write.kind == LedWrite::YELLOW_OFF);
        if ((!green && !takesTheLed) || write.index != 0)
        {
            return false;
        }
    }
    return true;
}

/**
 * The module is on and the network is not there: setup does not wait for it. The association is
 * the network task's, once the loop runs, and the front panel is handed to the working indication
 * as soon as the local half of the boot is done - a module without a network says so in red, not
 * with a boot pattern that lasts a minute.
 */
void test_a_boot_that_cannot_find_wifi_hands_the_led_over_without_waiting_for_it(void)
{
    BootUnderTest module;
    module.mqttManager.connectSucceeds = false;

    module.bootFor(60000);

    TEST_ASSERT_TRUE_MESSAGE(module.wifiManager.wasCalled, "the boot never reached the radio");
    TEST_ASSERT_TRUE_MESSAGE(module.setupFinished, "the boot did not finish inside the window");
    TEST_ASSERT_TRUE_MESSAGE(module.setupFinishedAtMs < 1000,
                             "setup waited for a network that is not there");
    TEST_ASSERT_EQUAL_MESSAGE(LED_BOOT_STAGE_RUNNING, module.ledUseCases.bootStage(),
                              "the boot ended without handing the LED over");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.ledAdapter.countOfKind(LedWrite::YELLOW_ON),
                              "the boot lit the yellow LED - the boot is one colour now");
}

/**
 * The other wait, and the whole of the decision: it looks the same.
 *
 * A module that has its IP and cannot reach the backend is in a different place from one that
 * cannot find the network, and the front panel does not try to say which - the two are told apart
 * by the log, which names every wait of a boot with the milliseconds it took. What the panel says
 * is that the module is still coming up, at one rate, whichever wait it is in.
 */
void test_the_two_waits_of_a_boot_look_the_same(void)
{
    BootUnderTest module;
    module.mqttManager.connectTakesMs = 20000;
    module.mqttManager.connectSucceeds = false;

    module.bootFor(120000);

    TEST_ASSERT_TRUE_MESSAGE(module.setupFinished, "the boot did not finish inside the window");
    TEST_ASSERT_TRUE_MESSAGE(module.mqttManager.leftAtMs - module.mqttManager.enteredAtMs >= 20000,
                             "the boot never waited on the broker - there is no second wait to "
                             "compare");

    const uint64_t longestGap = module.ledAdapter.longestGapBetween(
        module.mqttManager.enteredAtMs, module.mqttManager.leftAtMs);
    TEST_ASSERT_TRUE_MESSAGE(longestGap < 600,
                             "the wait on the broker was drawn at another rate - a boot would show "
                             "two patterns and an installer would have to know both");

    const size_t duringTheWait =
        module.ledAdapter.countBetween(module.mqttManager.enteredAtMs, module.mqttManager.leftAtMs);
    TEST_ASSERT_TRUE_MESSAGE(duringTheWait >= 60,
                             "the wait on the broker produced too few LED writes for the boot "
                             "pattern's rate");
}

/**
 * A boot where everything answers: one pattern from the first write to the hand-over.
 *
 * The claim is about the whole boot and not about any wait inside it - the button held at
 * power-on, the association, the broker, the sensor-type synchronization, all of it - and it is
 * two things: nothing but the boot pattern is drawn on that LED, and it is drawn all the way
 * through at one rate.
 */
void test_the_boot_draws_one_pattern_until_the_loop_takes_over(void)
{
    BootUnderTest module;
    // The button is held at power-on, so the first phase of the boot has a length worth looking at.
    module.buttonUseCases.holdsTheBootForMs = 400;
    module.mqttManager.connectTakesMs = 4000;
    module.mqttManager.connectSucceeds = true;

    module.bootFor(60000);

    TEST_ASSERT_TRUE_MESSAGE(module.setupFinished, "the boot did not finish inside the window");
    TEST_ASSERT_TRUE_MESSAGE(!module.ledAdapter.writes.empty(), "the boot wrote nothing to the LED");

    // The last write of a boot is the clearing of the LED on the way out; everything before it is
    // the boot pattern, on the first LED and in green.
    const size_t last = module.ledAdapter.writes.size() - 1;
    TEST_ASSERT_EQUAL_MESSAGE(LedWrite::ALL_YELLOW_OFF, module.ledAdapter.writes[last].kind,
                              "the boot did not clear the LED before handing it over");
    TEST_ASSERT_TRUE_MESSAGE(everyWriteIsTheBootPattern(module.ledAdapter, last),
                             "something other than the boot pattern was drawn during the boot - a "
                             "boot shows one thing now");

    // And it was one rate the whole way: from the first write to the hand-over there is no gap
    // longer than the pattern's own period.
    const uint64_t longestGap = module.ledAdapter.longestGapBetween(
        module.ledAdapter.writes[0].atMs, module.ledAdapter.writes[last].atMs);
    TEST_ASSERT_TRUE_MESSAGE(longestGap < 600,
                             "the boot pattern stopped somewhere inside the boot - the LED was "
                             "still for longer than its period");
    TEST_ASSERT_TRUE_MESSAGE(module.ledAdapter.writes[0].atMs < module.wifiManager.enteredAtMs,
                             "the boot said nothing until it reached the radio - the phases before "
                             "that are waits too");
}

/**
 * The hand-over, on its own: the render task must have stopped before the working indication
 * takes the LED, or the two write to it in turn and the working pattern - which redraws only at
 * its own edges, up to five seconds apart - loses.
 */
void test_the_boot_indication_stops_before_the_working_indication_starts(void)
{
    BootUnderTest module;
    module.mqttManager.connectTakesMs = 1000;

    module.bootFor(30000);

    TEST_ASSERT_TRUE_MESSAGE(module.setupFinished, "the boot did not finish inside the window");
    TEST_ASSERT_EQUAL_MESSAGE(LED_BOOT_STAGE_RUNNING, module.ledUseCases.bootStage(),
                              "the boot ended without handing the LED over");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.ledUseCases.bootTaskStopTimeoutCount(),
                              "the render task did not confirm it had stopped");

    const size_t writesAtHandOver = module.ledAdapter.writes.size();
    module.scheduler.runForMs(10000);
    TEST_ASSERT_EQUAL_MESSAGE(writesAtHandOver, (int)module.ledAdapter.writes.size(),
                              "something went on writing to the LED after the boot was over");
}

/**
 * The one refusal this has to survive: the scheduler cannot spare the stack for the render task.
 *
 * What is left then is one write, at the level the pattern happened to be on when the boot
 * announced itself, held for the whole boot - a steady light or a dark LED, neither of which says
 * the module is coming up. That is the state this whole change exists to replace, so a boot that
 * ends up back in it is counted rather than left to be guessed at from a debug channel that ships
 * disabled.
 */
void test_a_render_task_the_scheduler_refuses_is_counted(void)
{
    BootUnderTest module;
    module.mqttManager.connectTakesMs = 20000;
    module.mqttManager.connectSucceeds = false;

    module.startBoot();
    // The task the boot runs on exists now; the next createTask() is the render task's own.
    module.scheduler.failNextCreateTasks(1);
    module.scheduler.runForMs(60000);

    TEST_ASSERT_TRUE_MESSAGE(module.setupFinished,
                             "a boot whose render task was refused did not finish");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.scheduler.refusedTaskCount(),
                              "the scheduler refused something other than the one task");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.ledUseCases.taskStartFailureCount(),
                              "a render task the scheduler refused was not counted - the boot went "
                              "back to a light that says nothing, silently");

    // And the fallback is what it is said to be: one level is put on the LED when the boot
    // announces itself, and nothing moves it for the whole of the wait that follows.
    TEST_ASSERT_TRUE_MESSAGE(module.ledAdapter.writes.size() >= 1,
                             "the boot did not even light the LED once");
    // The wait is twenty seconds long and the LED holds its level for the whole of it; the only
    // write that lands on its last millisecond is the boot clearing the LED on its way out.
    const uint64_t longestGap = module.ledAdapter.longestGapBetween(
        module.mqttManager.enteredAtMs, module.mqttManager.leftAtMs);
    TEST_ASSERT_TRUE_MESSAGE(longestGap >= 19000,
                             "the LED moved during the wait although there was no task to move it");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.ledUseCases.bootTaskStopTimeoutCount(),
                              "the boot waited for a task that was never created");
}

// --- the end of the boot, in the log -------------------------------------------------------------

/**
 * The boot says once that it is over, and says it at the moment it hands the front panel over.
 *
 * A field log is read backwards from something that went wrong, and the first question asked of it
 * is how long the module took to come up - a boot of a healthy module is four seconds and a boot
 * that took two minutes is a module that could not find its network. That number is in the log: without it the end of setup could only
 * be inferred from the uptime that the next thing to write
 * anything happened to carry, which on a quiet module is the first diagnostic, half a minute later.
 *
 * The number is read from millis(), so it is the module's uptime and not the length of any one
 * phase: the scene starts the clock five seconds before setup begins, which is what the reset
 * reason, the filesystem and the stored configuration cost before this service is called at all.
 * A line that timed the boot from its own beginning would report five seconds less.
 */
void test_a_boot_says_when_it_is_over_and_says_it_once(void)
{
    BootUnderTest module;
    // The debug feature the setup log goes out under ships enabled, as on a module in the field.
    module.debugSystem.featureEnabled = true;
    module.clock.advanceMs(5000);

    module.buttonUseCases.holdsTheBootForMs = 400;
    module.mqttManager.connectTakesMs = 4000;
    module.mqttManager.connectSucceeds = true;

    module.bootFor(60000);

    TEST_ASSERT_TRUE_MESSAGE(module.setupFinished, "the boot did not finish inside the window");

    const std::string marker = "BOOT: complete in ";
    size_t said = 0;
    size_t index = 0;
    for (size_t i = 0; i < module.logger.messages.size(); i++)
    {
        if (module.logger.messages[i].compare(0, marker.size(), marker) == 0)
        {
            said++;
            index = i;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)said,
                              "a boot did not say exactly once that it was over");

    const unsigned long reported =
        strtoul(module.logger.messages[index].c_str() + marker.size(), nullptr, 10);

    // The number is the clock at the moment the line was written, which is what makes it an
    // uptime: this scene's clock was already at five seconds when setup began.
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(module.logger.times[index], (uint64_t)reported,
                                     "the milliseconds the boot reports are not the module's "
                                     "uptime at the moment it said them");
    TEST_ASSERT_TRUE_MESSAGE(reported > 5000,
                             "the boot reported a number that cannot be an uptime - the module had "
                             "been running for five seconds before setup was called");

    // And the moment is the milestone the front panel changes hands on: the last thing written to
    // the LED is the boot clearing it on its way out, and it follows this line by the length of
    // the wait for the render task to stop and nothing more.
    const size_t last = module.ledAdapter.writes.size() - 1;
    TEST_ASSERT_EQUAL_MESSAGE(LedWrite::ALL_YELLOW_OFF, module.ledAdapter.writes[last].kind,
                              "the boot did not clear the LED before handing it over");
    TEST_ASSERT_TRUE_MESSAGE(module.ledAdapter.writes[last].atMs >= reported,
                             "the boot said it was over after the front panel had already changed "
                             "hands");
    TEST_ASSERT_TRUE_MESSAGE(module.ledAdapter.writes[last].atMs - reported < 200,
                             "the line the boot ends with is not on the milestone the front panel "
                             "changes hands on");
}

// --- the scan-entry overlay --------------------------------------------------------------------

/** @brief A settled, online, configured module whose LED is driven one loop() at a time. */
struct WorkingModule
{
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    RecordingLedAdapter ledAdapter;
    MockArduinoAdapter arduino;
    MockNetworkStatusProvider network;
    StubScanEntryStatus scanStatus;
    RelayDeviceModel deviceModel;
    LedUseCases ledUseCases;

    WorkingModule()
        : moduleConfig(&logger, &debugSystem, &settings),
          ledUseCases(&ledAdapter, &moduleConfig, &arduino, &logger, &debugSystem, &deviceModel,
                      &network)
    {
        debugSystem.featureEnabled = false;
        settings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
        moduleConfig.setLedsCount(1);
        network.offline = false;
        ledUseCases.setScanEntryStatus(&scanStatus);
    }

    /**
     * @brief A relay module with its first relay closed - the one working pattern that holds the
     *        first green LED steadily on, whatever the heartbeat is doing.
     */
    void becomeARelayModuleWithRelayZeroOn()
    {
        moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
        deviceModel.addWithState(DEVICE_STATE_ON);
    }

    /** @brief The control loop, at the cadence the module actually runs it. */
    void run(unsigned long untilMs, unsigned long stepMs = 10)
    {
        for (unsigned long now = _nowMs; now <= untilMs; now += stepMs)
        {
            arduino.setMillis(now);
            ledAdapter.manualNowMs = now;
            ledUseCases.loop();
            _nowMs = now;
        }
    }

private:
    unsigned long _nowMs = 0;
};

/**
 * A scan entry is two short flashes every five seconds, on the first LED, on top of whatever the
 * working indication is doing. Fifteen seconds is three of them, and each is a pair.
 */
void test_a_scan_entry_puts_a_double_flash_over_the_working_green(void)
{
    WorkingModule module;
    module.scanStatus.running = true;

    // Three whole periods: the pairs land at 0, 5000 and 10000 ms.
    module.run(14990);

    TEST_ASSERT_EQUAL_MESSAGE(6, (int)module.ledAdapter.countOfKind(LedWrite::GREEN_ON),
                              "a scan entry did not produce two flashes every five seconds");
    TEST_ASSERT_EQUAL_MESSAGE(6, (int)module.ledAdapter.countOfKind(LedWrite::GREEN_OFF),
                              "the flashes of a scan entry did not end");

    // The pair is short: both flashes are inside the first third of a second of each period.
    for (size_t i = 0; i < module.ledAdapter.writes.size(); i++)
    {
        const LedWrite& write = module.ledAdapter.writes[i];
        if (write.kind != LedWrite::GREEN_ON && write.kind != LedWrite::GREEN_OFF)
        {
            continue;
        }
        TEST_ASSERT_TRUE_MESSAGE(write.atMs % 5000 <= 320,
                                 "a flash of the pair landed outside the pair's window");
        TEST_ASSERT_EQUAL_MESSAGE(0, write.index,
                                  "the scan flash was drawn on an LED other than the first");
    }
}

/**
 * What the discovery does on its own - the pass a restart arms, and the cheap pass over the free
 * ports every half minute afterwards - does not reach the LED at all. The first outlives the boot
 * by half a minute or more and nobody asked for it; the second never ends, and a module that
 * indicated it would blink for the rest of its life.
 */
void test_a_discovery_nobody_asked_for_does_not_reach_the_led(void)
{
    WorkingModule module;
    module.scanStatus.running = false;

    module.run(15000);

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.ledAdapter.countOfKind(LedWrite::GREEN_ON),
                              "something drew a scan flash while no entry was running");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.ledAdapter.countOfKind(LedWrite::GREEN_OFF),
                              "something drew a scan flash while no entry was running");
    TEST_ASSERT_TRUE_MESSAGE(module.ledAdapter.countOfKind(LedWrite::ALL_GREEN_ON) > 0,
                             "the working green heartbeat stopped");
}

/**
 * The working pattern draws at its own edges, which are up to five seconds apart, so a pair of
 * flashes that left the LED where its last pulse put it would leave it there for that long. What
 * the pair found is put back the moment its window closes.
 *
 * The scene is a relay module with relay 0 closed, because that is the one working pattern which
 * holds the first green LED steadily on: what has to come back is a light, and a scene where the
 * working pattern wanted the LED dark anyway could not tell "put back" from "left alone". It is
 * also arranged so that the working pattern's own next edge falls past the end of the run - the
 * heartbeat turns over at 5010 and 6020 ms and then not again until 11030 - so between the last
 * pulse of the pair and the end of the run there is nothing else that could light the LED.
 */
void test_the_working_green_is_put_back_when_the_flash_pair_ends(void)
{
    WorkingModule module;
    module.becomeARelayModuleWithRelayZeroOn();

    // Settle first, with no entry running, so the heartbeat's edges are where they are said to be.
    module.run(7000);
    TEST_ASSERT_EQUAL_MESSAGE(1, module.ledAdapter.greenZeroStateAt(7000),
                              "a closed relay did not hold the first green LED on - the scene "
                              "cannot show anything being put back");

    module.scanStatus.running = true;
    module.run(10500);

    // The pair lands at 10000: on, off at 10080, on at 10160, off at 10240, and the window ends
    // at 10320.
    TEST_ASSERT_EQUAL_MESSAGE(0, module.ledAdapter.greenZeroStateAt(10300),
                              "the flash pair never took the working green down - there is nothing "
                              "for it to have put back");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.ledAdapter.greenZeroStateAt(10330),
                              "the working green was not put back when the flash pair ended - a "
                              "closed relay would read as open until the working pattern's own "
                              "next edge, up to five seconds later");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.ledAdapter.greenZeroStateAt(10500),
                              "the working green did not stay back after the pair ended");
}

/**
 * A module that has lost the server has something worse to say than that it is looking at its
 * sensors, and the red says it.
 */
void test_an_offline_module_stays_red_while_a_scan_entry_runs(void)
{
    WorkingModule module;
    module.scanStatus.running = true;
    module.network.offline = true;

    module.run(15000);

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.ledAdapter.countOfKind(LedWrite::GREEN_ON),
                              "a scan flash was drawn over the offline red");
    TEST_ASSERT_TRUE_MESSAGE(module.ledAdapter.countOfKind(LedWrite::ALL_RED_ON) > 0,
                             "an offline module did not go red");
}

/**
 * The overlay and the discovery in one movement: the real use case wired into the real LED use
 * case, with nothing standing in between.
 *
 * The tests above drive the overlay from a stub and the ones in test_startup_sensor_scan.cpp
 * drive the discovery for real, and two halves that never meet can both be right while the module
 * does something else. Here the LED asks the discovery itself.
 */
void test_the_led_asks_the_real_discovery_whether_to_flash(void)
{
    WorkingModule module;
    StartupSensorScanUseCase discovery;
    module.ledUseCases.setScanEntryStatus(&discovery);

    // The pass a restart arms is running - and the front panel says nothing about it.
    discovery.arm();
    module.run(14990);

    TEST_ASSERT_TRUE_MESSAGE(discovery.isRunning(),
                             "the pass a restart armed was not running - there is no window in "
                             "which the LED could have flashed");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.ledAdapter.countOfKind(LedWrite::GREEN_ON),
                              "the module flashed for a pass nobody asked for");
    TEST_ASSERT_TRUE_MESSAGE(module.ledAdapter.countOfKind(LedWrite::ALL_GREEN_ON) > 0,
                             "the working heartbeat stopped - the scene is not a working module");

    // The pass a person asks for, from the moment they ask: three pairs in the fifteen seconds
    // that follow, at the same places in the five-second period as ever.
    discovery.rescan(1);
    module.run(29990);

    TEST_ASSERT_EQUAL_MESSAGE(6, (int)module.ledAdapter.countOfKind(LedWrite::GREEN_ON),
                              "a pass a ScanSensors command asked for did not put the flash pair "
                              "on the LED");
}

/**
 * The one entry the discovery answers true for, on the discovery itself: the pass a ScanSensors
 * command asks for. Nothing else in this file drives the real machinery, and a stub that agreed
 * with a machine that does not would prove nothing.
 */
void test_the_discovery_calls_only_a_commanded_pass_an_entry(void)
{
    StartupSensorScanUseCase startupScan;

    TEST_ASSERT_FALSE_MESSAGE(startupScan.isScanEntryRunning(),
                              "a discovery nobody armed reported an entry in flight");

    startupScan.arm();
    TEST_ASSERT_FALSE_MESSAGE(startupScan.isScanEntryRunning(),
                              "the pass a restart arms was reported as an entry - it runs on every "
                              "boot, nobody asked for it, and it outlives the boot");

    StartupSensorScanUseCase commanded;
    commanded.rescan(2);
    TEST_ASSERT_TRUE_MESSAGE(commanded.isScanEntryRunning(),
                             "the pass a ScanSensors command asked for was not reported as an "
                             "entry - it is owed from the moment the command asks");
}

} // namespace

extern "C" int run_led_boot_stage_tests(void)
{
    RUN_TEST(test_a_boot_that_cannot_find_wifi_hands_the_led_over_without_waiting_for_it);
    RUN_TEST(test_the_two_waits_of_a_boot_look_the_same);
    RUN_TEST(test_the_boot_draws_one_pattern_until_the_loop_takes_over);
    RUN_TEST(test_the_boot_indication_stops_before_the_working_indication_starts);
    RUN_TEST(test_a_render_task_the_scheduler_refuses_is_counted);
    RUN_TEST(test_a_boot_says_when_it_is_over_and_says_it_once);

    RUN_TEST(test_a_scan_entry_puts_a_double_flash_over_the_working_green);
    RUN_TEST(test_a_discovery_nobody_asked_for_does_not_reach_the_led);
    RUN_TEST(test_the_working_green_is_put_back_when_the_flash_pair_ends);
    RUN_TEST(test_an_offline_module_stays_red_while_a_scan_entry_runs);
    RUN_TEST(test_the_led_asks_the_real_discovery_whether_to_flash);
    RUN_TEST(test_the_discovery_calls_only_a_commanded_pass_an_entry);

    return 0;
}
