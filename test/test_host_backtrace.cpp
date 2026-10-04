// A stack trace taken from a simulated task has to be readable, and it has to end where the task
// began.
//
// Every task in this harness runs on a fiber, so every trace the allocation meter takes is taken
// on a fiber stack. The platform backtrace() cannot do that: it needs the frame descriptions the
// compiler emits, and a fiber's outermost frame is built by makecontext, which leaves none. What
// that costs is not theoretical: on macOS it is silence, and on Linux it is a trace that runs one
// frame past the fiber entry - or, on the 8 KB stacks the firmware's own tasks use, a dead process,
// because the unwinder wants stack the fiber does not have. Hence the exact depth asserted here,
// and the same number on both platforms.

#include <unity.h>

#include <cstddef>

#include "Sim/HostBacktrace.h"
#include "Sim/HostFiber.h"

namespace {

const int kMaxFrames = 64;

void* g_frames[kMaxFrames];
int g_depth = -1;

// A chain of frames of known length. noinline because the point is to count frames: an inlined
// call is not a frame, and then the test would be measuring the optimiser.
__attribute__((noinline)) void captureHere()
{
    g_depth = hostsim::captureBacktrace(g_frames, kMaxFrames);
}

__attribute__((noinline)) void chainOf1() { captureHere(); }
__attribute__((noinline)) void chainOf2() { chainOf1(); }
__attribute__((noinline)) void chainOf3() { chainOf2(); }
__attribute__((noinline)) void chainOf4() { chainOf3(); }

void runChainOf1(void*) { chainOf1(); }
void runChainOf4(void*) { chainOf4(); }

int depthOnFiber(hostsim::Fiber::Entry entry)
{
    g_depth = -1;
    hostsim::Fiber fiber(entry, nullptr, 64 * 1024);
    fiber.resume();
    return g_depth;
}

// The frames a trace taken inside runChainOf1() must have, outwards from the capture:
// captureHere, chainOf1, runChainOf1, and the fiber trampoline it was entered on. Nothing below
// that: what the trampoline returns into is makecontext's own starter, named differently on each
// platform, so the trace ends above it and this number is the same everywhere.
const int kFramesForChainOf1 = 4;

void test_a_trace_taken_on_a_fiber_reaches_the_fiber_entry_and_stops_there(void)
{
    const int depth = depthOnFiber(runChainOf1);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        kFramesForChainOf1, depth,
        "a trace taken on a fiber stack did not stop at the fiber entry: the platform unwinder "
        "either gives up on a makecontext stack and returns nothing, or walks off the end of it");

    for (int i = 0; i < depth; i++)
    {
        TEST_ASSERT_NOT_NULL_MESSAGE(g_frames[i], "a captured frame has no return address");
    }
}

void test_a_trace_counts_one_entry_per_real_frame(void)
{
    const int shallow = depthOnFiber(runChainOf1);
    const int deep = depthOnFiber(runChainOf4);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        3, deep - shallow,
        "three more calls did not produce three more frames - the walk is not following the "
        "frame chain");
}

void test_a_trace_on_the_thread_stack_still_works(void)
{
    g_depth = -1;
    chainOf1();

    TEST_ASSERT_TRUE_MESSAGE(g_depth >= 2,
                             "a trace taken off a fiber, on the thread stack, came back empty");
    TEST_ASSERT_TRUE_MESSAGE(g_depth < kMaxFrames,
                             "the walk on the thread stack did not terminate on its own");
}

void test_two_fibers_of_the_same_shape_trace_to_the_same_depth(void)
{
    const int first = depthOnFiber(runChainOf4);
    const int second = depthOnFiber(runChainOf4);

    TEST_ASSERT_EQUAL_INT_MESSAGE(first, second,
                                  "the same call chain on two fibers traced to different depths: "
                                  "the walk is ending on something other than the fiber entry");
}

}  // namespace

extern "C" void run_host_backtrace_tests(void)
{
    RUN_TEST(test_a_trace_taken_on_a_fiber_reaches_the_fiber_entry_and_stops_there);
    RUN_TEST(test_a_trace_counts_one_entry_per_real_frame);
    RUN_TEST(test_a_trace_on_the_thread_stack_still_works);
    RUN_TEST(test_two_fibers_of_the_same_shape_trace_to_the_same_depth);
}
