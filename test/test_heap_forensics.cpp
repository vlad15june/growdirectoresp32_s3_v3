// Which task holds the heap when it is at its floor, and the dips the since-boot watermark hides.
//
// Heap-forensics builds only: every line of this file is behind GD_HEAP_FORENSICS, like the code it tests,
// and is executed by the native-heap-forensics environment.
//
// A run on a device lost fifty thousand bytes of heap in half a second inside one TLS handshake - seven
// HEAP_LOW lines, 39 600 down to 10 516 - with a single participant at work, thirteen thousand of
// it accounted for by the TLS allocator and the rest held by nobody the firmware knows of. And
// once the watermark had been to 10 516, no later dip shallower than that could make a line: the
// platform's watermark only falls.
//
// So the heap-forensics build adds two things to the trace, and the tape of a low then reads:
//
//   HEAP_LOW: new watermark 39600 (was 41332) net=SENSOR_UPDATE(1982ms) acq=idle ... free=39624 step=5ms
//   HEAP_LOW: held by pre-sched=61200/410 wifi=31240/212 tiT=9120/40 ... other=0/0 all=.../... owners=9 walk=2300us names=900us
//   HEAP_LOW: released after 560 ms (free 62244)
//   HEAP_LOW: after the release held by pre-sched=61200/410 wifi=9040/61 ...
//   HEAP_LOW: episode low 25012 (watermark 10516) net=idle ... free=25012 step=0ms
//
// What is under test is production code: HeapHolders, which turns the platform's ledger into a
// line, and HeapWatermarkTracer driven by the one caller it has, LoopServiceBase::loop(). What is
// replaced is the board: the clock, the two heap figures, and the ledger - which on the device is
// one platform call each in ArduinoAdapter::readHeapOwners() and readLiveTasks(), and here is
// whatever the test scripts. Those two adapter methods are the one part no host test executes;
// they decide nothing.
//
// ### Reproducing the red
//
// Every scene below is red with HeapWatermarkTracer.h and LoopServiceBase.h reduced to their plain HEAP_LOW line
// - the second and later lines of the tape above do not exist - and each test names
// the mutation that turns it red on its own.

#ifdef GD_HEAP_FORENSICS

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/BackgroundActs.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Core/Domain/Diagnostics/HeapTaskLedger.h"
#include "Core/Domain/Diagnostics/HeapWatermarkTracer.h"
#include "Core/Domain/Diagnostics/RxFrameClassCounter.h"
#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"

#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"

#include "Sim/HostAllocationCounter.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

// The figures of the logged run, kept as they were logged: the level the module sat at, the watermark it had
// before the handshake, and the seven readings of the sag - the watermark each found and the free
// heap it found with it.
const uint32_t STEADY_FREE_HEAP = 62244;
const uint32_t STEADY_WATERMARK = 41332;

struct SagReading
{
    uint32_t watermark;
    uint32_t free;
};

const SagReading THE_SAG[] = {
    {39600, 39624}, {36380, 36404}, {31700, 32252}, {26900, 26924},
    {22172, 22116}, {17212, 17156}, {10516, 10540},
};
const size_t SAG_READINGS = sizeof(THE_SAG) / sizeof(THE_SAG[0]);
const uint32_t THE_FLOOR = 10516;

// The control loop steps every ten milliseconds from zero; the heap is moved three milliseconds
// after a step, so the reading that finds it is the one at the end of the next step.
const uint32_t CONTROL_PERIOD_MS = 10;
const uint64_t DIP_AT_MS = 1003;
const uint64_t FOUND_AT_MS = 1010;
const uint64_t SAID_AT_MS = 1020;

/** What the scripted platform calls cost, so that the figures on the line are checked ones. */
const uint32_t WALK_COST_US = 2300;
const uint32_t NAMES_COST_US = 900;

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

    /** @brief Every line of the trace, one per row - what a failed assertion shows. */
    std::string quoted() const
    {
        std::string all;
        const std::vector<Line> found = linesSaying("HEAP_LOW");
        for (size_t i = 0; i < found.size(); i++)
        {
            char at[32];
            snprintf(at, sizeof(at), "\n      | %6llu ", (unsigned long long)found[i].atMs);
            all += at + found[i].text;
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

/** @brief A log that keeps the last line in a fixed buffer, for the test that counts allocations. */
class FixedLogger : public MockLogger
{
public:
    void println(const char* topic, const char* message) override { keep(message); }
    void println(const char* topic, const char* message, MqttLoggerMode) override { keep(message); }

    unsigned lineCount = 0;
    unsigned heldByCount = 0;
    char last[HeapHolders::LINE_BYTES];

private:
    void keep(const char* message)
    {
        lineCount++;
        snprintf(last, sizeof(last), "%s", message ? message : "");
        if (strstr(last, "held by") != nullptr)
        {
            heldByCount++;
        }
    }
};

/** @brief The channel the trace rides, as the module ships it: on, and a test may turn it off. */
class DiagnosticChannel : public MockDebugSystem
{
public:
    bool enabled = true;

    bool isFeatureEnabled(const char* featureName) override
    {
        return enabled && featureName != nullptr
               && std::string(featureName) == std::string(DebugFeatures::DIAGNOSTIC_DEBUG);
    }
};

// --- the board -------------------------------------------------------------------------------------

/**
 * @brief SimArduino with a ledger the test scripts, counting what the trace asks of the platform.
 *
 * On the device the free heap is a sum over the registered heaps and the ledger is a walk over
 * every allocated block with the heap locked; how often each is asked for is the price of the
 * heap-forensics build, so both are counted. The walk takes time, as it does on the device: the clock
 * moves while it runs, and the line has to say by how much.
 */
class LedgerArduino : public SimArduino
{
public:
    explicit LedgerArduino(VirtualClock& clock) : SimArduino(clock), _clock(clock)
    {
        memset(&ledger, 0, sizeof(ledger));
        ledger.tasksListed = true;
    }

    uint32_t getFreeHeap() override
    {
        freeHeapReads++;
        return SimArduino::getFreeHeap();
    }

    bool readHeapOwners(HeapTaskLedger& out) override
    {
        ledgerReads++;
        ledgerReadAtMs.push_back(_clock.nowMs());
        _clock.advanceUs(WALK_COST_US);
        out.ownerCount = ledger.ownerCount;
        memcpy(out.owners, ledger.owners, sizeof(out.owners));
        return true;
    }

    void readLiveTasks(HeapTaskLedger& out) override
    {
        taskListReads++;
        _clock.advanceUs(NAMES_COST_US);
        out.taskCount = ledger.taskCount;
        out.tasksListed = ledger.tasksListed;
        memcpy(out.tasks, ledger.tasks, sizeof(out.tasks));
    }

    void addTask(uintptr_t handle, const char* name)
    {
        HeapTaskLedger::LiveTask& task = ledger.tasks[ledger.taskCount++];
        task.task = handle;
        snprintf(task.name, sizeof(task.name), "%s", name);
    }

    void addOwner(uintptr_t handle, uint32_t bytes, uint32_t blocks)
    {
        HeapTaskLedger::Owner& owner = ledger.owners[ledger.ownerCount++];
        owner.task = handle;
        owner.bytes = bytes;
        owner.blocks = blocks;
    }

    void setOwner(uintptr_t handle, uint32_t bytes, uint32_t blocks)
    {
        for (size_t i = 0; i < ledger.ownerCount; i++)
        {
            if (ledger.owners[i].task == handle)
            {
                ledger.owners[i].bytes = bytes;
                ledger.owners[i].blocks = blocks;
                return;
            }
        }
        addOwner(handle, bytes, blocks);
    }

    HeapTaskLedger ledger;
    uint32_t freeHeapReads = 0;
    uint32_t ledgerReads = 0;
    uint32_t taskListReads = 0;
    std::vector<uint64_t> ledgerReadAtMs;

private:
    VirtualClock& _clock;
};

/**
 * @brief The free heap moved in the middle of a step - after the step's look at an open watch,
 *        before its reading - which is where another core moves it on the device.
 *
 * Stands where the button handling stands in LoopServiceBase::loop(): after the top of the step
 * and before everything else.
 */
class MidStepHeap : public IButtonUseCases
{
public:
    MidStepHeap(SimArduino& arduino, VirtualClock& clock) : _arduino(arduino), _clock(clock) {}

    void loop() override
    {
        if (armed)
        {
            armed = false;
            _arduino.freeHeapValue = freeBytes;
            // The step's own work, when the scene wants the step to have a length.
            _clock.advanceMs(takesMs);
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
    uint32_t freeBytes = 0;
    uint32_t takesMs = 0;

private:
    SimArduino& _arduino;
    VirtualClock& _clock;
};

// Handles as the device prints them: addresses in internal RAM.
const uintptr_t LOOP_TASK = 0x3fcb1000;
const uintptr_t WIFI_TASK = 0x3fcb2000;
const uintptr_t TCPIP_TASK = 0x3fcb3000;
const uintptr_t OUTBOX_TASK = 0x3fcb4000;
const uintptr_t ASYNC_TCP_TASK = 0x3fcb5000;
const uintptr_t STARTUP_TASK = 0x3fcb6000; // deleted once it has started the others

/**
 * @brief The control loop taking its readings, and the board under it.
 *
 * The loop is stepped the way main.cpp steps it - loop() and nothing else - every ten
 * milliseconds of the virtual clock. The heap is two numbers the test moves between steps, read
 * by the trace exactly as it reads the platform's.
 */
struct Bench
{
    VirtualClock clock;
    LedgerArduino arduino;
    TapeLogger logger;
    DiagnosticChannel channel;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    BackgroundActBoard board;
    MidStepHeap midStep;
    LoopServiceConfigureDone loopService;
    uint32_t steps = 0;
    /** Where the clock stands at the first step; every time a scene names is counted from zero. */
    uint64_t originMs;

    explicit Bench(uint64_t startsAtMs = 0)
        : arduino(clock),
          logger(clock),
          moduleConfig(&logger, &channel, &settings),
          board(&arduino),
          midStep(arduino, clock),
          originMs(startsAtMs)
    {
        clock.advanceMs(originMs);

        arduino.freeHeapValue = STEADY_FREE_HEAP;
        arduino.minFreeHeapValue = STEADY_WATERMARK;

        // The module at rest: what each task holds between two handshakes.
        arduino.addTask(LOOP_TASK, "loopTask");
        arduino.addTask(WIFI_TASK, "wifi");
        arduino.addTask(TCPIP_TASK, "tiT");
        arduino.addTask(OUTBOX_TASK, "outbox-net");
        arduino.addTask(ASYNC_TCP_TASK, "async_tcp");
        arduino.addOwner(0, 61200, 410);
        arduino.addOwner(STARTUP_TASK, 24816, 97);
        arduino.addOwner(LOOP_TASK, 18344, 655);
        arduino.addOwner(WIFI_TASK, 9040, 61);
        arduino.addOwner(TCPIP_TASK, 2112, 17);
        arduino.addOwner(OUTBOX_TASK, 8424, 23);
        arduino.addOwner(ASYNC_TCP_TASK, 512, 4);

        loopService.setLogging(&logger, &channel);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setBackgroundActBoard(&board);
        loopService.setButtonUseCases(&midStep);
    }

    /** @brief Every step that begins before `untilMs`, on the ten-millisecond grid. */
    void runUntilMs(uint64_t untilMs)
    {
        while ((uint64_t)steps * CONTROL_PERIOD_MS < untilMs)
        {
            clock.advanceToUs((originMs + (uint64_t)steps * CONTROL_PERIOD_MS) * 1000ULL);
            loopService.loop();
            steps++;
        }
        clock.advanceToUs((originMs + untilMs) * 1000ULL);
    }

    /** @brief A dip to just under the ordinary level and back just over it: a module hovering there. */
    void crossTheLevelAt(uint64_t atMs, uint32_t downTo = 38500)
    {
        runUntilMs(atMs);
        holdDownTo(downTo);
        runUntilMs(atMs + 100);
        arduino.freeHeapValue = 41500;
    }

    /** @brief Somebody takes memory and keeps it: the free heap falls, and the watermark with it. */
    void holdDownTo(uint32_t freeBytes)
    {
        arduino.freeHeapValue = freeBytes;
        if (freeBytes < arduino.minFreeHeapValue)
        {
            arduino.minFreeHeapValue = freeBytes;
        }
    }

    /** @brief One reading of the logged sag, both figures as the tape had them. */
    void holdAt(const SagReading& reading)
    {
        arduino.freeHeapValue = reading.free;
        arduino.minFreeHeapValue = reading.watermark;
    }

    void giveBack() { arduino.freeHeapValue = STEADY_FREE_HEAP; }

    /** @brief The handshake's share of the ledger: the driver's buffers and the TLS session. */
    void theHandshakeHolds(uint32_t wifiBytes, uint32_t wifiBlocks)
    {
        arduino.setOwner(WIFI_TASK, wifiBytes, wifiBlocks);
    }

    /**
     * @brief The whole sag and its release: seven readings eighty milliseconds apart and the
     *        memory back 560 ms after the first, which is how the tape had it.
     */
    void playTheSag()
    {
        for (size_t i = 0; i < SAG_READINGS; i++)
        {
            runUntilMs(DIP_AT_MS + i * 80);
            holdAt(THE_SAG[i]);
        }
        runUntilMs(DIP_AT_MS + 560);
        giveBack();
        runUntilMs(2000);
    }

    std::vector<TapeLogger::Line> traceLines() const { return logger.linesSaying("HEAP_LOW"); }
    std::vector<TapeLogger::Line> episodeLines() const { return logger.linesSaying("HEAP_LOW: episode low"); }
    std::vector<TapeLogger::Line> heldByLines() const { return logger.linesSaying("held by"); }
};

/** @brief The whole trace of a scene, line for line; on a mismatch the message is the tape. */
void assertTheTrace(const Bench& bench, const char* const expected[], size_t expectedCount)
{
    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    const std::string tape = "the trace wrote:" + bench.logger.quoted();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)expectedCount, (uint32_t)lines.size(), tape.c_str());
    for (size_t i = 0; i < expectedCount; i++)
    {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected[i], lines[i].text.c_str(), tape.c_str());
    }
}

#define IDLE " net=idle acq=idle cmd=idle cat=idle"
#define AT_REST_HELD_BY \
    " pre-sched=61200/410 gone=24816/97 loopTask=18344/655 wifi=9040/61 outbox-net=8424/23" \
    " tiT=2112/17 async_tcp=512/4 other=0/0 all=124448/1267 owners=7 walk=2300us names=900us"

// --------------------------------------------------------------------------------------------
// The ledger cut down to a line
// --------------------------------------------------------------------------------------------

struct LedgerFixture
{
    HeapTaskLedger ledger;

    LedgerFixture()
    {
        memset(&ledger, 0, sizeof(ledger));
        ledger.tasksListed = true;
    }

    void task(uintptr_t handle, const char* name)
    {
        HeapTaskLedger::LiveTask& entry = ledger.tasks[ledger.taskCount++];
        entry.task = handle;
        snprintf(entry.name, sizeof(entry.name), "%s", name);
    }

    void owner(uintptr_t handle, uint32_t bytes, uint32_t blocks)
    {
        HeapTaskLedger::Owner& entry = ledger.owners[ledger.ownerCount++];
        entry.task = handle;
        entry.bytes = bytes;
        entry.blocks = blocks;
    }

    std::string line(uint32_t walkMicros = 0, uint32_t namesMicros = 0) const
    {
        HeapHolders holders;
        HeapHolders::reduce(ledger, walkMicros, namesMicros, holders);
        char text[HeapHolders::LINE_BYTES];
        HeapHolders::format(holders, text, sizeof(text));
        return text;
    }
};

/**
 * Thirteen tasks hold memory and the line has room for ten names: the ten largest, largest first,
 * whatever order the platform met them in, and the other three as one sum - so that the line has
 * a length that does not depend on how many tasks the module runs, and nothing falls off it. Two
 * of the three are pushed off the end by larger holders met later; the third is met last and is
 * too small for the line from the start.
 *
 * Mutations: place() inserts at the end - the order of the ledger instead of the order of size;
 * the holder that falls off the end is dropped instead of being added to "other"; so is the one
 * that never made the line.
 */
void test_the_largest_holders_are_named_largest_first_and_the_rest_are_summed(void)
{
    LedgerFixture f;
    const uint32_t bytes[] = {300, 1200, 100, 900, 500, 1100, 700, 200, 1000, 800, 600, 400, 50};
    for (size_t i = 0; i < 13; i++)
    {
        char name[16];
        snprintf(name, sizeof(name), "t%u", (unsigned)bytes[i]);
        f.task(0x1000 + i, name);
        f.owner(0x1000 + i, bytes[i], (uint32_t)(i + 1));
    }

    // Blocks are 1..13 in ledger order: t200 is the eighth (8 blocks), t100 the third (3) and
    // t50 the thirteenth.
    TEST_ASSERT_EQUAL_STRING(" t1200=1200/2 t1100=1100/6 t1000=1000/9 t900=900/4 t800=800/10"
                             " t700=700/7 t600=600/11 t500=500/5 t400=400/12 t300=300/1"
                             " other=350/24 all=7850/91 owners=13 walk=41us names=7us",
                             f.line(41, 7).c_str());
}

/**
 * Two kinds of owner are not live tasks. Handle 0 is memory taken before the scheduler started -
 * the static object graph - and is "pre-sched". A handle no live task answers to is a task that
 * was deleted while its memory stayed; there can be several, their handles mean nothing to a
 * reader, and they are one holder, "gone", placed by what they hold together.
 *
 * Mutations: a deleted task is named by its handle; each deleted task is a holder of its own -
 * "gone" twice, the smaller one below the wifi task.
 */
void test_memory_from_before_the_scheduler_and_from_deleted_tasks_is_named_for_what_it_is(void)
{
    LedgerFixture f;
    f.task(WIFI_TASK, "wifi");
    f.owner(0x3fcb6000, 300, 3);
    f.owner(WIFI_TASK, 500, 5);
    f.owner(0, 9000, 90);
    f.owner(0x3fcb7000, 400, 4);

    TEST_ASSERT_EQUAL_STRING(" pre-sched=9000/90 gone=700/7 wifi=500/5"
                             " other=0/0 all=10200/102 owners=4 walk=0us names=0us",
                             f.line().c_str());
}

/**
 * A name is one token of the line. FreeRTOS names may carry a space ("Tmr Svc"), and whatever a
 * name carries it must not be able to split the token or look like the separator of the next
 * one; a name of the full fifteen characters is kept whole, and one without its terminator is
 * cut at fifteen rather than read on.
 *
 * Mutation: the name is copied as it is - "Tmr Svc=96/2".
 */
void test_a_name_is_one_token_of_the_line(void)
{
    LedgerFixture f;
    f.task(0x1001, "Tmr Svc");
    f.task(0x1002, "a=b/c");
    f.task(0x1003, "");
    f.task(0x1004, "fifteen_chars_x");
    f.owner(0x1001, 96, 2);
    f.owner(0x1002, 80, 1);
    f.owner(0x1003, 64, 1);
    f.owner(0x1004, 48, 1);

    // A name that fills its sixteen bytes and has no terminator, as a careless copy would leave it.
    HeapTaskLedger::LiveTask& unterminated = f.ledger.tasks[f.ledger.taskCount++];
    unterminated.task = 0x1005;
    memset(unterminated.name, 'z', sizeof(unterminated.name));
    f.owner(0x1005, 32, 1);

    TEST_ASSERT_EQUAL_STRING(" Tmr_Svc=96/2 a_b_c=80/1 ?=64/1 fifteen_chars_x=48/1 zzzzzzzzzzzzzzz=32/1"
                             " other=0/0 all=320/6 owners=5 walk=0us names=0us",
                             f.line().c_str());
}

/**
 * The platform lists no tasks when more are alive than it was given room for. Owners are then
 * named by handle - a name nobody can read beats a wrong one - and none of them is called gone,
 * because without the list nothing says so.
 *
 * Mutation: tasksListed is ignored - every owner of a ledger without names becomes "gone".
 */
void test_owners_are_named_by_handle_when_the_tasks_could_not_be_listed(void)
{
    LedgerFixture f;
    f.ledger.tasksListed = false;
    f.owner(0x3fcb2000, 500, 5);
    f.owner(0, 400, 4);
    f.owner(0x3fcb3000, 300, 3);

    TEST_ASSERT_EQUAL_STRING(" @3fcb2000=500/5 pre-sched=400/4 @3fcb3000=300/3"
                             " other=0/0 all=1200/12 owners=3 walk=0us names=0us",
                             f.line().c_str());
}

/**
 * Holders of the same size keep the order the platform reported them in, so that the same heap
 * gives the same line and two lines can be compared by eye.
 *
 * Mutation: place() compares with <= - equal holders come out reversed.
 */
void test_equal_holders_keep_the_order_they_were_reported_in(void)
{
    LedgerFixture f;
    f.task(0x1001, "first");
    f.task(0x1002, "second");
    f.task(0x1003, "third");
    f.owner(0x1001, 512, 1);
    f.owner(0x1002, 512, 2);
    f.owner(0x1003, 512, 3);

    TEST_ASSERT_EQUAL_STRING(" first=512/1 second=512/2 third=512/3"
                             " other=0/0 all=1536/6 owners=3 walk=0us names=0us",
                             f.line().c_str());
}

/**
 * The platform collects no owner past the room it was given, and says nothing about it. A ledger
 * that comes back full may therefore be missing owners, and the line marks it: "owners=32+".
 *
 * Mutation: the mark is dropped - a truncated account reads as a whole one.
 */
void test_a_ledger_that_came_back_full_says_it_may_be_short(void)
{
    LedgerFixture f;
    for (size_t i = 0; i < HeapTaskLedger::MAX_OWNERS; i++)
    {
        f.owner(0, 10, 1);
    }

    const std::string full = f.line();
    TEST_ASSERT_TRUE_MESSAGE(full.find(" owners=32+ ") != std::string::npos, full.c_str());
    TEST_ASSERT_TRUE_MESSAGE(full.find(" all=320/32 ") != std::string::npos, full.c_str());

    LedgerFixture short_;
    for (size_t i = 0; i + 1 < HeapTaskLedger::MAX_OWNERS; i++)
    {
        short_.owner(0, 10, 1);
    }
    const std::string notFull = short_.line();
    TEST_ASSERT_TRUE_MESSAGE(notFull.find(" owners=31 ") != std::string::npos, notFull.c_str());
}

/**
 * A count larger than the array it counts - which neither platform call produces, both are given
 * the capacity and keep to it - is read up to the capacity and not past the array. What lies
 * behind each array in the ledger is made to matter here, so that a read past the end shows:
 * behind `owners` lie the count and the first live task, whose handle read as an owner's figures
 * is a billion bytes; behind `tasks` lies the count of tasks itself, and the one deleted task of
 * this ledger has that very number for a handle - a lookup that runs past the capacity finds it
 * there and calls the task live.
 *
 * The test leans on the order of the ledger's members for its power to catch the mutants, not for
 * its verdict on the code: reordered, it stays green and only stops being a proof.
 *
 * Mutations: the count of owners is not held to the capacity - "all=" grows by what lies behind
 * the array; the count of tasks is not - "gone=" becomes a holder with a name made of padding.
 */
void test_a_count_past_the_capacity_is_read_up_to_the_capacity_and_not_past_the_array(void)
{
    const uintptr_t handleBehindTheTasks = HeapTaskLedger::MAX_TASKS + 5;

    LedgerFixture f;
    for (size_t i = 0; i < HeapTaskLedger::MAX_TASKS; i++)
    {
        char name[16];
        snprintf(name, sizeof(name), "task%u", (unsigned)i);
        f.task(0x3fcb1000 + i * 0x100, name);
    }
    f.owner(handleBehindTheTasks, 100, 1);
    for (size_t i = 1; i < HeapTaskLedger::MAX_OWNERS; i++)
    {
        f.owner(0, 10, 1);
    }

    f.ledger.ownerCount = HeapTaskLedger::MAX_OWNERS + 5;
    f.ledger.taskCount = HeapTaskLedger::MAX_TASKS + 5;

    const std::string line = f.line();
    TEST_ASSERT_TRUE_MESSAGE(line.find(" all=410/32 ") != std::string::npos, line.c_str());
    TEST_ASSERT_TRUE_MESSAGE(line.find(" gone=100/1 ") == 0, line.c_str());
    TEST_ASSERT_TRUE_MESSAGE(line.find(" owners=37+ ") != std::string::npos, line.c_str());
}

/**
 * The buffer the loop service gives the line holds the widest line there can be: the longest
 * opening, ten fifteen-character names with ten-digit figures, and ten-digit sums. And a buffer
 * that is too small is filled to its end and not past it.
 */
void test_the_widest_line_fits_the_buffer_and_a_small_buffer_is_not_overrun(void)
{
    LedgerFixture f;
    for (size_t i = 0; i < HeapTaskLedger::MAX_OWNERS; i++)
    {
        char name[16];
        snprintf(name, sizeof(name), "fifteen_chars_%c", (char)('a' + (i % 26)));
        f.task(0x1000 + i, name);
        f.owner(0x1000 + i, 4000000000u - (uint32_t)i, 4000000000u);
    }

    HeapHolders holders;
    HeapHolders::reduce(f.ledger, 4000000000u, 4000000000u, holders);
    holders.skippedEpisodes = 4000000000u;
    holders.rxFramesKnown = true;
    holders.rxUnicastFrames = 4000000000u;
    holders.rxGroupFrames = 4000000000u;

    const char* const opening = "HEAP_LOW: after the release held by";
    char line[HeapHolders::LINE_BYTES];
    const int opened = snprintf(line, sizeof(line), "%s", opening);
    const size_t written = HeapHolders::format(holders, line + opened, sizeof(line) - (size_t)opened);

    char message[64];
    snprintf(message, sizeof(message), "the widest line is %u characters", (unsigned)(opened + written));
    TEST_ASSERT_TRUE_MESSAGE((size_t)opened + written < sizeof(line) - 1, message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, " walk=4000000000us names=4000000000us skipped=4000000000 rx_uc=4000000000 rx_mc=4000000000"), "the end of the widest line was cut");

    char small[40];
    memset(small, '#', sizeof(small));
    const size_t cut = HeapHolders::format(holders, small, 24);
    TEST_ASSERT_EQUAL_UINT32(23, (uint32_t)cut);
    TEST_ASSERT_EQUAL_UINT32(23, (uint32_t)strlen(small));
    TEST_ASSERT_EQUAL_CHAR_MESSAGE('#', small[24], "format() wrote past the size it was given");
}

// --------------------------------------------------------------------------------------------
// What the radio handed over, by the class of its destination
// --------------------------------------------------------------------------------------------

/**
 * The counter reads one bit of one byte: the group bit of the Ethernet destination address, the
 * lowest bit of the first byte of the frame. A frame to this station's address is unicast, and so
 * is one to a locally administered unicast address; a frame to an IPv4 multicast address
 * (01:00:5e:...), to the IPv6 multicast prefix (33:33:...) or to the broadcast address is group.
 * Nothing to count is nothing counted.
 *
 * Mutation: testing bit 0x02 (locally administered) instead of 0x01 - the IPv4 multicast frame is
 * then counted as unicast, and the locally administered unicast frame as group.
 */
void test_a_frame_is_counted_by_the_group_bit_of_its_destination(void)
{
    RxFrameClassCounter counter;
    const uint8_t toThisStation[] = {0x9C, 0x13, 0x9E, 0x9A, 0x52, 0x3C, 0x08, 0x00};
    const uint8_t toIpv4Multicast[] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0xFB, 0x08, 0x00};
    const uint8_t toIpv6Multicast[] = {0x33, 0x33, 0x00, 0x00, 0x00, 0x01, 0x86, 0xDD};
    const uint8_t toEveryone[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x06};
    const uint8_t toALocallyAdministeredAddress[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x08, 0x00};

    counter.count(toThisStation, sizeof(toThisStation));
    TEST_ASSERT_EQUAL_UINT32(1, counter.unicast());
    TEST_ASSERT_EQUAL_UINT32(0, counter.group());

    counter.count(toIpv4Multicast, sizeof(toIpv4Multicast));
    TEST_ASSERT_EQUAL_UINT32(1, counter.unicast());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, counter.group(), "an IPv4 multicast frame is group traffic");

    counter.count(toIpv6Multicast, sizeof(toIpv6Multicast));
    counter.count(toEveryone, sizeof(toEveryone));
    TEST_ASSERT_EQUAL_UINT32(1, counter.unicast());
    TEST_ASSERT_EQUAL_UINT32(3, counter.group());

    counter.count(toALocallyAdministeredAddress, sizeof(toALocallyAdministeredAddress));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, counter.unicast(), "a locally administered address is still unicast");
    TEST_ASSERT_EQUAL_UINT32(3, counter.group());

    counter.count(toThisStation, 0);
    counter.count(nullptr, sizeof(toThisStation));
    TEST_ASSERT_EQUAL_UINT32(2, counter.unicast());
    TEST_ASSERT_EQUAL_UINT32(3, counter.group());
}

/**
 * The account of a low says what the radio had handed over by then, unicast and group, and the
 * account at rest says it again: the difference between the two lines is what arrived while the
 * memory was held, and its class - which is what tells a burst of the station's own traffic from
 * the group traffic the access point releases after a beacon. The figures are the reading's, like
 * the holders: frames counted after the walk and before the print are not on the line about the
 * low. A loop that was given no counter writes the lines as before (every other scene here).
 *
 * Mutation: copying the pair when the line is written - the line about the low reads rx_uc=6.
 */
void test_the_account_of_a_low_says_what_the_radio_had_handed_over_by_then_by_class(void)
{
    Bench bench;
    RxFrameClassCounter frames;
    bench.loopService.setRxFrameClasses(&frames);

    const uint8_t unicast[] = {0x9C, 0x13, 0x9E, 0x9A, 0x52, 0x3C, 0x08, 0x00};
    const uint8_t group[] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0xFB, 0x08, 0x00};
    for (int i = 0; i < 3; i++) frames.count(unicast, sizeof(unicast));
    for (int i = 0; i < 2; i++) frames.count(group, sizeof(group));

    bench.runUntilMs(DIP_AT_MS);
    bench.holdDownTo(24996);
    bench.theHandshakeHolds(46288, 310);
    bench.runUntilMs(FOUND_AT_MS + 1);
    // After the reading that walked the heap, before its line is written.
    for (int i = 0; i < 3; i++) frames.count(unicast, sizeof(unicast));
    bench.runUntilMs(1503);
    bench.giveBack();
    bench.theHandshakeHolds(9040, 61);
    for (int i = 0; i < 7; i++) frames.count(group, sizeof(group));
    bench.runUntilMs(2000);

    const char* const expected[] = {
        "HEAP_LOW: new watermark 24996 (was 41332)" IDLE " free=24996 step=0ms",
        "HEAP_LOW: held by pre-sched=61200/410 wifi=46288/310 gone=24816/97 loopTask=18344/655"
        " outbox-net=8424/23 tiT=2112/17 async_tcp=512/4 other=0/0 all=161696/1516 owners=7 walk=2300us names=900us"
        " rx_uc=3 rx_mc=2",
        "HEAP_LOW: released after 500 ms (free 62244)",
        "HEAP_LOW: after the release held by" AT_REST_HELD_BY " rx_uc=6 rx_mc=9",
    };
    assertTheTrace(bench, expected, 4);
}

// --------------------------------------------------------------------------------------------
// Who holds the memory at a low
// --------------------------------------------------------------------------------------------

/**
 * The headline. Somebody takes thirty-seven thousand bytes and keeps them; every task of the
 * board is idle, and the line about the low can only say so. The reading that found the low also
 * asks the platform who holds the heap - there and then, while it is held - and the account
 * goes out with the line, at the top of the next step: the wifi task has 46 288 bytes in 310
 * blocks. When the memory is back the closing line is followed by the same account at rest, where
 * the wifi task has 9 040 in 61 - and the difference is who took the memory.
 *
 * Without the holders' account the tape is the first and the third line only.
 */
void test_a_low_that_is_still_held_is_followed_by_who_holds_it(void)
{
    Bench bench;

    bench.runUntilMs(DIP_AT_MS);
    bench.holdDownTo(24996);
    bench.theHandshakeHolds(46288, 310);
    bench.runUntilMs(1503);
    bench.giveBack();
    bench.theHandshakeHolds(9040, 61);
    bench.runUntilMs(2000);

    const char* const expected[] = {
        "HEAP_LOW: new watermark 24996 (was 41332)" IDLE " free=24996 step=0ms",
        "HEAP_LOW: held by pre-sched=61200/410 wifi=46288/310 gone=24816/97 loopTask=18344/655"
        " outbox-net=8424/23 tiT=2112/17 async_tcp=512/4 other=0/0 all=161696/1516 owners=7 walk=2300us names=900us",
        "HEAP_LOW: released after 500 ms (free 62244)",
        "HEAP_LOW: after the release held by" AT_REST_HELD_BY,
    };
    assertTheTrace(bench, expected, 4);

    // The account of the low is taken by the reading and written with the line about the low, a
    // step later; the account at rest is taken and written on the step that found the release.
    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    TEST_ASSERT_EQUAL_UINT64(SAID_AT_MS, lines[0].atMs);
    TEST_ASSERT_EQUAL_UINT64(SAID_AT_MS, lines[1].atMs);
    TEST_ASSERT_EQUAL_UINT64(1510, lines[2].atMs);
    TEST_ASSERT_EQUAL_UINT32(2, bench.arduino.ledgerReads);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(FOUND_AT_MS, bench.arduino.ledgerReadAtMs[0],
                                     "the heap was not walked by the reading that found the low");
    TEST_ASSERT_EQUAL_UINT64(1510, bench.arduino.ledgerReadAtMs[1]);
}

/**
 * The account is the reading's, like everything else on the line: by the time the line is
 * written, a step later, the holder may have let go. Here it has - the ledger at the moment of
 * printing shows the wifi task back at rest - and the line still says what the reading found.
 *
 * Mutation: the heap is walked when the line is written - "wifi=9040/61" under a low of 24 996.
 */
void test_the_account_of_the_holders_is_of_the_reading_and_not_of_the_print(void)
{
    Bench bench;

    bench.runUntilMs(DIP_AT_MS);
    bench.holdDownTo(24996);
    bench.theHandshakeHolds(46288, 310);
    bench.runUntilMs(FOUND_AT_MS + 1);
    bench.theHandshakeHolds(9040, 61);
    bench.runUntilMs(SAID_AT_MS + 1);

    const std::vector<TapeLogger::Line> heldBy = bench.heldByLines();
    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)heldBy.size(), tape.c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(heldBy[0].text.c_str(), " wifi=46288/310 "), tape.c_str());
}

/**
 * A dip that opened and closed inside the step has no holders left to find: the reading shows the
 * free heap back, and a walk then would lock the heap to describe a module at rest under a line
 * about a low. The line goes out alone.
 *
 * Mutation: every low found is accounted for, held or not.
 */
void test_a_low_already_given_back_is_not_walked_for(void)
{
    Bench bench;

    bench.runUntilMs(DIP_AT_MS);
    bench.arduino.minFreeHeapValue = 24996; // taken and given back before the reading
    bench.runUntilMs(2000);

    const char* const expected[] = {
        "HEAP_LOW: new watermark 24996 (was 41332)" IDLE " free=62244 step=0ms",
    };
    assertTheTrace(bench, expected, 1);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, bench.arduino.ledgerReads,
                                     "the heap was walked for memory nobody holds any more");
}

/**
 * The sag of the logged run, reading for reading: seven lines about a low in half a second. Seven
 * walks of the heap would be seven times the heap locked inside the handshake that is being
 * watched, so the walks follow the depth and not the lines: the first low of the episode, and
 * after that a low only when the free heap is a quarter below the last one accounted for -
 * 39 624, then 26 924, then 17 156, then 10 540. Four, the deepest among them; and one more at
 * rest when the memory is back.
 *
 * Mutations: every line is accounted for - seven walks; only the first low of an episode is -
 * the account of the floor, which is the one the question is about, is missing.
 */
void test_the_sag_of_the_logged_run_is_walked_four_times_and_not_seven(void)
{
    Bench bench;
    bench.playTheSag();

    const char* const expected[] = {
        "HEAP_LOW: new watermark 39600 (was 41332)" IDLE " free=39624 step=0ms",
        "HEAP_LOW: held by" AT_REST_HELD_BY,
        "HEAP_LOW: new watermark 36380 (was 39600)" IDLE " free=36404 step=0ms",
        "HEAP_LOW: new watermark 31700 (was 36380)" IDLE " free=32252 step=0ms",
        "HEAP_LOW: new watermark 26900 (was 31700)" IDLE " free=26924 step=0ms",
        "HEAP_LOW: held by" AT_REST_HELD_BY,
        "HEAP_LOW: new watermark 22172 (was 26900)" IDLE " free=22116 step=0ms",
        "HEAP_LOW: new watermark 17212 (was 22172)" IDLE " free=17156 step=0ms",
        "HEAP_LOW: held by" AT_REST_HELD_BY,
        "HEAP_LOW: new watermark 10516 (was 17212)" IDLE " free=10540 step=0ms",
        "HEAP_LOW: held by" AT_REST_HELD_BY,
        "HEAP_LOW: released after 560 ms (free 62244)",
        "HEAP_LOW: after the release held by" AT_REST_HELD_BY,
    };
    assertTheTrace(bench, expected, 13);
    TEST_ASSERT_EQUAL_UINT32(4 + 1, bench.arduino.ledgerReads);
}

/**
 * The edges of "a quarter below": at exactly three quarters of the last low accounted for the
 * heap is walked again, one byte above it is not. 32 000 was accounted for; 24 001 is said and
 * not walked for, 24 000 is both.
 *
 * Mutation: `<` for `<=` in the comparison - the low at exactly three quarters gets no account.
 */
void test_a_low_is_accounted_for_again_at_three_quarters_of_the_last_and_not_above(void)
{
    Bench above;
    above.runUntilMs(DIP_AT_MS);
    above.holdDownTo(32000);
    above.runUntilMs(1103);
    above.holdDownTo(24001);
    above.runUntilMs(1203);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)above.logger.linesSaying("HEAP_LOW: new watermark").size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, above.arduino.ledgerReads,
                                     "a low less than a quarter below the last account was walked for");

    Bench onIt;
    onIt.runUntilMs(DIP_AT_MS);
    onIt.holdDownTo(32000);
    onIt.runUntilMs(1103);
    onIt.holdDownTo(24000);
    onIt.runUntilMs(1203);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)onIt.logger.linesSaying("HEAP_LOW: new watermark").size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, onIt.arduino.ledgerReads,
                                     "a low a quarter below the last account was not walked for");
}

// --------------------------------------------------------------------------------------------
// The dips the since-boot watermark hides
// --------------------------------------------------------------------------------------------

/**
 * The second blindness. The sag has been and gone and the watermark stands at 10 516; minutes
 * later somebody takes thirty-seven thousand bytes for four hundred milliseconds. The platform's
 * watermark does not move - 25 012 is not lower than 10 516 - so the trace that reads only the
 * watermark has nothing to say, and the dip is silent. The heap-forensics build looks at the free heap
 * itself: the dip is an episode low, said like any low - who was at work, the free heap, the
 * step - followed by who holds the memory, closed by its release and by the account at rest.
 *
 * Without the episode look the tape ends with the release of the sag.
 */
void test_a_dip_shallower_than_the_floor_is_said_as_an_episode_low(void)
{
    Bench bench;
    bench.playTheSag();
    const size_t linesOfTheSag = bench.traceLines().size();

    bench.runUntilMs(300003);
    bench.holdDownTo(25012);
    bench.theHandshakeHolds(46272, 309);
    bench.runUntilMs(300403);
    bench.giveBack();
    bench.theHandshakeHolds(9040, 61);
    bench.runUntilMs(301000);

    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)linesOfTheSag + 4, (uint32_t)lines.size(), tape.c_str());

    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 25012 (watermark 10516)" IDLE " free=25012 step=0ms",
                             lines[linesOfTheSag].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: held by pre-sched=61200/410 wifi=46272/309 gone=24816/97"
                             " loopTask=18344/655 outbox-net=8424/23 tiT=2112/17 async_tcp=512/4"
                             " other=0/0 all=161680/1515 owners=7 walk=2300us names=900us",
                             lines[linesOfTheSag + 1].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 400 ms (free 62244)",
                             lines[linesOfTheSag + 2].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: after the release held by" AT_REST_HELD_BY,
                             lines[linesOfTheSag + 3].text.c_str());

    // Found by the reading at the end of the step the dip fell in, said at the top of the next.
    TEST_ASSERT_EQUAL_UINT64(300020, lines[linesOfTheSag].atMs);
    TEST_ASSERT_EQUAL_UINT32(THE_FLOOR, bench.arduino.minFreeHeapValue);
}

/**
 * Inside an episode the hysteresis of the trace applies, against the last low said in it: half a
 * kilobyte deeper is not news, a kilobyte is. 25 012 is said; 24 500 is not; 23 988 - one
 * REPORT_STEP_BYTES below 25 012 - is.
 *
 * Mutations: an episode has no hysteresis - a line for every step the free heap is lower than on
 * the one before; every episode low restarts the watch - "released after 100 ms".
 */
void test_inside_an_episode_a_low_within_the_hysteresis_is_not_said_again(void)
{
    Bench bench;
    bench.playTheSag();
    const size_t before = bench.episodeLines().size();

    bench.runUntilMs(300003);
    bench.holdDownTo(25012);
    bench.runUntilMs(300103);
    bench.holdDownTo(24500);
    bench.runUntilMs(300203);
    bench.holdDownTo(23989);
    bench.runUntilMs(300303);

    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 1, (uint32_t)bench.episodeLines().size(), tape.c_str());

    bench.holdDownTo(23988);
    bench.runUntilMs(300403);

    const std::vector<TapeLogger::Line> episodes = bench.episodeLines();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 2, (uint32_t)episodes.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 23988 (watermark 10516)" IDLE " free=23988 step=0ms",
                             episodes.back().text.c_str());

    // Two lows of one holding-on: the watch is the one the first low opened, and closes once.
    bench.giveBack();
    bench.runUntilMs(301000);
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 400 ms (free 62244)",
                             bench.logger.linesSaying("HEAP_LOW: released").back().text.c_str());
}

/**
 * The level is the trace's own: a free heap at REPORT_BELOW_BYTES is ordinary, one byte under it
 * is an episode low.
 *
 * Mutation: `>` for `>=` on the level.
 */
void test_the_free_heap_at_the_ordinary_level_is_not_an_episode_low(void)
{
    Bench bench;
    bench.playTheSag();
    const size_t before = bench.episodeLines().size();

    bench.runUntilMs(300003);
    bench.holdDownTo(40000);
    bench.runUntilMs(300203);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before, (uint32_t)bench.episodeLines().size(),
                                     "a free heap at the ordinary level was said as a low");

    bench.holdDownTo(39999);
    bench.runUntilMs(300403);
    const std::vector<TapeLogger::Line> episodes = bench.episodeLines();
    TEST_ASSERT_EQUAL_UINT32((uint32_t)before + 1, (uint32_t)episodes.size());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 39999 (watermark 10516)" IDLE " free=39999 step=0ms",
                             episodes.back().text.c_str());
}

/** @brief A heap that loses another two thousand bytes between two looks of the same reading. */
class SinkingArduino : public LedgerArduino
{
public:
    explicit SinkingArduino(VirtualClock& clock) : LedgerArduino(clock) {}

    uint32_t getFreeHeap() override
    {
        const uint32_t seen = LedgerArduino::getFreeHeap();
        if (sinkOnce && seen < HeapWatermarkTracer::REPORT_BELOW_BYTES)
        {
            sinkOnce = false;
            freeHeapValue -= 2000;
            minFreeHeapValue = freeHeapValue;
        }
        return seen;
    }

    bool sinkOnce = false;
};

/**
 * The reading that finds a new watermark looks at the free heap twice - once for the line about
 * the watermark, once for the episode - and on the device another core may take more memory in
 * between. The second look then finds a low a kilobyte deeper than the first, which would be an
 * episode low worth a line - and that line would take the place of the one about the watermark,
 * which has not been written yet. The watermark's line is the reading's line; what the second
 * look saw is found again by the next reading, as the new watermark it is.
 *
 * Mutation: the look for the episode does not ask whether this reading has its line already -
 * "new watermark 24996" never reaches the tape.
 */
void test_a_low_that_deepens_between_the_two_looks_of_a_reading_keeps_its_line(void)
{
    VirtualClock clock;
    SinkingArduino arduino(clock);
    TapeLogger logger(clock);
    DiagnosticChannel channel;
    LoopServiceConfigureDone loopService;
    arduino.freeHeapValue = STEADY_FREE_HEAP;
    arduino.minFreeHeapValue = STEADY_WATERMARK;
    loopService.setLogging(&logger, &channel);
    loopService.setArduinoAdapter(&arduino);

    for (int step = 0; step < 120; step++)
    {
        if (step == 100)
        {
            arduino.freeHeapValue = 24996;
            arduino.minFreeHeapValue = 24996;
            arduino.sinkOnce = true;
        }
        loopService.loop();
        clock.advanceToUs((uint64_t)(step + 1) * CONTROL_PERIOD_MS * 1000ULL);
    }

    const std::vector<TapeLogger::Line> lows = logger.linesSaying("HEAP_LOW: ");
    TEST_ASSERT_TRUE(lows.size() >= 3);
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 24996 (was 41332) free=24996 step=0ms",
                             lows[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: held by other=0/0 all=0/0 owners=0 walk=2300us names=900us",
                             lows[1].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 22996 (was 24996) free=22996 step=0ms",
                             lows[2].text.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)logger.linesSaying("episode low").size());
}

/**
 * The line of a new since-boot watermark is the one it always was, whatever the line before it
 * was. Here the line before it was an episode low - and the low that follows goes under the
 * floor: 8 000 where the watermark stood at 10 516.
 *
 * Mutation: sample() leaves the episode mark of the previous line on the one it fills in -
 * "episode low 8000 (watermark 10516)", a line that says the opposite of what happened.
 */
void test_a_new_watermark_after_an_episode_low_is_said_as_a_new_watermark(void)
{
    Bench bench;
    bench.playTheSag();

    bench.runUntilMs(300003);
    bench.holdDownTo(25012);
    bench.runUntilMs(300403);
    bench.giveBack();
    bench.runUntilMs(301003);
    bench.holdDownTo(8000);
    bench.runUntilMs(301403);
    bench.giveBack();
    bench.runUntilMs(302000);

    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    const std::vector<TapeLogger::Line> floors = bench.logger.linesSaying(" 8000 ");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)floors.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: new watermark 8000 (was 10516)" IDLE " free=8000 step=0ms",
                                     floors[0].text.c_str(), tape.c_str());
}

/**
 * An episode low says how long its step was, like any low: the figure that tells a step's own
 * work from somebody holding on. Here the step itself takes twelve milliseconds and leaves
 * thirty-seven thousand bytes held at its end.
 *
 * Mutation: the loop hands the episode a constant for the step's length - "step=0ms", which is
 * also what every scene with a step of no length would print.
 */
void test_an_episode_low_says_how_long_its_step_was(void)
{
    Bench bench;
    bench.playTheSag();

    bench.runUntilMs(300003);
    bench.midStep.freeBytes = 25012;
    bench.midStep.takesMs = 12;
    bench.midStep.armed = true;
    bench.runUntilMs(300100);

    const std::vector<TapeLogger::Line> episodes = bench.episodeLines();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)episodes.size());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 25012 (watermark 10516)" IDLE " free=25012 step=12ms",
                             episodes[0].text.c_str());
}

/**
 * "Released" is the end of the episode there and then, not at the next reading that happens to
 * find the heap back. The memory is given back, the look at the top of a step sees it and closes
 * the watch - and inside that same step thirty-two thousand bytes are taken again, so that no
 * reading ever finds the heap at its level. The second holding-on is shallower than the low the
 * first one said; it is a new episode all the same, with its line, its account and its release.
 *
 * The step that carries it is three milliseconds long: the account at rest after the release is
 * taken inside it, and the scripted platform calls cost 2 300 + 900 microseconds.
 *
 * Mutation: pollRelease() leaves the forgetting to the next reading - the second holding-on is
 * silent from its low to its release, which is the blindness the episodes were built against.
 */
void test_memory_given_back_and_taken_again_inside_one_step_is_a_new_episode(void)
{
    Bench bench;
    bench.playTheSag();
    const size_t linesOfTheSag = bench.traceLines().size();

    bench.runUntilMs(300003);
    bench.holdDownTo(25012);
    bench.runUntilMs(300203);
    bench.giveBack();                 // the look at the top of the step at 300210 finds it back ...
    bench.midStep.freeBytes = 30000;  // ... and inside that same step it is taken again
    bench.midStep.armed = true;
    bench.runUntilMs(300603);
    bench.giveBack();
    bench.runUntilMs(301000);

    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)linesOfTheSag + 8, (uint32_t)lines.size(), tape.c_str());

    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 200 ms (free 62244)", lines[linesOfTheSag + 2].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: after the release held by" AT_REST_HELD_BY,
                             lines[linesOfTheSag + 3].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 30000 (watermark 10516)" IDLE " free=30000 step=3ms",
                             lines[linesOfTheSag + 4].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: held by" AT_REST_HELD_BY, lines[linesOfTheSag + 5].text.c_str());
    // Found by the reading at 300213, three milliseconds into its step; found back at 300610.
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 397 ms (free 62244)", lines[linesOfTheSag + 6].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: after the release held by" AT_REST_HELD_BY,
                             lines[linesOfTheSag + 7].text.c_str());
}

/**
 * An episode is forgotten when its watch closes and not before. While the memory is held the
 * free heap may come most of the way back and fall again: that is the same holding-on, and a
 * second fall shallower than the low already said adds nothing. Once the watch has closed with
 * "released", the very same fall is a new episode and is said.
 *
 * Mutations: the episode is forgotten by any reading that finds the free heap higher than the
 * low it said - the second fall is said inside the open watch; the episode is never forgotten -
 * the fall after the release is silent, which is the blindness this was built against.
 */
void test_an_episode_is_forgotten_when_its_watch_closes_and_not_before(void)
{
    Bench bench;
    bench.playTheSag();
    const size_t before = bench.episodeLines().size();

    bench.runUntilMs(300003);
    bench.holdDownTo(25012);
    bench.runUntilMs(300203);
    bench.holdDownTo(39000); // most of it back - and still below the ordinary level
    bench.runUntilMs(300403);
    bench.holdDownTo(30000); // down again, shallower than the low already said
    bench.runUntilMs(300603);

    std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 1, (uint32_t)bench.episodeLines().size(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)bench.logger.linesSaying("HEAP_LOW: released").size(),
                                     "the watch of the second episode closed while the memory was held");

    bench.giveBack();
    bench.runUntilMs(300803);
    bench.holdDownTo(30000);
    bench.runUntilMs(301000);

    tape = "the trace wrote:" + bench.logger.quoted();
    const std::vector<TapeLogger::Line> episodes = bench.episodeLines();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 2, (uint32_t)episodes.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 30000 (watermark 10516)" IDLE " free=30000 step=0ms",
                             episodes.back().text.c_str());
    // From the reading at 300010 to the step at 300610 that found the heap back.
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 600 ms (free 62244)",
                             bench.logger.linesSaying("HEAP_LOW: released").back().text.c_str());
}

/**
 * "Not before" to the letter. The memory comes back in the middle of a step and is taken again
 * before the next one begins: the reading at the end of the step finds the heap at its level,
 * and no look at the watch ever does - the watch stays open, and so does the episode. The second
 * holding-on, shallower than the low already said, is the same episode and adds no line; the
 * release that does come is dated from the first.
 *
 * Mutation: a reading that finds the heap back forgets the episode whether or not a watch is
 * open - "episode low 30000" inside the watch that 25 012 opened.
 */
void test_a_reading_that_finds_the_heap_back_does_not_end_an_episode_a_watch_still_holds(void)
{
    Bench bench;
    bench.playTheSag();
    const size_t before = bench.episodeLines().size();

    bench.runUntilMs(300003);
    bench.holdDownTo(25012);
    bench.runUntilMs(300203);

    // The step at 300210: its look at the watch finds 25 012, its reading finds 62 244 ...
    bench.midStep.freeBytes = STEADY_FREE_HEAP;
    bench.midStep.armed = true;
    bench.runUntilMs(300211);
    // ... and before the step at 300220 looks again, thirty-two thousand are taken once more.
    bench.holdDownTo(30000);
    bench.runUntilMs(300403);
    bench.giveBack();
    bench.runUntilMs(301000);

    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 1, (uint32_t)bench.episodeLines().size(), tape.c_str());
    const std::vector<TapeLogger::Line> released = bench.logger.linesSaying("HEAP_LOW: released");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)released.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 400 ms (free 62244)", released.back().text.c_str());
}

/**
 * A watch that runs out of time says "still held" once and ends; the memory is still down, and
 * the episode stays open with it: a module that has moved to a level below the ordinary one is
 * not told so again every minute. It is forgotten when the heap is next seen back - nobody says
 * that, there is no watch left to - and the dip after that is a new episode.
 *
 * Mutation: the episode is forgotten on "still held" as it is on "released" - the same low is
 * said again on the very next step, and again a minute later, for as long as the level lasts.
 */
void test_a_level_held_past_the_limit_is_said_once_and_the_dip_after_its_return_is_said(void)
{
    Bench bench;
    bench.playTheSag();
    const size_t before = bench.episodeLines().size();

    bench.runUntilMs(300003);
    bench.holdDownTo(25012);
    bench.runUntilMs(300003 + 200000);

    std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 1, (uint32_t)bench.episodeLines().size(), tape.c_str());
    const std::vector<TapeLogger::Line> stillHeld = bench.logger.linesSaying("HEAP_LOW: still held");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stillHeld.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: still held after 60000 ms (free 25012)", stillHeld[0].text.c_str());
    // And who holds it a minute on: the account that follows a watch that ran out, taken on the
    // same step - the two platform calls are what the three milliseconds between the lines are.
    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: held by" AT_REST_HELD_BY, lines.back().text.c_str());
    TEST_ASSERT_EQUAL_UINT64(stillHeld[0].atMs + (WALK_COST_US + NAMES_COST_US) / 1000, lines.back().atMs);

    bench.giveBack();
    bench.runUntilMs(600003);
    bench.holdDownTo(30000);
    bench.runUntilMs(600403);

    tape = "the trace wrote:" + bench.logger.quoted();
    const std::vector<TapeLogger::Line> episodes = bench.episodeLines();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 2, (uint32_t)episodes.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 30000 (watermark 10516)" IDLE " free=30000 step=0ms",
                             episodes.back().text.c_str());
}

/**
 * An episode low names who was at work, read against the same window as any other low: the span
 * since the previous reading. A request that began three milliseconds after a step is seven
 * milliseconds old at the reading; one that was over before the previous reading is not named.
 *
 * Mutation: the window is opened at the reading itself, the date sample() has just written over
 * the previous one - an act that ended inside the window reads as idle.
 */
void test_an_episode_low_names_who_was_at_work(void)
{
    Bench bench;
    bench.playTheSag();

    // Over before the window of the reading that will find the low: not to be named.
    bench.runUntilMs(299903);
    bench.board.actStarted(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_OTHER);
    bench.runUntilMs(299906);
    bench.board.actFinished(BackgroundActBoard::COMMAND_NETWORK, BackgroundActs::CMD_OTHER);

    bench.runUntilMs(300003);
    bench.board.actStarted(BackgroundActBoard::NETWORK,
                           BackgroundActs::ofOutboxKind(OutboxKind::SENSOR_UPDATE));
    bench.board.actStarted(BackgroundActBoard::CATALOG_STORAGE, BackgroundActs::CATALOG_WRITE);
    bench.holdDownTo(25012);
    bench.runUntilMs(300006);
    bench.board.actFinished(BackgroundActBoard::CATALOG_STORAGE, BackgroundActs::CATALOG_WRITE);
    bench.runUntilMs(300100);

    const std::vector<TapeLogger::Line> episodes = bench.episodeLines();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)episodes.size());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 25012 (watermark 10516)"
                             " net=SENSOR_UPDATE(7ms) acq=idle cmd=idle cat=WRITE(done 4ms ago)"
                             " free=25012 step=0ms",
                             episodes[0].text.c_str());
}

/**
 * A low that is a new since-boot watermark is one line, the one it always was. The reading that
 * found it enters it in the episode, so the looks at the free heap that follow - on the same
 * reading and on every later step while the memory stays where it is - find a low already said.
 *
 * Mutation: noteTheMoment() does not enter the low in the episode - the step after the watermark's
 * line says the same low again, as "episode low 24996 (watermark 24996)". (The guard against a
 * second line on the very same reading is another test's:
 * test_a_low_that_deepens_between_the_two_looks_of_a_reading_keeps_its_line.)
 */
void test_a_new_watermark_is_one_line_and_not_an_episode_low_as_well(void)
{
    Bench bench;

    bench.runUntilMs(DIP_AT_MS);
    bench.holdDownTo(24996);
    bench.runUntilMs(1500);

    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)bench.logger.linesSaying("HEAP_LOW: new watermark").size(),
                                     tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)bench.episodeLines().size(), tape.c_str());
}

// --------------------------------------------------------------------------------------------
// A module that hovers at the ordinary level
// --------------------------------------------------------------------------------------------

/**
 * A working set of three kilobytes that crosses the level twice a second is forty episodes in ten
 * seconds. Each of them is said, low and release - that is what the module is doing, and it costs
 * two short lines. What they do not each get is the heap walked for them, twice, and half a
 * kilobyte of log: the first account of an episode is rationed. Three episodes in a row have it -
 * the two handshakes of a returning link are both wanted - and then one episode in ten seconds.
 * The ones that went without are counted, and the next account says how many: "skipped=17".
 *
 * Mutations: no ration - forty walks and eighty lines in these ten seconds; a ration that is
 * never earned back - the twenty-first episode goes without as well; one earned back in five
 * seconds instead of ten; an episode that went without still gets the account at its close; the
 * count is not kept, not said, or not started again.
 */
void test_a_module_hovering_at_the_level_is_walked_for_three_episodes_and_then_one_in_ten_seconds(void)
{
    Bench bench;
    bench.playTheSag();
    const size_t linesOfTheSag = bench.traceLines().size();
    const uint32_t walksOfTheSag = bench.arduino.ledgerReads;

    for (int i = 0; i < 20; i++)
    {
        bench.crossTheLevelAt(300003 + (uint64_t)i * 500);
    }
    bench.runUntilMs(310000);

    std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(20, (uint32_t)bench.episodeLines().size(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(20 + 1, (uint32_t)bench.logger.linesSaying("HEAP_LOW: released after 100 ms").size()
                                                 + (uint32_t)bench.logger.linesSaying("HEAP_LOW: released after 560 ms").size(),
                                     tape.c_str());
    // Twenty lows and twenty releases; three of the episodes with an account at the low and one at
    // the close - the first three.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)linesOfTheSag + 40 + 6, (uint32_t)bench.traceLines().size(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(walksOfTheSag + 6, bench.arduino.ledgerReads, tape.c_str());
    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    for (size_t episode = 0; episode < 3; episode++)
    {
        const size_t at = linesOfTheSag + episode * 4;
        TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: episode low 38500 (watermark 10516)" IDLE " free=38500 step=0ms",
                                         lines[at].text.c_str(), tape.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: held by" AT_REST_HELD_BY, lines[at + 1].text.c_str(), tape.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: released after 100 ms (free 41500)", lines[at + 2].text.c_str(),
                                         tape.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: after the release held by" AT_REST_HELD_BY,
                                         lines[at + 3].text.c_str(), tape.c_str());
    }

    // Ten seconds after the first of the three, one more episode has earned its account, and the
    // account says how many went without.
    bench.crossTheLevelAt(310003);
    bench.runUntilMs(310500);

    tape = "the trace wrote:" + bench.logger.quoted();
    const std::vector<TapeLogger::Line> after = bench.traceLines();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)linesOfTheSag + 40 + 6 + 4, (uint32_t)after.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: held by" AT_REST_HELD_BY " skipped=17",
                                     after[after.size() - 3].text.c_str(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: after the release held by" AT_REST_HELD_BY,
                                     after.back().text.c_str(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32(walksOfTheSag + 8, bench.arduino.ledgerReads);

    // And the count starts again: the episode after that goes without, and is the first of a new count.
    bench.crossTheLevelAt(310503);
    bench.crossTheLevelAt(320503);
    bench.runUntilMs(321000);
    const std::vector<TapeLogger::Line> heldBy = bench.logger.linesSaying("HEAP_LOW: held by");
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: held by" AT_REST_HELD_BY " skipped=1", heldBy.back().text.c_str());
}

/**
 * What is rationed is the first account of an episode and nothing else. An episode that went
 * without and then sags a quarter deeper is walked for there and then - hovering does not go a
 * quarter deeper, a sag does - and, having been walked for, gets the account at its close.
 *
 * Mutation: a low a quarter deeper asks the ration as well - a sag that begins while the ration
 * is spent goes to its floor without a single account.
 */
void test_a_sag_inside_an_episode_that_went_without_its_account_is_walked_for(void)
{
    Bench bench;
    bench.playTheSag();

    bench.crossTheLevelAt(300003);
    bench.crossTheLevelAt(300503);
    bench.crossTheLevelAt(301003);
    bench.runUntilMs(301503);
    const size_t before = bench.traceLines().size();

    bench.holdDownTo(38500);       // the fourth in a row: said, not walked for
    bench.runUntilMs(301603);
    bench.holdDownTo(28875);       // three quarters of 38 500 to the byte
    bench.runUntilMs(301803);
    bench.giveBack();
    bench.runUntilMs(302500);

    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 5, (uint32_t)lines.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 38500 (watermark 10516)" IDLE " free=38500 step=0ms",
                             lines[before].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 28875 (watermark 10516)" IDLE " free=28875 step=0ms",
                             lines[before + 1].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: held by" AT_REST_HELD_BY " skipped=1", lines[before + 2].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 300 ms (free 62244)", lines[before + 3].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: after the release held by" AT_REST_HELD_BY, lines[before + 4].text.c_str());
}

/**
 * The wait for the fourth episode runs from the first of the row. A row that begins a second
 * before a round ten seconds of uptime is still three long: the fourth, half a second past the
 * round figure, goes without.
 *
 * Mutation: a full ration keeps the date it was last counted from, which is a round ten seconds -
 * the third episode of the row crosses it, earns one, and the row is four long.
 */
void test_the_ten_seconds_run_from_the_first_episode_of_the_row(void)
{
    Bench bench;
    bench.playTheSag();
    const uint32_t walksOfTheSag = bench.arduino.ledgerReads;

    bench.crossTheLevelAt(309003);
    bench.crossTheLevelAt(309503);
    bench.crossTheLevelAt(310003);
    bench.crossTheLevelAt(310503);
    bench.runUntilMs(311000);

    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, (uint32_t)bench.episodeLines().size(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(walksOfTheSag + 6, bench.arduino.ledgerReads, tape.c_str());
}

/**
 * The ration is the episodes', whichever kind of line opens them. A module that hovers at the
 * level while its since-boot watermark is still up there makes a new watermark of every crossing
 * that is a kilobyte deeper than the last - and those are rationed by the same dates: three in a
 * row, the fourth goes without, and ten seconds after the first the fifth does not.
 *
 * Mutation: the reading of a new watermark hands the ration no date - the fifth episode, ten
 * seconds on, finds nothing earned.
 */
void test_episodes_opened_by_a_new_watermark_are_rationed_by_the_same_dates(void)
{
    Bench bench;

    bench.crossTheLevelAt(1003, 39000);
    bench.crossTheLevelAt(1503, 37900);
    bench.crossTheLevelAt(2003, 36800);
    bench.crossTheLevelAt(2503, 35700);
    bench.runUntilMs(3000);

    std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, (uint32_t)bench.logger.linesSaying("HEAP_LOW: new watermark").size(),
                                     tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)bench.episodeLines().size(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, bench.arduino.ledgerReads, tape.c_str());

    bench.crossTheLevelAt(11003, 34600);
    bench.runUntilMs(11500);

    tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, bench.arduino.ledgerReads, tape.c_str());
    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: new watermark 34600 (was 35700)" IDLE " free=34600 step=0ms",
                                     lines[lines.size() - 4].text.c_str(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: held by" AT_REST_HELD_BY " skipped=1",
                                     lines[lines.size() - 3].text.c_str(), tape.c_str());
}

/**
 * The ration is dated by millis(), which wraps after forty-nine days. Three episodes in a row
 * just before the wraparound, a fourth just after it - which goes without, two seconds being less
 * than ten on either side of the wrap - and a fifth ten seconds after the first, which does not.
 *
 * Mutation: the dates are compared as they stand instead of being subtracted as unsigned - just
 * past the wrap the fourth episode looks four billion milliseconds overdue.
 */
void test_the_ration_keeps_its_dates_across_the_clock_wraparound(void)
{
    Bench bench(0x100000000ULL - 2000);
    bench.arduino.minFreeHeapValue = THE_FLOOR;

    bench.crossTheLevelAt(503);
    bench.crossTheLevelAt(1003);
    bench.crossTheLevelAt(1503);
    bench.crossTheLevelAt(2503);   // the clock wrapped at 2000
    bench.runUntilMs(3000);

    std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, (uint32_t)bench.episodeLines().size(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, bench.arduino.ledgerReads, tape.c_str());

    bench.crossTheLevelAt(10503);
    bench.runUntilMs(11000);

    tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, bench.arduino.ledgerReads, tape.c_str());
    const std::vector<TapeLogger::Line> heldBy = bench.logger.linesSaying("HEAP_LOW: held by");
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: held by" AT_REST_HELD_BY " skipped=1", heldBy.back().text.c_str());
}

/**
 * Once the row is spent, the next account is earned on the grid of the row - ten seconds after
 * its first episode, twenty after it, and so on - and not ten seconds after whichever episode
 * last took one. A row of three; an episode fourteen seconds after the first takes the account
 * earned at ten; an episode at twenty-one seconds is past the twenty-second mark of the grid and
 * takes the next, though only seven seconds have passed since the last grant.
 *
 * Mutation: the ration is dated by the grant - the episode at twenty-one seconds finds nothing
 * earned, ten seconds not having passed since the fourteenth, and goes without.
 */
void test_an_account_earned_after_the_row_is_dated_by_the_grid_of_the_row_and_not_by_the_grant(void)
{
    Bench bench;
    bench.playTheSag();

    bench.crossTheLevelAt(300003);
    bench.crossTheLevelAt(300503);
    bench.crossTheLevelAt(301003);
    bench.crossTheLevelAt(314003);   // fourteen seconds after the first of the row: earned
    bench.runUntilMs(320000);
    const size_t before = bench.traceLines().size();
    const uint32_t walksBefore = bench.arduino.ledgerReads;

    bench.crossTheLevelAt(321003);   // twenty-one seconds after the first, seven after the grant
    bench.runUntilMs(322000);

    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 4, (uint32_t)lines.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: episode low 38500 (watermark 10516)" IDLE " free=38500 step=0ms",
                                     lines[before].text.c_str(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: held by" AT_REST_HELD_BY, lines[before + 1].text.c_str(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: released after 100 ms (free 41500)", lines[before + 2].text.c_str(),
                                     tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: after the release held by" AT_REST_HELD_BY,
                                     lines[before + 3].text.c_str(), tape.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(walksBefore + 2, bench.arduino.ledgerReads, tape.c_str());
}

/**
 * A level taken while the ration is spent. Six crossings in three seconds have used up the row
 * and earned nothing back; then the module moves to 35 000 and stays there. The low is said and,
 * the ration being spent, not accounted for; a minute on the watch runs out, and "still held" is
 * followed by who holds the level - ration or no ration. That account is the one the watch
 * exists for, and a watch runs out once a minute at most: hovering cannot spend it.
 *
 * Mutation: the account after "still held" is taken only for an episode that was walked for at
 * its low - a level taken while the ration is spent is never accounted for at all.
 */
void test_a_level_taken_while_the_ration_is_spent_is_accounted_for_when_its_watch_runs_out(void)
{
    Bench bench;
    bench.playTheSag();
    for (int i = 0; i < 6; i++)
    {
        bench.crossTheLevelAt(300003 + (uint64_t)i * 500);
    }
    bench.runUntilMs(303503);
    const size_t before = bench.traceLines().size();
    const uint32_t walksBefore = bench.arduino.ledgerReads;

    bench.holdDownTo(35000);
    bench.runUntilMs(303503 + 130000);

    const std::vector<TapeLogger::Line> lines = bench.traceLines();
    const std::string tape = "the trace wrote:" + bench.logger.quoted();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)before + 3, (uint32_t)lines.size(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: episode low 35000 (watermark 10516)" IDLE " free=35000 step=0ms",
                                     lines[before].text.c_str(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: still held after 60000 ms (free 35000)",
                                     lines[before + 1].text.c_str(), tape.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("HEAP_LOW: held by" AT_REST_HELD_BY, lines[before + 2].text.c_str(), tape.c_str());
    // Taken on the step that found the watch run out, right behind its line: the two platform
    // calls are what the three milliseconds between the lines are.
    TEST_ASSERT_EQUAL_UINT64(lines[before + 1].atMs + (WALK_COST_US + NAMES_COST_US) / 1000, lines[before + 2].atMs);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(walksBefore + 1, bench.arduino.ledgerReads, tape.c_str());
}

// --------------------------------------------------------------------------------------------
// What the heap-forensics build costs
// --------------------------------------------------------------------------------------------

/**
 * A module that is not at a low pays one look at the free heap per step and nothing else: six
 * seconds with the free heap moving above the ordinary level, no line and not one walk.
 *
 * Mutation: the holders are read on every step.
 */
void test_a_settled_module_pays_one_look_at_the_free_heap_per_step_and_no_walk(void)
{
    Bench bench;

    bench.runUntilMs(2003);
    bench.holdDownTo(HeapWatermarkTracer::REPORT_BELOW_BYTES + 500);
    bench.runUntilMs(4003);
    bench.giveBack();
    bench.runUntilMs(6000);

    TEST_ASSERT_EQUAL_UINT32(600, bench.steps);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(bench.steps, bench.arduino.freeHeapReads,
                                     "the free heap is asked for once per step, no more and no less");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, bench.arduino.ledgerReads, "a module that was not at a low was walked");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)bench.traceLines().size());
}

/**
 * The walk locks the heap, and a trace nobody hears is not worth that: with the diagnostic
 * channel off the lows and the release are found as ever and the platform is not asked once.
 *
 * Mutation: the channel is checked only where the line is written.
 */
void test_a_trace_nobody_hears_does_not_walk_the_heap(void)
{
    Bench bench;
    bench.channel.enabled = false;
    bench.playTheSag();

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)bench.traceLines().size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, bench.arduino.ledgerReads,
                                     "the heap was walked for a line that could not be written");
}

/** @brief A platform that keeps no ledger - what IArduinoAdapter answers unless told otherwise. */
class NoLedgerArduino : public SimArduino
{
public:
    explicit NoLedgerArduino(VirtualClock& clock) : SimArduino(clock) {}
};

/**
 * On a platform without the account the trace is what it was, plus the episodes: the line about
 * the low goes out alone, and nothing is written in place of the account it could not get.
 */
void test_a_platform_without_a_ledger_says_the_low_alone(void)
{
    VirtualClock clock;
    NoLedgerArduino arduino(clock);
    TapeLogger logger(clock);
    DiagnosticChannel channel;
    LoopServiceConfigureDone loopService;
    arduino.freeHeapValue = STEADY_FREE_HEAP;
    arduino.minFreeHeapValue = THE_FLOOR;
    loopService.setLogging(&logger, &channel);
    loopService.setArduinoAdapter(&arduino);

    for (int step = 0; step < 200; step++)
    {
        if (step == 100)
        {
            arduino.freeHeapValue = 25012;
        }
        if (step == 150)
        {
            arduino.freeHeapValue = STEADY_FREE_HEAP;
        }
        loopService.loop();
        clock.advanceMs(CONTROL_PERIOD_MS);
    }

    const std::vector<TapeLogger::Line> lines = logger.linesSaying("HEAP_LOW");
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)lines.size());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: episode low 25012 (watermark 10516) free=25012 step=0ms",
                             lines[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 500 ms (free 62244)", lines[1].text.c_str());
}

/**
 * Everything between a low and its line happens with the heap at its floor, so none of it may ask
 * for memory: the two readings, the ledger, cutting it down, and formatting both lines. The whole
 * sag and a later episode, through the real loop service, against a log that keeps its line in a
 * fixed buffer - the platform's own part, the two calls in ArduinoAdapter, is the one piece this
 * cannot reach, and those calls fill arrays the adapter owns.
 *
 * Mutation: the account is kept in a std::vector, or the line is built in a std::string.
 */
void test_a_low_with_its_holders_is_found_and_said_without_asking_for_memory(void)
{
    VirtualClock clock;
    LedgerArduino arduino(clock);
    FixedLogger logger;
    DiagnosticChannel channel;
    BackgroundActBoard board(&arduino);
    LoopServiceConfigureDone loopService;
    arduino.freeHeapValue = STEADY_FREE_HEAP;
    arduino.minFreeHeapValue = STEADY_WATERMARK;
    arduino.addTask(WIFI_TASK, "wifi");
    arduino.addOwner(0, 61200, 410);
    arduino.addOwner(WIFI_TASK, 9040, 61);
    arduino.addOwner(STARTUP_TASK, 24816, 97);
    arduino.ledgerReadAtMs.reserve(64);
    loopService.setLogging(&logger, &channel);
    loopService.setArduinoAdapter(&arduino);
    loopService.setBackgroundActBoard(&board);

    // Out of the way of the measurement: the first reading is a baseline.
    loopService.loop();
    clock.advanceMs(CONTROL_PERIOD_MS);

    HostAllocationCounter::reset();

    for (int step = 0; step < 400; step++)
    {
        if (step >= 100 && step < 100 + (int)SAG_READINGS * 10 && step % 10 == 0)
        {
            const SagReading& reading = THE_SAG[(step - 100) / 10];
            arduino.freeHeapValue = reading.free;
            arduino.minFreeHeapValue = reading.watermark;
        }
        if (step == 200 || step == 350)
        {
            arduino.freeHeapValue = STEADY_FREE_HEAP;
        }
        if (step == 300)
        {
            arduino.freeHeapValue = 25012;
        }
        loopService.loop();
        clock.advanceMs(CONTROL_PERIOD_MS);
    }

    const unsigned long long allocations = HostAllocationCounter::newCount();

    // The scene did what it is here for: seven lows and an episode, their closing lines, and the
    // accounts - four in the sag, one for the episode, one at rest after each release.
    TEST_ASSERT_EQUAL_UINT32(7 + 1 + 2 + 7, logger.lineCount);
    TEST_ASSERT_EQUAL_UINT32(4 + 1 + 2, logger.heldByCount);
    TEST_ASSERT_EQUAL_UINT32(7, arduino.ledgerReads);

    char message[96];
    snprintf(message, sizeof(message), "finding and saying the lows made %llu allocations", allocations);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0ULL, allocations, message);
}

} // namespace

extern "C" void run_heap_forensics_tests(void)
{
    RUN_TEST(test_the_largest_holders_are_named_largest_first_and_the_rest_are_summed);
    RUN_TEST(test_memory_from_before_the_scheduler_and_from_deleted_tasks_is_named_for_what_it_is);
    RUN_TEST(test_a_name_is_one_token_of_the_line);
    RUN_TEST(test_owners_are_named_by_handle_when_the_tasks_could_not_be_listed);
    RUN_TEST(test_equal_holders_keep_the_order_they_were_reported_in);
    RUN_TEST(test_a_ledger_that_came_back_full_says_it_may_be_short);
    RUN_TEST(test_a_count_past_the_capacity_is_read_up_to_the_capacity_and_not_past_the_array);
    RUN_TEST(test_the_widest_line_fits_the_buffer_and_a_small_buffer_is_not_overrun);
    RUN_TEST(test_a_frame_is_counted_by_the_group_bit_of_its_destination);
    RUN_TEST(test_the_account_of_a_low_says_what_the_radio_had_handed_over_by_then_by_class);
    RUN_TEST(test_a_low_that_is_still_held_is_followed_by_who_holds_it);
    RUN_TEST(test_the_account_of_the_holders_is_of_the_reading_and_not_of_the_print);
    RUN_TEST(test_a_low_already_given_back_is_not_walked_for);
    RUN_TEST(test_the_sag_of_the_logged_run_is_walked_four_times_and_not_seven);
    RUN_TEST(test_a_low_is_accounted_for_again_at_three_quarters_of_the_last_and_not_above);
    RUN_TEST(test_a_dip_shallower_than_the_floor_is_said_as_an_episode_low);
    RUN_TEST(test_inside_an_episode_a_low_within_the_hysteresis_is_not_said_again);
    RUN_TEST(test_the_free_heap_at_the_ordinary_level_is_not_an_episode_low);
    RUN_TEST(test_a_low_that_deepens_between_the_two_looks_of_a_reading_keeps_its_line);
    RUN_TEST(test_a_new_watermark_after_an_episode_low_is_said_as_a_new_watermark);
    RUN_TEST(test_an_episode_low_says_how_long_its_step_was);
    RUN_TEST(test_memory_given_back_and_taken_again_inside_one_step_is_a_new_episode);
    RUN_TEST(test_an_episode_is_forgotten_when_its_watch_closes_and_not_before);
    RUN_TEST(test_a_reading_that_finds_the_heap_back_does_not_end_an_episode_a_watch_still_holds);
    RUN_TEST(test_a_level_held_past_the_limit_is_said_once_and_the_dip_after_its_return_is_said);
    RUN_TEST(test_an_episode_low_names_who_was_at_work);
    RUN_TEST(test_a_new_watermark_is_one_line_and_not_an_episode_low_as_well);
    RUN_TEST(test_a_module_hovering_at_the_level_is_walked_for_three_episodes_and_then_one_in_ten_seconds);
    RUN_TEST(test_a_sag_inside_an_episode_that_went_without_its_account_is_walked_for);
    RUN_TEST(test_the_ten_seconds_run_from_the_first_episode_of_the_row);
    RUN_TEST(test_episodes_opened_by_a_new_watermark_are_rationed_by_the_same_dates);
    RUN_TEST(test_the_ration_keeps_its_dates_across_the_clock_wraparound);
    RUN_TEST(test_an_account_earned_after_the_row_is_dated_by_the_grid_of_the_row_and_not_by_the_grant);
    RUN_TEST(test_a_level_taken_while_the_ration_is_spent_is_accounted_for_when_its_watch_runs_out);
    RUN_TEST(test_a_settled_module_pays_one_look_at_the_free_heap_per_step_and_no_walk);
    RUN_TEST(test_a_trace_nobody_hears_does_not_walk_the_heap);
    RUN_TEST(test_a_platform_without_a_ledger_says_the_low_alone);
    RUN_TEST(test_a_low_with_its_holders_is_found_and_said_without_asking_for_memory);
}

#endif // GD_HEAP_FORENSICS
