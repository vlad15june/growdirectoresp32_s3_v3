// Tests for the host simulation harness itself.
//
// These exist because of what they let the S1 test claim. S1 asserts that the control loop
// stalls, and that verdict is only worth something if the instrument works: if the clock moves
// when asked, if a blocked task really does let other tasks run, if the scheduler produces the
// same run twice. All of that is proven here, on the harness alone, with no production code
// involved. When S1 then goes red, the harness is not a suspect.

#include <unity.h>

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Sim/HostFiber.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimNetwork.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimNetwork;
using hostsim::VirtualClock;

// --- VirtualClock -------------------------------------------------------------------------

static void test_virtual_clock_moves_only_when_asked(void)
{
    VirtualClock clock;

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)clock.nowMs());

    clock.advanceMs(1500);
    TEST_ASSERT_EQUAL_UINT32(1500, (uint32_t)clock.nowMs());
    TEST_ASSERT_EQUAL_UINT32(1500000, (uint32_t)clock.nowUs());

    clock.advanceUs(500);
    TEST_ASSERT_EQUAL_UINT32(1500, (uint32_t)clock.nowMs());
    TEST_ASSERT_EQUAL_UINT32(1500500, (uint32_t)clock.nowUs());

    // Time never runs backwards, whatever a caller asks for.
    clock.advanceToUs(1000);
    TEST_ASSERT_EQUAL_UINT32(1500500, (uint32_t)clock.nowUs());
}

// --- SimArduino ---------------------------------------------------------------------------

static void test_sim_arduino_reads_the_virtual_clock(void)
{
    VirtualClock clock;
    SimArduino arduino(clock);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)arduino.millis());

    clock.advanceMs(250);
    TEST_ASSERT_EQUAL_UINT32(250, (uint32_t)arduino.millis());
    TEST_ASSERT_EQUAL_UINT32(250000, (uint32_t)arduino.micros());

    // Without a scheduler, delay() is pure time: no real sleeping either way.
    arduino.delay(30000);
    TEST_ASSERT_EQUAL_UINT32(30250, (uint32_t)arduino.millis());
    TEST_ASSERT_EQUAL_INT(1, arduino.delayCount);
}

// --- Scheduler ----------------------------------------------------------------------------

static std::string runTwoTasksAndRecordOrder(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    std::string order;

    scheduler.createTask("a", 64 * 1024, 1, [&]() {
        for (int i = 0; i < 3; ++i)
        {
            order += "a";
            scheduler.delayMs(10);
        }
    });

    scheduler.createTask("b", 64 * 1024, 1, [&]() {
        for (int i = 0; i < 3; ++i)
        {
            order += "b";
            scheduler.delayMs(10);
        }
    });

    scheduler.runForMs(100);
    return order;
}

static void test_scheduler_interleaves_tasks_and_repeats_itself_exactly(void)
{
    const std::string first = runTwoTasksAndRecordOrder();
    const std::string second = runTwoTasksAndRecordOrder();

    // Both tasks ran, alternating - this is a scheduler, not a queue of callbacks.
    TEST_ASSERT_EQUAL_STRING("ababab", first.c_str());

    // And the run is a function of the test alone. This is the property std::thread would
    // not give us, and the reason the harness does not use threads.
    TEST_ASSERT_EQUAL_STRING(first.c_str(), second.c_str());
}

static void test_scheduler_jumps_over_idle_time(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    uint64_t wokeAtMs = 0;

    scheduler.createTask("sleeper", 64 * 1024, 1, [&]() {
        scheduler.delayMs(30000);
        wokeAtMs = clock.nowMs();
    });

    scheduler.runForMs(60000);

    // Thirty seconds of virtual time, none of it spent waiting: the whole test file runs in
    // well under a second of real time.
    TEST_ASSERT_EQUAL_UINT32(30000, (uint32_t)wokeAtMs);
}

static void test_scheduler_runs_the_higher_priority_task_first(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    std::string order;

    scheduler.createTask("low", 64 * 1024, 1, [&]() {
        order += "l";
        scheduler.delayMs(10);
    });

    scheduler.createTask("high", 64 * 1024, 5, [&]() {
        order += "h";
        scheduler.delayMs(10);
    });

    scheduler.runForMs(50);

    TEST_ASSERT_EQUAL_STRING("hl", order.c_str());
}

// --- Task stacks are sized by the host's frames, not by the device's ------------------------
//
// The firmware creates its tasks with a stack measured on the board: Xtensa frames at -Os, where
// the largest frame in the whole image is 240 bytes. The same functions on a host fiber are
// x86-64 frames at -O0, where every temporary gets a slot of its own. Handing such a fiber the
// device's number does not fail as a refusal - it writes past the stack into the heap behind it,
// and the run dies later, somewhere unrelated, with a corrupted allocator.
//
// So the measurement below is taken here rather than quoted: the task asks for the stack the
// firmware asks for, calls what the catalog writer task calls, and reports how deep it went.

/** @brief What one call of the production serializer cost the fiber it ran on. */
struct StackProbe
{
    size_t fiberBytes = 0;
    size_t deepestBytes = 0;
    size_t documentChars = 0;
    bool ran = false;
};

/**
 * @brief Fill the unused part of the running fiber's stack with a pattern; answer where the fill
 *        stopped, which is the level the next call starts from.
 *
 * The margin is what keeps this function from painting the frame it is painting from: the fill
 * runs from the bottom of the stack up to a point safely below this frame's own locals.
 */
static const char* paintTheStackBelow(char* low)
{
    const size_t marginBytes = 1024;
    char here = 0;
    char* fillTo = &here - marginBytes;
    for (char* p = low; p < fillTo; ++p)
    {
        *p = (char)0xA5;
    }
    return fillTo;
}

/** The lowest painted byte the call since disturbed: the deepest address it reached. */
static const char* deepestTouchedByte(const char* low, const char* fillTo)
{
    for (const char* p = low; p < fillTo; ++p)
    {
        if (*p != (char)0xA5)
        {
            return p;
        }
    }
    return fillTo;
}

static void test_a_task_gets_the_stack_its_host_frames_need(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    StackProbe probe;

    // The stack the firmware creates its own tasks with - the catalog writer's among them.
    const uint32_t deviceStackBytes = 8192;

    scheduler.createTask("catalog-io-probe", deviceStackBytes, 1, [&]() {
        const char* low = nullptr;
        const char* high = nullptr;
        const void* entryFrame = nullptr;
        if (!hostsim::Fiber::currentStack(&low, &high, &entryFrame))
        {
            return;
        }
        probe.fiberBytes = (size_t)(high - low);
        probe.ran = true;
        if (probe.fiberBytes < HostTaskScheduler::kMinimumFiberStackBytes)
        {
            // The measurement below would go deeper than this fiber can hold. Reporting the size
            // is what the test needs; making the call anyway would corrupt the heap behind the
            // stack and take the whole run down somewhere unrelated.
            return;
        }

        const char* const fillTo = paintTheStackBelow(const_cast<char*>(low));

        ArduinoJsonConverter converter;
        SensorType type;
        type.uid = "type-uid-0007";
        type.name = "RK330-01";
        type.displayName = "RK330-01 air";
        type.protocol = SensorProtocol::MODBUS_PROTOCOL;
        type.baudRate = 9600;
        type.modbusDeviceAddress = 1;
        for (int channel = 1; channel <= 4; channel++)
        {
            ModbusRegisterConfig config;
            config.channelName = "channel";
            config.channel = channel;
            config.startRegister = channel;
            type.modbusRegisterConfig.push_back(config);
        }

        const std::string document = converter.sensorTypeToJson(type);
        probe.documentChars = document.size();
        probe.deepestBytes = (size_t)(fillTo - deepestTouchedByte(low, fillTo));
    });

    scheduler.runForMs(10);

    TEST_ASSERT_TRUE_MESSAGE(probe.ran, "the probe task never ran");

    char message[256];

    snprintf(message, sizeof(message),
             "the task asked for %lu bytes and the bench gave it %lu: a device-sized stack is "
             "being used for host-sized frames",
             (unsigned long)deviceStackBytes, (unsigned long)probe.fiberBytes);
    TEST_ASSERT_TRUE_MESSAGE(probe.fiberBytes >= HostTaskScheduler::kMinimumFiberStackBytes,
                             message);
    TEST_ASSERT_TRUE_MESSAGE(probe.documentChars > 0, "the serializer produced nothing to measure");

    // The measurement, taken on this compiler at these flags: one document does not fit the
    // stack the device gives the task that writes it. This is what the floor exists for, and if
    // a future compiler ever makes it fit, this assertion says so instead of the floor quietly
    // becoming decoration.
    snprintf(message, sizeof(message),
             "one call of the production serializer reached %lu bytes deep, against the %lu bytes "
             "the firmware creates the task with - if this now fits, the bench no longer needs a "
             "stack floor",
             (unsigned long)probe.deepestBytes, (unsigned long)deviceStackBytes);
    TEST_ASSERT_TRUE_MESSAGE(probe.deepestBytes > deviceStackBytes, message);

    snprintf(message, sizeof(message),
             "the call reached %lu bytes into a %lu byte fiber stack: the floor has no room left "
             "for those frames to nest",
             (unsigned long)probe.deepestBytes, (unsigned long)probe.fiberBytes);
    TEST_ASSERT_TRUE_MESSAGE(probe.deepestBytes * 4 < probe.fiberBytes, message);
}

/** A stack larger than the floor is left alone: the firmware's number wins when it is bigger. */
static void test_a_task_that_asks_for_more_than_the_floor_keeps_it(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    size_t fiberBytes = 0;

    const uint32_t asked = HostTaskScheduler::kMinimumFiberStackBytes * 2;
    scheduler.createTask("greedy", asked, 1, [&]() {
        const char* low = nullptr;
        const char* high = nullptr;
        const void* entryFrame = nullptr;
        if (hostsim::Fiber::currentStack(&low, &high, &entryFrame))
        {
            fiberBytes = (size_t)(high - low);
        }
    });

    scheduler.runForMs(10);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)asked, (uint32_t)fiberBytes,
                                     "the floor was applied as a ceiling as well");
}

static void test_scheduler_reports_a_task_that_never_lets_time_move(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    scheduler.setStepBudget(1000);

    scheduler.createTask("spinner", 64 * 1024, 1, [&]() {
        while (true)
        {
            scheduler.yieldTask();
        }
    });

    scheduler.runForMs(1000);

    // A busy task must surface as a failed assertion, never as a test run that hangs.
    TEST_ASSERT_TRUE_MESSAGE(scheduler.stepBudgetExhausted(),
                             "scheduler should report an exhausted step budget, not spin forever");
}

// --- Message queue ------------------------------------------------------------------------

static void test_message_queue_keeps_order_and_reports_size(void)
{
    HostMessageQueue<int> queue(4);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)queue.size());
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)queue.capacity());

    TEST_ASSERT_TRUE(queue.push(1));
    TEST_ASSERT_TRUE(queue.push(2));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)queue.size());

    int value = 0;
    TEST_ASSERT_TRUE(queue.pop(value));
    TEST_ASSERT_EQUAL_INT(1, value);
    TEST_ASSERT_TRUE(queue.pop(value));
    TEST_ASSERT_EQUAL_INT(2, value);
    TEST_ASSERT_FALSE(queue.pop(value));
}

static void test_message_queue_counts_what_it_refuses(void)
{
    HostMessageQueue<int> queue(2);

    TEST_ASSERT_TRUE(queue.push(1));
    TEST_ASSERT_TRUE(queue.push(2));

    // Full: the producer is told so and is not blocked, and the loss is counted rather than
    // being silent.
    TEST_ASSERT_FALSE(queue.push(3));
    TEST_ASSERT_FALSE(queue.push(4));
    TEST_ASSERT_EQUAL_UINT32(2, queue.overflowCount());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)queue.size());
}

// --- SimNetwork ---------------------------------------------------------------------------

static void test_slow_send_blocks_only_the_task_that_made_it(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    SimNetwork network(clock, scheduler);
    network.setResponseLatencyMs(30000);

    int tickerRuns = 0;
    uint64_t worstTickerGapMs = 0;
    uint64_t previousTickMs = 0;
    bool sendReturnedOk = false;
    uint64_t sendReturnedAtMs = 0;

    scheduler.createTask("ticker", 128 * 1024, 1, [&]() {
        while (true)
        {
            const uint64_t nowMs = clock.nowMs();
            if (tickerRuns > 0 && (nowMs - previousTickMs) > worstTickerGapMs)
            {
                worstTickerGapMs = nowMs - previousTickMs;
            }
            previousTickMs = nowMs;
            tickerRuns++;
            scheduler.delayMs(10);
        }
    });

    scheduler.createTask("sender", 128 * 1024, 1, [&]() {
        SimNetwork::Response response = network.send("slow-endpoint", "payload");
        sendReturnedOk = response.ok;
        sendReturnedAtMs = clock.nowMs();
    });

    scheduler.runForMs(60000);

    // The sender waited the full thirty seconds...
    TEST_ASSERT_TRUE(sendReturnedOk);
    TEST_ASSERT_EQUAL_UINT32(30000, (uint32_t)sendReturnedAtMs);

    // ...and the other task never noticed. This is the property S1 asks the control loop for,
    // demonstrated here on the harness alone: a blocking send does not have to stop everything,
    // it only stops whoever called it.
    TEST_ASSERT_EQUAL_UINT32(10, (uint32_t)worstTickerGapMs);
    TEST_ASSERT_TRUE(tickerRuns > 5000);
}

extern "C" void run_host_harness_tests(void)
{
    printf("\n----- Running Host Harness Tests -----\n");

    RUN_TEST(test_virtual_clock_moves_only_when_asked);
    RUN_TEST(test_sim_arduino_reads_the_virtual_clock);
    RUN_TEST(test_scheduler_interleaves_tasks_and_repeats_itself_exactly);
    RUN_TEST(test_scheduler_jumps_over_idle_time);
    RUN_TEST(test_scheduler_runs_the_higher_priority_task_first);
    RUN_TEST(test_a_task_gets_the_stack_its_host_frames_need);
    RUN_TEST(test_a_task_that_asks_for_more_than_the_floor_keeps_it);
    RUN_TEST(test_scheduler_reports_a_task_that_never_lets_time_move);
    RUN_TEST(test_message_queue_keeps_order_and_reports_size);
    RUN_TEST(test_message_queue_counts_what_it_refuses);
    RUN_TEST(test_slow_send_blocks_only_the_task_that_made_it);

    printf("\n----- Host Harness Tests Complete -----\n");
}
