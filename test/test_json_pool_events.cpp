// The two requests the JSON pool does not serve from a slot, and what the log says about them.
//
// JsonMemoryPool counts both - heapFallbacks for a request a slot could have served if one had
// been free, heapBypasses for a request smaller than the smallest slot - and reports each in the log.
// A fallback is a heap block the size of a document, up to 16 KB, taken and released without a
// trace unless it is reported; a run that loses tens of kilobytes between two heap samples cannot
// be told apart from one that did not, and that is the question these lines exist to answer.
//
// One line per event and nothing on the path that works: the pool sits under every conversion the
// control loop makes, so a line per allocation would be the log and not a signal.
//
// The pool is driven through its own allocate()/deallocate(), which is what ArduinoJson's
// allocator calls - see test_json_pool_small_documents.cpp for the same pool driven through the
// converter the board runs. Every test here reads what it needs and gives its blocks back before
// it asserts: the pool is a process-wide singleton, so a slot still marked used when an assertion
// fires would be held for every test that runs after this file.

#include <unity.h>

#include "Infrastructure/Adapters/Json/JsonMemoryPool.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{

JsonMemoryPool &pool()
{
    return JsonMemoryPool::instance();
}

void forgetWhatWasPrinted()
{
    Serial.printlnCalls = 0;
    Serial.printfCalls = 0;
    Serial.printCalls = 0;
    Serial.lastText.clear();
}

// --- the events -------------------------------------------------------------------------------

// The 16 KB tier has one slot and five call sites, on tasks that run at the same time. The second
// request is the event this line exists for: a 16 KB heap block, which must not be taken silently.
void test_a_pool_full_fallback_is_reported_in_one_line(void)
{
    void *held = pool().allocate(16384);
    const uint32_t fallbacksBefore = pool().heapFallbacks();
    forgetWhatWasPrinted();

    void *fellBack = pool().allocate(16384);

    const uint32_t fallbacksAfter = pool().heapFallbacks();
    const int lines = Serial.printlnCalls;
    const std::string said = Serial.lastText;
    pool().deallocate(fellBack);
    pool().deallocate(held);

    TEST_ASSERT_NOT_NULL(fellBack);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fallbacksBefore + 1, fallbacksAfter,
                                     "the request was served from a slot after all");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, lines, "a 16 KB heap block was taken without a line");

    char expected[JsonMemoryPool::kEventLineBytes];
    snprintf(expected, sizeof(expected),
             "JSON_POOL: counter=heapFallbacks size=16384 n=%lu slots=1/10 heap=ok",
             (unsigned long)(fallbacksBefore + 1));
    TEST_ASSERT_EQUAL_STRING(expected, said.c_str());
}

// The other way past the slots: a document smaller than the smallest tier. Nothing asks for one
// today, so a line here means a new call site with a capacity the pool is not shaped for.
void test_a_request_below_the_smallest_slot_is_reported_as_a_bypass(void)
{
    const uint32_t bypassesBefore = pool().heapBypasses();
    forgetWhatWasPrinted();

    void *small = pool().allocate(128);

    const uint32_t bypassesAfter = pool().heapBypasses();
    const int lines = Serial.printlnCalls;
    const std::string said = Serial.lastText;
    pool().deallocate(small);

    TEST_ASSERT_NOT_NULL(small);
    TEST_ASSERT_EQUAL_UINT32(bypassesBefore + 1, bypassesAfter);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, lines,
                                  "a request the pool is not shaped for passed unreported");

    char expected[JsonMemoryPool::kEventLineBytes];
    snprintf(expected, sizeof(expected),
             "JSON_POOL: counter=heapBypasses size=128 n=%lu slots=0/10 heap=ok",
             (unsigned long)(bypassesBefore + 1));
    TEST_ASSERT_EQUAL_STRING(expected, said.c_str());
}

// How full the pool was when the request missed it, because that is what says whether the tiers
// are the wrong shape or simply all taken at that moment.
void test_the_line_says_how_many_slots_were_held_when_the_request_missed(void)
{
    void *first = pool().allocate(16384);
    void *second = pool().allocate(8192);
    void *third = pool().allocate(4096);

    const uint32_t fallbacksBefore = pool().heapFallbacks();
    forgetWhatWasPrinted();

    // Nothing of 8 KB or more is free any more, so this one cannot be served.
    void *fellBack = pool().allocate(8192);

    const std::string said = Serial.lastText;
    pool().deallocate(fellBack);
    pool().deallocate(third);
    pool().deallocate(second);
    pool().deallocate(first);

    char expected[JsonMemoryPool::kEventLineBytes];
    snprintf(expected, sizeof(expected),
             "JSON_POOL: counter=heapFallbacks size=8192 n=%lu slots=3/10 heap=ok",
             (unsigned long)(fallbacksBefore + 1));
    TEST_ASSERT_EQUAL_STRING(expected, said.c_str());
}

// Releasing is not an event: a slot coming free is the pool working, and the line would double
// the traffic of the one that is.
void test_giving_a_block_back_prints_nothing(void)
{
    void *held = pool().allocate(16384);
    void *fellBack = pool().allocate(16384);

    forgetWhatWasPrinted();
    pool().deallocate(fellBack);
    pool().deallocate(held);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, Serial.printlnCalls, "the release path printed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, Serial.printfCalls, "the release path printed");
}

// The path that works stays silent - every conversion the control loop makes goes through it.
void test_a_request_a_slot_serves_prints_nothing(void)
{
    forgetWhatWasPrinted();

    void *large = pool().allocate(16384);
    void *small = pool().allocate(256);
    pool().deallocate(small);
    pool().deallocate(large);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, Serial.printlnCalls, "the pool printed on the served path");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, Serial.printfCalls, "the pool printed on the served path");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, Serial.printCalls, "the pool printed on the served path");
}

// --- the text of the line ---------------------------------------------------------------------

// Pinned character for character: the line is read by grep and by whoever is comparing two runs,
// so the field names and their order are the interface, not the printf call that happens to
// produce them.
void test_the_line_names_the_counter_the_size_and_the_occupancy(void)
{
    char line[JsonMemoryPool::kEventLineBytes];

    const size_t needed = JsonMemoryPool::formatHeapEvent("heapFallbacks", 16384, 7, 10, true,
                                                          line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("JSON_POOL: counter=heapFallbacks size=16384 n=7 slots=10/10 heap=ok",
                             line);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(line), (uint32_t)needed);
}

// A heap that had nothing left. The document is then built on no capacity at all and every value
// put into it is dropped, which is worth more than the fallback itself.
void test_a_refused_heap_block_is_said_to_be_refused(void)
{
    char line[JsonMemoryPool::kEventLineBytes];

    JsonMemoryPool::formatHeapEvent("heapFallbacks", 16384, 7, 10, false, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING(
        "JSON_POOL: counter=heapFallbacks size=16384 n=7 slots=10/10 heap=null", line);
}

// The widest line the fields can produce still fits the buffer the pool puts on the stack: a
// request of every byte the width allows, and a counter that has run for the life of the device.
void test_the_widest_line_fits_the_buffer_the_pool_reserves(void)
{
    char line[JsonMemoryPool::kEventLineBytes];

    const size_t needed = JsonMemoryPool::formatHeapEvent("heapFallbacks", 4294967295u,
                                                          4294967295u, 10, false, line,
                                                          sizeof(line));
    TEST_ASSERT_TRUE_MESSAGE(needed < JsonMemoryPool::kEventLineBytes,
                             "the widest line does not fit kEventLineBytes");
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(line), (uint32_t)needed);
}

} // namespace

extern "C" void run_json_pool_event_tests(void)
{
    RUN_TEST(test_a_pool_full_fallback_is_reported_in_one_line);
    RUN_TEST(test_a_request_below_the_smallest_slot_is_reported_as_a_bypass);
    RUN_TEST(test_the_line_says_how_many_slots_were_held_when_the_request_missed);
    RUN_TEST(test_giving_a_block_back_prints_nothing);
    RUN_TEST(test_a_request_a_slot_serves_prints_nothing);

    RUN_TEST(test_the_line_names_the_counter_the_size_and_the_occupancy);
    RUN_TEST(test_a_refused_heap_block_is_said_to_be_refused);
    RUN_TEST(test_the_widest_line_fits_the_buffer_the_pool_reserves);
}
