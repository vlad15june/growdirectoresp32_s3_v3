// The attribution half of the RAM census: does a byte end up in the right owner's row?
//
// The census table itself is produced by the endurance bench, where the owners are real
// subsystems and nobody can say in advance what they should cost. These tests use a synthetic
// owner with an appetite decided here, so that every claim the table makes about attribution -
// exactly what was asked for, given back to the owner that paid, innermost scope wins, a peak
// that keeps its moment, a tag that follows a task across a context switch - has a case where the
// right answer is known.
//
// They allocate with new char[n], which asks the counted operator for exactly n bytes: char is
// trivially destructible, so no array cookie is added and the request is the number in the test.

#include <unity.h>

#include "Sim/HostAllocationCounter.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/VirtualClock.h"

namespace {

namespace census = HostAllocationCensus;

using hostsim::HostTaskScheduler;
using hostsim::VirtualClock;

/** Everything the case itself allocates is the harness's, not the subject's. */
const census::Owner kHarness = census::kBench;

void test_a_synthetic_owner_is_charged_exactly_what_it_asked_for(void)
{
    census::begin();

    char* first = nullptr;
    char* second = nullptr;
    {
        census::Scope owner(census::kProbe);
        first = new char[4096];
        second = new char[1024];
    }

    TEST_ASSERT_EQUAL_INT64_MESSAGE(5120, census::liveBytes(census::kProbe),
                                    "the owner was not charged what it asked for");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(2, census::liveBlocks(census::kProbe),
                                    "the owner's block count is wrong");

    delete[] first;
    TEST_ASSERT_EQUAL_INT64_MESSAGE(1024, census::liveBytes(census::kProbe),
                                    "releasing one block did not return exactly its bytes");

    delete[] second;
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::liveBytes(census::kProbe),
                                    "the owner's row did not come back to zero");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::liveBlocks(census::kProbe),
                                    "the owner's block count did not come back to zero");

    census::end();
}

void test_bytes_go_back_to_the_owner_that_paid_for_them(void)
{
    census::begin();

    char* block = nullptr;
    {
        census::Scope owner(census::kProbe);
        block = new char[2048];
    }

    // Released while somebody else is the current owner - which is what happens all the time in
    // the module: a buffer built by the catalog sync is freed on the control loop. A meter that
    // subtracted from whoever is on top of the stack would push this owner into the negative and
    // credit the other with bytes it never had.
    {
        census::Scope elsewhere(census::kRules);
        delete[] block;
    }

    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::liveBytes(census::kProbe),
                                    "the paying owner did not get its bytes back");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::liveBytes(census::kRules),
                                    "the owner that only freed was charged for it");

    census::end();
}

void test_the_innermost_scope_is_the_one_charged(void)
{
    census::begin();

    char* block = nullptr;
    {
        census::Scope outer(census::kCatalog);
        census::Scope inner(census::kJson);
        block = new char[3000];
    }

    TEST_ASSERT_EQUAL_INT64_MESSAGE(3000, census::liveBytes(census::kJson),
                                    "the inner scope was not the one charged");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::liveBytes(census::kCatalog),
                                    "the outer scope was charged for what the inner one did");

    delete[] block;
    census::end();
}

void test_a_peak_keeps_the_moment_that_made_it(void)
{
    census::begin();

    census::setMoment(1000, "quiet");
    {
        census::Scope owner(census::kProbe);
        char* small = new char[1024];
        delete[] small;
    }

    census::setMoment(2000, "the catalog arrives");
    {
        census::Scope owner(census::kProbe);
        char* big = new char[40000];
        delete[] big;
    }

    census::setMoment(3000, "quiet again");
    {
        census::Scope owner(census::kProbe);
        char* small = new char[2048];
        delete[] small;
    }

    TEST_ASSERT_EQUAL_INT64_MESSAGE(40000, census::peakBytes(census::kProbe),
                                    "the peak is not the largest the owner ever held");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(2000, census::peakAtMs(census::kProbe),
                                     "the peak was stamped with the wrong moment");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("the catalog arrives", census::peakLabel(census::kProbe),
                                     "the peak was stamped with the wrong reason");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::liveBytes(census::kProbe),
                                    "a peak that was released still shows as held");

    census::end();
}

void test_the_module_total_leaves_the_harness_out(void)
{
    census::begin();

    char* mine = nullptr;
    char* theirs = nullptr;
    {
        census::Scope owner(census::kRules);
        mine = new char[5000];
    }
    {
        census::Scope owner(kHarness);
        theirs = new char[7000];
    }

    TEST_ASSERT_EQUAL_INT64_MESSAGE(5000, census::moduleLiveBytes(),
                                    "the module total counted the harness's own bytes");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(7000, census::liveBytes(kHarness),
                                    "the harness's bytes went missing instead of into its row");

    delete[] mine;
    delete[] theirs;
    census::end();
}

void test_what_is_allocated_outside_the_census_is_not_charged_to_anyone(void)
{
    char* before = new char[8192];

    census::begin();
    delete[] before;

    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::moduleLiveBytes(),
                                    "a block from before the census moved a bucket on release");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::liveBytes(census::kOther),
                                    "a block from before the census was charged to somebody");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        0, census::unpairedReleases(),
        "an untagged block was counted as a release from an earlier census");

    census::end();
}

void test_a_block_tagged_by_a_previous_census_is_counted_apart(void)
{
    census::begin();
    char* fromTheFirstRun = nullptr;
    {
        census::Scope owner(census::kProbe);
        fromTheFirstRun = new char[6000];
    }
    census::end();

    // The second run of the bench in the same process: its buckets start at zero, and the first
    // run's blocks are still being released into them. Subtracting there would make the second
    // table smaller than the module actually is - by an amount decided by the first run.
    census::begin();
    delete[] fromTheFirstRun;

    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::liveBytes(census::kProbe),
                                    "the previous run's bytes were subtracted from this run");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, census::unpairedReleases(),
                                     "the release of a foreign block was not counted apart");

    census::end();
}

void test_the_owner_of_a_task_survives_the_switch_between_tasks(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);

    char* heldByOutbox = nullptr;
    char* heldByOutboxLater = nullptr;
    char* heldBySensors = nullptr;

    census::begin();
    {
        // Creating the tasks is the harness's own cost: fiber stacks, names, the task records.
        census::Scope harness(kHarness);
        scheduler.setTaskCensusOwner("outbox-net", census::kOutbox);
        scheduler.setTaskCensusOwner("sensor-acq", census::kSensors);

        scheduler.createTask("outbox-net", 256 * 1024, 1, [&]() {
            // A scope entered before the task gives up the processor and closed after it comes
            // back: the tag has to be parked with the task, not left standing in the process.
            census::Scope owner(census::kJson);
            heldByOutbox = new char[1500];
            scheduler.delayMs(20);
            heldByOutboxLater = new char[2500];
        });

        scheduler.createTask("sensor-acq", 256 * 1024, 1, [&]() {
            scheduler.delayMs(10);
            heldBySensors = new char[900];
            scheduler.delayMs(1000);
        });
    }

    scheduler.runForMs(100);

    TEST_ASSERT_NOT_NULL_MESSAGE(heldByOutboxLater, "the outbox task never resumed");
    TEST_ASSERT_NOT_NULL_MESSAGE(heldBySensors, "the acquisition task never ran");

    TEST_ASSERT_EQUAL_INT64_MESSAGE(
        4000, census::liveBytes(census::kJson),
        "the scope the outbox task held across a context switch did not charge both allocations");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(
        900, census::liveBytes(census::kSensors),
        "what the acquisition task allocated was not charged to the acquisition task");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(
        0, census::liveBytes(census::kOutbox),
        "the outbox task's base tag was charged although a scope was open over it");

    delete[] heldByOutbox;
    delete[] heldByOutboxLater;
    delete[] heldBySensors;
    census::end();
}

void test_the_nesting_the_bench_uses_fits_the_tag_stack(void)
{
    census::begin();
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, census::overflowedPushes(),
                                     "the census started with pushes already lost");

    {
        census::Scope a(census::kSensors);
        census::Scope b(census::kOutbox);
        census::Scope c(census::kJson);
        census::Scope d(census::kRules);
        census::Scope e(census::kCatalog);
        census::Scope f(census::kCommands);
        char* block = new char[512];
        TEST_ASSERT_EQUAL_INT64_MESSAGE(512, census::liveBytes(census::kCommands),
                                        "six nested scopes already lose the innermost tag");
        delete[] block;
    }

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        0, census::overflowedPushes(),
        "the deepest nesting the bench uses does not fit the tag stack");
    census::end();
}

/**
 * @brief A member list, tagged the only way a member list can be tagged.
 *
 * The shape under test is the fixture's: a graph whose members are built before any constructor
 * body runs, some of them production objects that reserve their buffers up front. The markers
 * have to bracket exactly the members that follow them, and they have to unwind correctly when
 * the object is destroyed, because destruction runs in the reverse order.
 */
struct GraphWithPhases {
    census::PhaseMarker phaseHarness{census::kBench};
    char* standIn;

    census::PhaseMarker phaseRules{census::kRules};
    char* ruleBuffer;

    census::PhaseMarker phaseCommands{census::kCommands};
    char* commandBuffer;

    GraphWithPhases()
        : standIn(new char[1000]), ruleBuffer(new char[2000]), commandBuffer(new char[3000])
    {
    }

    ~GraphWithPhases()
    {
        delete[] commandBuffer;
        delete[] ruleBuffer;
        delete[] standIn;
    }
};

void test_a_member_list_is_charged_by_the_phase_it_was_declared_in(void)
{
    census::begin();

    GraphWithPhases* graph = nullptr;
    {
        // The scope names the default. Everything the markers do not claim stays here - which is
        // exactly the mistake this test exists to catch: with no markers the whole graph, the
        // production buffers included, would be this one number.
        census::Scope harness(census::kBench);
        graph = new GraphWithPhases();
    }

    TEST_ASSERT_EQUAL_INT64_MESSAGE(2000, census::liveBytes(census::kRules),
                                    "a member declared after a phase marker was not charged to it");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(3000, census::liveBytes(census::kCommands),
                                    "the second phase did not take over from the first");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(
        5000, census::moduleLiveBytes(),
        "the production members of the graph were built on the harness's account");

    // The object itself and the member the harness claimed - and nothing else.
    TEST_ASSERT_EQUAL_INT64_MESSAGE(
        1000 + (long long)sizeof(GraphWithPhases), census::liveBytes(census::kBench),
        "the harness was charged for something the markers had already claimed");

    // Destruction runs in reverse; the markers have to unwind with it or the buckets end the run
    // holding what the object no longer does.
    {
        census::Scope harness(census::kBench);
        delete graph;
    }
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, census::moduleLiveBytes(),
                                    "the graph was destroyed and its owners kept the bytes");

    census::end();
}

}  // namespace

extern "C" void run_ram_census_tests(void)
{
    RUN_TEST(test_a_synthetic_owner_is_charged_exactly_what_it_asked_for);
    RUN_TEST(test_bytes_go_back_to_the_owner_that_paid_for_them);
    RUN_TEST(test_the_innermost_scope_is_the_one_charged);
    RUN_TEST(test_a_peak_keeps_the_moment_that_made_it);
    RUN_TEST(test_the_module_total_leaves_the_harness_out);
    RUN_TEST(test_what_is_allocated_outside_the_census_is_not_charged_to_anyone);
    RUN_TEST(test_a_block_tagged_by_a_previous_census_is_counted_apart);
    RUN_TEST(test_the_owner_of_a_task_survives_the_switch_between_tasks);
    RUN_TEST(test_the_nesting_the_bench_uses_fits_the_tag_stack);
    RUN_TEST(test_a_member_list_is_charged_by_the_phase_it_was_declared_in);
}
