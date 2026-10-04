// Memory made visible: a refused allocation and the largest block a single allocation can get.
//
// A refusal tells the caller nothing but a null pointer, so without a counter the event leaves
// no trace at all. It is counted at the point where it happens, and the count rides the periodic
// counters line. The largest free PSRAM block is the other half of the picture: free memory can
// be plentiful while no single block is big enough for the next TLS handshake, and a free-bytes
// total cannot show that - only the largest block can.
//
// The failure here is produced, not stubbed: the TLS pool is driven through the very calloc hook
// mbedtls calls on the device, with a request no heap can serve. The platform hook is driven
// through the host stand-in for esp_heap_caps.h, which captures the callback the ESP-IDF heap
// would call.

#include <unity.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Application/Services/CounterReport.h"
#include "Application/Services/DiagnosticService.h"
#include "Application/Services/MemoryCounters.h"
#include "Core/Domain/Diagnostics/AllocationFailureCounter.h"
#include "Infrastructure/Adapters/Http/TlsMemoryPool.h"
#include "Infrastructure/Adapters/Memory/HeapFailureHook.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"

// Larger than any heap can serve, so calloc() genuinely returns null and the pool takes the
// failure path. Over the pool's slot size, so the request goes straight to the heap.
static const size_t kUnservableSize = (size_t)-1 / 2;

// --- The refusal is counted where it happens --------------------------------------------------

static void test_a_refused_allocation_is_counted(void)
{
    AllocationFailureCounter::setHeapHookInstalled(false);
    TlsMemoryPool::install();
    const uint32_t before = AllocationFailureCounter::count();

    void* refused = mbedtlsInstalledCalloc(1, kUnservableSize);

    TEST_ASSERT_NULL_MESSAGE(refused, "the host heap served a request meant to be unservable");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before + 1, AllocationFailureCounter::count(),
                                     "a refused allocation left no trace on the counter");
}

static void test_a_served_heap_allocation_does_not_touch_the_counter(void)
{
    AllocationFailureCounter::setHeapHookInstalled(false);
    TlsMemoryPool::install();
    const uint32_t before = AllocationFailureCounter::count();

    // Under the pooled size, so the pool hands this straight to the heap - and the heap serves
    // it. What is counted is a refusal, not a trip to the heap.
    void* served = mbedtlsInstalledCalloc(1, 1024);

    TEST_ASSERT_NOT_NULL(served);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before, AllocationFailureCounter::count(),
                                     "a served allocation moved the failure counter");

    mbedtlsInstalledFree(served);
}

static void test_a_pool_full_fallback_that_the_heap_serves_is_not_a_failure(void)
{
    AllocationFailureCounter::setHeapHookInstalled(false);
    TlsMemoryPool::install();

    // Both slots taken: the third pooled request falls back to the heap, which serves it. This
    // is the busy-pool case - frequent in the field, and not a memory failure.
    void* first = mbedtlsInstalledCalloc(1, 16384);
    void* second = mbedtlsInstalledCalloc(1, 16384);
    const uint32_t before = AllocationFailureCounter::count();
    const uint32_t fallbacksBefore = TlsMemoryPool::heapFallbacks();

    void* third = mbedtlsInstalledCalloc(1, 16384);

    TEST_ASSERT_NOT_NULL(third);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fallbacksBefore + 1, TlsMemoryPool::heapFallbacks(),
                                     "the request did not take the pool-full fallback path");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before, AllocationFailureCounter::count(),
                                     "a busy pool was counted as a memory failure");

    mbedtlsInstalledFree(first);
    mbedtlsInstalledFree(second);
    mbedtlsInstalledFree(third);
}

static void test_a_failure_the_platform_hook_reports_is_counted_once(void)
{
    TlsMemoryPool::install();
    AllocationFailureCounter::setHeapHookInstalled(false);
    TEST_ASSERT_FALSE(AllocationFailureCounter::isHeapHookInstalled());

    TEST_ASSERT_TRUE_MESSAGE(HeapFailureHook::install(), "the platform hook was not registered");
    TEST_ASSERT_NOT_NULL_MESSAGE(espInstalledAllocFailedHook,
                                 "install() registered no callback with the heap");
    // Whether anything is watching is itself reportable: a zero count means far less without it.
    TEST_ASSERT_TRUE(AllocationFailureCounter::isHeapHookInstalled());

    const uint32_t before = AllocationFailureCounter::count();

    // The platform hook sees this very failure, so the call site must step aside: one event
    // has to move the counter by one.
    void* refused = mbedtlsInstalledCalloc(1, kUnservableSize);
    TEST_ASSERT_NULL(refused);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before, AllocationFailureCounter::count(),
                                     "the same failure was counted twice");

    // And the hook is what moves it: this is the entry point the ESP-IDF heap calls.
    espInstalledAllocFailedHook(kUnservableSize, 0, "heap_caps_malloc_default");
    TEST_ASSERT_EQUAL_UINT32(before + 1, AllocationFailureCounter::count());

    AllocationFailureCounter::setHeapHookInstalled(false);
}

// --- Both numbers reach the counters line ------------------------------------------------------

static void test_the_memory_columns_carry_the_platform_numbers(void)
{
    AllocationFailureCounter::setHeapHookInstalled(false);
    MockArduinoAdapter arduino;
    arduino.setMaxAllocPsram(65536);

    CounterReport report;
    registerMemoryCounters(report, arduino);

    char line[CounterReport::LINE_BUFFER_CHARS];
    report.format(line, sizeof(line));

    char expected[128];
    snprintf(expected, sizeof(expected), "DIAG-COUNTERS: alloc_fails=%lu psram_largest=65536",
             (unsigned long)AllocationFailureCounter::count());
    TEST_ASSERT_EQUAL_STRING(expected, line);

    // Live values, read when the line is built: a refusal after registration shows up.
    TlsMemoryPool::install();
    const uint32_t before = AllocationFailureCounter::count();
    mbedtlsInstalledCalloc(1, kUnservableSize);

    report.format(line, sizeof(line));
    snprintf(expected, sizeof(expected), "DIAG-COUNTERS: alloc_fails=%lu psram_largest=65536",
             (unsigned long)(before + 1));
    TEST_ASSERT_EQUAL_STRING(expected, line);
}

static void test_fragmentation_shows_as_a_falling_largest_block(void)
{
    MockRestAPIService restApi;
    MockModuleConfig moduleConfig;
    MockLogger logger;
    MockDebugSystem debugSystem;
    debugSystem.featureEnabled = true;

    MockArduinoAdapter arduino;
    arduino.setPsramSize(2 * 1024 * 1024);
    arduino.setFreePsram(200000);
    arduino.setMaxAllocPsram(131072);

    DiagnosticService service(&restApi, &moduleConfig, logger, debugSystem, nullptr, nullptr,
                              nullptr, &arduino);
    CounterReport report;
    registerMemoryCounters(report, arduino);
    service.setCounterReport(&report);

    const DiagnosticDto whole = service.perform();
    TEST_ASSERT_TRUE(logger.hasMessageContaining("psram_largest=131072"));

    // The heap fragments: the same number of free bytes, but no block big enough for a TLS
    // handshake left in it.
    logger.messages.clear();
    arduino.setMaxAllocPsram(8192);

    const DiagnosticDto fragmented = service.perform();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(whole.psramFree, fragmented.psramFree,
                                     "the scenario changed the free total, not the fragmentation");
    TEST_ASSERT_TRUE_MESSAGE(logger.hasMessageContaining("psram_largest=8192"),
                             "the line did not follow the largest block down");
    TEST_ASSERT_FALSE(logger.hasMessageContaining("psram_largest=131072"));
}

// --- The line still holds them ------------------------------------------------------------------

static void test_the_memory_columns_fit_the_table_and_the_line(void)
{
    MockArduinoAdapter arduino;
    arduino.setMaxAllocPsram(UINT32_MAX);

    CounterReport report;
    // The composition root fills the table to its cap, and the memory columns are its last two
    // slots. A refused column would be missing from the line altogether, so the refusal count
    // has to stay at zero.
    for (size_t i = 0; i < CounterReport::MAX_COUNTERS - 2; i++)
    {
        TEST_ASSERT_TRUE(report.addCounter("occupied", [] { return (uint64_t)UINT64_MAX; }));
    }

    registerMemoryCounters(report, arduino);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, report.refusedAddCount(),
                                     "a memory column did not fit the table and was refused");

    char line[CounterReport::LINE_BUFFER_CHARS];
    const size_t used = report.format(line, sizeof(line));

    TEST_ASSERT_NOT_NULL(strstr(line, " alloc_fails="));
    TEST_ASSERT_NOT_NULL(strstr(line, " psram_largest=4294967295"));
    TEST_ASSERT_TRUE_MESSAGE(used <= CounterReport::WORST_LINE_CHARS,
                             "the line outgrew the compile-time worst case");
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(line), (uint32_t)used);
}

extern "C" void run_memory_visibility_tests(void)
{
    RUN_TEST(test_a_refused_allocation_is_counted);
    RUN_TEST(test_a_served_heap_allocation_does_not_touch_the_counter);
    RUN_TEST(test_a_pool_full_fallback_that_the_heap_serves_is_not_a_failure);
    RUN_TEST(test_a_failure_the_platform_hook_reports_is_counted_once);
    RUN_TEST(test_the_memory_columns_carry_the_platform_numbers);
    RUN_TEST(test_fragmentation_shows_as_a_falling_largest_block);
    RUN_TEST(test_the_memory_columns_fit_the_table_and_the_line);
}
