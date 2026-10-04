// Who was at work when the heap floor broke.
//
// A run on a device in a quiet regime left two lines of the heap watermark trace - 29 748 free bytes
// where the previous worst was 41 780, and 24 932 six minutes later, against a steady sixty-four
// thousand - and each of them stood alone: nothing within seconds of it on the log said what had
// taken thirty-five thousand bytes and given them back. The trace says when the floor broke and
// how deep. The work that broke it runs on another task and, more often than not, says nothing at
// the time.
//
// So the line names its neighbours. Each background task keeps one word on a board - the act it
// is in, or the act it has just finished, and when - and the reading of the watermark that finds a
// new low copies the board there and then. The line written a step later carries that copy:
//
//   HEAP_LOW: new watermark 29748 (was 41780) net=RULE_LIST(7ms) acq=idle cmd=idle cat=idle free=29748 step=0ms
//   HEAP_LOW: released after 300 ms (free 64800)
//
// The last two figures of the first line and the whole of the second are for the low that no
// task on the board answers for. Free heap back at its level and a long step: the step's own
// work. Free heap still at the floor when the step is over: somebody is holding on past the end
// of the step, which the step's own work cannot do - and the second line says for how long.
//
// And the fetches that bring a body back say what they weighed, one line each, with the status
// and the size of the answer - the size being the point:
//
//   NET: GET /api/v1/HardWare/rulelist -> response 200, body 5153 bytes
//
// What is under test is production code: the real LoopServiceConfigureDone taking the readings,
// the real Outbox, SensorAcquisitionService, NetworkCommandRunner and SensorTypeCatalogWriter each
// on the task it creates for itself, the real RestAPIService behind the real HybridAPIService.
// Replaced are the board and the world: the clock, the scheduler, the heap - a number the work
// takes from and gives back to, read by the trace exactly as it reads the platform's - and the
// far end of every participant: the socket, the radio, the broker's link machine, the RS-485
// line, the command executor, the flash.
//
// ### Reproducing the red
//
// Without the board wired to the loop service every scene is red for the same reason - the line
// ends after "(was N)" - and without the line the fetches write, every NET: test is red because
// no line carries the status and the size of the answer together. The unit tests each name the
// mutation that turns them red.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Storage/SensorTypeCatalogWriter.h"
#include "Core/Domain/Diagnostics/HeapWatermarkTracer.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"

#include "Application/Services/BackgroundActs.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"

#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"

#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorTypeDao.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/UseCases/ModuleCommand/MockModuleCommandExecutionUseCase.h"

#include "Sim/HostAllocationCounter.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimHttpClient.h"
#include "Sim/VirtualClock.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimHttpClient;
using hostsim::VirtualClock;

namespace {

// The figures of the logged run, kept as they were logged: the level the module sat at, the worst point it had
// on record before the first dip, and the two dips.
const uint32_t STEADY_FREE_HEAP = 64800;
const uint32_t STEADY_WATERMARK = 41780;
const uint32_t FIRST_DIP_WATERMARK = 29748;
const uint32_t SECOND_DIP_WATERMARK = 24932;

/** What a piece of work has to hold for the free heap to touch a watermark. */
uint32_t bytesThatReach(uint32_t watermark) { return STEADY_FREE_HEAP - watermark; }

// The control loop steps every ten milliseconds from zero; work is started three milliseconds
// after a step so that the reading that finds it is seven milliseconds into it - a number that is
// then on the line, and wrong by a whole step if the account were taken at any other moment.
const uint32_t CONTROL_PERIOD_MS = 10;
const uint64_t WORK_STARTS_AT_MS = 1003;
const uint64_t FOUND_BY_THE_READING_AT_MS = 1010;
const uint64_t LINE_WRITTEN_AT_MS = 1020;
const uint32_t LONG_WORK_MS = 300;
const uint64_t SCENE_ENDS_AT_MS = 2000;

// How a line about a low ends, by whether the work still held its memory when the reading was
// taken. No step of these scenes takes any time of its own, except where a test says so.
#define HELD_AT_THE_READING " free=29748 step=0ms"
#define BACK_AT_THE_READING " free=64800 step=0ms"

/** Host frames, not the device's: a fiber gets what it is asked for and host frames are large. */
const uint32_t HOST_TASK_STACK_BYTES = 512 * 1024;

const char* const SERVER_URL = "http://gd.test";
const char* const TOKEN = "TOK123";
const char* const MODULE_UID = "UID-1";

// --- the log, with the moment of every line ------------------------------------------------------

class TapeLogger : public MockLogger
{
public:
    struct Line
    {
        uint64_t atMs;
        std::string text;
    };

    explicit TapeLogger(VirtualClock& clock) : _clock(clock) {}

    void println(const char* topic, const char* message) override { record(message); }

    void println(const char* topic, const char* message, MqttLoggerMode) override
    {
        record(message);
    }

    std::vector<Line> linesSaying(const char* needle) const
    {
        std::vector<Line> found;
        for (size_t i = 0; i < tape.size(); i++)
        {
            if (tape[i].text.find(needle) != std::string::npos)
            {
                found.push_back(tape[i]);
            }
        }
        return found;
    }

    /** @brief Every line that reads exactly like this - for a text another line starts with. */
    std::vector<Line> linesReading(const char* text) const
    {
        std::vector<Line> found;
        for (size_t i = 0; i < tape.size(); i++)
        {
            if (tape[i].text == text)
            {
                found.push_back(tape[i]);
            }
        }
        return found;
    }

    /** @brief Every line saying the needle, one per row - what a failed assertion shows. */
    std::string quoted(const char* needle) const
    {
        std::string all;
        const std::vector<Line> found = linesSaying(needle);
        for (size_t i = 0; i < found.size(); i++)
        {
            all += "\n      | " + found[i].text;
        }
        return all.empty() ? std::string(" (none)") : all;
    }

    std::vector<Line> tape;

private:
    void record(const char* message)
    {
        Line line;
        line.atMs = _clock.nowMs();
        line.text = message ? message : "";
        tape.push_back(line);
    }

    VirtualClock& _clock;
};

/** @brief The two channels these lines ride, as the module ships them: both on. */
class TwoChannelDebugSystem : public MockDebugSystem
{
public:
    bool isFeatureEnabled(const char* featureName) override
    {
        if (featureName == nullptr)
        {
            return false;
        }
        const std::string name(featureName);
        return name == DebugFeatures::DIAGNOSTIC_DEBUG || name == DebugFeatures::NET_DEBUG;
    }
};

// --- the board ------------------------------------------------------------------------------------

/**
 * @brief SimArduino that counts how often the free heap is asked for.
 *
 * On the device that is heap_caps_get_free_size(), a walk over the registered heaps; the trace
 * may make it on the step that finds a low and while a low is still held, and at no other time.
 */
class CountingArduino : public SimArduino
{
public:
    CountingArduino(VirtualClock& clock, ITaskScheduler* scheduler) : SimArduino(clock, scheduler) {}

    uint32_t getFreeHeap() override
    {
        freeHeapReads++;
        return SimArduino::getFreeHeap();
    }

    uint32_t freeHeapReads = 0;
};

// --- the heap, and work that weighs on it ---------------------------------------------------------

/**
 * @brief The device heap as a number, written straight into SimArduino.
 *
 * The free figure comes back when the work gives its bytes back; the watermark keeps the low for
 * good, as heap_caps_get_minimum_free_size() does. That asymmetry is why a dip needs a witness.
 */
class BenchHeap
{
public:
    explicit BenchHeap(SimArduino& arduino) : _arduino(arduino)
    {
        _arduino.freeHeapValue = STEADY_FREE_HEAP;
        _arduino.minFreeHeapValue = STEADY_WATERMARK;
    }

    void take(uint32_t bytes)
    {
        _arduino.freeHeapValue -= bytes;
        if (_arduino.freeHeapValue < _arduino.minFreeHeapValue)
        {
            _arduino.minFreeHeapValue = _arduino.freeHeapValue;
        }
    }

    void give(uint32_t bytes) { _arduino.freeHeapValue += bytes; }

private:
    SimArduino& _arduino;
};

/** @brief What the far end of a participant does: hold memory for a while on the calling task. */
class HeavyWork
{
public:
    HeavyWork(BenchHeap& heap, HostTaskScheduler& scheduler) : _heap(heap), _scheduler(scheduler) {}

    void run(uint32_t bytes, uint32_t ms)
    {
        _heap.take(bytes);
        _scheduler.delayMs(ms);
        _heap.give(bytes);
    }

private:
    BenchHeap& _heap;
    HostTaskScheduler& _scheduler;
};

/** @brief How heavy and how long the next piece of work at a participant's far end is. */
struct Weight
{
    uint32_t bytes = 0;
    uint32_t ms = 0;
};

// --- the far ends ----------------------------------------------------------------------------------

/** @brief The socket: a GET holds the network task, and memory, for as long as it is told to. */
class HeavyHttpClient : public SimHttpClient
{
public:
    explicit HeavyHttpClient(HeavyWork& work) : _work(work) {}

    /** @brief Weights are consumed one per GET, in order; a GET past the last weighs nothing. */
    void queueWeight(uint32_t bytes, uint32_t ms)
    {
        Weight weight;
        weight.bytes = bytes;
        weight.ms = ms;
        _weights.push_back(weight);
    }

    int GET() override
    {
        const int code = SimHttpClient::GET();
        if (_next < _weights.size())
        {
            const Weight weight = _weights[_next++];
            _work.run(weight.bytes, weight.ms);
        }
        return code;
    }

private:
    HeavyWork& _work;
    std::vector<Weight> _weights;
    size_t _next = 0;
};

/** @brief The radio: a restart holds its caller and brings the link back. */
class HeavyWifi : public MockWifiAdapter
{
public:
    explicit HeavyWifi(HeavyWork& work) : _work(work) { setConnected(true); }

    bool reconnect() override
    {
        _work.run(weight.bytes, weight.ms);
        return MockWifiAdapter::reconnect();
    }

    Weight weight;

private:
    HeavyWork& _work;
};

/**
 * @brief The broker's link machine: an act that is in progress between two moments.
 *
 * It holds its memory from its first beat to its last, the way the socket being rebuilt does -
 * not on the task that beats it, which only sleeps between beats.
 */
class ScriptedMqttLink : public IMqttLinkMachine
{
public:
    ScriptedMqttLink(VirtualClock& clock, BenchHeap& heap) : _clock(clock), _heap(heap) {}

    bool serviceLinkOnce() override
    {
        const uint64_t now = _clock.nowMs();
        const bool inProgress = (actBytes > 0) && now >= actFromMs && now < actUntilMs;
        if (inProgress && !_holding)
        {
            _heap.take(actBytes);
            _holding = true;
        }
        else if (!inProgress && _holding)
        {
            _heap.give(actBytes);
            _holding = false;
        }
        return inProgress;
    }

    uint64_t actFromMs = 0;
    uint64_t actUntilMs = 0;
    uint32_t actBytes = 0;

private:
    VirtualClock& _clock;
    BenchHeap& _heap;
    bool _holding = false;
};

/** @brief The line: a read holds the acquisition task for its transaction. */
class HeavySensorReader : public MockSensorReader
{
public:
    explicit HeavySensorReader(HeavyWork& work) : _work(work) {}

    bool readSensor(const SensorEntity& sensor, float& outValue) override
    {
        _work.run(weight.bytes, weight.ms);
        return MockSensorReader::readSensor(sensor, outValue);
    }

    bool readMultiChannelSensor(const SensorEntity& sensor,
                                std::map<std::string, float>& channelValues) override
    {
        _work.run(weight.bytes, weight.ms);
        return MockSensorReader::readMultiChannelSensor(sensor, channelValues);
    }

    Weight weight;

private:
    HeavyWork& _work;
};

/** @brief The discovery, reduced to the one half that runs on the acquisition task. */
class HeavyStartupScan : public IStartupSensorScan
{
public:
    explicit HeavyStartupScan(HeavyWork& work) : _work(work) {}

    StartupScanOutcome runStepOnBus(const StartupScanStep&) override
    {
        _work.run(weight.bytes, weight.ms);
        return StartupScanOutcome();
    }

    void describeStoredSensors(Node&) override {}
    void onDescribedStructureSettled(bool) override {}
    bool isScanEntryRunning() const override { return false; }
    void arm() override {}
    void rescan(int) override {}
    void armPreparationOnly() override {}
    bool isRunning() const override { return false; }
    bool takeNextStep(StartupScanStep&) override { return false; }
    void returnStep() override {}
    void applyStepOutcome(const StartupScanOutcome&) override {}
    bool holdsAWindowOpen() const override { return false; }
    bool keepsThePortDark(int) const override { return false; }
    void reviewSensorLiveness() override {}

    Weight weight;

private:
    HeavyWork& _work;
};

/** @brief The command executor: whichever task runs a slow part is held for it. */
class HeavyCommandExecutor : public MockModuleCommandExecutionUseCase
{
public:
    explicit HeavyCommandExecutor(HeavyWork& work) : _work(work) {}

    RawBusCommandResult executeRaw(const std::string& command, const std::string& options) override
    {
        _work.run(weight.bytes, weight.ms);
        return MockModuleCommandExecutionUseCase::executeRaw(command, options);
    }

    ComplexCommandOutcome executeBusWork(const ComplexCommandWork& work) override
    {
        _work.run(weight.bytes, weight.ms);
        return MockModuleCommandExecutionUseCase::executeBusWork(work);
    }

    ComplexCommandOutcome executeNetworkWork(const ComplexCommandWork& work) override
    {
        _work.run(weight.bytes, weight.ms);
        return MockModuleCommandExecutionUseCase::executeNetworkWork(work);
    }

    Weight weight;

private:
    HeavyWork& _work;
};

/** @brief The flash: storing a catalog holds the storage task. */
class HeavySensorTypeDao : public MockSpiffsSensorTypeDao
{
public:
    explicit HeavySensorTypeDao(HeavyWork& work) : _work(work) {}

    bool saveAll(const std::vector<SensorType>& entities) override
    {
        _work.run(weight.bytes, weight.ms);
        return MockSpiffsSensorTypeDao::saveAll(entities);
    }

    Weight weight;

private:
    HeavyWork& _work;
};

/**
 * @brief The control loop's own work: a step that, once, holds memory for as long as it lasts.
 *
 * Stands where the button handling stands in LoopServiceBase::loop() - the first thing a step
 * does - because what it models is any work the step does itself: the memory is taken and given
 * back on the control task, inside one step, and the step is that much longer for it.
 */
class HeavyStep : public IButtonUseCases
{
public:
    explicit HeavyStep(HeavyWork& work) : _work(work) {}

    void loop() override
    {
        stepsBegun++;
        if (armed)
        {
            armed = false;
            _work.run(weight.bytes, weight.ms);
        }
    }

    bool checkButtonPressedWhenOn() override { return false; }
    void checkStartupFactoryReset() override {}
    bool isButtonPressedWhenOn() override { return false; }
    bool isButtonHeldForDuration() override { return false; }
    bool isTestModeRequested() override { return false; }
    bool isFactoryResetRequested() override { return false; }
    bool isAddModeRequested() override { return false; }
    void clearSavedButtonState() override {}

    bool armed = false;
    Weight weight;
    uint32_t stepsBegun = 0;

private:
    HeavyWork& _work;
};

// --- the module -----------------------------------------------------------------------------------

Rule makeRule(size_t index)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a%04x", (unsigned)index);

    Rule rule;
    rule.uid = uid;
    rule.version = 3;
    rule.deviceUid = "7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71";
    rule.sensorUid = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";
    rule.ruleType = RuleType::SENSOR;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.timeToSetPoint = 10;
    return rule;
}

/** @brief A rule list of eight, as the backend puts it on the wire. */
std::string ruleListBody(IJsonConverter& converter)
{
    std::string body = "[";
    for (size_t i = 0; i < 8; i++)
    {
        if (i > 0)
        {
            body += ",";
        }
        body += converter.ruleToJson(makeRule(i));
    }
    body += "]";
    return body;
}

SensorEntity makeSensor()
{
    SensorEntity sensor;
    sensor.uid = "UID-1.00";
    sensor.protocol = "ModBus";
    sensor.model = "ry_ws301";
    sensor.sensorType = "temperature";
    sensor.isActive = true;
    sensor.isError = false;
    sensor.pin = 0;
    sensor.modbusDeviceAddress = 112;
    sensor.baudRate = 9600;
    return sensor;
}

/**
 * @brief The control loop taking its readings, and the four background tasks wired as globals.h
 *        wires them.
 *
 * The control loop is a task of the same scheduler so that it keeps stepping while a participant
 * is held at its far end - which is the whole situation: the work is on another task, and the
 * reading that finds the low is taken beside it.
 */
struct Stand
{
    VirtualClock clock;
    HostTaskScheduler scheduler;
    CountingArduino arduino;
    BenchHeap heap;
    HeavyWork work;
    HeavyStep heavyStep;

    TapeLogger logger;
    TwoChannelDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    ArduinoJsonConverter jsonConverter;

    HeavyHttpClient http;
    RestAPIService restApi;
    HybridAPIService api;
    HeavyWifi wifi;
    ScriptedMqttLink mqttLink;
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;

    HeavySensorReader sensorReader;
    HeavyStartupScan startupScan;
    HeavyCommandExecutor executor;
    SensorAcquisitionService acquisition;

    NetworkCommandRunner commandRunner;

    HeavySensorTypeDao sensorTypeDao;
    SensorTypeCatalogWriter catalogWriter;

    BackgroundActBoard board;
    LoopServiceConfigureDone loopService;

    Stand()
        : scheduler(clock),
          arduino(clock, &scheduler),
          heap(arduino),
          work(heap, scheduler),
          heavyStep(work),
          logger(clock),
          moduleConfig(&logger, &debugSystem, &settings),
          http(work),
          restApi(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter),
          api(&restApi, nullptr, &moduleConfig, &logger, &debugSystem),
          wifi(work),
          mqttLink(clock, heap),
          completions(Outbox::COMPLETION_QUEUE_DEPTH),
          outbox(&api, &scheduler, &completions, &logger, &debugSystem, HOST_TASK_STACK_BYTES),
          sensorReader(work),
          startupScan(work),
          executor(work),
          acquisition(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          commandRunner(&scheduler, &logger, &debugSystem, HOST_TASK_STACK_BYTES),
          sensorTypeDao(work),
          catalogWriter(&sensorTypeDao, &scheduler, &logger, &debugSystem, HOST_TASK_STACK_BYTES)
          ,
          board(&arduino)
    {
        moduleConfig.setModuleUid(MODULE_UID);
        settings.setServerUrl(SERVER_URL);
        settings.setToken(TOKEN);
        restApi.init();
        http.alwaysRespond(200, "[]");

        outbox.setWifiAdapter(&wifi);
        outbox.setMqttLink(&mqttLink);
        acquisition.setRawCommandExecutor(&executor);
        acquisition.setStartupSensorScan(&startupScan);
        commandRunner.setExecutor(&executor);

        outbox.setActBoard(&board);
        acquisition.setActBoard(&board);
        commandRunner.setActBoard(&board);
        catalogWriter.setActBoard(&board);
        loopService.setBackgroundActBoard(&board);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setButtonUseCases(&heavyStep);

        scheduler.createTask("control", HOST_TASK_STACK_BYTES, 1, [this]() {
            while (true)
            {
                loopService.loop();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }

    /** @brief A low that no task on the board is at work for - the loop's own, or the system's. */
    void dipByNobody(uint32_t toWatermark)
    {
        heap.take(bytesThatReach(toWatermark));
        heap.give(bytesThatReach(toWatermark));
    }

    /** @brief Somebody no task on the board answers for takes memory, and keeps it. */
    void takeByNobody(uint32_t toWatermark) { heap.take(bytesThatReach(toWatermark)); }

    /**
     * @brief How often the trace of the watermark asked for the free heap.
     *
     * In an image that ships that is every time anybody asked. A heap-forensics build (GD_HEAP_FORENSICS)
     * also looks once per step for its episode trace, whatever the heap is doing; those looks
     * are taken out here so that the figures the tests hold are the same trace's in both builds -
     * and so that a heap-forensics build which looked more or less often than once per step is red.
     */
    uint32_t looksOfTheTrace() const
    {
#ifdef GD_HEAP_FORENSICS
        return arduino.freeHeapReads - heavyStep.stepsBegun;
#else
        return arduino.freeHeapReads;
#endif
    }

    /** @brief The lines about a low, as against the lines about the memory's return. */
    std::vector<TapeLogger::Line> heapLowLines() const
    {
        return logger.linesSaying("HEAP_LOW: new watermark");
    }

    /** @brief How the watches ended, either way: "released after" and "still held after". */
    std::vector<TapeLogger::Line> watchEndLines() const
    {
        std::vector<TapeLogger::Line> ends;
        const std::vector<TapeLogger::Line> all = logger.linesSaying("HEAP_LOW: ");
        for (size_t i = 0; i < all.size(); i++)
        {
            if (all[i].text.find("HEAP_LOW: new watermark") == std::string::npos)
            {
                ends.push_back(all[i]);
            }
        }
        return ends;
    }
};

/** @brief The one line a scene expects, character for character, and the step it is written on. */
void assertTheOneLine(Stand& stand, const char* expected, uint64_t expectedAtMs)
{
    const std::vector<TapeLogger::Line> lines = stand.heapLowLines();

    std::string message = "the tape was expected to carry exactly one line about the low:" +
                          stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)lines.size(), message.c_str());
    TEST_ASSERT_EQUAL_STRING(expected, lines[0].text.c_str());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(expectedAtMs, lines[0].atMs,
                                     "the line was not written at the top of the step after the "
                                     "reading that found the low");
}

// --------------------------------------------------------------------------------------------
// The board on its own
// --------------------------------------------------------------------------------------------

struct BoardFixture
{
    VirtualClock clock;
    SimArduino arduino;
    BackgroundActBoard board;

    BoardFixture() : arduino(clock), board(&arduino) {}

    BackgroundActBoard::Reading readAt(BackgroundActBoard::Slot slot, uint32_t windowStartedAtMs)
    {
        BackgroundActBoard::Snapshot snapshot;
        board.capture(windowStartedAtMs, snapshot);
        return BackgroundActBoard::read(snapshot, slot);
    }
};

/**
 * Mutation: read() answers NO_ACT for a word whose finished bit is clear.
 */
void test_a_running_act_is_read_back_with_how_long_it_has_run(void)
{
    BoardFixture f;
    f.clock.advanceMs(5000);
    f.board.actStarted(BackgroundActBoard::NETWORK,
                       BackgroundActs::ofOutboxKind(OutboxKind::RULE_LIST));
    f.clock.advanceMs(820);

    // The window plays no part for an act that is still running: it was started four steps ago
    // and is named all the same.
    const BackgroundActBoard::Reading reading = f.readAt(BackgroundActBoard::NETWORK, 5810);

    TEST_ASSERT_EQUAL_UINT8(BackgroundActs::ofOutboxKind(OutboxKind::RULE_LIST), reading.act);
    TEST_ASSERT_FALSE(reading.finished);
    TEST_ASSERT_EQUAL_UINT32(820, reading.ms);
}

/**
 * The dip is transient: the work may be over by the time the reading is taken, and it is still the
 * work the low fell on.
 *
 * Mutation: actFinished() writes NO_ACT - the word forgets which act it was.
 */
void test_an_act_that_ended_inside_the_window_is_read_back_as_done(void)
{
    BoardFixture f;
    f.clock.advanceMs(5003);
    f.board.actStarted(BackgroundActBoard::ACQUISITION, BackgroundActs::ACQ_SWEEP);
    f.clock.advanceMs(3);
    f.board.actFinished(BackgroundActBoard::ACQUISITION, BackgroundActs::ACQ_SWEEP);
    f.clock.advanceMs(4);

    const BackgroundActBoard::Reading reading = f.readAt(BackgroundActBoard::ACQUISITION, 5000);

    TEST_ASSERT_EQUAL_UINT8(BackgroundActs::ACQ_SWEEP, reading.act);
    TEST_ASSERT_TRUE(reading.finished);
    TEST_ASSERT_EQUAL_UINT32(4, reading.ms);
}

/**
 * The false accusation. A low that was not there at the previous reading cannot be the work of an
 * act that was over before it.
 *
 * Mutation: read() ignores the window - every finished act is named until the next one replaces
 * it, and the sweep is blamed for whatever happens in the fifteen seconds after it.
 */
void test_an_act_that_ended_before_the_window_is_not_read_back(void)
{
    BoardFixture f;
    f.clock.advanceMs(5003);
    f.board.actStarted(BackgroundActBoard::ACQUISITION, BackgroundActs::ACQ_SWEEP);
    f.clock.advanceMs(3);
    f.board.actFinished(BackgroundActBoard::ACQUISITION, BackgroundActs::ACQ_SWEEP);
    f.clock.advanceMs(14);

    // The window opened at 5010; the act was over at 5006.
    const BackgroundActBoard::Reading reading = f.readAt(BackgroundActBoard::ACQUISITION, 5010);

    TEST_ASSERT_EQUAL_UINT8(BackgroundActBoard::NO_ACT, reading.act);

    // The edge is the act's: over in the very millisecond the window opened is inside it. The
    // clock cannot say which of the two came first, and a line that names the act says "done 10ms
    // ago" beside it - the reader sees the tie.
    const BackgroundActBoard::Reading onTheEdge = f.readAt(BackgroundActBoard::ACQUISITION, 5006);
    TEST_ASSERT_EQUAL_UINT8(BackgroundActs::ACQ_SWEEP, onTheEdge.act);
    TEST_ASSERT_EQUAL_UINT32(14, onTheEdge.ms);
}

void test_a_slot_nothing_was_written_to_reads_idle(void)
{
    BoardFixture f;
    f.clock.advanceMs(5000);

    for (int slot = 0; slot < BackgroundActBoard::SLOT_COUNT; slot++)
    {
        const BackgroundActBoard::Reading reading =
            f.readAt((BackgroundActBoard::Slot)slot, 0);
        TEST_ASSERT_EQUAL_UINT8(BackgroundActBoard::NO_ACT, reading.act);
    }
}

/**
 * The stamp is the low 24 bits of millis(), which wrap every four hours and forty minutes. An act
 * that straddles the wrap has to keep its age, or the line reports a request of sixteen million
 * milliseconds.
 *
 * Mutation: the age is a plain subtraction of the stamps, without the modulus.
 */
void test_an_act_that_straddles_the_stamp_wrap_keeps_its_age(void)
{
    BoardFixture f;
    f.clock.advanceMs(0x01000000ULL - 40);
    f.board.actStarted(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_FIRMWARE_UPDATE);
    f.clock.advanceMs(100);

    const BackgroundActBoard::Reading running =
        f.readAt(BackgroundActBoard::COMMAND_NETWORK, 0x01000000UL + 50);
    TEST_ASSERT_EQUAL_UINT8(BackgroundActs::CMD_FIRMWARE_UPDATE, running.act);
    TEST_ASSERT_EQUAL_UINT32(100, running.ms);

    f.board.actFinished(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_FIRMWARE_UPDATE);
    f.clock.advanceMs(5);

    const BackgroundActBoard::Reading done =
        f.readAt(BackgroundActBoard::COMMAND_NETWORK, 0x01000000UL + 50);
    TEST_ASSERT_TRUE(done.finished);
    TEST_ASSERT_EQUAL_UINT32(5, done.ms);
}

/**
 * Every act a task can write has a code that fits the word and a name no longer than the line
 * buffer was sized for.
 *
 * That a kind added to the outbox, or a step added to the discovery, is given a name at all is
 * not this test's to check and it does not pretend to: it can only walk the enumerators it knows.
 * The header makes the missing case a compile error instead, on the device's compiler and on both
 * hosts', which is the one place such an omission can be caught before it is a "?" on a log.
 */
void test_every_act_has_a_name_the_line_has_room_for(void)
{
    std::vector<std::pair<BackgroundActBoard::Slot, uint8_t> > acts;
    for (uint8_t kind = 0; kind <= (uint8_t)OutboxKind::WIFI_ROAM; kind++)
    {
        acts.push_back(std::make_pair(BackgroundActBoard::NETWORK,
                                      BackgroundActs::ofOutboxKind((OutboxKind)kind)));
    }
    acts.push_back(std::make_pair(BackgroundActBoard::NETWORK, BackgroundActs::NET_MQTT_CONNECT));
    acts.push_back(std::make_pair(BackgroundActBoard::ACQUISITION, BackgroundActs::ACQ_SWEEP));
    acts.push_back(std::make_pair(BackgroundActBoard::ACQUISITION, BackgroundActs::ACQ_RAW_COMMAND));
    acts.push_back(std::make_pair(BackgroundActBoard::ACQUISITION, BackgroundActs::ACQ_SCAN_COMMAND));
    for (int kind = StartupScanStep::NONE; kind <= StartupScanStep::RELEASE_FREE_PORTS; kind++)
    {
        acts.push_back(std::make_pair(BackgroundActBoard::ACQUISITION,
                                      BackgroundActs::ofLadderStep((StartupScanStep::Kind)kind)));
    }
    acts.push_back(std::make_pair(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_CATALOG_FETCH));
    acts.push_back(std::make_pair(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_FIRMWARE_UPDATE));
    acts.push_back(std::make_pair(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_DELETION_CHECK));
    acts.push_back(std::make_pair(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_OTHER));
    acts.push_back(std::make_pair(BackgroundActBoard::CATALOG_STORAGE, BackgroundActs::CATALOG_WRITE));

    size_t longest = 0;
    for (size_t i = 0; i < acts.size(); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(acts[i].second >= 1 && acts[i].second <= BackgroundActBoard::MAX_ACT,
                                 "an act code does not fit the word");
        const char* name = BackgroundActs::name(acts[i].first, acts[i].second);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE_MESSAGE(strcmp(name, "?") != 0, "an act has no name");
        longest = std::max(longest, strlen(name));
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(BackgroundActs::LONGEST_NAME_CHARS, longest,
                                     "the line buffer is sized from a name that is not the longest");
    TEST_ASSERT_EQUAL_STRING("?", BackgroundActs::name(BackgroundActBoard::NETWORK, 0x7F));
}

/**
 * The widest the account can be: every slot naming its longest act as finished the longest time
 * ago a word can express. Truncated, the line would lose its last task without saying so.
 */
void test_the_widest_account_fits_the_buffer_it_is_given(void)
{
    BoardFixture f;
    f.board.actFinished(BackgroundActBoard::NETWORK,
                        BackgroundActs::ofOutboxKind(OutboxKind::MODULE_COMMAND_RESULT));
    f.board.actFinished(BackgroundActBoard::ACQUISITION,
                        BackgroundActs::ofLadderStep(StartupScanStep::RELEASE_FREE_PORTS));
    f.board.actFinished(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_FIRMWARE_UPDATE);
    f.board.actFinished(BackgroundActBoard::CATALOG_STORAGE, BackgroundActs::CATALOG_WRITE);
    f.clock.advanceMs(0x00FFFFFFULL);

    BackgroundActBoard::Snapshot snapshot;
    f.board.capture(0, snapshot);

    char tail[BackgroundActs::CONTEXT_BYTES];
    const size_t needed = BackgroundActs::formatContext(snapshot, tail, sizeof(tail));

    TEST_ASSERT_TRUE_MESSAGE(needed < sizeof(tail), "the widest account does not fit its buffer");
    TEST_ASSERT_EQUAL_STRING(" net=MODULE_COMMAND_RESULT(done 16777215ms ago)"
                             " acq=LADDER:RELEASE_FREE_PORTS(done 16777215ms ago)"
                             " cmd=FIRMWARE_UPDATE(done 16777215ms ago)"
                             " cat=WRITE(done 16777215ms ago)",
                             tail);

    // And a buffer too small is filled to its end and no further, with the full length reported.
    char tooSmall[16];
    const size_t stillNeeded = BackgroundActs::formatContext(snapshot, tooSmall, sizeof(tooSmall));
    TEST_ASSERT_EQUAL_size_t(needed, stillNeeded);
    TEST_ASSERT_EQUAL_size_t(sizeof(tooSmall) - 1, strlen(tooSmall));
}

/**
 * What a task pays at the boundary of an act, and what the control loop pays on every step and on
 * the step that finds a low: no allocation anywhere. The reading is taken when the heap is at its
 * floor, and the boundaries are crossed by the very work that put it there.
 *
 * Mutation: keep the snapshot in a std::vector, or the names in std::string.
 */
void test_marking_an_act_and_reading_the_board_allocate_nothing(void)
{
    BoardFixture f;
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;
    char tail[BackgroundActs::CONTEXT_BYTES];
    uint32_t reports = 0;

    tracer.sample(STEADY_WATERMARK, 0, &f.board);

    HostAllocationCounter::reset();

    for (uint32_t step = 1; step <= 1000; step++)
    {
        f.clock.advanceMs(10);
        f.board.actStarted(BackgroundActBoard::NETWORK,
                           BackgroundActs::ofOutboxKind(OutboxKind::SENSOR_UPDATE));
        f.board.actFinished(BackgroundActBoard::NETWORK,
                            BackgroundActs::ofOutboxKind(OutboxKind::SENSOR_UPDATE));

        // Every step reads; every fiftieth finds a new low, copies the board and formats it.
        const uint32_t watermark = 39000 - (step / 50) * 1100;
        tracer.sample(watermark, (uint32_t)f.clock.nowMs(), &f.board);
        if (tracer.takeReport(report))
        {
            BackgroundActs::formatContext(report.acts, tail, sizeof(tail));
            reports++;
        }
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(21, reports, "the lows this test formats did not happen");

    char message[128];
    snprintf(message, sizeof(message), "a thousand steps of marking and reading made %llu allocations",
             HostAllocationCounter::newCount());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0ULL, HostAllocationCounter::newCount(), message);
}

/** @brief A clock across whose reading the other core gets a word in. */
class ClockReadAcrossAWrite : public SimArduino
{
public:
    explicit ClockReadAcrossAWrite(VirtualClock& clock) : SimArduino(clock) {}

    unsigned long millis() override
    {
        const unsigned long value = SimArduino::millis();
        if (afterTheClockIsRead)
        {
            std::function<void()> once;
            once.swap(afterTheClockIsRead);
            once();
        }
        return value;
    }

    /** Runs once, after the time has been fetched and before the reader has it back. */
    std::function<void()> afterTheClockIsRead;
};

/**
 * capture() copies the words first and reads the clock after them. The other way round, an act
 * that ends between the two carries a stamp later than the date of the copy; its age, taken
 * modulo the stamp's width, comes out as sixteen million milliseconds - "over long before the
 * window" - and the task that made the low reads idle.
 *
 * Mutation: read the clock before the words in BackgroundActBoard::capture().
 */
void test_the_copy_is_dated_after_the_words_are_read(void)
{
    VirtualClock clock;
    ClockReadAcrossAWrite arduino(clock);
    BackgroundActBoard board(&arduino);

    clock.advanceMs(4990);
    board.actStarted(BackgroundActBoard::NETWORK,
                     BackgroundActs::ofOutboxKind(OutboxKind::RULE_LIST));
    clock.advanceMs(10);

    // While the copy is being dated, a millisecond passes and the fetch ends.
    arduino.afterTheClockIsRead = [&]() {
        clock.advanceMs(1);
        board.actFinished(BackgroundActBoard::NETWORK,
                          BackgroundActs::ofOutboxKind(OutboxKind::RULE_LIST));
    };

    BackgroundActBoard::Snapshot snapshot;
    board.capture(4990, snapshot);
    const BackgroundActBoard::Reading reading =
        BackgroundActBoard::read(snapshot, BackgroundActBoard::NETWORK);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(BackgroundActs::ofOutboxKind(OutboxKind::RULE_LIST), reading.act,
                                    "the fetch that was at work when the copy was taken reads idle");
    // The copy saw the fetch running, ten milliseconds in: the word was read before its end.
    TEST_ASSERT_FALSE(reading.finished);
    TEST_ASSERT_EQUAL_UINT32(10, reading.ms);
}

// --------------------------------------------------------------------------------------------
// The trace and the board together
// --------------------------------------------------------------------------------------------

/**
 * The account belongs to the reading. Between the reading and the print there is a whole step, and
 * the work that made the low is as likely as not to be over and replaced by then.
 *
 * Mutation: capture the board in takeReport() instead of in sample().
 */
void test_the_account_is_of_the_reading_and_not_of_the_print(void)
{
    BoardFixture f;
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;

    f.clock.advanceMs(1000);
    tracer.sample(STEADY_WATERMARK, 1000, &f.board);

    f.clock.advanceMs(3);
    f.board.actStarted(BackgroundActBoard::NETWORK,
                       BackgroundActs::ofOutboxKind(OutboxKind::RULE_LIST));
    f.clock.advanceMs(7);
    tracer.sample(FIRST_DIP_WATERMARK, 1010, &f.board);

    // Before the line is asked for, the fetch ends and a telemetry request takes the task.
    f.clock.advanceMs(4);
    f.board.actFinished(BackgroundActBoard::NETWORK,
                        BackgroundActs::ofOutboxKind(OutboxKind::RULE_LIST));
    f.board.actStarted(BackgroundActBoard::NETWORK,
                       BackgroundActs::ofOutboxKind(OutboxKind::SENSOR_UPDATE));
    f.clock.advanceMs(6);

    TEST_ASSERT_TRUE(tracer.takeReport(report));
    TEST_ASSERT_TRUE(report.hasActs);

    char tail[BackgroundActs::CONTEXT_BYTES];
    BackgroundActs::formatContext(report.acts, tail, sizeof(tail));
    TEST_ASSERT_EQUAL_STRING(" net=RULE_LIST(7ms) acq=idle cmd=idle cat=idle", tail);
}

/** A reading given no board reports what it always did, and says so. */
void test_a_reading_without_a_board_reports_as_before(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;

    tracer.sample(STEADY_WATERMARK);
    tracer.sample(FIRST_DIP_WATERMARK);

    TEST_ASSERT_TRUE(tracer.takeReport(report));
    TEST_ASSERT_FALSE(report.hasActs);
    TEST_ASSERT_EQUAL_UINT32(FIRST_DIP_WATERMARK, report.watermarkBytes);
    TEST_ASSERT_EQUAL_UINT32(STEADY_WATERMARK, report.previousBytes);
}

// --------------------------------------------------------------------------------------------
// The watch for the memory's return, on its own
// --------------------------------------------------------------------------------------------

/** @brief A tracer that has just found the logged run's first low at `atMs`, its line already taken. */
void findTheFirstLow(HeapWatermarkTracer& tracer, uint32_t atMs, uint32_t freeAtTheReading)
{
    HeapWatermarkTracer::Report report;
    tracer.sample(STEADY_WATERMARK, atMs - 10);
    TEST_ASSERT_TRUE(tracer.sample(FIRST_DIP_WATERMARK, atMs));
    tracer.noteTheMoment(freeAtTheReading, 0);
    TEST_ASSERT_TRUE(tracer.takeReport(report));
    TEST_ASSERT_TRUE(report.hasMoment);
    TEST_ASSERT_EQUAL_UINT32(freeAtTheReading, report.freeBytes);
}

/**
 * The one flag an ordinary step tests. It has to be up for as long as there is a line to write
 * or a watch to keep, and down the rest of the time - down is what makes the settled module pay
 * nothing, and up is what gets the release noticed at all.
 *
 * Mutation: takeReport() clears the flag without regard to the watch - the release is never seen.
 */
void test_the_trace_asks_for_attention_while_a_line_waits_or_a_watch_is_open(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;
    HeapWatermarkTracer::Release release;

    tracer.sample(STEADY_WATERMARK, 990);
    TEST_ASSERT_FALSE_MESSAGE(tracer.needsAttention(), "an ordinary reading asked for attention");
    TEST_ASSERT_FALSE(tracer.sample(STEADY_WATERMARK, 1000));
    TEST_ASSERT_FALSE(tracer.needsAttention());

    TEST_ASSERT_TRUE(tracer.sample(FIRST_DIP_WATERMARK, 1010));
    tracer.noteTheMoment(FIRST_DIP_WATERMARK, 0);
    TEST_ASSERT_TRUE(tracer.needsAttention());

    TEST_ASSERT_TRUE(tracer.takeReport(report));
    TEST_ASSERT_TRUE_MESSAGE(tracer.needsAttention(), "the line was taken and the watch was forgotten");
    TEST_ASSERT_TRUE(tracer.isWatchingRelease());

    TEST_ASSERT_FALSE(tracer.pollRelease(FIRST_DIP_WATERMARK, 1020, release));
    TEST_ASSERT_TRUE(tracer.needsAttention());

    TEST_ASSERT_TRUE(tracer.pollRelease(STEADY_FREE_HEAP, 1030, release));
    TEST_ASSERT_FALSE_MESSAGE(tracer.needsAttention(), "the watch is over and the trace still asks");
    TEST_ASSERT_FALSE(tracer.isWatchingRelease());
}

/**
 * Memory that is back by the end of the step needs no watch: the line about the low already says
 * so, and a second line "released after 0 ms" would say it again.
 *
 * Mutation: noteTheMoment() opens the watch whatever the free heap is.
 */
void test_memory_back_by_the_end_of_the_step_opens_no_watch(void)
{
    HeapWatermarkTracer tracer;
    findTheFirstLow(tracer, 1010, HeapWatermarkTracer::RELEASED_AT_BYTES);

    TEST_ASSERT_FALSE_MESSAGE(tracer.isWatchingRelease(),
                              "free heap at an ordinary level opened a watch");
    TEST_ASSERT_FALSE(tracer.needsAttention());
}

/**
 * The two edges of a watch. The level: free heap *at* the ordinary level is released, one byte
 * under it is not. The limit: a look one millisecond short of it keeps watching, the look on it
 * says "still held" - and either way the watch is over after it has spoken.
 *
 * Mutations: `>` for `>=` on the level; `<=` for `<` on the limit.
 */
void test_a_watch_ends_on_the_level_or_on_the_limit_and_says_which(void)
{
    HeapWatermarkTracer::Release release;

    // The figures themselves, so that the edges below are edges of the right numbers: the level
    // is the one the trace already reports below, the limit is a minute.
    TEST_ASSERT_EQUAL_UINT32(40000, HeapWatermarkTracer::RELEASED_AT_BYTES);
    TEST_ASSERT_EQUAL_UINT32(HeapWatermarkTracer::REPORT_BELOW_BYTES,
                             HeapWatermarkTracer::RELEASED_AT_BYTES);
    TEST_ASSERT_EQUAL_UINT32(60000, HeapWatermarkTracer::WATCH_LIMIT_MS);

    HeapWatermarkTracer released;
    findTheFirstLow(released, 1010, FIRST_DIP_WATERMARK);
    TEST_ASSERT_FALSE(released.pollRelease(HeapWatermarkTracer::RELEASED_AT_BYTES - 1, 1020, release));
    TEST_ASSERT_TRUE(released.pollRelease(HeapWatermarkTracer::RELEASED_AT_BYTES, 2510, release));
    TEST_ASSERT_TRUE(release.released);
    TEST_ASSERT_EQUAL_UINT32(1500, release.afterMs);
    TEST_ASSERT_EQUAL_UINT32(HeapWatermarkTracer::RELEASED_AT_BYTES, release.freeBytes);
    TEST_ASSERT_FALSE_MESSAGE(released.pollRelease(STEADY_FREE_HEAP, 2520, release),
                              "a watch that had ended spoke again");

    HeapWatermarkTracer held;
    findTheFirstLow(held, 1010, FIRST_DIP_WATERMARK);
    TEST_ASSERT_FALSE(held.pollRelease(FIRST_DIP_WATERMARK,
                                       1010 + HeapWatermarkTracer::WATCH_LIMIT_MS - 1, release));
    TEST_ASSERT_TRUE(held.pollRelease(FIRST_DIP_WATERMARK,
                                      1010 + HeapWatermarkTracer::WATCH_LIMIT_MS, release));
    TEST_ASSERT_FALSE(release.released);
    TEST_ASSERT_EQUAL_UINT32(HeapWatermarkTracer::WATCH_LIMIT_MS, release.afterMs);
    TEST_ASSERT_EQUAL_UINT32(FIRST_DIP_WATERMARK, release.freeBytes);
    TEST_ASSERT_FALSE_MESSAGE(held.pollRelease(STEADY_FREE_HEAP,
                                               1010 + HeapWatermarkTracer::WATCH_LIMIT_MS + 10, release),
                              "the memory came back after the limit and the watch spoke again");
}

/** A watch that straddles the millis() wraparound keeps its length. */
void test_a_watch_across_the_clock_wraparound_keeps_its_length(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Release release;
    findTheFirstLow(tracer, 0xFFFFFF00u, FIRST_DIP_WATERMARK);

    TEST_ASSERT_FALSE(tracer.pollRelease(FIRST_DIP_WATERMARK, 0xFFFFFFF0u, release));
    TEST_ASSERT_TRUE(tracer.pollRelease(STEADY_FREE_HEAP, 0x00000100u, release));
    TEST_ASSERT_TRUE(release.released);
    TEST_ASSERT_EQUAL_UINT32(0x200, release.afterMs);
}

/**
 * What sample() answers is what the caller acts on: "true" sends it for the free heap and for the
 * step's length. A watermark that fell by less than the hysteresis is not a low worth a line, and
 * the answer has to say so - 248 bytes under a low already said, here.
 *
 * Mutation: sample() returns true from the hysteresis gate.
 */
void test_a_reading_inside_the_hysteresis_says_it_found_nothing(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;

    tracer.sample(STEADY_WATERMARK, 1000);
    TEST_ASSERT_TRUE(tracer.sample(FIRST_DIP_WATERMARK, 1010));
    TEST_ASSERT_TRUE(tracer.takeReport(report));

    TEST_ASSERT_FALSE_MESSAGE(tracer.sample(FIRST_DIP_WATERMARK - 248, 1020),
                              "a reading inside the hysteresis said it had found a low");
    TEST_ASSERT_FALSE(tracer.needsAttention());
    TEST_ASSERT_FALSE(tracer.takeReport(report));
}

/**
 * A moment described with no low waiting is nobody's moment. It must not open a watch: that watch
 * would be dated by a low long gone and, with no line to raise the flag, would sleep until the
 * next real low - and then close with that low's release and the wrong length.
 *
 * Without the guard in noteTheMoment(): the watch opens, and the release of the next low is
 * said to have taken six and a half seconds instead of half a second.
 */
void test_a_moment_described_with_no_low_waiting_opens_no_watch(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Release release;

    // A low found, said and released: nothing is waiting and nothing is watched.
    findTheFirstLow(tracer, 1010, FIRST_DIP_WATERMARK);
    TEST_ASSERT_TRUE(tracer.pollRelease(STEADY_FREE_HEAP, 1510, release));

    // A caller that describes a moment all the same - free heap at the floor, no line waiting.
    tracer.sample(FIRST_DIP_WATERMARK - 248, 5010);
    tracer.noteTheMoment(FIRST_DIP_WATERMARK - 248, 0);

    TEST_ASSERT_FALSE_MESSAGE(tracer.isWatchingRelease(),
                              "a moment nobody asked for opened a watch");
    TEST_ASSERT_FALSE(tracer.needsAttention());

    // And the next real low is dated from itself.
    HeapWatermarkTracer::Report report;
    TEST_ASSERT_TRUE(tracer.sample(SECOND_DIP_WATERMARK, 7010));
    tracer.noteTheMoment(SECOND_DIP_WATERMARK, 0);
    TEST_ASSERT_TRUE(tracer.takeReport(report));
    TEST_ASSERT_TRUE(tracer.pollRelease(STEADY_FREE_HEAP, 7510, release));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(500, release.afterMs,
                                     "the release was dated from a low that was not this one");
}

// --------------------------------------------------------------------------------------------
// The network task: a fetch nobody hears
// --------------------------------------------------------------------------------------------

/**
 * The headline. A fetch of the rule list holds thirty-five thousand bytes for a third of a second
 * on the network task; the control loop keeps stepping beside it and its reading finds the low.
 *
 * Without the board wired to the loop service: the line ends after "(was 41780)", and of the lines the fetch itself
 * writes none carries the status and the size of the answer.
 */
void test_a_heavy_fetch_is_named_with_the_low_it_made_and_says_what_it_weighed(void)
{
    Stand stand;
    const std::string body = ruleListBody(stand.jsonConverter);
    stand.http.alwaysRespond(200, body.c_str());
    stand.http.queueWeight(bytesThatReach(FIRST_DIP_WATERMARK), LONG_WORK_MS);

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    TEST_ASSERT_NOT_EQUAL(IOutbox::kRejected, stand.outbox.submitRuleListRequest());
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    // --- the scene has to have happened ------------------------------------------------------
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(FIRST_DIP_WATERMARK, stand.arduino.minFreeHeapValue,
                                     "the fetch did not make the low this test is about");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(STEADY_FREE_HEAP, stand.arduino.freeHeapValue,
                                     "the low was not transient");
    TEST_ASSERT_EQUAL_UINT32(1, stand.outbox.deliveredCount());

    // --- the low names the fetch -------------------------------------------------------------
    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=RULE_LIST(7ms) acq=idle cmd=idle cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);

    // --- and the fetch says what it weighed --------------------------------------------------
    char expected[160];
    snprintf(expected, sizeof(expected),
             "NET: GET /api/v1/HardWare/rulelist -> response 200, body %lu bytes",
             (unsigned long)body.size());

    const std::vector<TapeLogger::Line> said = stand.logger.linesSaying(expected);
    std::string message = std::string("no line says '") + expected + "'; the fetch wrote:" +
                          stand.logger.quoted("NET:");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)said.size(), message.c_str());
}

/**
 * The dip is transient, and so can the fetch be: four milliseconds, begun and over between two
 * readings. It is still the work the low fell on, and the line says how long before the reading
 * it ended.
 *
 * Mutation: read() names running acts only.
 */
void test_a_fetch_that_was_over_before_the_reading_is_still_named(void)
{
    Stand stand;
    stand.http.queueWeight(bytesThatReach(FIRST_DIP_WATERMARK), 4);

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.outbox.submitRuleListRequest();
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=RULE_LIST(done 3ms ago) acq=idle cmd=idle cat=idle" BACK_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

/**
 * The false accusation. The fetch makes its low and is named for it; three steps later something
 * no task on the board answers for makes a deeper one. The fetch was over before the reading that
 * preceded that low, and is not named again.
 *
 * Mutation: read() ignores the window - the second line blames RULE_LIST(done 33ms ago).
 */
void test_a_fetch_that_was_over_a_step_earlier_is_not_accused(void)
{
    Stand stand;
    stand.http.queueWeight(bytesThatReach(FIRST_DIP_WATERMARK), 4);

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.outbox.submitRuleListRequest();
    stand.scheduler.runUntilMs(1033);
    stand.dipByNobody(SECOND_DIP_WATERMARK);
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    const std::vector<TapeLogger::Line> lines = stand.heapLowLines();
    std::string message = "two lows were expected to give two lines:" + stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)lines.size(), message.c_str());

    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 29748 (was 41780)"
                             " net=RULE_LIST(done 3ms ago) acq=idle cmd=idle cat=idle" BACK_AT_THE_READING,
                             lines[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 24932 (was 29748)"
                             " net=idle acq=idle cmd=idle cat=idle" BACK_AT_THE_READING,
                             lines[1].text.c_str());
    TEST_ASSERT_EQUAL_UINT64(1050, lines[1].atMs);
}

/**
 * The same through the loop service: the fetch that made the low ends between the reading and the
 * print, and another fetch has the task by the time the line is written. The line is about the
 * reading.
 *
 * Mutation: LoopServiceBase formats a fresh capture of the board instead of the report's.
 */
void test_the_line_says_what_ran_at_the_reading_and_not_at_the_print(void)
{
    Stand stand;
    // 1003..1015: the rule list, heavy. 1015..: the module commands, light, still running at 1020.
    stand.http.queueWeight(bytesThatReach(FIRST_DIP_WATERMARK), 12);
    stand.http.queueWeight(1000, LONG_WORK_MS);

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.outbox.submitRuleListRequest();
    stand.outbox.submitModuleCommandFetch();
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    // The scene: by the time the line was written the second fetch had been asked for. The line
    // the request writes is the endpoint and nothing else, so it is matched whole: the line the
    // answer writes starts with the same text.
    const std::vector<TapeLogger::Line> gets =
        stand.logger.linesReading("NET: GET http://gd.test/api/v1/HardWare/module_command");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)gets.size(), "the second fetch never ran");
    TEST_ASSERT_TRUE_MESSAGE(gets[0].atMs < LINE_WRITTEN_AT_MS,
                             "the second fetch did not have the task when the line was written");

    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=RULE_LIST(7ms) acq=idle cmd=idle cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

/** @brief A board whose other core gets a word in while the watermark is being read. */
class PreemptedArduino : public SimArduino
{
public:
    explicit PreemptedArduino(VirtualClock& clock) : SimArduino(clock) {}

    uint32_t getMinFreeHeap() override
    {
        const uint32_t value = SimArduino::getMinFreeHeap();
        if (duringTheRead)
        {
            std::function<void()> once;
            once.swap(duringTheRead);
            once();
        }
        return value;
    }

    /** Runs once, after the figure has been fetched and before the reader has it back. */
    std::function<void()> duringTheRead;
};

/**
 * The window of a reading opens at the date of the previous one, so that date has to be taken
 * before the watermark is: a low that the previous reading just missed happened after its date,
 * and whatever made it ended after that. Dated the other way round, work that ends in the gap
 * between the two - the other core does not wait for this one - is over "before the window" of
 * the very reading that finds its low, and the line says idle.
 *
 * Mutation: read the watermark first and the clock second in LoopServiceBase::loop().
 */
void test_work_that_ends_while_the_watermark_is_being_read_is_still_named(void)
{
    VirtualClock clock;
    PreemptedArduino arduino(clock);
    BenchHeap heap(arduino);
    TapeLogger logger(clock);
    TwoChannelDebugSystem debugSystem;
    BackgroundActBoard board(&arduino);
    LoopServiceConfigureDone loopService;
    loopService.setLogging(&logger, &debugSystem);
    loopService.setArduinoAdapter(&arduino);
    loopService.setBackgroundActBoard(&board);

    for (uint64_t at = 0; at < 1000; at += CONTROL_PERIOD_MS)
    {
        loopService.loop();
        clock.advanceMs(CONTROL_PERIOD_MS);
    }

    // The fetch has been running since 990. The step at 1000 fetches the old watermark - and
    // before it has the figure back, a millisecond passes, the fetch makes its low and ends, and
    // another millisecond passes.
    clock.advanceToUs(990 * 1000ULL);
    board.actStarted(BackgroundActBoard::NETWORK,
                     BackgroundActs::ofOutboxKind(OutboxKind::RULE_LIST));
    clock.advanceToUs(1000 * 1000ULL);
    arduino.duringTheRead = [&]() {
        clock.advanceMs(1);
        heap.take(bytesThatReach(FIRST_DIP_WATERMARK));
        heap.give(bytesThatReach(FIRST_DIP_WATERMARK));
        board.actFinished(BackgroundActBoard::NETWORK,
                          BackgroundActs::ofOutboxKind(OutboxKind::RULE_LIST));
        clock.advanceMs(1);
    };
    loopService.loop();

    clock.advanceToUs(1012 * 1000ULL);
    loopService.loop();   // the reading that finds the low
    clock.advanceToUs(1022 * 1000ULL);
    loopService.loop();   // the line

    const std::vector<TapeLogger::Line> lines = logger.linesSaying("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)lines.size());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 29748 (was 41780)"
                             " net=RULE_LIST(done 11ms ago) acq=idle cmd=idle cat=idle" BACK_AT_THE_READING,
                             lines[0].text.c_str());
}

// --------------------------------------------------------------------------------------------
// The network task: the acts of the link
// --------------------------------------------------------------------------------------------

void test_a_radio_restart_is_named(void)
{
    Stand stand;
    stand.wifi.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.wifi.weight.ms = LONG_WORK_MS;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.outbox.submitWifiReconnect();
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    TEST_ASSERT_EQUAL_INT(1, stand.wifi.reconnectCallCount);
    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=WIFI_RECONNECT(7ms) acq=idle cmd=idle cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

/**
 * The connect act has no request of its own: the network task only beats it. It is written to the
 * board on the act's edges, from the first beat that finds it in progress.
 */
void test_an_mqtt_connect_act_is_named(void)
{
    Stand stand;
    stand.mqttLink.actBytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.mqttLink.actFromMs = WORK_STARTS_AT_MS;
    stand.mqttLink.actUntilMs = WORK_STARTS_AT_MS + 2000;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    // The network task is created by the first submission; a telemetry-sized one, answered at
    // once, is what the module would have sent anyway. From here the task beats the link.
    stand.outbox.submitModuleCommandFetch();
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS + 2000);

    const std::vector<TapeLogger::Line> lines = stand.heapLowLines();
    std::string message = "one low, one line:" + stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)lines.size(), message.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 29748 (was 41780)"
                             " net=MQTT_CONNECT(7ms) acq=idle cmd=idle cat=idle" HELD_AT_THE_READING,
                             lines[0].text.c_str());
}

/**
 * The slot holds one act. A radio restart taken while a connect act is in progress replaces it on
 * the board; when the restart is over the act is still going, and the next beat writes it back -
 * a low that falls then is the act's, not the finished restart's and not nobody's.
 *
 * Mutation: noteMqttAct() follows the act's own edges only (compare with _mqttActInProgress).
 */
void test_a_connect_act_a_restart_ran_across_is_named_again(void)
{
    Stand stand;
    stand.mqttLink.actBytes = 1000;
    stand.mqttLink.actFromMs = 500;
    stand.mqttLink.actUntilMs = 5000;
    stand.wifi.weight.bytes = 1000;
    stand.wifi.weight.ms = 100;

    stand.scheduler.runUntilMs(400);
    stand.outbox.submitModuleCommandFetch();
    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.outbox.submitWifiReconnect();
    stand.scheduler.runUntilMs(1503);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stand.wifi.reconnectCallCount, "the restart did not run");

    stand.dipByNobody(FIRST_DIP_WATERMARK);
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    const std::vector<TapeLogger::Line> lines = stand.heapLowLines();
    std::string message = "one low, one line:" + stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)lines.size(), message.c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(lines[0].text.c_str(), " net=MQTT_CONNECT("),
                                 lines[0].text.c_str());
    TEST_ASSERT_NULL_MESSAGE(strstr(lines[0].text.c_str(), "done"), lines[0].text.c_str());
}

// --------------------------------------------------------------------------------------------
// The acquisition task
// --------------------------------------------------------------------------------------------

void test_a_sensor_of_the_sweep_is_named(void)
{
    Stand stand;
    stand.sensorReader.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.sensorReader.weight.ms = LONG_WORK_MS;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.acquisition.setSensors(std::vector<SensorEntity>(1, makeSensor()));
    TEST_ASSERT_TRUE(stand.acquisition.ensureRunning());
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    TEST_ASSERT_EQUAL_UINT64(1, stand.acquisition.pollCount());
    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=SWEEP(7ms) cmd=idle cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

void test_a_step_of_the_discovery_is_named_by_what_it_does_on_the_line(void)
{
    Stand stand;
    stand.startupScan.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.startupScan.weight.ms = LONG_WORK_MS;

    StartupScanStep step;
    step.kind = StartupScanStep::PROBE_CANDIDATE;
    step.pin = 1;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    TEST_ASSERT_NOT_EQUAL(ISensorAcquisition::kBusCommandRejected,
                          stand.acquisition.submitStartupScanStep(step));
    TEST_ASSERT_TRUE(stand.acquisition.ensureRunning());
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    TEST_ASSERT_EQUAL_UINT64(1, stand.acquisition.startupScanStepCount());
    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=LADDER:PROBE_CANDIDATE(7ms) cmd=idle cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

void test_a_bus_command_is_named(void)
{
    Stand stand;
    stand.executor.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.executor.weight.ms = LONG_WORK_MS;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    TEST_ASSERT_NOT_EQUAL(ISensorAcquisition::kBusCommandRejected,
                          stand.acquisition.submitBusCommand("ModbusRead", "{}"));
    TEST_ASSERT_TRUE(stand.acquisition.ensureRunning());
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    TEST_ASSERT_TRUE(stand.executor.executeRawCalled);
    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=RAW_COMMAND(7ms) cmd=idle cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

void test_the_bus_part_of_a_scan_command_is_named(void)
{
    Stand stand;
    stand.executor.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.executor.weight.ms = LONG_WORK_MS;

    ComplexCommandWork scan;
    scan.command.command = "ScanSensors";
    scan.onBus = true;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    TEST_ASSERT_NOT_EQUAL(ISensorAcquisition::kBusCommandRejected,
                          stand.acquisition.submitComplexBusCommand(scan));
    TEST_ASSERT_TRUE(stand.acquisition.ensureRunning());
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    TEST_ASSERT_TRUE(stand.executor.executeBusWorkCalled);
    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=SCAN_COMMAND(7ms) cmd=idle cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

// --------------------------------------------------------------------------------------------
// The command network task
// --------------------------------------------------------------------------------------------

void test_a_catalog_fetch_for_a_command_is_named(void)
{
    Stand stand;
    stand.executor.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.executor.weight.ms = LONG_WORK_MS;

    ComplexCommandWork fetch;
    fetch.command.command = "ScanSensors";
    fetch.fetchCatalog = true;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    TEST_ASSERT_NOT_EQUAL(INetworkCommandRunner::kRejected, stand.commandRunner.submit(fetch));
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    TEST_ASSERT_TRUE(stand.executor.executeNetworkWorkCalled);
    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=idle cmd=CATALOG_FETCH(7ms) cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

void test_a_firmware_download_is_named(void)
{
    Stand stand;
    stand.executor.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.executor.weight.ms = LONG_WORK_MS;

    ComplexCommandWork update;
    update.command.command = "UpdateFirmware";

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    TEST_ASSERT_NOT_EQUAL(INetworkCommandRunner::kRejected, stand.commandRunner.submit(update));
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=idle cmd=FIRMWARE_UPDATE(7ms) cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

// --------------------------------------------------------------------------------------------
// The catalog storage task
// --------------------------------------------------------------------------------------------

void test_a_catalog_write_is_named(void)
{
    Stand stand;
    stand.sensorTypeDao.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.sensorTypeDao.weight.ms = LONG_WORK_MS;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    TEST_ASSERT_NOT_EQUAL(ISensorTypeCatalogWriter::kRejected,
                          stand.catalogWriter.submit(std::vector<SensorType>(1)));
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    TEST_ASSERT_EQUAL_UINT64(1, stand.catalogWriter.writtenCount());
    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=idle cmd=idle cat=WRITE(7ms)" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

// --------------------------------------------------------------------------------------------
// An act that is over is over - for every task, and for every way into it
// --------------------------------------------------------------------------------------------

/**
 * @brief The second of two lows, made by nobody half a second after a task's act ended.
 *
 * A task that marks the beginning of an act and not its end is accused for ever: its word says
 * "running" until the next act replaces it, and every low in between is laid at its door with an
 * age that only grows. The fetch has this test above; these are its siblings, one for every place
 * an act ends. Each scene first checks that the act ran and was named for its own low, so that an
 * idle second line is a statement about the end of the act and not about a scene that never
 * happened.
 */
void assertTheSecondLowAccusesNobody(Stand& stand, const char* firstLine)
{
    const std::vector<TapeLogger::Line> lines = stand.heapLowLines();
    std::string message = "two lows were expected to give two lines:" + stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)lines.size(), message.c_str());
    TEST_ASSERT_EQUAL_STRING(firstLine, lines[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 24932 (was 29748)"
                             " net=idle acq=idle cmd=idle cat=idle" BACK_AT_THE_READING,
                             lines[1].text.c_str());
}

const uint32_t SHORT_WORK_MS = 4;
const uint64_t NOBODYS_LOW_AT_MS = 1533;

/** Mutation: SensorTypeCatalogWriter::serveOneWrite() does not mark the end - cat=WRITE(537ms). */
void test_a_catalog_write_that_is_over_is_not_accused(void)
{
    Stand stand;
    stand.sensorTypeDao.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.sensorTypeDao.weight.ms = SHORT_WORK_MS;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.catalogWriter.submit(std::vector<SensorType>(1));
    stand.scheduler.runUntilMs(NOBODYS_LOW_AT_MS);
    TEST_ASSERT_EQUAL_UINT64(1, stand.catalogWriter.writtenCount());
    stand.dipByNobody(SECOND_DIP_WATERMARK);
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    assertTheSecondLowAccusesNobody(stand, "HEAP_LOW: new watermark 29748 (was 41780)"
                                           " net=idle acq=idle cmd=idle cat=WRITE(done 3ms ago)" BACK_AT_THE_READING);
}

/** Mutation: NetworkCommandRunner::serveOneCommand() does not mark the end - cmd=CATALOG_FETCH(537ms). */
void test_a_network_command_that_is_over_is_not_accused(void)
{
    Stand stand;
    stand.executor.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.executor.weight.ms = SHORT_WORK_MS;

    ComplexCommandWork fetch;
    fetch.command.command = "ScanSensors";
    fetch.fetchCatalog = true;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.commandRunner.submit(fetch);
    stand.scheduler.runUntilMs(NOBODYS_LOW_AT_MS);
    TEST_ASSERT_EQUAL_UINT64(1, stand.commandRunner.executedCount());
    stand.dipByNobody(SECOND_DIP_WATERMARK);
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    assertTheSecondLowAccusesNobody(stand, "HEAP_LOW: new watermark 29748 (was 41780)"
                                           " net=idle acq=idle cmd=CATALOG_FETCH(done 3ms ago) cat=idle" BACK_AT_THE_READING);
}

/** Mutation: SensorAcquisitionService::step() does not mark the end of a poll - acq=SWEEP(537ms). */
void test_a_sensor_of_the_sweep_that_is_over_is_not_accused(void)
{
    Stand stand;
    stand.sensorReader.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.sensorReader.weight.ms = SHORT_WORK_MS;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.acquisition.setSensors(std::vector<SensorEntity>(1, makeSensor()));
    stand.acquisition.ensureRunning();
    stand.scheduler.runUntilMs(NOBODYS_LOW_AT_MS);
    TEST_ASSERT_EQUAL_UINT64(1, stand.acquisition.pollCount());
    stand.dipByNobody(SECOND_DIP_WATERMARK);
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    assertTheSecondLowAccusesNobody(stand, "HEAP_LOW: new watermark 29748 (was 41780)"
                                           " net=idle acq=SWEEP(done 3ms ago) cmd=idle cat=idle" BACK_AT_THE_READING);
}

/** Mutation: serveOneBusCommand() does not mark the end - acq=RAW_COMMAND(537ms). */
void test_a_bus_command_that_is_over_is_not_accused(void)
{
    Stand stand;
    stand.executor.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.executor.weight.ms = SHORT_WORK_MS;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.acquisition.submitBusCommand("ModbusRead", "{}");
    stand.acquisition.ensureRunning();
    stand.scheduler.runUntilMs(NOBODYS_LOW_AT_MS);
    TEST_ASSERT_EQUAL_UINT64(1, stand.acquisition.busCommandCount());
    stand.dipByNobody(SECOND_DIP_WATERMARK);
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    assertTheSecondLowAccusesNobody(stand, "HEAP_LOW: new watermark 29748 (was 41780)"
                                           " net=idle acq=RAW_COMMAND(done 3ms ago) cmd=idle cat=idle" BACK_AT_THE_READING);
}

/** Mutation: serveOneStartupScanStep() does not mark the end - acq=LADDER:PROBE_CANDIDATE(537ms). */
void test_a_step_of_the_discovery_that_is_over_is_not_accused(void)
{
    Stand stand;
    stand.startupScan.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.startupScan.weight.ms = SHORT_WORK_MS;

    StartupScanStep step;
    step.kind = StartupScanStep::PROBE_CANDIDATE;
    step.pin = 1;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.acquisition.submitStartupScanStep(step);
    stand.acquisition.ensureRunning();
    stand.scheduler.runUntilMs(NOBODYS_LOW_AT_MS);
    TEST_ASSERT_EQUAL_UINT64(1, stand.acquisition.startupScanStepCount());
    stand.dipByNobody(SECOND_DIP_WATERMARK);
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    assertTheSecondLowAccusesNobody(stand,
                                    "HEAP_LOW: new watermark 29748 (was 41780)"
                                    " net=idle acq=LADDER:PROBE_CANDIDATE(done 3ms ago) cmd=idle cat=idle" BACK_AT_THE_READING);
}

/**
 * The connect act has no request to be replaced by: it is asked for before the act, so that
 * nothing takes the slot afterwards and the act's own end is what the second line reads.
 *
 * Mutation: Outbox::noteMqttAct() does not mark the falling edge - net=MQTT_CONNECT(530ms).
 */
void test_a_connect_act_that_is_over_is_not_accused(void)
{
    Stand stand;
    stand.mqttLink.actBytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.mqttLink.actFromMs = WORK_STARTS_AT_MS;
    stand.mqttLink.actUntilMs = WORK_STARTS_AT_MS + 300;

    stand.scheduler.runUntilMs(400);
    stand.outbox.submitModuleCommandFetch();
    stand.scheduler.runUntilMs(1833);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stand.outbox.deliveredCount(),
                                     "the request that starts the network task was not over before the act");
    stand.dipByNobody(SECOND_DIP_WATERMARK);
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    const std::vector<TapeLogger::Line> lines = stand.heapLowLines();
    std::string message = "two lows were expected to give two lines:" + stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)lines.size(), message.c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(lines[0].text.c_str(), " net=MQTT_CONNECT("),
                                 lines[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 24932 (was 29748)"
                             " net=idle acq=idle cmd=idle cat=idle" BACK_AT_THE_READING,
                             lines[1].text.c_str());
}

// --------------------------------------------------------------------------------------------
// The act is the attempt, not the request
// --------------------------------------------------------------------------------------------

/**
 * A request whose link dropped under it waits inside the outbox for the link to come back - for
 * as long as that takes, holding nothing but itself. It is not at work while it waits: a low that
 * falls in that wait is not the request's, and a line that said net=MODULE_COMMAND_FETCH(950ms)
 * would send the reader after a fetch that has not been on the wire for a second.
 *
 * Mutation: mark the beginning of the act before holdForLink() instead of after it.
 */
void test_a_request_waiting_for_the_link_is_not_at_work(void)
{
    Stand stand;
    // The first attempt is on the wire for fifty milliseconds and fails; the link goes down under
    // it. The second attempt waits for the link.
    stand.http.queueWeight(0, 50);
    stand.http.queueResponse(-1, "");

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.outbox.submitModuleCommandFetch();
    stand.scheduler.runUntilMs(1020);
    stand.wifi.setConnected(false);

    // Attempt one ends at 1053, the backoff at 1553; from there the request is held.
    stand.scheduler.runUntilMs(2503);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, stand.http.requestCount(),
                                     "the request was attempted while the link was down");
    stand.dipByNobody(FIRST_DIP_WATERMARK);
    stand.scheduler.runUntilMs(3000);

    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=idle cmd=idle cat=idle" BACK_AT_THE_READING,
                     2520);

    // And the scene was the one described: the link comes back and the held request goes out.
    stand.wifi.setConnected(true);
    stand.scheduler.runUntilMs(4000);
    TEST_ASSERT_EQUAL_size_t(2, stand.http.requestCount());
    TEST_ASSERT_EQUAL_UINT32(1, stand.outbox.deliveredCount());
}

// --------------------------------------------------------------------------------------------
// Nobody
// --------------------------------------------------------------------------------------------

/**
 * A low with every task idle is an answer too: the work was the control loop's own, or a system
 * task's. The line says so in as many words instead of ending early - an account that is absent
 * and an account of four idle tasks must not read the same.
 */
void test_a_low_with_every_task_idle_says_so(void)
{
    Stand stand;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.dipByNobody(FIRST_DIP_WATERMARK);
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=idle cmd=idle cat=idle" BACK_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);
}

/** Work that stays above the threshold of the trace marks the board and writes nothing. */
void test_work_that_makes_no_low_writes_no_line(void)
{
    Stand stand;
    stand.http.queueWeight(1000, LONG_WORK_MS);

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.outbox.submitRuleListRequest();
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    TEST_ASSERT_EQUAL_UINT32(1, stand.outbox.deliveredCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.heapLowLines().size(),
                                     "a fetch that made no low still wrote a HEAP_LOW line");
}

// --------------------------------------------------------------------------------------------
// The low nobody answers for: the step's own work, or somebody holding on past the step
// --------------------------------------------------------------------------------------------

/**
 * The control loop's own work. The memory is taken and given back inside one step, on the control
 * task, and the step is twelve milliseconds long for it. The reading at the end of that step
 * finds the low with the free heap already back - and the line says both, which is the signature:
 * every task idle, free heap at its level, a step with a length. No watch, no second line; the
 * free heap was asked for once.
 *
 * Mutation: the step's length is not handed to the trace - step=0ms.
 */
void test_a_dip_inside_a_step_shows_the_heap_back_and_how_long_the_step_was(void)
{
    Stand stand;
    stand.heavyStep.weight.bytes = bytesThatReach(FIRST_DIP_WATERMARK);
    stand.heavyStep.weight.ms = 12;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.heavyStep.armed = true;
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    // The step that began at 1010 lasted until 1022; the next one, at 1032, writes the line.
    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=idle cmd=idle cat=idle free=64800 step=12ms",
                     1032);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.watchEndLines().size(),
                                     "memory that was back by the end of the step got a second line");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stand.looksOfTheTrace(),
                                     "the free heap is asked for once, by the reading that found the low");
}

/**
 * Somebody holding on past the step: no task on the board, and not the step's own work either -
 * that cannot outlive the step. The line about the low shows the free heap still at the floor;
 * the trace then looks once per step, and the step that finds the memory back says after how
 * long. One line, on that step, and the looking stops with it.
 *
 * Mutations: the memory counts as released at any level - "released after 10 ms"; the flag an
 * ordinary step tests forgets the watch - no second line at all.
 */
void test_memory_held_past_the_step_is_followed_by_the_moment_it_came_back(void)
{
    Stand stand;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.takeByNobody(FIRST_DIP_WATERMARK);
    stand.scheduler.runUntilMs(2503);
    stand.heap.give(bytesThatReach(FIRST_DIP_WATERMARK));
    stand.scheduler.runUntilMs(2600);

    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=idle cmd=idle cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);

    const std::vector<TapeLogger::Line> ends = stand.watchEndLines();
    std::string message = "one low held and released is one closing line:" + stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)ends.size(), message.c_str());
    // Found at 1010, found back by the step at 2510.
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 1500 ms (free 64800)", ends[0].text.c_str());
    TEST_ASSERT_EQUAL_UINT64(2510, ends[0].atMs);

    // One look by the reading that found the low, and one per step from 1020 to 2510.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1 + 150, stand.looksOfTheTrace(),
                                     "the free heap was not asked for once per step while the low was held");

    // And not once more after that.
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS + 3000);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1 + 150, stand.looksOfTheTrace(),
                                     "the trace went on asking for the free heap after the release");
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)stand.watchEndLines().size());
}

/**
 * The limit. Memory that is not given back is said to be still held, once, a minute after the
 * low - and that is the end of the watch: the memory coming back later gets no line, and the
 * free heap is not asked for again. What is still down after a minute is a level, and levels are
 * the diagnostic snapshot's business.
 *
 * Mutations: no limit - no line and a platform call per step for ever; a watch that goes on after
 * its limit - a second closing line when the memory does come back.
 */
void test_memory_never_given_back_is_said_once_and_the_watch_ends(void)
{
    Stand stand;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.takeByNobody(FIRST_DIP_WATERMARK);
    stand.scheduler.runUntilMs(70000);

    const std::vector<TapeLogger::Line> ends = stand.watchEndLines();
    std::string message = "a low held past the limit is one closing line:" + stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)ends.size(), message.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: still held after 60000 ms (free 29748)", ends[0].text.c_str());
    // A minute, said here and not read from the trace: the scene is what holds the figure.
    TEST_ASSERT_EQUAL_UINT64(FOUND_BY_THE_READING_AT_MS + 60000, ends[0].atMs);

    const uint32_t readsWhenTheWatchEnded = stand.looksOfTheTrace();
    TEST_ASSERT_EQUAL_UINT32(1 + 6000, readsWhenTheWatchEnded);

    stand.heap.give(bytesThatReach(FIRST_DIP_WATERMARK));
    stand.scheduler.runUntilMs(80000);

    message = "the watch had ended and still wrote a line:" + stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.watchEndLines().size(), message.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(readsWhenTheWatchEnded, stand.looksOfTheTrace(),
                                     "the trace went on asking for the free heap after its limit");
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)stand.heapLowLines().size());
}

/**
 * A sag is one episode. A second, deeper low found while the first is still held gets its own
 * line about the low - that is the hysteresis of the trace, a kilobyte at a time - but the
 * holding-on is one, and it is dated from the first: one closing line, fifteen hundred
 * milliseconds after the low that opened the watch.
 *
 * Mutation: every low restarts the watch - "released after 1000 ms".
 */
void test_a_deeper_low_while_the_first_is_held_is_the_same_episode(void)
{
    Stand stand;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.takeByNobody(FIRST_DIP_WATERMARK);
    stand.scheduler.runUntilMs(1503);
    stand.heap.take(FIRST_DIP_WATERMARK - SECOND_DIP_WATERMARK);
    stand.scheduler.runUntilMs(2503);
    stand.heap.give(bytesThatReach(SECOND_DIP_WATERMARK));
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS + 1000);

    const std::vector<TapeLogger::Line> lows = stand.heapLowLines();
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)lows.size());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 24932 (was 29748)"
                             " net=idle acq=idle cmd=idle cat=idle free=24932 step=0ms",
                             lows[1].text.c_str());

    const std::vector<TapeLogger::Line> ends = stand.watchEndLines();
    std::string message = "one holding-on is one closing line:" + stand.logger.quoted("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)ends.size(), message.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 1500 ms (free 64800)", ends[0].text.c_str());
}

/**
 * The free heap in the line about the low is the reading's, like the account of the tasks: by the
 * time the line is written, a step later, the memory may be back - and here it is. The line says
 * what the reading found, the floor; and the same step, having found the memory back, says so
 * next, in that order.
 *
 * Mutation: the line about the low asks for the free heap when it is written - free=64800.
 */
void test_the_free_heap_in_the_line_is_of_the_reading_and_not_of_the_print(void)
{
    Stand stand;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.takeByNobody(FIRST_DIP_WATERMARK);
    stand.scheduler.runUntilMs(1015);
    stand.heap.give(bytesThatReach(FIRST_DIP_WATERMARK));
    stand.scheduler.runUntilMs(SCENE_ENDS_AT_MS);

    assertTheOneLine(stand,
                     "HEAP_LOW: new watermark 29748 (was 41780)"
                     " net=idle acq=idle cmd=idle cat=idle" HELD_AT_THE_READING,
                     LINE_WRITTEN_AT_MS);

    const std::vector<TapeLogger::Line> all = stand.logger.linesSaying("HEAP_LOW: ");
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)all.size());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 10 ms (free 64800)", all[1].text.c_str());
    TEST_ASSERT_EQUAL_UINT64(LINE_WRITTEN_AT_MS, all[1].atMs);
    TEST_ASSERT_EQUAL_UINT32(2, stand.looksOfTheTrace());
}

/**
 * What all of this costs a module that is not at a low: nothing it can measure. Six seconds with
 * every task at work - a fetch, a sweep, a command, a catalog write, a step's own heavy work, and
 * a dip that stays above the level worth a report - and the free heap is not asked for once.
 *
 * Mutation: the step describes the moment to the trace on every step, not only on a low.
 */
void test_a_module_that_is_not_at_a_low_never_asks_for_the_free_heap(void)
{
    Stand stand;
    const uint32_t lightWork = bytesThatReach(HeapWatermarkTracer::REPORT_BELOW_BYTES + 500);
    stand.http.queueWeight(lightWork, LONG_WORK_MS);
    stand.sensorReader.weight.bytes = lightWork;
    stand.sensorReader.weight.ms = LONG_WORK_MS;
    stand.executor.weight.bytes = lightWork;
    stand.executor.weight.ms = LONG_WORK_MS;
    stand.sensorTypeDao.weight.bytes = lightWork;
    stand.sensorTypeDao.weight.ms = LONG_WORK_MS;
    stand.heavyStep.weight.bytes = lightWork;
    stand.heavyStep.weight.ms = 12;

    stand.scheduler.runUntilMs(WORK_STARTS_AT_MS);
    stand.outbox.submitRuleListRequest();
    stand.scheduler.runUntilMs(2003);
    stand.acquisition.setSensors(std::vector<SensorEntity>(1, makeSensor()));
    stand.acquisition.ensureRunning();
    // One at a time: together they would add up to a low, and that is another test.
    stand.scheduler.runUntilMs(3003);
    stand.acquisition.submitBusCommand("ModbusRead", "{}");
    stand.scheduler.runUntilMs(4003);
    stand.catalogWriter.submit(std::vector<SensorType>(1));
    stand.scheduler.runUntilMs(5003);
    stand.heavyStep.armed = true;
    stand.scheduler.runUntilMs(6000);

    // The scene: the work ran, and the watermark did move - to a level the trace keeps quiet about.
    TEST_ASSERT_EQUAL_UINT32(1, stand.outbox.deliveredCount());
    TEST_ASSERT_EQUAL_UINT64(1, stand.acquisition.pollCount());
    TEST_ASSERT_EQUAL_UINT64(1, stand.catalogWriter.writtenCount());
    TEST_ASSERT_TRUE(stand.arduino.minFreeHeapValue < STEADY_WATERMARK);
    TEST_ASSERT_TRUE(stand.arduino.minFreeHeapValue >= HeapWatermarkTracer::REPORT_BELOW_BYTES);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)stand.logger.linesSaying("HEAP_LOW").size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stand.looksOfTheTrace(),
                                     "a module that was not at a low asked for the free heap");
}

// --------------------------------------------------------------------------------------------
// The fetches say what they weighed
// --------------------------------------------------------------------------------------------

struct FetchFixture
{
    VirtualClock clock;
    MockModuleConfig moduleConfig;
    MockSettings settings;
    TapeLogger logger;
    TwoChannelDebugSystem debugSystem;
    ArduinoJsonConverter jsonConverter;
    SimHttpClient http;
    RestAPIService service;

    FetchFixture()
        : logger(clock),
          service(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter)
    {
        settings.setServerUrl(SERVER_URL);
        settings.setToken(TOKEN);
        moduleConfig.setModuleUid(MODULE_UID);
        service.init();
    }

    void assertSaidOnce(const char* expected)
    {
        const std::vector<TapeLogger::Line> said = logger.linesSaying(expected);
        std::string message = std::string("no line says '") + expected + "'; the fetch wrote:" +
                              logger.quoted("NET:");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)said.size(), message.c_str());
    }
};

void test_the_rule_list_fetch_says_its_status_and_the_size_of_the_answer(void)
{
    FetchFixture f;
    const std::string body = ruleListBody(f.jsonConverter);
    f.http.alwaysRespond(200, body.c_str());

    f.service.getRuleList();

    char expected[160];
    snprintf(expected, sizeof(expected),
             "NET: GET /api/v1/HardWare/rulelist -> response 200, body %lu bytes",
             (unsigned long)body.size());
    f.assertSaidOnce(expected);
}

void test_the_catalog_fetch_says_its_status_and_the_size_of_the_answer(void)
{
    FetchFixture f;
    f.http.alwaysRespond(200, "[]");

    f.service.getSensorTypesList();

    f.assertSaidOnce("NET: GET /api/v1/Hardware/sensor_models -> response 200, body 2 bytes");
}

void test_the_command_fetches_say_their_status_and_the_size_of_the_answer(void)
{
    FetchFixture f;
    f.http.alwaysRespond(200, "[]");

    f.service.getDeviceCommand();
    f.service.getModuleCommand();

    f.assertSaidOnce("NET: GET /api/v1/HardWare/devices_command -> response 200, body 2 bytes");
    f.assertSaidOnce("NET: GET /api/v1/HardWare/module_command -> response 200, body 2 bytes");
}

/**
 * A fetch that failed says so on the same line, with the status it got - the transport's negative
 * code when the server said nothing - and a body of zero, because none was read.
 */
void test_a_fetch_that_failed_says_so_on_the_same_line(void)
{
    FetchFixture f;
    f.http.queueResponse(500, "an error page the module does not read");
    f.http.queueResponse(-1, "");
    f.http.queueResponse(-1, "");

    f.service.getRuleList();
    f.service.getSensorTypesList();
    f.service.getModuleCommand();

    f.assertSaidOnce("NET: GET /api/v1/HardWare/rulelist -> response 500, body 0 bytes");
    f.assertSaidOnce("NET: GET /api/v1/Hardware/sensor_models -> response -1, body 0 bytes");
    f.assertSaidOnce("NET: GET /api/v1/HardWare/module_command -> response -1, body 0 bytes");
}

/**
 * The status and the size are said once, on that one line, and not on older lines as well: a
 * "HTTP Response code: N" line for the status and, on the two fetches that read the body as it
 * arrives, a "Response length: N bytes" for the size, said the same figures a line or two apart
 * on every fetch. What those older lines carried besides - how many rules, how many sensor types
 * the module took - stays, on a line of its own.
 */
void test_the_status_and_the_size_of_a_fetch_are_said_once_and_not_on_older_lines_as_well(void)
{
    FetchFixture f;
    const std::string body = ruleListBody(f.jsonConverter);
    f.http.queueResponse(200, body.c_str());
    f.http.queueResponse(200, "[]");
    f.http.queueResponse(200, "[]");
    f.http.queueResponse(200, "[]");

    f.service.getRuleList();
    f.service.getSensorTypesList();
    f.service.getDeviceCommand();
    f.service.getModuleCommand();

    const std::string tape = "the fetches wrote:" + f.logger.quoted("NET:");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, (uint32_t)f.logger.linesSaying(" -> response 200, body ").size(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)f.logger.linesSaying("NET: HTTP Response code").size(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)f.logger.linesSaying("Response length").size(), tape.c_str());
    f.assertSaidOnce("NET: getRuleList - rules: 8");
    f.assertSaidOnce("NET: getSensorTypesList - sensor types: 0");
}

} // namespace

extern "C" void run_heap_low_culprit_tests(void)
{
    RUN_TEST(test_a_running_act_is_read_back_with_how_long_it_has_run);
    RUN_TEST(test_an_act_that_ended_inside_the_window_is_read_back_as_done);
    RUN_TEST(test_an_act_that_ended_before_the_window_is_not_read_back);
    RUN_TEST(test_a_slot_nothing_was_written_to_reads_idle);
    RUN_TEST(test_an_act_that_straddles_the_stamp_wrap_keeps_its_age);
    RUN_TEST(test_every_act_has_a_name_the_line_has_room_for);
    RUN_TEST(test_the_widest_account_fits_the_buffer_it_is_given);
    RUN_TEST(test_marking_an_act_and_reading_the_board_allocate_nothing);
    RUN_TEST(test_the_copy_is_dated_after_the_words_are_read);
    RUN_TEST(test_the_account_is_of_the_reading_and_not_of_the_print);
    RUN_TEST(test_a_reading_without_a_board_reports_as_before);
    RUN_TEST(test_the_trace_asks_for_attention_while_a_line_waits_or_a_watch_is_open);
    RUN_TEST(test_memory_back_by_the_end_of_the_step_opens_no_watch);
    RUN_TEST(test_a_watch_ends_on_the_level_or_on_the_limit_and_says_which);
    RUN_TEST(test_a_watch_across_the_clock_wraparound_keeps_its_length);
    RUN_TEST(test_a_reading_inside_the_hysteresis_says_it_found_nothing);
    RUN_TEST(test_a_moment_described_with_no_low_waiting_opens_no_watch);
    RUN_TEST(test_a_heavy_fetch_is_named_with_the_low_it_made_and_says_what_it_weighed);
    RUN_TEST(test_a_fetch_that_was_over_before_the_reading_is_still_named);
    RUN_TEST(test_a_fetch_that_was_over_a_step_earlier_is_not_accused);
    RUN_TEST(test_the_line_says_what_ran_at_the_reading_and_not_at_the_print);
    RUN_TEST(test_work_that_ends_while_the_watermark_is_being_read_is_still_named);
    RUN_TEST(test_a_radio_restart_is_named);
    RUN_TEST(test_an_mqtt_connect_act_is_named);
    RUN_TEST(test_a_connect_act_a_restart_ran_across_is_named_again);
    RUN_TEST(test_a_sensor_of_the_sweep_is_named);
    RUN_TEST(test_a_step_of_the_discovery_is_named_by_what_it_does_on_the_line);
    RUN_TEST(test_a_bus_command_is_named);
    RUN_TEST(test_the_bus_part_of_a_scan_command_is_named);
    RUN_TEST(test_a_catalog_fetch_for_a_command_is_named);
    RUN_TEST(test_a_firmware_download_is_named);
    RUN_TEST(test_a_catalog_write_is_named);
    RUN_TEST(test_a_catalog_write_that_is_over_is_not_accused);
    RUN_TEST(test_a_network_command_that_is_over_is_not_accused);
    RUN_TEST(test_a_sensor_of_the_sweep_that_is_over_is_not_accused);
    RUN_TEST(test_a_bus_command_that_is_over_is_not_accused);
    RUN_TEST(test_a_step_of_the_discovery_that_is_over_is_not_accused);
    RUN_TEST(test_a_connect_act_that_is_over_is_not_accused);
    RUN_TEST(test_a_request_waiting_for_the_link_is_not_at_work);
    RUN_TEST(test_a_low_with_every_task_idle_says_so);
    RUN_TEST(test_work_that_makes_no_low_writes_no_line);
    RUN_TEST(test_a_dip_inside_a_step_shows_the_heap_back_and_how_long_the_step_was);
    RUN_TEST(test_memory_held_past_the_step_is_followed_by_the_moment_it_came_back);
    RUN_TEST(test_memory_never_given_back_is_said_once_and_the_watch_ends);
    RUN_TEST(test_a_deeper_low_while_the_first_is_held_is_the_same_episode);
    RUN_TEST(test_the_free_heap_in_the_line_is_of_the_reading_and_not_of_the_print);
    RUN_TEST(test_a_module_that_is_not_at_a_low_never_asks_for_the_free_heap);
    RUN_TEST(test_the_rule_list_fetch_says_its_status_and_the_size_of_the_answer);
    RUN_TEST(test_the_catalog_fetch_says_its_status_and_the_size_of_the_answer);
    RUN_TEST(test_the_command_fetches_say_their_status_and_the_size_of_the_answer);
    RUN_TEST(test_a_fetch_that_failed_says_so_on_the_same_line);
    RUN_TEST(test_the_status_and_the_size_of_a_fetch_are_said_once_and_not_on_older_lines_as_well);
}
