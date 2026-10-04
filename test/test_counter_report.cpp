// The periodic counters line: orphan counters made visible in the device log.
//
// Three claims. The line is stable and greppable - "DIAG-COUNTERS:" then name=value pairs,
// single spaces, watermarks the scheduler cannot measure printed as "n/a" and never as a
// lying zero. The numbers in the line are the live counters - cranked here by driving the
// real services into refusals, not by stubbing values. And the line reaches the log through
// DiagnosticService::perform() on the diagnostic channel, only while diagnostic logging is
// enabled.

#include <unity.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "Application/Services/CounterReport.h"
#include "Application/Services/DiagnosticService.h"
#include "Application/Services/JsonCounters.h"
#include "Application/Services/LoopServices/LoopServiceCanConfigure.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Core/Domain/Diagnostics/LoopStepMonitor.h"
#include "Core/Domain/Diagnostics/MalformedJsonNumberCounter.h"
#include "Infrastructure/Adapters/Http/TlsMemoryPool.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"

// --- Scheduler stand-ins ----------------------------------------------------------------------

// Accepts every task but never runs it: submissions queue up until the cap refuses them.
class ParkedScheduler : public ITaskScheduler
{
public:
    bool createTask(const char*, uint32_t, uint8_t, TaskBody) override { return true; }
    void yieldTask() override {}
    void delayMs(uint32_t) override {}
};

// Refuses every task: exercises the task-start-failure counters.
class FailingScheduler : public ITaskScheduler
{
public:
    bool createTask(const char*, uint32_t, uint8_t, TaskBody) override { return false; }
    void yieldTask() override {}
    void delayMs(uint32_t) override {}
};

// Knows one task's watermark, like the device scheduler knows the tasks it created.
class WatermarkScheduler : public ITaskScheduler
{
public:
    const char* knownTask = "cmd-net";
    uint32_t knownBytes = 1234;

    bool createTask(const char*, uint32_t, uint8_t, TaskBody) override { return true; }
    void yieldTask() override {}
    void delayMs(uint32_t) override {}
    bool stackHighWaterMarkBytes(const char* taskName, uint32_t& outBytes) override
    {
        if (taskName != nullptr && strcmp(taskName, knownTask) == 0)
        {
            outBytes = knownBytes;
            return true;
        }
        return false;
    }
};

// --- The line itself --------------------------------------------------------------------------

static void test_line_is_prefix_plus_name_value_pairs(void)
{
    CounterReport report;
    report.addCounter("alpha", [] { return (uint64_t)7; });
    report.addCounter("beta", [] { return (uint64_t)0; });

    char line[256];
    size_t written = report.format(line, sizeof(line));

    TEST_ASSERT_EQUAL_STRING("DIAG-COUNTERS: alpha=7 beta=0", line);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(line), (uint32_t)written);
}

static void test_values_are_read_at_format_time(void)
{
    static uint64_t value = 1;
    CounterReport report;
    report.addCounter("live", [] { return value; });

    char line[64];
    report.format(line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("DIAG-COUNTERS: live=1", line);

    value = 42;
    report.format(line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("DIAG-COUNTERS: live=42", line);
}

static void test_unknown_watermark_prints_na_not_zero(void)
{
    // No scheduler at all, and a scheduler that does not know the task: both are unknowns.
    CounterReport bare;
    bare.addTaskStack("stack_outbox_net", "outbox-net");
    char line[64];
    bare.format(line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("DIAG-COUNTERS: stack_outbox_net=n/a", line);

    WatermarkScheduler scheduler;
    CounterReport report;
    report.setTaskScheduler(&scheduler);
    report.addTaskStack("stack_cmd_net", "cmd-net");
    report.addTaskStack("stack_sensor_acq", "sensor-acq");
    report.format(line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("DIAG-COUNTERS: stack_cmd_net=1234 stack_sensor_acq=n/a", line);
}

static void test_full_tables_refuse_and_count_never_grow(void)
{
    CounterReport report;
    for (size_t i = 0; i < CounterReport::MAX_COUNTERS; i++)
    {
        TEST_ASSERT_TRUE(report.addCounter("c", [] { return (uint64_t)0; }));
    }
    TEST_ASSERT_FALSE(report.addCounter("overflow", [] { return (uint64_t)0; }));
    TEST_ASSERT_EQUAL_UINT32(1, report.refusedAddCount());

    for (size_t i = 0; i < CounterReport::MAX_TASK_STACKS; i++)
    {
        TEST_ASSERT_TRUE(report.addTaskStack("s", "t"));
    }
    TEST_ASSERT_FALSE(report.addTaskStack("overflow", "t"));
    TEST_ASSERT_EQUAL_UINT32(2, report.refusedAddCount());

    // The refused columns are absent, not truncated into the line.
    char line[512];
    report.format(line, sizeof(line));
    TEST_ASSERT_NULL(strstr(line, "overflow"));
}

static void test_line_truncates_to_the_buffer(void)
{
    CounterReport report;
    // At the name bound - long enough to overflow the deliberately small buffer below.
    report.addCounter("a_rather_long_counter_nm", [] { return (uint64_t)123456789; });

    char line[24];
    size_t written = report.format(line, sizeof(line));

    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(line) - 1), (uint32_t)written);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(line) - 1), (uint32_t)strlen(line));
}

// --- The numbers are the real counters --------------------------------------------------------

static void test_line_carries_counters_cranked_in_the_real_services(void)
{
    // A parked runner task: fill the queue to its cap, then two submissions are refused.
    ParkedScheduler parked;
    NetworkCommandRunner runner(&parked);
    ComplexCommandWork work;
    for (size_t i = 0; i < INetworkCommandRunner::MAX_PENDING_COMMANDS; i++)
    {
        TEST_ASSERT_TRUE(runner.submit(work) != INetworkCommandRunner::kRejected);
    }
    TEST_ASSERT_TRUE(runner.submit(work) == INetworkCommandRunner::kRejected);
    TEST_ASSERT_TRUE(runner.submit(work) == INetworkCommandRunner::kRejected);

    // A runner whose task cannot start: every submission fails and is counted.
    FailingScheduler failing;
    NetworkCommandRunner unstartable(&failing);
    TEST_ASSERT_TRUE(unstartable.submit(work) == INetworkCommandRunner::kRejected);

    // The TLS pool, driven through the hooks it registers with mbedtls: both slots taken,
    // the third pooled request falls back to the heap and lands on the counter.
    TlsMemoryPool::install();
    uint32_t fallbacksBefore = TlsMemoryPool::heapFallbacks();
    void* first = mbedtlsInstalledCalloc(1, 16384);
    void* second = mbedtlsInstalledCalloc(1, 16384);
    void* third = mbedtlsInstalledCalloc(1, 16384);

    CounterReport report;
    report.addCounter("net_refused", [&runner] { return (uint64_t)runner.refusedCount(); });
    report.addCounter("net_task_fail",
                      [&unstartable] { return (uint64_t)unstartable.taskStartFailureCount(); });
    report.addCounter("tls_fallbacks", [] { return (uint64_t)TlsMemoryPool::heapFallbacks(); });

    char line[256];
    report.format(line, sizeof(line));

    char expected[256];
    snprintf(expected, sizeof(expected),
             "DIAG-COUNTERS: net_refused=2 net_task_fail=1 tls_fallbacks=%lu",
             (unsigned long)(fallbacksBefore + 1));
    TEST_ASSERT_EQUAL_STRING(expected, line);

    mbedtlsInstalledFree(first);
    mbedtlsInstalledFree(second);
    mbedtlsInstalledFree(third);
}

// --- The line reaches the log through the diagnostic service ----------------------------------

static void test_perform_emits_the_line_on_the_diagnostic_channel(void)
{
    MockRestAPIService restApi;
    MockModuleConfig moduleConfig;
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = true;

    DiagnosticService service(&restApi, &moduleConfig, logger, debugSystem);

    static uint64_t cranked = 31337;
    CounterReport report;
    report.addCounter("net_refused", [] { return cranked; });
    report.addTaskStack("stack_cmd_net", "cmd-net");
    service.setCounterReport(&report);

    service.perform();

    TEST_ASSERT_TRUE(logger.hasMessageContaining(
        "DIAG-COUNTERS: net_refused=31337 stack_cmd_net=n/a"));
}

static void test_perform_stays_silent_when_diagnostic_logging_is_off(void)
{
    MockRestAPIService restApi;
    MockModuleConfig moduleConfig;
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = false;

    DiagnosticService service(&restApi, &moduleConfig, logger, debugSystem);

    CounterReport report;
    report.addCounter("net_refused", [] { return (uint64_t)1; });
    service.setCounterReport(&report);

    service.perform();

    TEST_ASSERT_FALSE(logger.hasMessageContaining("DIAG-COUNTERS:"));
}

// --------------------------------------------------------------------------------------------
// The one column that is a measurement: how long the slowest control-loop step of the period
// took. Nothing on the device reported it before, so a loop that stalled for a second every half
// minute - which is what a synchronous POST on the loop looks like - left no trace at all.
//
// The step is measured by LoopServiceBase::loop() around the whole iteration, so what is under
// test here is a real loop service with a slow thing inside it: the web server stand-in below
// takes the time a web server takes, and the measurement is the production code around it.
// --------------------------------------------------------------------------------------------

/** A web server whose loop() costs the caller the milliseconds a real one would. */
class SlowWebServer : public IWebServerLooper
{
public:
    MockArduinoAdapter* clock = nullptr;
    unsigned long costMs = 0;

    void loop() override
    {
        if (clock != nullptr)
        {
            clock->currentMillis += costMs;
        }
    }
};

/** The column, wired exactly as globals.h wires it. */
void addLoopStepColumn(CounterReport& report)
{
    report.addCounter("max_loop_step_ms", [] { return (uint64_t)LoopStepMonitor::take(); });
}

static void test_a_long_loop_step_reaches_the_counters_line(void)
{
    LoopStepMonitor::reset();

    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = true;
    MockArduinoAdapter arduino;

    SlowWebServer webServer;
    webServer.clock = &arduino;

    LoopServiceCanConfigure loopService;
    loopService.setLogging(&logger, &debugSystem);
    loopService.setArduinoAdapter(&arduino);
    loopService.setWebServer(&webServer);

    // Three iterations, one of them long. The worst is what the column has to carry - not the
    // last one, and not their sum.
    const unsigned long steps[] = {5, 900, 12};
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
    {
        webServer.costMs = steps[i];
        loopService.loop();
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(900, LoopStepMonitor::peek(),
                                     "the monitor did not keep the worst of the three steps");

    MockRestAPIService restApi;
    MockModuleConfig moduleConfig;
    DiagnosticService service(&restApi, &moduleConfig, logger, debugSystem);

    CounterReport report;
    addLoopStepColumn(report);
    service.setCounterReport(&report);

    service.perform();

    TEST_ASSERT_TRUE_MESSAGE(logger.hasMessageContaining("DIAG-COUNTERS: max_loop_step_ms=900"),
                             "the worst control-loop step did not reach the counters line");
}

static void test_the_loop_step_column_describes_one_diagnostic_interval(void)
{
    LoopStepMonitor::reset();

    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = true;
    MockArduinoAdapter arduino;

    SlowWebServer webServer;
    webServer.clock = &arduino;

    LoopServiceCanConfigure loopService;
    loopService.setLogging(&logger, &debugSystem);
    loopService.setArduinoAdapter(&arduino);
    loopService.setWebServer(&webServer);

    MockRestAPIService restApi;
    MockModuleConfig moduleConfig;
    DiagnosticService service(&restApi, &moduleConfig, logger, debugSystem);

    CounterReport report;
    addLoopStepColumn(report);
    service.setCounterReport(&report);

    webServer.costMs = 900;
    loopService.loop();
    service.perform();
    TEST_ASSERT_TRUE(logger.hasMessageContaining("DIAG-COUNTERS: max_loop_step_ms=900"));

    // The next interval is its own: the stall above must not colour every line after it, which is
    // the whole reason this probe consumes what it reports.
    webServer.costMs = 7;
    loopService.loop();
    service.perform();

    TEST_ASSERT_TRUE_MESSAGE(logger.hasMessageContaining("DIAG-COUNTERS: max_loop_step_ms=7"),
                             "the column reported a high-water mark instead of the interval that "
                             "just ended");
}

// --------------------------------------------------------------------------------------------
// The column that says a number the module ran on was not the number it was given. A rule body
// whose threshold arrived as text leaves a rule that looks entirely healthy and switches at a
// value nobody configured; before this column, nothing on the device said so.
//
// What runs here is the converter globals.h gives the board, over the body as it arrives, and the
// registration the composition root calls - not a copy of either.
// --------------------------------------------------------------------------------------------

/** @brief A rule as RULE_PUSH delivers it, with a target that is valid JSON and not a number. */
static const char* const RULES_WITH_A_TARGET_THAT_IS_TEXT =
    "[{\"uid\":\"rule-uid-0001\",\"version\":3,\"deviceUid\":\"7C:DF:A1:96:B4:94.01\","
    "\"ruleType\":\"step\",\"sensorUid\":\"84:F7:03:D7:8E:60.00.01\",\"valueTarget\":\"-\","
    "\"direction\":\"down\",\"workingTime\":60,\"waitingTime\":600,\"isRuleOn\":false,"
    "\"mode\":\"Off\",\"state\":\"Synchronized\",\"daysOfWeek\":[],\"daysOfMonth\":[]}]";

static void test_a_substituted_number_reaches_the_counters_line(void)
{
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = true;
    MockRestAPIService restApi;
    MockModuleConfig moduleConfig;
    DiagnosticService service(&restApi, &moduleConfig, logger, debugSystem);

    CounterReport report;
    registerJsonCounters(report);
    service.setCounterReport(&report);

    ArduinoJsonConverter converter;
    const uint32_t before = MalformedJsonNumberCounter::count();

    std::vector<Rule> rules;
    TEST_ASSERT_EQUAL_MESSAGE(
        static_cast<int>(JsonListParse::Ok),
        static_cast<int>(converter.rulesFromJson(RULES_WITH_A_TARGET_THAT_IS_TEXT, rules)),
        "the scenario needs a rule the module accepts, damaged number and all");
    TEST_ASSERT_EQUAL_size_t(1, rules.size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before + 1, MalformedJsonNumberCounter::count(),
                                     "one field that was not a number has to count once");

    service.perform();

    char expected[64];
    snprintf(expected, sizeof(expected), "DIAG-COUNTERS: json_bad_numbers=%lu",
             (unsigned long)(before + 1));
    TEST_ASSERT_TRUE_MESSAGE(logger.hasMessageContaining(expected),
                             "the number the module substituted did not reach the counters line");
}

// --------------------------------------------------------------------------------------------
// The worst line is a compile-time number, and a buffer sized to it loses nothing. Names are
// what made the length unbounded, so a name over MAX_NAME_CHARS is refused and counted; with
// names bounded, full tables and saturated values must fit LINE_BUFFER_CHARS with the last
// column - a watermark, the first thing silent truncation would eat - intact.
// --------------------------------------------------------------------------------------------

static void test_a_name_over_the_bound_is_refused_and_counted(void)
{
    CounterReport report;

    // MAX_NAME_CHARS + 1 characters: one over the bound the line arithmetic assumes.
    std::string longName(CounterReport::MAX_NAME_CHARS + 1, 'x');

    TEST_ASSERT_FALSE_MESSAGE(report.addCounter(longName.c_str(), [] { return (uint64_t)1; }),
                              "a name longer than the bound was accepted - the worst-line "
                              "arithmetic no longer holds");
    TEST_ASSERT_FALSE_MESSAGE(report.addTaskStack(longName.c_str(), "cmd-net"),
                              "a label longer than the bound was accepted");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, report.refusedAddCount(),
                                     "the refusals were not counted - never silent");

    // At the bound is fine: the arithmetic covers it.
    std::string maxName(CounterReport::MAX_NAME_CHARS, 'y');
    TEST_ASSERT_TRUE(report.addCounter(maxName.c_str(), [] { return (uint64_t)1; }));
}

static void test_the_worst_possible_line_fits_the_declared_buffer(void)
{
    CounterReport report;

    // Saturate everything: full tables, names at the bound, values at their widest.
    std::string names[CounterReport::MAX_COUNTERS + CounterReport::MAX_TASK_STACKS];
    for (size_t i = 0; i < CounterReport::MAX_COUNTERS; i++)
    {
        char tail[8];
        snprintf(tail, sizeof(tail), "%02u", (unsigned)i);
        names[i] = std::string(CounterReport::MAX_NAME_CHARS - 2, 'c') + tail;
        TEST_ASSERT_TRUE(report.addCounter(names[i].c_str(),
                                           [] { return (uint64_t)UINT64_MAX; }));
    }

    WatermarkScheduler scheduler;
    scheduler.knownTask = "wide";
    scheduler.knownBytes = UINT32_MAX;
    report.setTaskScheduler(&scheduler);
    for (size_t i = 0; i < CounterReport::MAX_TASK_STACKS; i++)
    {
        char tail[8];
        snprintf(tail, sizeof(tail), "%02u", (unsigned)i);
        const size_t slot = CounterReport::MAX_COUNTERS + i;
        names[slot] = std::string(CounterReport::MAX_NAME_CHARS - 2, 's') + tail;
        TEST_ASSERT_TRUE(report.addTaskStack(names[slot].c_str(), "wide"));
    }

    char line[CounterReport::LINE_BUFFER_CHARS];
    const size_t used = report.format(line, sizeof(line));

    char message[160];
    snprintf(message, sizeof(message),
             "the saturated line is %lu characters against a declared worst of %lu",
             (unsigned long)strlen(line), (unsigned long)CounterReport::WORST_LINE_CHARS);
    TEST_ASSERT_TRUE_MESSAGE(strlen(line) <= CounterReport::WORST_LINE_CHARS, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)strlen(line), (uint32_t)used,
                                     "format() reported a different length than it wrote");

    // Nothing fell off the end: the last watermark column arrived whole, value included.
    const std::string expectedTail =
        names[CounterReport::MAX_COUNTERS + CounterReport::MAX_TASK_STACKS - 1] + "=4294967295";
    TEST_ASSERT_TRUE_MESSAGE(strstr(line, expectedTail.c_str()) != nullptr,
                             "the last watermark column was truncated away - exactly the loss "
                             "the sized buffer exists to prevent");
}

// --------------------------------------------------------------------------------------------
// The table holds the columns the module registers, with room left over, and it says when it could
// not. A refused registration leaves no gap in the line and no word in the log: the column is
// simply not there, and the number it carried stops existing for anybody reading the field log. So
// the cap is kept ahead of the list, and the count of refusals is itself a column - registered
// before the table can fill, because what a full table refuses is whatever was offered to it last.
// --------------------------------------------------------------------------------------------

/** Columns the composition root registers today: the list in globals.h plus the three helpers. */
static const size_t COLUMNS_THE_COMPOSITION_ROOT_REGISTERS = 26;

static void test_the_table_holds_the_columns_the_module_registers_and_more(void)
{
    CounterReport report;

    for (size_t i = 0; i < COLUMNS_THE_COMPOSITION_ROOT_REGISTERS; i++)
    {
        char message[128];
        snprintf(message, sizeof(message),
                 "column %u of the %u the module registers was refused - it would be missing from "
                 "the field log with nothing there to say so",
                 (unsigned)(i + 1), (unsigned)COLUMNS_THE_COMPOSITION_ROOT_REGISTERS);
        TEST_ASSERT_TRUE_MESSAGE(report.addCounter("c", [] { return (uint64_t)0; }), message);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, report.refusedAddCount(),
                                     "the table refused one of the columns the module registers");

    // And the headroom, which is what the cap is for: the next column is one nobody has written
    // yet, and it has to have somewhere to go when it is.
    TEST_ASSERT_TRUE_MESSAGE(report.addCounter("scan_adopted", [] { return (uint64_t)7; }),
                             "the table has no room past the columns the module already registers "
                             "- the next one added to it would be refused");

    char line[CounterReport::LINE_BUFFER_CHARS];
    report.format(line, sizeof(line));
    TEST_ASSERT_TRUE_MESSAGE(strstr(line, "scan_adopted=7") != nullptr,
                             "a column the table accepted did not reach the line");
}

static void test_a_refused_column_is_reported_by_the_line_it_is_missing_from(void)
{
    CounterReport report;

    // Before every other column, which is the whole point of this one.
    TEST_ASSERT_TRUE_MESSAGE(report.addRefusalColumn("counters_refused"),
                             "an empty table refused the column that reports its refusals");
    for (size_t i = 1; i < CounterReport::MAX_COUNTERS; i++)
    {
        TEST_ASSERT_TRUE(report.addCounter("c", [] { return (uint64_t)0; }));
    }

    TEST_ASSERT_FALSE_MESSAGE(report.addCounter("scan_adopted", [] { return (uint64_t)1; }),
                              "a full table took another column - the cap is not a cap");

    char line[CounterReport::LINE_BUFFER_CHARS];
    report.format(line, sizeof(line));

    TEST_ASSERT_NULL_MESSAGE(strstr(line, "scan_adopted"),
                             "the refused column is in the line after all");
    TEST_ASSERT_TRUE_MESSAGE(strstr(line, "DIAG-COUNTERS: counters_refused=1") == line,
                             "the line does not open with the count of what the table refused - a "
                             "column was lost and the log says nothing about it");

    // Read at format time like every other column, so a refusal after that line shows in the next.
    const std::string overTheBound(CounterReport::MAX_NAME_CHARS + 1, 'x');
    TEST_ASSERT_FALSE(report.addTaskStack(overTheBound.c_str(), "cmd-net"));
    report.format(line, sizeof(line));
    TEST_ASSERT_TRUE_MESSAGE(strstr(line, "DIAG-COUNTERS: counters_refused=2") == line,
                             "the column reports the refusals of some earlier moment, not of the "
                             "line it is in");
}

extern "C" void run_counter_report_tests(void)
{
    RUN_TEST(test_line_is_prefix_plus_name_value_pairs);
    RUN_TEST(test_values_are_read_at_format_time);
    RUN_TEST(test_unknown_watermark_prints_na_not_zero);
    RUN_TEST(test_full_tables_refuse_and_count_never_grow);
    RUN_TEST(test_line_truncates_to_the_buffer);
    RUN_TEST(test_line_carries_counters_cranked_in_the_real_services);
    RUN_TEST(test_perform_emits_the_line_on_the_diagnostic_channel);
    RUN_TEST(test_perform_stays_silent_when_diagnostic_logging_is_off);
    RUN_TEST(test_a_long_loop_step_reaches_the_counters_line);
    RUN_TEST(test_the_loop_step_column_describes_one_diagnostic_interval);
    RUN_TEST(test_a_substituted_number_reaches_the_counters_line);
    RUN_TEST(test_a_name_over_the_bound_is_refused_and_counted);
    RUN_TEST(test_the_worst_possible_line_fits_the_declared_buffer);
    RUN_TEST(test_the_table_holds_the_columns_the_module_registers_and_more);
    RUN_TEST(test_a_refused_column_is_reported_by_the_line_it_is_missing_from);
}
