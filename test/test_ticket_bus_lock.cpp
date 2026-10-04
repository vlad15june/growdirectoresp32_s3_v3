// The RS-485 lock itself - the production class, not a model of it.
//
// This file exists because of a defect and the way that defect got through. The first version of
// the bus lock identified its owner with `std::this_thread::get_id()`, which on arduino-esp32 is
// `pthread_self()`, which asserts when the calling task was not created by `pthread_create`.
// Neither task that takes this lock is: `sensor-acq` comes from `xTaskCreatePinnedToCore()` and
// the Arduino `loopTask` comes from the core. The module aborted on its first Modbus transaction,
// inside setupModule(), before ever reaching loop().
//
// Six hundred and ninety-two green tests said nothing about it, because the host ran a *second
// implementation* of the ticket algorithm and the production class was compiled into no test at
// all. So the algorithm was pulled into one class - `TicketBusLock`, plain C++ - and everything
// platform-specific went behind four calls (`IBusLockPlatform`). What runs below is the class
// `globals.h` constructs on the device. What is left untested on the host is
// `FreeRtosBusLockPlatform`: an identity, a mutex and a counting semaphore, and no algorithm.
//
// `hostsim::HostBusLock` used in the other files is a *decorator* over this same class, added for
// its bookkeeping, not a second implementation.

#include <unity.h>

#include "Infrastructure/Scheduling/TicketBusLock.h"

#include "Sim/HostBusLock.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::HostBusLockPlatform;
using hostsim::HostTaskScheduler;
using hostsim::VirtualClock;

namespace {

struct LockFixture {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    HostBusLockPlatform platform;
    TicketBusLock lock;

    LockFixture() : scheduler(clock), platform(scheduler, clock), lock(&platform) {}
};

} // namespace

// --------------------------------------------------------------------------------------------
// The property the whole bus-lock bound rests on: first come, first served.
// --------------------------------------------------------------------------------------------

static void test_the_line_is_granted_in_the_order_it_was_asked_for(void)
{
    LockFixture f;
    std::vector<std::string> order;

    // The holder keeps the line for 100 ms, long enough for both others to queue behind it, and
    // then asks again immediately - which is exactly what the acquisition task does between two
    // channels, and exactly what a barging lock lets it win.
    f.scheduler.createTask("holder", 256 * 1024, 1, [&]() {
        f.lock.lock();
        order.push_back("holder");
        f.scheduler.delayMs(100);
        f.lock.unlock();
        f.lock.lock();
        order.push_back("holder-again");
        f.lock.unlock();
        f.scheduler.delayMs(100000);
    });

    // Asks second.
    f.scheduler.createTask("second", 256 * 1024, 1, [&]() {
        f.scheduler.delayMs(10);
        f.lock.lock();
        order.push_back("second");
        f.lock.unlock();
        f.scheduler.delayMs(100000);
    });

    // Asks third.
    f.scheduler.createTask("third", 256 * 1024, 1, [&]() {
        f.scheduler.delayMs(20);
        f.lock.lock();
        order.push_back("third");
        f.lock.unlock();
        f.scheduler.delayMs(100000);
    });

    f.scheduler.runForMs(2000);

    TEST_ASSERT_FALSE_MESSAGE(f.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_FALSE_MESSAGE(f.platform.deadlockDetected(),
                              "a waiter could never have been served - a fault in the test");

    TEST_ASSERT_EQUAL_INT_MESSAGE(4, (int)order.size(),
                                  "not everyone who asked for the line got it");

    char message[256];
    snprintf(message, sizeof(message),
             "the line was granted in the order [%s, %s, %s, %s]; the holder asking again after "
             "releasing must not overtake the two that were already waiting",
             order[0].c_str(), order[1].c_str(), order[2].c_str(), order[3].c_str());

    TEST_ASSERT_EQUAL_STRING_MESSAGE("holder", order[0].c_str(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("second", order[1].c_str(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("third", order[2].c_str(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("holder-again", order[3].c_str(), message);
}

// --------------------------------------------------------------------------------------------
// Reentrancy: writeSensorAddress() and scanModbusSensor() hold the line across a sequence and
// call nested methods that take it again. Without this they would deadlock against themselves.
// --------------------------------------------------------------------------------------------

static void test_an_owner_may_take_the_line_it_already_holds(void)
{
    LockFixture f;
    std::vector<std::string> order;
    bool innerReached = false;

    f.scheduler.createTask("sequence", 256 * 1024, 1, [&]() {
        f.lock.lock();          // the sequence takes the line
        f.lock.lock();          // a nested transaction takes it again
        innerReached = true;
        f.scheduler.delayMs(50);
        f.lock.unlock();        // the nested transaction is done...
        f.scheduler.delayMs(50); // ...but the sequence still holds the line
        order.push_back("sequence");
        f.lock.unlock();
        f.scheduler.delayMs(100000);
    });

    f.scheduler.createTask("other", 256 * 1024, 1, [&]() {
        f.scheduler.delayMs(10);
        f.lock.lock();
        order.push_back("other");
        f.lock.unlock();
        f.scheduler.delayMs(100000);
    });

    f.scheduler.runForMs(2000);

    TEST_ASSERT_FALSE_MESSAGE(f.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(innerReached,
                             "the owner deadlocked taking a lock it already held");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)order.size(), "somebody never got the line");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("sequence", order[0].c_str(),
                                     "the inner unlock released the line early: another task got "
                                     "in while the sequence still held it");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("other", order[1].c_str(), "the waiter never got the line");

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, f.lock.reentrantTakes(),
                                     "the nested take was not recognised as reentrant");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(2, f.lock.takes(),
                                     "the line changed hands a different number of times");
    TEST_ASSERT_FALSE_MESSAGE(f.lock.held(), "the line was left held at the end of the test");
}

// --------------------------------------------------------------------------------------------
// A waiter is actually released, not left to time out. This is what wakeWaiters() is for on the
// device; on the host the platform polls, so what is asserted is that the production class asked.
// --------------------------------------------------------------------------------------------

static void test_releasing_the_line_wakes_the_task_waiting_for_it(void)
{
    LockFixture f;
    uint64_t secondAcquiredAtMs = 0;

    f.scheduler.createTask("holder", 256 * 1024, 1, [&]() {
        f.lock.lock();
        f.scheduler.delayMs(200);
        f.lock.unlock();
        f.scheduler.delayMs(100000);
    });

    f.scheduler.createTask("waiter", 256 * 1024, 1, [&]() {
        f.scheduler.delayMs(10);
        f.lock.lock();
        secondAcquiredAtMs = f.clock.nowMs();
        f.lock.unlock();
        f.scheduler.delayMs(100000);
    });

    f.scheduler.runForMs(2000);

    TEST_ASSERT_TRUE_MESSAGE(secondAcquiredAtMs > 0, "the waiter never got the line at all");
    TEST_ASSERT_TRUE_MESSAGE(f.platform.wakeCalls() > 0,
                             "the lock never asked the platform to release its waiters: on the "
                             "device that is the semaphore give, and without it a waiter only "
                             "moves when its wait times out");

    char message[192];
    snprintf(message, sizeof(message),
             "the waiter got the line at t=%lu ms; the holder let go at t=200 ms",
             (unsigned long)secondAcquiredAtMs);

    // Within the platform's 1 ms polling granularity of the moment the holder let go.
    TEST_ASSERT_TRUE_MESSAGE(secondAcquiredAtMs >= 200 && secondAcquiredAtMs <= 202, message);
}

extern "C" void run_ticket_bus_lock_tests(void)
{
    printf("\n----- Running Ticket Bus Lock Tests -----\n");

    RUN_TEST(test_the_line_is_granted_in_the_order_it_was_asked_for);
    RUN_TEST(test_an_owner_may_take_the_line_it_already_holds);
    RUN_TEST(test_releasing_the_line_wakes_the_task_waiting_for_it);

    printf("\n----- Ticket Bus Lock Tests Complete -----\n");
}
