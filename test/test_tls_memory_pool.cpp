// TlsMemoryPool: the calloc/free hooks mbedtls calls on every TLS handshake.
//
// The pool is reached through the function pointers install() hands to mbedtls; the host
// mbedtls/platform.h stand-in captures them, so these tests drive exactly the entry points the
// device uses. Two claims are pinned: the hot path is silent - a release build prints nothing
// per allocation - and a pool-full fallback to the heap is still not lost, because it is
// counted where it happens.

#include <unity.h>
#include <cstring>

#include "Infrastructure/Adapters/Http/TlsMemoryPool.h"

// Both slots taken plus one more request of pooled size: the third must fall back to the heap.
static const size_t kPooledSize = 16384;

static void reset_pool()
{
    // install() re-marks every slot free and re-registers the hooks; the counter is monotonic
    // by design, so tests read it as a delta.
    TlsMemoryPool::install();
    Serial.printfCalls = 0;
}

static void test_install_registers_both_hooks(void)
{
    reset_pool();

    TEST_ASSERT_NOT_NULL(mbedtlsInstalledCalloc);
    TEST_ASSERT_NOT_NULL(mbedtlsInstalledFree);
}

static void test_pooled_request_returns_zeroed_slot_silently(void)
{
    reset_pool();

    void *first = mbedtlsInstalledCalloc(1, kPooledSize);

    TEST_ASSERT_NOT_NULL(first);
    // calloc contract: the slot is handed out zeroed even after previous use.
    unsigned char probe[32];
    memset(probe, 0, sizeof(probe));
    TEST_ASSERT_EQUAL_MEMORY(probe, first, sizeof(probe));
    // The allocation itself printed nothing - this is the per-handshake hot path.
    TEST_ASSERT_EQUAL_INT(0, Serial.printfCalls);

    mbedtlsInstalledFree(first);
    TEST_ASSERT_EQUAL_INT(0, Serial.printfCalls);
}

static void test_pool_full_falls_back_to_heap_and_is_counted(void)
{
    reset_pool();
    uint32_t fallbacksBefore = TlsMemoryPool::heapFallbacks();

    void *first = mbedtlsInstalledCalloc(1, kPooledSize);
    void *second = mbedtlsInstalledCalloc(1, kPooledSize);
    void *third = mbedtlsInstalledCalloc(1, kPooledSize);

    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_NOT_NULL(third);
    // Two distinct slots, then a heap pointer - and the refusal is on the counter, not lost.
    TEST_ASSERT_TRUE(first != second);
    TEST_ASSERT_TRUE(third != first && third != second);
    TEST_ASSERT_EQUAL_UINT32(fallbacksBefore + 1, TlsMemoryPool::heapFallbacks());
    // Still silent, fallback included.
    TEST_ASSERT_EQUAL_INT(0, Serial.printfCalls);

    mbedtlsInstalledFree(first);
    mbedtlsInstalledFree(second);
    mbedtlsInstalledFree(third);
}

static void test_freed_slot_is_reused_and_small_requests_bypass_the_pool(void)
{
    reset_pool();
    uint32_t fallbacksBefore = TlsMemoryPool::heapFallbacks();

    void *first = mbedtlsInstalledCalloc(1, kPooledSize);
    void *second = mbedtlsInstalledCalloc(1, kPooledSize);
    mbedtlsInstalledFree(first);

    // The slot released a line above is available again: no fallback for the third request.
    void *third = mbedtlsInstalledCalloc(1, kPooledSize);
    TEST_ASSERT_TRUE(third == first);
    TEST_ASSERT_EQUAL_UINT32(fallbacksBefore, TlsMemoryPool::heapFallbacks());

    // Below kMinPooled the pool is not involved: heap pointer, no fallback counted.
    void *small = mbedtlsInstalledCalloc(1, 512);
    TEST_ASSERT_NOT_NULL(small);
    TEST_ASSERT_TRUE(small != first && small != second);
    TEST_ASSERT_EQUAL_UINT32(fallbacksBefore, TlsMemoryPool::heapFallbacks());
    TEST_ASSERT_EQUAL_INT(0, Serial.printfCalls);

    mbedtlsInstalledFree(second);
    mbedtlsInstalledFree(third);
    mbedtlsInstalledFree(small);
}

extern "C" void run_tls_memory_pool_tests(void)
{
    RUN_TEST(test_install_registers_both_hooks);
    RUN_TEST(test_pooled_request_returns_zeroed_slot_silently);
    RUN_TEST(test_pool_full_falls_back_to_heap_and_is_counted);
    RUN_TEST(test_freed_slot_is_reused_and_small_requests_bypass_the_pool);
}
