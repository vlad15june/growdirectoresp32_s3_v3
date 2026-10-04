// The bookkeeping behind FreeRtosTaskScheduler::stackHighWaterMarkBytes(), executed.
//
// The scheduler answers the diagnostic line's stack_* columns by task name, reading each live
// task's watermark from its FreeRTOS handle. A task whose body returns deletes itself, and FreeRTOS
// then frees its control block and its stack (the idle task does, tasks.c prvDeleteTCB) - so a
// handle read after that reads freed memory. The storage task of the sensor type catalog is such a
// task, once per catalog write, and its column is asked for every thirty seconds.
//
// What is under test is TaskWatermarkTable, the scheduler's bookkeeping with FreeRTOS taken out:
// the scheduler hands it handles and a reader and holds its lock around every call - one line of
// FreeRTOS each, compiled for the device only. Replaced here is the reader, by one that records
// every handle it is asked to read.

#include <unity.h>

#include "Infrastructure/Scheduling/TaskWatermarkTable.h"

#include <cstdint>
#include <cstdio>
#include <map>

namespace {

const char* const STORAGE_TASK = "catalog-io";

// Distinct addresses to stand for task handles; nothing is read through them.
int g_handles[32];

void* handle(size_t i) { return &g_handles[i]; }

/** What each live task's stack shows, and which handles were read. */
std::map<void*, uint32_t> g_liveWatermark;
std::map<void*, uint32_t> g_reads;

uint32_t recordingReader(void* taskHandle)
{
    g_reads[taskHandle]++;
    std::map<void*, uint32_t>::const_iterator it = g_liveWatermark.find(taskHandle);
    return it == g_liveWatermark.end() ? 0 : it->second;
}

void resetReader()
{
    g_liveWatermark.clear();
    g_reads.clear();
}

uint32_t readsOf(void* taskHandle)
{
    std::map<void*, uint32_t>::const_iterator it = g_reads.find(taskHandle);
    return it == g_reads.end() ? 0 : it->second;
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. A task that has ended is never read again - its name still answers, with what it measured
//    as it left.
// --------------------------------------------------------------------------------------------

static void test_an_ended_task_is_not_read_and_answers_with_what_it_left(void)
{
    resetReader();
    TaskWatermarkTable table;
    g_liveWatermark[handle(0)] = 3104;

    TEST_ASSERT_TRUE(table.started(STORAGE_TASK, handle(0)));
    uint32_t bytes = 0;
    TEST_ASSERT_TRUE(table.lowest(STORAGE_TASK, &recordingReader, bytes));
    TEST_ASSERT_EQUAL_UINT32(3104, bytes);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, readsOf(handle(0)), "the live task was not read");

    table.ended(handle(0), 3096);

    bytes = 0;
    TEST_ASSERT_TRUE_MESSAGE(table.lowest(STORAGE_TASK, &recordingReader, bytes),
                             "a task that ended is reported as never having run");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3096, bytes,
                                     "the ended task does not answer with what it measured as it left");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, readsOf(handle(0)),
                                     "the handle of a task that ended was read - freed memory");
}

// --------------------------------------------------------------------------------------------
// 2. A task created again under the same name takes its old entry and answers with the least of
//    all its lifetimes - and coming and going a hundred times does not fill the table.
// --------------------------------------------------------------------------------------------

static void test_a_task_that_comes_and_goes_keeps_one_entry_and_its_least(void)
{
    resetReader();
    TaskWatermarkTable table;

    // Seven other tasks that live for good: the table has one entry left.
    const char* const others[] = {"loop-a", "loop-b", "loop-c", "loop-d", "loop-e", "loop-f",
                                  "loop-g"};
    for (size_t i = 0; i < 7; i++)
    {
        g_liveWatermark[handle(i)] = 4000;
        TEST_ASSERT_TRUE(table.started(others[i], handle(i)));
    }

    uint32_t least = UINT32_MAX;
    for (uint32_t lifetime = 0; lifetime < 100; lifetime++)
    {
        void* storage = handle(8 + (lifetime % 2));
        TEST_ASSERT_TRUE_MESSAGE(table.started(STORAGE_TASK, storage),
                                 "a task created again found no entry");
        // The lifetimes measure differently; one of them, in the middle, goes deepest.
        const uint32_t left = (lifetime == 37) ? 2900 : 3100 + lifetime;
        table.ended(storage, left);
        if (left < least)
        {
            least = left;
        }
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, table.untrackedCount(),
                                     "a task that comes and goes filled the table");

    uint32_t bytes = 0;
    TEST_ASSERT_TRUE(table.lowest(STORAGE_TASK, &recordingReader, bytes));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(least, bytes, "the least of the lifetimes was not kept");

    // A live lifetime that goes deeper still is what the name answers with.
    g_liveWatermark[handle(10)] = 2700;
    TEST_ASSERT_TRUE(table.started(STORAGE_TASK, handle(10)));
    TEST_ASSERT_TRUE(table.lowest(STORAGE_TASK, &recordingReader, bytes));
    TEST_ASSERT_EQUAL_UINT32(2700, bytes);
}

// --------------------------------------------------------------------------------------------
// 3. A new task starting while the one before it is still on its way out: both are read while
//    both live, and each leaves on its own.
// --------------------------------------------------------------------------------------------

static void test_two_lifetimes_that_overlap_are_both_read_and_leave_one_by_one(void)
{
    resetReader();
    TaskWatermarkTable table;
    g_liveWatermark[handle(0)] = 3000;
    g_liveWatermark[handle(1)] = 3500;

    TEST_ASSERT_TRUE(table.started(STORAGE_TASK, handle(0)));
    TEST_ASSERT_TRUE(table.started(STORAGE_TASK, handle(1)));

    uint32_t bytes = 0;
    TEST_ASSERT_TRUE(table.lowest(STORAGE_TASK, &recordingReader, bytes));
    TEST_ASSERT_EQUAL_UINT32(3000, bytes);

    table.ended(handle(0), 2990);
    resetReader();
    g_liveWatermark[handle(1)] = 3500;
    TEST_ASSERT_TRUE(table.lowest(STORAGE_TASK, &recordingReader, bytes));
    TEST_ASSERT_EQUAL_UINT32(2990, bytes);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, readsOf(handle(0)), "the task that left was read");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, readsOf(handle(1)), "the task still alive was not read");

    table.ended(handle(1), 3400);
    resetReader();
    TEST_ASSERT_TRUE(table.lowest(STORAGE_TASK, &recordingReader, bytes));
    TEST_ASSERT_EQUAL_UINT32(2990, bytes);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)g_reads.size(), "a task that left was read");
}

// --------------------------------------------------------------------------------------------
// 4. The edges: a full table counts the task it could not take and does not pretend to know it;
//    a name nobody ran is unknown, not zero.
// --------------------------------------------------------------------------------------------

static void test_a_full_table_counts_the_task_it_could_not_take(void)
{
    resetReader();
    TaskWatermarkTable table;
    const char* const names[] = {"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7"};
    for (size_t i = 0; i < TaskWatermarkTable::CAPACITY; i++)
    {
        TEST_ASSERT_TRUE(table.started(names[i], handle(i)));
    }

    TEST_ASSERT_FALSE(table.started(STORAGE_TASK, handle(20)));
    TEST_ASSERT_EQUAL_UINT32(1, table.untrackedCount());

    uint32_t bytes = 1234;
    TEST_ASSERT_FALSE_MESSAGE(table.lowest(STORAGE_TASK, &recordingReader, bytes),
                              "a task the table could not take is reported as measured");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1234, bytes, "an unknown overwrote the caller's value");

    // Leaving without having entered changes nothing.
    table.ended(handle(20), 100);
    TEST_ASSERT_FALSE(table.lowest(STORAGE_TASK, &recordingReader, bytes));
    TEST_ASSERT_FALSE_MESSAGE(table.lowest("never-run", &recordingReader, bytes),
                              "a name nobody ran is reported as measured");
}

extern "C" void run_task_watermark_table_tests(void)
{
    RUN_TEST(test_an_ended_task_is_not_read_and_answers_with_what_it_left);
    RUN_TEST(test_a_task_that_comes_and_goes_keeps_one_entry_and_its_least);
    RUN_TEST(test_two_lifetimes_that_overlap_are_both_read_and_leave_one_by_one);
    RUN_TEST(test_a_full_table_counts_the_task_it_could_not_take);
}
