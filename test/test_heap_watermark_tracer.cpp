// The moment the heap floor broke, said on the step it broke on.
//
// A run on a device reported a worst point of 5 628 free bytes since boot while every thirty-second
// diagnostic snapshot of the same run showed sixty-odd thousand free: the dip opened and closed
// between two snapshots, so the log said that the module had been that low and nothing about when
// or during what. The number the snapshot carries - minFreeHeap, which is ESP.getMinFreeHeap() on
// the device and "worst point since board boot" on the monitor panel - answers how deep and never
// when.
//
// These tests are of HeapWatermarkTracer and of the one caller that drives it,
// LoopServiceBase::loop() - the step main.cpp repeats and nothing else. What is replaced is the
// board: SimArduino answers the watermark the test scripts, and the virtual clock says when.
//
// ### Reproducing the red
//
// The headline test below (a dip between two snapshots) fails without the tracer:
// the tape carries no HEAP_LOW line at all, only the snapshot fifteen seconds later. The unit
// tests each name the mutation they catch.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/DiagnosticService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Core/Domain/Diagnostics/HeapWatermarkTracer.h"

#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostAllocationCounter.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::SimArduino;
using hostsim::VirtualClock;

namespace {

// The figures of the logged run, kept as they were measured: the module sits a little over sixty thousand
// free, the watermark it had before the dip is the worst snapshot of that run, and the dip itself
// is the worst point the panel reported.
const uint32_t STEADY_FREE_HEAP = 61000;
const uint32_t STEADY_WATERMARK = 54664;
const uint32_t DIP_FREE_HEAP = 5628;

// How fast the control loop is driven here, and when the dip happens: a third of a second, in the
// middle of the fifteen seconds between two diagnostic snapshots.
const uint32_t CONTROL_PERIOD_MS = 10;
const uint64_t DIP_AT_MS = 15000;
const uint64_t DIP_LENGTH_MS = 300;
const uint64_t DIAGNOSTIC_INTERVAL_MS = 30000;
const uint64_t OBSERVATION_WINDOW_MS = 65000;

// --- the module, wired as the device wires it -----------------------------------------------

/** @brief Every log line with the moment it was written, which is what "when" means here. */
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

/**
 * @brief The debug channels as the module ships them.
 *
 * DebugSystem::initializeFeatures() registers DIAGNOSTIC enabled and on Serial only, and
 * LOOP_SERVICE disabled; MockDebugSystem answers one flag for every feature, which would put a
 * line per loop iteration on the tape and tell nothing about the channel the trace actually uses.
 */
class ShippedDebugSystem : public MockDebugSystem
{
public:
    bool diagnosticEnabled = true;

    bool isFeatureEnabled(const char* featureName) override
    {
        return featureName != nullptr && diagnosticEnabled
               && std::string(featureName) == std::string(DebugFeatures::DIAGNOSTIC_DEBUG);
    }
};

/** @brief The diagnostic snapshots that reached the server, and when each of them did. */
class RecordingRestApiService : public MockRestAPIService
{
public:
    struct Snapshot
    {
        uint64_t atMs;
        uint32_t freeHeap;
        uint32_t minFreeHeap;
    };

    explicit RecordingRestApiService(VirtualClock& clock) : _clock(clock) {}

    bool sendDiagnosticData(const DiagnosticDto& diagnosticData) override
    {
        Snapshot snapshot;
        snapshot.atMs = _clock.nowMs();
        snapshot.freeHeap = diagnosticData.freeHeap;
        snapshot.minFreeHeap = diagnosticData.minFreeHeap;
        snapshots.push_back(snapshot);
        return MockRestAPIService::sendDiagnosticData(diagnosticData);
    }

    std::vector<Snapshot> snapshots;

private:
    VirtualClock& _clock;
};

/**
 * @brief The CONFIGURED_DONE loop with the real diagnostic service behind it.
 *
 * The services the loop drives are left out: what is under test is the step itself - the part of
 * LoopServiceBase::loop() that reads the watermark - and the diagnostic pass that is the only
 * other thing on the device that reports the same number.
 */
struct ModuleUnderTest
{
    VirtualClock clock;
    SimArduino arduino;
    TapeLogger logger;
    ShippedDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockWifiAdapter wifiAdapter;
    MockMqttManager mqttManager;
    RecordingRestApiService restApiService;
    DiagnosticService diagnosticService;
    LoopServiceConfigureDone loopService;

    ModuleUnderTest()
        : arduino(clock),
          logger(clock),
          moduleConfig(&logger, &debugSystem, &settings),
          restApiService(clock),
          diagnosticService(&restApiService,
                            &moduleConfig,
                            logger,
                            debugSystem,
                            nullptr,   // json converter - not on this path
                            &wifiAdapter,
                            nullptr,   // NTP adapter - not on this path
                            &arduino,
                            &mqttManager,
                            nullptr)   // hybrid API - the REST service is the one that records
    {
        settings.setUser("bench@example.com");
        settings.setAccountUid("bench-account");
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleUid("bench-module");
        moduleConfig.setFirmwareVersion("test");

        wifiAdapter.setConnected(true);
        mqttManager.isConnectedReturnValue = true;

        arduino.freeHeapValue = STEADY_FREE_HEAP;
        arduino.minFreeHeapValue = STEADY_WATERMARK;

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setMqttManager(&mqttManager);
        loopService.setDiagnosticService(&diagnosticService);
    }

    /**
     * @brief Run the control loop for a window of virtual time, one step per control period.
     *
     * Mirrors main.cpp: loop() calls loopService->loop() and nothing else.
     */
    void runForMs(uint64_t windowMs)
    {
        for (uint64_t elapsed = 0; elapsed < windowMs; elapsed += CONTROL_PERIOD_MS)
        {
            scriptTheHeap(clock.nowMs());
            loopService.loop();
            clock.advanceMs(CONTROL_PERIOD_MS);
        }
    }

    /**
     * @brief The transient the logged run saw, as the platform would report it.
     *
     * Free heap collapses for a third of a second and comes back; the watermark keeps the low for
     * good, because heap_caps_get_minimum_free_size() is a minimum since boot and nothing resets
     * it. That asymmetry is the whole problem: the snapshots sample free heap and miss the dip,
     * and the watermark that remembers it carries no time.
     */
    void scriptTheHeap(uint64_t nowMs)
    {
        if (!dipScripted)
        {
            return;
        }

        if (nowMs >= DIP_AT_MS && nowMs < DIP_AT_MS + DIP_LENGTH_MS)
        {
            arduino.freeHeapValue = DIP_FREE_HEAP;
            arduino.minFreeHeapValue = DIP_FREE_HEAP;
        }
        else if (nowMs >= DIP_AT_MS + DIP_LENGTH_MS)
        {
            arduino.freeHeapValue = STEADY_FREE_HEAP;
        }
    }

    bool dipScripted = true;
};

// --------------------------------------------------------------------------------------------
// The tracer on its own
// --------------------------------------------------------------------------------------------

/**
 * Levels the module passes through in ordinary work are not news. Without the threshold the
 * channel would carry a line for every step of the boot sequence, when the heap is at its busiest
 * and its lows mean nothing.
 *
 * Mutation: drop the REPORT_BELOW_BYTES gate from sample().
 */
void test_a_watermark_above_the_threshold_says_nothing(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;

    tracer.sample(240000);
    tracer.sample(150000);
    tracer.sample(90000);
    tracer.sample(HeapWatermarkTracer::REPORT_BELOW_BYTES);

    TEST_ASSERT_FALSE_MESSAGE(tracer.takeReport(report),
                              "a watermark at or above the threshold was reported");
}

/**
 * The first reading is what the tracer inherits, not a drop it witnessed. Reporting it would date
 * a dip that happened before the loop existed to the moment the loop started.
 *
 * Mutation: report the first reading when it is below the threshold.
 */
void test_the_first_reading_is_a_baseline_and_not_a_report(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;

    tracer.sample(9000);

    TEST_ASSERT_FALSE_MESSAGE(tracer.takeReport(report),
                              "the first reading was reported as a new low");

    tracer.sample(8000);

    TEST_ASSERT_TRUE_MESSAGE(tracer.takeReport(report), "a drop from the baseline was not reported");
    TEST_ASSERT_EQUAL_UINT32(8000, report.watermarkBytes);
    TEST_ASSERT_EQUAL_UINT32(9000, report.previousBytes);
}

/**
 * What the line has to carry: the new floor and the level it replaced. The second number is the
 * depth of the dip - a drop to 5 628 from 54 664 is a different event from a drop to 5 628 from
 * 6 000, and the log has to tell them apart.
 *
 * Mutation: report the last printed watermark instead of the previous reading.
 */
void test_a_drop_below_the_threshold_names_the_new_low_and_the_one_it_replaced(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;

    tracer.sample(STEADY_WATERMARK);
    tracer.sample(DIP_FREE_HEAP);

    TEST_ASSERT_TRUE_MESSAGE(tracer.takeReport(report), "the dip was not reported");
    TEST_ASSERT_EQUAL_UINT32(DIP_FREE_HEAP, report.watermarkBytes);
    TEST_ASSERT_EQUAL_UINT32(STEADY_WATERMARK, report.previousBytes);
}

/**
 * A report is a line to be printed once. Handing it out twice would put the same dip on the tape
 * on two different steps, and the second copy would be dated wrong.
 *
 * Mutation: leave the pending flag set in takeReport().
 */
void test_a_report_is_handed_out_once(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;

    tracer.sample(STEADY_WATERMARK);
    tracer.sample(DIP_FREE_HEAP);

    TEST_ASSERT_TRUE(tracer.takeReport(report));
    TEST_ASSERT_FALSE_MESSAGE(tracer.takeReport(report), "the same report was handed out twice");

    // A watermark that stays where it is has nothing to add, however often it is read.
    for (int step = 0; step < 100; step++)
    {
        tracer.sample(DIP_FREE_HEAP);
    }

    TEST_ASSERT_FALSE_MESSAGE(tracer.takeReport(report),
                              "a watermark that did not move produced a report");
}

/**
 * A heap that sags instead of collapsing must not turn the channel into a ribbon. From 41 000 down
 * to 30 000 in hundred-byte steps there are a hundred readings below the threshold; the hysteresis
 * turns them into one line per kilobyte of further loss, and the values below say which ones.
 *
 * Mutation: drop the REPORT_STEP_BYTES gate - the count becomes 100.
 */
void test_a_slow_sag_costs_one_line_per_step_of_hysteresis(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;

    std::vector<uint32_t> reported;
    int readingsBelowThreshold = 0;

    for (uint32_t watermark = 41000; watermark >= 30000; watermark -= 100)
    {
        tracer.sample(watermark);
        if (watermark < HeapWatermarkTracer::REPORT_BELOW_BYTES)
        {
            readingsBelowThreshold++;
        }
        if (tracer.takeReport(report))
        {
            reported.push_back(report.watermarkBytes);
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(100, readingsBelowThreshold,
                                  "the sag did not produce the readings this test is about");

    // First line as soon as the sag crosses the threshold, then one per further kilobyte:
    // 39 900 - 1 024 = 38 876, so the next reading low enough is 38 800, and so on.
    const uint32_t expected[] = {39900, 38800, 37700, 36600, 35500, 34400, 33300, 32200, 31100, 30000};
    const size_t expectedCount = sizeof(expected) / sizeof(expected[0]);

    char message[128];
    snprintf(message, sizeof(message), "a hundred readings below the threshold produced %d lines",
             (int)reported.size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)expectedCount, (uint32_t)reported.size(), message);

    for (size_t i = 0; i < expectedCount; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(expected[i], reported[i]);
    }
}

/**
 * The reading happens on the control loop, on every step, and at moments when the heap is at its
 * floor - the one thing it must never do is ask for memory.
 *
 * Mutation: hold the reports in a std::vector instead of one slot.
 */
void test_reading_the_watermark_allocates_nothing(void)
{
    HeapWatermarkTracer tracer;
    HeapWatermarkTracer::Report report;

    // Out of the way of the measurement: the first reading and the first report both take their
    // branches here, so what is measured below is the steady state of a loop that keeps sampling.
    tracer.sample(STEADY_WATERMARK);
    tracer.sample(DIP_FREE_HEAP);
    tracer.takeReport(report);

    HostAllocationCounter::reset();

    for (int step = 0; step < 1000; step++)
    {
        tracer.sample(DIP_FREE_HEAP - (uint32_t)step);
        tracer.takeReport(report);
    }

    char message[128];
    snprintf(message, sizeof(message), "a thousand steps of sampling made %llu allocations",
             HostAllocationCounter::newCount());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0ULL, HostAllocationCounter::newCount(), message);
}

// --------------------------------------------------------------------------------------------
// The tracer where it runs: one reading per control-loop step
// --------------------------------------------------------------------------------------------

/**
 * The claim of this file. A dip that opens and closes between two diagnostic snapshots gets a line
 * of its own, on the step it happened, with the depth of the fall in it - and the snapshot that
 * eventually carries the same watermark comes fifteen seconds later, showing a free heap that was
 * never anywhere near the floor.
 *
 * Without the tracer nothing writes HEAP_LOW, and the only trace of the dip in the whole run
 * is the minFreeHeap field of a snapshot at t = 30 s.
 */
void test_a_dip_between_two_snapshots_is_named_on_the_step_it_happened(void)
{
    ModuleUnderTest module;

    module.runForMs(OBSERVATION_WINDOW_MS);

    // --- the run has to be sound before its verdict means anything --------------------------
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.restApiService.snapshots.size(),
                                     "the diagnostic pass did not run twice in the window");

    const RecordingRestApiService::Snapshot& afterTheDip = module.restApiService.snapshots[0];
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(DIAGNOSTIC_INTERVAL_MS, afterTheDip.atMs,
                                     "the first snapshot did not land on the diagnostic interval");

    // What the thirty-second sampling saw: a module at its steady level, with one number at the
    // bottom saying it had been at 5 628 - and no way to say when.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(STEADY_FREE_HEAP, afterTheDip.freeHeap,
                                     "the snapshot caught the dip itself, so nothing was missed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(DIP_FREE_HEAP, afterTheDip.minFreeHeap,
                                     "the snapshot did not carry the watermark the dip left");

    // --- the claim ---------------------------------------------------------------------------
    std::vector<TapeLogger::Line> lines = module.logger.linesSaying("HEAP_LOW: new watermark");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)lines.size(),
                                     "the tape does not carry exactly one line about the dip");

    // The reading found the heap still at its floor - the dip outlasts the step by a third of a
    // second - and the step itself took no time.
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: new watermark 5628 (was 54664) free=5628 step=0ms",
                             lines[0].text.c_str());

    // One step later than the dip and not one more: the reading is taken at the end of the step
    // the dip happened in, and the line is written at the top of the next one - away from the
    // moment the heap was at its floor, and still fifteen seconds before the snapshot.
    char message[192];
    snprintf(message, sizeof(message),
             "the line landed at t=%lu ms; the dip was at t=%lu ms and the snapshot at t=%lu ms",
             (unsigned long)lines[0].atMs, (unsigned long)DIP_AT_MS,
             (unsigned long)afterTheDip.atMs);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(DIP_AT_MS + CONTROL_PERIOD_MS, lines[0].atMs, message);
    TEST_ASSERT_TRUE_MESSAGE(lines[0].atMs < afterTheDip.atMs, message);

    // And the other end of the dip: one line on the step that finds the memory back, with how
    // long it was held - which the snapshot, showing sixty-one thousand free, cannot say either.
    std::vector<TapeLogger::Line> released = module.logger.linesSaying("HEAP_LOW: released");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)released.size(),
                                     "the tape does not carry exactly one line about the release");
    TEST_ASSERT_EQUAL_STRING("HEAP_LOW: released after 300 ms (free 61000)",
                             released[0].text.c_str());
    TEST_ASSERT_EQUAL_UINT64(DIP_AT_MS + DIP_LENGTH_MS, released[0].atMs);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.logger.linesSaying("HEAP_LOW").size(),
                                     "a dip is two lines, the low and the release, and no more");
}

/**
 * The same run without the dip: a module whose watermark never moves says nothing at all. Without
 * this the test above would pass just as well against a line written on every step.
 */
void test_a_run_without_a_dip_says_nothing(void)
{
    ModuleUnderTest module;
    module.dipScripted = false;

    module.runForMs(OBSERVATION_WINDOW_MS);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)module.restApiService.snapshots.size(),
                                     "the diagnostic pass did not run twice in the window");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.logger.linesSaying("HEAP_LOW").size(),
                                     "a run with a steady watermark still wrote a HEAP_LOW line");
}

/**
 * The trace rides the diagnostic channel, so a module with that channel switched off stays as
 * quiet on this as it is on everything else it reports.
 */
void test_a_trace_nobody_asked_for_says_nothing(void)
{
    ModuleUnderTest module;
    module.debugSystem.diagnosticEnabled = false;

    module.runForMs(OBSERVATION_WINDOW_MS);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.logger.linesSaying("HEAP_LOW").size(),
                                     "the trace wrote lines with its debug channel off");
}

} // namespace

extern "C" void run_heap_watermark_tracer_tests(void)
{
    RUN_TEST(test_a_watermark_above_the_threshold_says_nothing);
    RUN_TEST(test_the_first_reading_is_a_baseline_and_not_a_report);
    RUN_TEST(test_a_drop_below_the_threshold_names_the_new_low_and_the_one_it_replaced);
    RUN_TEST(test_a_report_is_handed_out_once);
    RUN_TEST(test_a_slow_sag_costs_one_line_per_step_of_hysteresis);
    RUN_TEST(test_reading_the_watermark_allocates_nothing);
    RUN_TEST(test_a_dip_between_two_snapshots_is_named_on_the_step_it_happened);
    RUN_TEST(test_a_run_without_a_dip_says_nothing);
    RUN_TEST(test_a_trace_nobody_asked_for_says_nothing);
}
