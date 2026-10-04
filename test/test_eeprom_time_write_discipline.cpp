// The stored time seed is a bridge over a reboot, not a clock - and it is paid for in flash.
//
// The board has no battery-backed RTC. After a reset the system clock is at zero until a server
// answers, and the rules that switch relays by time of day are already running, so the module
// keeps a copy of the last known time in EEPROM and starts from it. Every refresh of that copy is
// a full EEPROM commit - an erase and reprogram of the settings sector - so the question these
// tests answer is not whether the seed is written but how often, and against what.
//
// The seed is written when it has aged past the refresh interval - five minutes, the pace at
// which a time server answers - off schedule whenever the clock actually steps, and
// unconditionally on the way into a planned restart, which is the one moment the seed is about to
// be read. The refresh interval is what bounds how far behind the module comes back from a power
// cut it takes with the network down.
//
// ### What the stand is
//
// The real storage path, wired the way globals.h wires it: the production NTPTimeAdapter over
// the production Settings over the production EepromAdapter over a host EEPROM device that
// counts commits. Nothing between the SNTP notification and the flash write is a stand-in, so
// the numbers below are commits of the settings sector, not calls of an interface.
//
// ### Reproducing the red
//
// - The schedule: in NTPTimeAdapter::onTimeSynchronized(), call saveTimeToStorage() directly
//   again instead of going through the policy. At the interval a settled module runs on, writing
//   always and writing on schedule cost the same 288 commits a day, so the tests that tell them
//   apart are the ones where answers come faster than the seed ages - the sub-interval test, the
//   aggressive-interval test, the boundary test and the ordinary-correction test. The step tests
//   still pass, which is the point: writing always also writes on a step.
// - The restart paths: delete the saveTimeToStorage() call in OnChangeModuleModeUseCase::restart()
//   and in ModuleCommandExecutionUseCase::handleReboot(). Each planned-restart test then reports
//   zero commits before the reset.

#include <unity.h>

#include <cstring>
#include <ctime>
#include <memory>

// Arduino.h stand-in first: the services below pull in headers that expect it (env:native).
#include "Mock/ArduinoMock.h"

#include "Infrastructure/Configuration/Settings.h"
#include "Infrastructure/Adapters/Time/NTPTimeAdapter.h"

#include "Application/Services/Net/LinkSupervisor.h"
#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Core/Domain/UseCases/Diagnostics/PreventiveRestartUseCase.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/OnChangeModuleModeUseCase.h"
#include "Core/Domain/UseCases/Sensors/SensorCommandUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"

#include "Mock/Core/Domain/MockFirmwareRemoteUpdate.h"
#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Logging/MockBootEventLog.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/UseCases/Rules/MockEvaluateRulesUseCase.h"

namespace {

// EEPROM_ADDRES_LOCAL_TIME in Settings - the cell the seed has always lived in. Named here so a
// test can look at the bytes without asking Settings to read them back.
const int SEED_ADDRESS = 296;

// A quiet afternoon in the middle of a month, far from any month or year boundary: the seed
// arithmetic is civil-time arithmetic, and a test should fail because of the schedule, not
// because it stepped over a boundary the test author did not think about.
const int BOOT_YEAR = 2026;
const int BOOT_MONTH = 8;
const int BOOT_DAY = 12;
const int BOOT_HOUR = 14;

// The age at which the seed is refreshed, spelled out here instead of read from
// StoredTimeSeedPolicy: a change of the policy constant has to change what these tests report,
// not what they compare against.
const unsigned long SEED_REFRESH_SEC = 300;

// A cadence at which answers come in faster than the seed ages. A test that wants to watch the
// step detector alone has to use it: at the interval a settled module runs on, every answer is
// also a planned refresh, and a commit would say nothing about which of the two caused it.
const unsigned long SYNC_INSIDE_INTERVAL_SEC = 60;

struct tm makeTime(int year, int month, int day, int hour, int minute, int second)
{
    struct tm value;
    memset(&value, 0, sizeof(value));
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    return value;
}

/**
 * @brief Arduino adapter that photographs the stored seed at the instant the module resets.
 *
 * "The seed is written before the reboot" is a statement about order, and the only way to check
 * an order from outside is to look at the flash at the moment the reset is asked for. Counting
 * commits would not do: the paths into a restart write other settings too.
 */
class RestartWatch : public MockArduinoAdapter
{
public:
    bool sawReset = false;
    struct tm seedAtReset;

    RestartWatch() { memset(&seedAtReset, 0, sizeof(seedAtReset)); }

    void restart() override
    {
        photographSeed();
        MockArduinoAdapter::restart();
    }

    void hardReset() override
    {
        photographSeed();
        MockArduinoAdapter::hardReset();
    }

private:
    void photographSeed()
    {
        sawReset = true;
        memcpy(&seedAtReset, EEPROM.cells() + SEED_ADDRESS, sizeof(struct tm));
    }
};

/**
 * @brief The time adapter over its real storage path: Settings, EepromAdapter, flash device.
 *
 * One instance at a time: NTPTimeAdapter keeps a static pointer to the instance the SNTP
 * callback belongs to, exactly as the device does.
 */
struct TimeSeedStand
{
    MockLogger logger;
    MockDebugSystem debugSystem;
    EepromAdapter eeprom;
    Settings settings;
    RestartWatch arduino;
    NTPTimeAdapter ntp;

    TimeSeedStand()
        : eeprom(1024, 0),
          settings(eeprom, &logger, &debugSystem),
          ntp(&logger, &debugSystem, &settings, &arduino)
    {
        debugSystem.featureEnabled = false;
    }

    /** @brief Uptime in milliseconds, the only clock the adapter can trust across a time step. */
    void setUptimeSeconds(unsigned long seconds)
    {
        arduino.setMillis(seconds * 1000UL);
    }
};

std::unique_ptr<TimeSeedStand> g_stand;

/**
 * @brief Power on: erased flash, no system time, SNTP not yet started.
 *
 * Returns with the module booted and running - EEPROM statistics reset after the boot, so every
 * number a test reports is a cost of what the test itself did.
 */
void bootModule(const struct tm& wallClock)
{
    g_stand.reset();
    hostSntpReset();
    EEPROM.erase();
    hostWallClockInvalidate();

    g_stand.reset(new TimeSeedStand());
    g_stand->settings.begin();
    g_stand->setUptimeSeconds(0);

    hostWallClockSet(wallClock);
    g_stand->ntp.setup();

    EEPROM.resetStats();
}

/** @brief One SNTP notification `seconds` of wall clock and uptime after the previous one. */
void syncAfterSeconds(unsigned long seconds)
{
    hostWallClockAdvance(static_cast<long>(seconds));
    g_stand->arduino.setMillis(g_stand->arduino.currentMillis + seconds * 1000UL);
    TEST_ASSERT_TRUE_MESSAGE(hostSntpNotifySync(),
        "NTPTimeAdapter::setup() left no callback registered, so nothing delivers a sync");
}

/** @brief Time passes with no server answering: uptime and clock advance, nothing is stored. */
void driftMinutes(unsigned long minutes)
{
    hostWallClockAdvance(static_cast<long>(minutes * 60UL));
    g_stand->arduino.setMillis(g_stand->arduino.currentMillis + minutes * 60UL * 1000UL);
}

/**
 * @brief A sync in which the clock steps: uptime advances by `seconds`, the clock by
 *        `seconds + stepSeconds`. A negative step moves the clock back.
 */
void syncWithClockStep(unsigned long seconds, long stepSeconds)
{
    hostWallClockAdvance(static_cast<long>(seconds) + stepSeconds);
    g_stand->arduino.setMillis(g_stand->arduino.currentMillis + seconds * 1000UL);
    TEST_ASSERT_TRUE(hostSntpNotifySync());
}

/** @brief Seconds between SNTP notifications, as NTPTimeAdapter::setup() configures them. */
unsigned long syncIntervalSeconds()
{
    TEST_ASSERT_TRUE_MESSAGE(hostSntpSyncIntervalMs > 0,
        "NTPTimeAdapter::setup() did not configure an SNTP sync interval");
    return hostSntpSyncIntervalMs / 1000UL;
}

/** @brief The seed as it lies in EEPROM, read through the production path. */
struct tm storedSeed()
{
    return g_stand->settings.getLocalTime();
}

} // namespace

// --- The schedule -------------------------------------------------------------------------

void test_a_day_of_ntp_syncs_costs_one_eeprom_commit_per_refresh_interval(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    const unsigned long interval = syncIntervalSeconds();
    const unsigned long syncsPerDay = 24UL * 3600UL / interval;

    for (unsigned long i = 0; i < syncsPerDay; ++i)
    {
        syncAfterSeconds(interval);
    }

    // The five-minute interval NTPTimeAdapter::setup() configures is 288 answers a day.
    TEST_ASSERT_EQUAL_MESSAGE(288, syncsPerDay,
        "the configured sync interval is no longer five minutes, so the numbers below mean "
        "something else than they say");

    // A settled module answers on the same period the seed ages out on, so in this run every
    // answer refreshes the seed: 288 commits a day, which the flash of this board absorbs for
    // well over a decade. What the schedule does is bound the cost when answers come faster -
    // see the tests below.
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(290, EEPROM.commitCalls,
        "a day of NTP syncs with a steady clock committed the EEPROM more than 290 times");
    TEST_ASSERT_EQUAL_MESSAGE(288, EEPROM.commitCalls,
        "expected one commit for each of the 288 answers of a day, the first of them the boot");

    // The seed is current: the module would come back within a refresh interval of where it was.
    struct tm stored = storedSeed();
    struct tm now = hostWallClockGet();
    struct tm storedCopy = stored;
    struct tm nowCopy = now;
    long age = static_cast<long>(timegm(&nowCopy) - timegm(&storedCopy));
    TEST_ASSERT_TRUE_MESSAGE(age >= 0 && age <= static_cast<long>(SEED_REFRESH_SEC),
        "the stored seed is more than five minutes behind the clock");
}

void test_a_day_on_the_aggressive_sync_interval_costs_the_same(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    // What LinkSupervisor asks for every time the network comes back: SNTP restarted with a
    // thirty-second interval to catch up with. The answers below keep arriving at that rate for a
    // whole day, which is not what the module does any more - what is under test is that the cost
    // of the seed does not follow the rate at which answers arrive, whatever that rate is.
    g_stand->ntp.forceSyncTime();

    const unsigned long interval = syncIntervalSeconds();
    TEST_ASSERT_LESS_THAN_MESSAGE(300, interval,
        "forceSyncTime() no longer shortens the sync interval, so this test proves nothing");

    const unsigned long syncsPerDay = 24UL * 3600UL / interval;
    for (unsigned long i = 0; i < syncsPerDay; ++i)
    {
        syncAfterSeconds(interval);
    }

    // The cost of the seed is a property of the seed, not of how often a server answers: 2880
    // answers a day still buy the 288 refreshes the schedule allows.
    TEST_ASSERT_EQUAL_MESSAGE(288, EEPROM.commitCalls,
        "the number of EEPROM commits followed the sync interval instead of the schedule");
}

void test_a_sync_before_the_seed_ages_out_leaves_it_alone(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    // The first sync after a boot writes: the seed in flash is from before the reset.
    syncAfterSeconds(syncIntervalSeconds());
    TEST_ASSERT_EQUAL_MESSAGE(1, EEPROM.commitCalls,
        "the first sync after a boot must refresh the seed");
    struct tm afterFirst = storedSeed();

    // Four answers a minute apart, all of them inside one refresh interval.
    EEPROM.resetStats();
    for (unsigned long elapsed = SYNC_INSIDE_INTERVAL_SEC; elapsed < SEED_REFRESH_SEC;
         elapsed += SYNC_INSIDE_INTERVAL_SEC)
    {
        syncAfterSeconds(SYNC_INSIDE_INTERVAL_SEC);
    }

    TEST_ASSERT_EQUAL_MESSAGE(0, EEPROM.commitCalls,
        "syncs inside one refresh interval committed the EEPROM");
    TEST_ASSERT_EQUAL_MESSAGE(0, EEPROM.writeCalls,
        "syncs inside one refresh interval wrote EEPROM cells");

    struct tm unchanged = storedSeed();
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&afterFirst, &unchanged, sizeof(struct tm),
        "the stored seed changed without a commit");
}

void test_the_seed_is_refreshed_once_it_is_five_minutes_old(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    syncAfterSeconds(syncIntervalSeconds());
    EEPROM.resetStats();

    // One second short of the refresh interval: the seed still describes this clock closely
    // enough, and the answer costs nothing.
    syncAfterSeconds(SEED_REFRESH_SEC - 1);
    TEST_ASSERT_EQUAL_MESSAGE(0, EEPROM.commitCalls,
        "the seed was refreshed a second before it aged out");

    // And one second later it is exactly a refresh interval old, which is what the schedule
    // writes on.
    syncAfterSeconds(1);
    TEST_ASSERT_EQUAL_MESSAGE(1, EEPROM.commitCalls,
        "five minutes after the last write the seed was not refreshed");
}

void test_a_power_cut_with_the_network_down_comes_back_five_minutes_behind_at_worst(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    // An hour of ordinary running, a server answering on the configured interval.
    const unsigned long interval = syncIntervalSeconds();
    for (unsigned long i = 0; i < 3600UL / interval; ++i)
    {
        syncAfterSeconds(interval);
    }

    // Two minutes into the next interval nobody has answered yet, and the power goes.
    driftMinutes(2);
    struct tm atThePowerCut = hostWallClockGet();

    // Back up with the network still down: RAM and the system clock are gone, the EEPROM cells
    // are not, and no server will answer. Everything the module knows about the time is the seed.
    g_stand.reset();
    hostSntpReset();
    hostWallClockInvalidate();
    EEPROM.resetStats();

    g_stand.reset(new TimeSeedStand());
    g_stand->settings.begin();
    g_stand->ntp.setup();

    struct tm afterBoot;
    TEST_ASSERT_TRUE_MESSAGE(g_stand->ntp.getLocalTime(&afterBoot),
        "the module came back with no time at all: the seed was not restored");

    struct tm cutCopy = atThePowerCut;
    struct tm bootCopy = afterBoot;
    long behind = static_cast<long>(timegm(&cutCopy) - timegm(&bootCopy));
    TEST_ASSERT_TRUE_MESSAGE(behind >= 0 && behind <= static_cast<long>(SEED_REFRESH_SEC),
        "the module came back more than five minutes behind the time it was switched off at");
}

// --- The clock steps ----------------------------------------------------------------------

void test_a_clock_step_forward_is_stored_at_once(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    const unsigned long interval = syncIntervalSeconds();
    syncAfterSeconds(interval);
    EEPROM.resetStats();

    // Fifteen minutes ahead - the smallest offset step any timezone uses. A step this size also
    // leaves the seed describing a time more than a refresh interval ago, so either rule pays for
    // the write; what this test pins is that the seed that lands in flash is the time after the
    // step. The step alone is watched by the test below.
    syncWithClockStep(SYNC_INSIDE_INTERVAL_SEC, 15 * 60);

    TEST_ASSERT_EQUAL_MESSAGE(1, EEPROM.commitCalls,
        "the clock jumped a quarter of an hour forward and the seed was not written");

    struct tm stored = storedSeed();
    TEST_ASSERT_EQUAL_MESSAGE(BOOT_HOUR, stored.tm_hour,
        "the stored seed is not the time after the step");
    // Five minutes of running to the first sync, then one more and the quarter-hour step.
    TEST_ASSERT_EQUAL_MESSAGE(21, stored.tm_min,
        "the stored seed is not the time after the step");
}

void test_a_forward_step_too_small_to_age_the_seed_is_stored_at_once(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    syncAfterSeconds(syncIntervalSeconds());
    EEPROM.resetStats();

    // Two minutes ahead: what a server corrects a module to after it has been offline long enough
    // for the crystal to wander. The seed is a minute old at that point, so the schedule has
    // nothing to say and the write can only come from the step detector.
    syncWithClockStep(SYNC_INSIDE_INTERVAL_SEC, 2 * 60);

    TEST_ASSERT_EQUAL_MESSAGE(1, EEPROM.commitCalls,
        "the clock was set two minutes forward and the seed was not written");

    struct tm stored = storedSeed();
    TEST_ASSERT_EQUAL_MESSAGE(BOOT_HOUR, stored.tm_hour,
        "the stored seed is not the time after the step");
    // Five minutes of running to the first sync, then one more and the two-minute step.
    TEST_ASSERT_EQUAL_MESSAGE(8, stored.tm_min,
        "the stored seed is not the time after the step");
}

void test_a_clock_step_backward_is_stored_at_once(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    const unsigned long interval = syncIntervalSeconds();
    syncAfterSeconds(interval);
    EEPROM.resetStats();

    // Back an hour, the other half of a daylight-saving change.
    syncWithClockStep(interval, -3600);

    TEST_ASSERT_EQUAL_MESSAGE(1, EEPROM.commitCalls,
        "the clock jumped an hour back and the seed was not written");

    struct tm stored = storedSeed();
    TEST_ASSERT_EQUAL_MESSAGE(BOOT_HOUR - 1, stored.tm_hour,
        "the stored seed is not the time after the step");
}

void test_an_ntp_correction_smaller_than_the_threshold_is_not_stored(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    const unsigned long interval = syncIntervalSeconds();
    syncAfterSeconds(interval);
    EEPROM.resetStats();

    // What a real correction looks like: the board's oscillator drifts by seconds a day, and the
    // server pulls it back. Nothing about the seed changes, and it must not cost a commit - so
    // the corrections arrive before the seed ages out, where the schedule cannot pay for them.
    syncWithClockStep(SYNC_INSIDE_INTERVAL_SEC, 30);
    syncWithClockStep(SYNC_INSIDE_INTERVAL_SEC, -20);

    TEST_ASSERT_EQUAL_MESSAGE(0, EEPROM.commitCalls,
        "an ordinary NTP correction committed the EEPROM");
}

// --- The stored format --------------------------------------------------------------------

void test_the_seed_keeps_the_eeprom_layout_it_has_always_had(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    syncAfterSeconds(syncIntervalSeconds());

    struct tm expected = hostWallClockGet();
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&expected, EEPROM.cells() + SEED_ADDRESS, sizeof(struct tm),
        "the bytes at the local-time address are not the struct tm the clock reported");

    struct tm readBack = storedSeed();
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&expected, &readBack, sizeof(struct tm),
        "Settings::getLocalTime() no longer reads back what setLocalTime() stored");
}

void test_a_reboot_seeds_the_clock_from_the_stored_time(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));
    syncAfterSeconds(syncIntervalSeconds());
    struct tm seeded = storedSeed();

    // The reset: the adapter and the system clock go, the EEPROM cells stay.
    g_stand.reset();
    hostSntpReset();
    hostWallClockInvalidate();
    EEPROM.resetStats();

    g_stand.reset(new TimeSeedStand());
    g_stand->settings.begin();
    g_stand->ntp.setup();

    struct tm afterBoot;
    TEST_ASSERT_TRUE_MESSAGE(g_stand->ntp.getLocalTime(&afterBoot),
        "after the reboot the module has no time at all: the seed was not restored");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&seeded, &afterBoot, sizeof(struct tm),
        "the clock was not seeded with the stored time");
    TEST_ASSERT_EQUAL_MESSAGE(0, EEPROM.commitCalls,
        "restoring the seed on boot wrote to the EEPROM");
}

// --- What this boot's clock is made of ------------------------------------------------------
//
// The first telemetry waits for SNTP to set the clock on this boot. The adapter says whether it has, from
// SNTP's first notification - which arrives on the SNTP task, not on the loop - and whether setup seeded
// the clock from storage.

void test_the_first_sync_of_the_boot_is_noted_with_its_moment(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));
    TEST_ASSERT_FALSE_MESSAGE(g_stand->ntp.wasSetBySntpThisBoot(), "SNTP was taken to have answered before it did");

    syncAfterSeconds(42);
    TEST_ASSERT_TRUE_MESSAGE(g_stand->ntp.wasSetBySntpThisBoot(), "SNTP's answer was not noted");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(42000, (uint32_t)g_stand->ntp.sntpSetAtMs(), "the moment of SNTP's answer");

    syncAfterSeconds(syncIntervalSeconds());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(42000, (uint32_t)g_stand->ntp.sntpSetAtMs(),
                                     "a later sync moved the moment of the boot's first");
}

void test_a_sync_that_leaves_the_clock_unreadable_is_noted_too(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));
    g_stand->arduino.setMillis(7000);
    hostWallClockInvalidate();
    TEST_ASSERT_TRUE(hostSntpNotifySync());

    TEST_ASSERT_TRUE_MESSAGE(g_stand->ntp.wasSetBySntpThisBoot(),
                             "a notification with an unreadable clock was not noted");
    TEST_ASSERT_EQUAL_UINT32(7000, (uint32_t)g_stand->ntp.sntpSetAtMs());
}

void test_a_clock_seeded_from_storage_is_not_one_sntp_set(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));
    TEST_ASSERT_FALSE_MESSAGE(g_stand->ntp.wasSeededFromStorage(), "a clock that already read was taken as seeded");
    syncAfterSeconds(syncIntervalSeconds());

    // The reset: the adapter and the system clock go, the EEPROM cells stay.
    g_stand.reset();
    hostSntpReset();
    hostWallClockInvalidate();
    g_stand.reset(new TimeSeedStand());
    g_stand->settings.begin();
    g_stand->ntp.setup();

    TEST_ASSERT_TRUE_MESSAGE(g_stand->ntp.wasSeededFromStorage(), "the seeded clock was not noted as seeded");
    TEST_ASSERT_FALSE_MESSAGE(g_stand->ntp.wasSetBySntpThisBoot(), "a seeded clock was taken for one SNTP set");
}

// --- The planned restarts -----------------------------------------------------------------
//
// Every restart the module decides on itself is a moment when the seed is about to be read and
// can still be made current, so each of them writes it unconditionally. There are two places
// where that happens, because there are two ways a planned restart reaches the reset:
// OnChangeModuleModeUseCase::restart(), which every mode-change restart funnels through, and
// ModuleCommandExecutionUseCase::handleReboot(), which resets the board itself.
//
// The tests below drive the production originator of each path - not the mode flag it sets - and
// then look at the flash through the eyes of the reset call.

namespace {

/**
 * @brief Boot, take one sync so a seed exists, then let the clock run on without one.
 *
 * The drift is what makes the assertion mean something: at the reset the stored seed must be the
 * time now, not the time the last sync stored.
 */
void bootAndDrift(const struct tm& bootClock, unsigned long driftMinutesValue)
{
    bootModule(bootClock);
    syncAfterSeconds(syncIntervalSeconds());
    driftMinutes(driftMinutesValue);
}

void assertSeedWasCurrentAtReset(const char* message)
{
    TEST_ASSERT_TRUE_MESSAGE(g_stand->arduino.sawReset,
        "the path did not reach a reset at all, so it proves nothing about the seed");

    struct tm now = hostWallClockGet();
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&now, &g_stand->arduino.seedAtReset, sizeof(struct tm),
        message);
}

/** @brief The mode-change use case as globals.h builds it, over the stand's time adapter. */
OnChangeModuleModeUseCase makeModeUseCase(MockModuleConfig& moduleConfig)
{
    return OnChangeModuleModeUseCase(g_stand->arduino, moduleConfig, g_stand->logger,
                                     g_stand->debugSystem, &g_stand->ntp);
}

} // namespace

void test_the_server_reboot_command_stores_the_seed_before_restarting(void)
{
    bootAndDrift(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0), 10);

    ModuleCommandExecutionUseCase commands(
        &g_stand->logger, &g_stand->debugSystem, &g_stand->ntp, nullptr, &g_stand->arduino,
        nullptr, nullptr, nullptr, nullptr, &g_stand->settings,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    TEST_ASSERT_TRUE(commands.execute("Reboot", "", "cmd-reboot-seed", ""));

    TEST_ASSERT_TRUE_MESSAGE(g_stand->arduino.restartCalled, "the Reboot command did not restart");
    assertSeedWasCurrentAtReset(
        "the Reboot command reset the board with a stale seed in EEPROM");
}

void test_the_preventive_night_restart_stores_the_seed_before_restarting(void)
{
    // Ten minutes before the restart window, so the seed is already stale when it opens.
    bootAndDrift(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY,
                          PreventiveRestartUseCase::PREVENTIVE_RESTART_HOUR - 1, 50, 0), 10);

    MockModuleConfig moduleConfig;
    moduleConfig.setSettings(&g_stand->settings);
    MockDeviceModel deviceModel;
    MockEvaluateRulesUseCase evaluateRules;
    MockFirmwareRemoteUpdate firmwareUpdate;
    MockBootEventLog restartLog;

    PreventiveRestartUseCase preventive(&moduleConfig, &deviceModel, &evaluateRules,
                                        &firmwareUpdate, &g_stand->ntp, &g_stand->logger,
                                        &g_stand->debugSystem, &restartLog);
    preventive.perform();

    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, moduleConfig.getRequestedModuleMode(),
        "the preventive restart did not ask for a restart, so the test proves nothing");

    OnChangeModuleModeUseCase modeUseCase = makeModeUseCase(moduleConfig);
    modeUseCase.loop();

    // A technical restart goes through deep sleep, not a soft reset.
    TEST_ASSERT_TRUE_MESSAGE(g_stand->arduino.hardResetCalled,
        "the technical restart did not take the hard reset path");
    assertSeedWasCurrentAtReset(
        "the nightly preventive restart reset the board with a stale seed in EEPROM");
}

void test_the_restart_for_a_fault_of_the_module_stores_the_seed_before_restarting(void)
{
    bootAndDrift(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0), 10);

    MockModuleConfig moduleConfig;
    moduleConfig.setSettings(&g_stand->settings);
    MockRestAPIService restApi;
    MockHybridAPIService hybridApi;
    MockWifiAdapter wifiAdapter;
    MockMqttManager mqttManager;
    ArduinoJsonConverter jsonConverter;
    ModuleStatusBoard statusBoard;

    // The link in trouble - no WiFi, no MQTT, the HTTP client past its offline count - and a fault of
    // the module that holds: the network task the heap refused.
    wifiAdapter.setConnected(false);
    mqttManager.isConnectedReturnValue = false;
    hybridApi.isModuleOnlineReturnValue = false;
    restApi.countOffline = 10;
    statusBoard.noteNetworkTask(false);

    LinkSupervisor supervisor(&restApi, &moduleConfig, g_stand->logger, g_stand->debugSystem,
                               &wifiAdapter, &g_stand->ntp, &g_stand->arduino, &mqttManager);
    supervisor.setStatusBoard(&statusBoard);

    // First pass sees the fault with the link down, the second one is past the hold.
    supervisor.superviseOnce();
    driftMinutes(13);
    supervisor.superviseOnce();

    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, moduleConfig.getRequestedModuleMode(),
        "the fault of the module did not ask for a restart, so the test proves nothing");

    OnChangeModuleModeUseCase modeUseCase = makeModeUseCase(moduleConfig);
    modeUseCase.loop();

    TEST_ASSERT_TRUE_MESSAGE(g_stand->arduino.hardResetCalled,
        "the technical restart did not take the hard reset path");
    assertSeedWasCurrentAtReset(
        "the restart for a fault of the module reset the board with a stale seed in EEPROM");
}

void test_the_firmware_update_restart_stores_the_seed_before_restarting(void)
{
    bootAndDrift(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0), 10);

    MockModuleConfig moduleConfig;
    moduleConfig.setSettings(&g_stand->settings);
    MockFirmwareRemoteUpdate firmwareUpdater;
    firmwareUpdater.setUpdateFromURLResult(true);
    firmwareUpdater.setLastUpdateStatus(3); // FIRMWARE_UPDATE_STATE_DONE

    ModuleCommandExecutionUseCase commands(
        &g_stand->logger, &g_stand->debugSystem, &g_stand->ntp, &firmwareUpdater,
        &g_stand->arduino, nullptr, nullptr, nullptr, &moduleConfig, &g_stand->settings,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    ModuleCommandDto command;
    command.uid = "cmd-fw-seed";
    command.command = "UpdateFirmware";
    command.options = "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\","
                      "\"version\":\"2.18.4\"}";

    commands.executeCommands({command});

    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, moduleConfig.getRequestedModuleMode(),
        "the finished update did not ask for a restart, so the test proves nothing");

    OnChangeModuleModeUseCase modeUseCase = makeModeUseCase(moduleConfig);
    modeUseCase.loop();

    TEST_ASSERT_TRUE_MESSAGE(g_stand->arduino.restartCalled, "the update did not restart");
    assertSeedWasCurrentAtReset(
        "the restart into the new firmware left a stale seed in EEPROM");
}

void test_the_sensor_change_restart_stores_the_seed_before_restarting(void)
{
    bootAndDrift(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0), 10);

    MockModuleConfig moduleConfig;
    moduleConfig.setSettings(&g_stand->settings);
    moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
    MockSensorRepository sensorRepository;

    SensorCommandUseCase sensorCommand(&moduleConfig, &g_stand->settings, &g_stand->logger,
                                       &g_stand->debugSystem, &sensorRepository);

    SensorEntity sensor;
    sensor.uid = "84:F7:03:FE:1B:30.00"; // channel 0
    sensor.sensorType = getSensorTypeName(ANALOG_SOIL_MOISTURE);
    sensor.protocol = "Analog";

    SensorCommandResult result = sensorCommand.execute(sensor);

    TEST_ASSERT_TRUE_MESSAGE(result.success, result.message.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, moduleConfig.getRequestedModuleMode(),
        "the sensor change did not ask for a restart, so the test proves nothing");

    OnChangeModuleModeUseCase modeUseCase = makeModeUseCase(moduleConfig);
    modeUseCase.loop();

    TEST_ASSERT_TRUE_MESSAGE(g_stand->arduino.restartCalled, "the sensor change did not restart");
    assertSeedWasCurrentAtReset(
        "the restart after a sensor change left a stale seed in EEPROM");
}

void test_a_module_mode_change_stores_the_seed_before_restarting(void)
{
    bootAndDrift(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0), 10);

    MockModuleConfig moduleConfig;
    moduleConfig.setSettings(&g_stand->settings);

    // What the button and the configuration web server ask for: a different mode, applied by a
    // restart.
    moduleConfig.setRequestedModuleMode(USER_CAN_RE_CONFIGURE);

    OnChangeModuleModeUseCase modeUseCase = makeModeUseCase(moduleConfig);
    modeUseCase.loop();

    TEST_ASSERT_TRUE_MESSAGE(g_stand->arduino.restartCalled, "the mode change did not restart");
    assertSeedWasCurrentAtReset(
        "the restart into another module mode left a stale seed in EEPROM");
}

// --- The uptime counter ---------------------------------------------------------------------

void test_the_millisecond_counter_rolling_over_is_not_a_clock_step(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    // Run the module up to just short of 2^32 milliseconds of uptime - 49.7 days, which it
    // reaches whenever the nightly restart does not fire - and take a sync there. This is the
    // reading the stored seed is recorded against.
    const uint32_t nearWrap = 0xFFFFF000UL;
    hostWallClockAdvance(static_cast<long>(nearWrap / 1000UL));
    g_stand->arduino.setMillis(nearWrap);
    TEST_ASSERT_TRUE(hostSntpNotifySync());
    TEST_ASSERT_EQUAL_MESSAGE(1, EEPROM.commitCalls,
        "the seed was not refreshed after 49 days, so the rest of this test compares nothing");
    EEPROM.resetStats();

    // The next answer crosses the boundary: the counter starts from zero again while the clock
    // keeps going. Subtracting the two readings as signed numbers would read that as the clock
    // having jumped by 49 days; taking the elapsed uptime as zero would read it as a jump of the
    // step below. Four minutes is longer than the step threshold, so both of those are caught
    // here, and shorter than the refresh interval, so the schedule stays out of the count.
    syncAfterSeconds(4 * 60);

    TEST_ASSERT_EQUAL_MESSAGE(0, EEPROM.commitCalls,
        "the millisecond counter wrapping was mistaken for a clock step and cost a commit");
}

// --- The polling pace -------------------------------------------------------------------------
//
// How often the module asks a time server is a cost in traffic and radio, not in flash, and it
// has a switch of its own. LinkSupervisor calls forceSyncTime() every time the network comes
// back, and that puts SNTP on a thirty-second interval so the module catches up quickly. The
// catch-up is over the moment a server has answered, and the polling goes back to the interval
// setup() configured.
//
// These tests live in this file, and not beside it, because NTPTimeAdapter.h defines its members
// outside the class: exactly one translation unit of the host build can include that header
// without a duplicate-symbol link error, and this file is it.
//
// ### Reproducing the red
//
// Delete the block in NTPTimeAdapter::onTimeSynchronized() that puts the interval back. The
// module then polls every thirty seconds from the first network blip of its life until it
// reboots, which is what these tests were written against.

void test_a_boot_polls_the_time_server_on_the_normal_interval(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    TEST_ASSERT_EQUAL_MESSAGE(300, syncIntervalSeconds(),
        "a module that has just booted is not polling on the five-minute interval");
}

void test_a_forced_sync_polls_fast_until_a_server_answers(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));
    const unsigned long normal = syncIntervalSeconds();

    g_stand->ntp.forceSyncTime();

    const unsigned long forced = syncIntervalSeconds();
    TEST_ASSERT_LESS_THAN_MESSAGE(normal, forced,
        "forceSyncTime() no longer shortens the polling interval, so this test proves nothing");

    // Nobody has answered yet: the module still has the time it woke up with, and the fast pace
    // is what it is for.
    driftMinutes(2);
    TEST_ASSERT_EQUAL_MESSAGE(forced, syncIntervalSeconds(),
        "the catch-up pace was given up before a server had answered");

    // A server answers.
    syncAfterSeconds(forced);

    TEST_ASSERT_EQUAL_MESSAGE(normal, syncIntervalSeconds(),
        "the module went on polling every thirty seconds after the sync had succeeded");
}

void test_a_sync_that_leaves_the_clock_unreadable_keeps_the_fast_pace(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));

    g_stand->ntp.forceSyncTime();
    const unsigned long forced = syncIntervalSeconds();

    // The notification arrives with nothing behind it: the clock cannot be read, so the module has
    // not caught up with anything and the fast pace has to stay.
    hostWallClockInvalidate();
    TEST_ASSERT_TRUE(hostSntpNotifySync());

    TEST_ASSERT_EQUAL_MESSAGE(forced, syncIntervalSeconds(),
        "a notification that left the clock unreadable ended the catch-up pace");
}

void test_the_network_blinking_twice_speeds_the_polling_up_twice(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));
    const unsigned long normal = syncIntervalSeconds();

    // The switch has to work in both directions every time, not once: a module lives through many
    // outages between reboots.
    for (int blink = 0; blink < 2; ++blink)
    {
        g_stand->ntp.forceSyncTime();
        const unsigned long forced = syncIntervalSeconds();
        TEST_ASSERT_LESS_THAN_MESSAGE(normal, forced,
            "an outage after the first one no longer speeds the polling up");

        syncAfterSeconds(forced);
        TEST_ASSERT_EQUAL_MESSAGE(normal, syncIntervalSeconds(),
            "the polling did not settle back after the catch-up sync");
    }
}

void test_the_network_coming_back_forces_a_sync_and_the_answer_settles_the_pace(void)
{
    bootModule(makeTime(BOOT_YEAR, BOOT_MONTH, BOOT_DAY, BOOT_HOUR, 0, 0));
    const unsigned long normal = syncIntervalSeconds();

    MockModuleConfig moduleConfig;
    moduleConfig.setSettings(&g_stand->settings);
    MockRestAPIService restApi;
    MockHybridAPIService hybridApi;
    MockWifiAdapter wifiAdapter;
    MockMqttManager mqttManager;
    ArduinoJsonConverter jsonConverter;

    // The link is up and the internet is not: MQTT is down and the HTTP client is past its
    // offline count, which is the state LinkSupervisor calls an outage.
    wifiAdapter.setConnected(true);
    mqttManager.isConnectedReturnValue = false;
    hybridApi.isModuleOnlineReturnValue = false;
    restApi.countOffline = 10;

    LinkSupervisor supervisor(&restApi, &moduleConfig, g_stand->logger, g_stand->debugSystem,
                               &wifiAdapter, &g_stand->ntp, &g_stand->arduino, &mqttManager);

    supervisor.superviseOnce();
    TEST_ASSERT_TRUE_MESSAGE(supervisor.isNetworkOffline(),
        "the stand did not get the service to notice the outage, so it proves nothing");
    TEST_ASSERT_EQUAL_MESSAGE(normal, syncIntervalSeconds(),
        "an outage on its own changed the polling interval");

    // The network comes back.
    restApi.countOffline = 0;
    mqttManager.isConnectedReturnValue = true;
    hybridApi.isModuleOnlineReturnValue = true;
    supervisor.superviseOnce();

    TEST_ASSERT_FALSE_MESSAGE(supervisor.isNetworkOffline(),
        "the service did not see the network come back");
    const unsigned long forced = syncIntervalSeconds();
    TEST_ASSERT_LESS_THAN_MESSAGE(normal, forced,
        "the restored network did not force a faster sync");

    // And a server answers.
    syncAfterSeconds(forced);

    TEST_ASSERT_EQUAL_MESSAGE(normal, syncIntervalSeconds(),
        "after one network blip the module kept polling every thirty seconds for good");
}

extern "C" void run_sntp_sync_pace_tests(void)
{
    RUN_TEST(test_a_boot_polls_the_time_server_on_the_normal_interval);
    RUN_TEST(test_a_forced_sync_polls_fast_until_a_server_answers);
    RUN_TEST(test_a_sync_that_leaves_the_clock_unreadable_keeps_the_fast_pace);
    RUN_TEST(test_the_network_blinking_twice_speeds_the_polling_up_twice);
    RUN_TEST(test_the_network_coming_back_forces_a_sync_and_the_answer_settles_the_pace);

    // Same reason as below: the endurance bench that runs after all of this counts live
    // allocations, so the stand does not get to outlive its tests.
    g_stand.reset();
}

extern "C" void run_eeprom_time_write_discipline_tests(void)
{
    RUN_TEST(test_a_day_of_ntp_syncs_costs_one_eeprom_commit_per_refresh_interval);
    RUN_TEST(test_the_first_sync_of_the_boot_is_noted_with_its_moment);
    RUN_TEST(test_a_sync_that_leaves_the_clock_unreadable_is_noted_too);
    RUN_TEST(test_a_clock_seeded_from_storage_is_not_one_sntp_set);
    RUN_TEST(test_a_day_on_the_aggressive_sync_interval_costs_the_same);
    RUN_TEST(test_a_sync_before_the_seed_ages_out_leaves_it_alone);
    RUN_TEST(test_the_seed_is_refreshed_once_it_is_five_minutes_old);
    RUN_TEST(test_a_power_cut_with_the_network_down_comes_back_five_minutes_behind_at_worst);
    RUN_TEST(test_a_clock_step_forward_is_stored_at_once);
    RUN_TEST(test_a_forward_step_too_small_to_age_the_seed_is_stored_at_once);
    RUN_TEST(test_a_clock_step_backward_is_stored_at_once);
    RUN_TEST(test_an_ntp_correction_smaller_than_the_threshold_is_not_stored);
    RUN_TEST(test_the_seed_keeps_the_eeprom_layout_it_has_always_had);
    RUN_TEST(test_a_reboot_seeds_the_clock_from_the_stored_time);
    RUN_TEST(test_the_millisecond_counter_rolling_over_is_not_a_clock_step);

    RUN_TEST(test_the_server_reboot_command_stores_the_seed_before_restarting);
#ifndef PREVENTIVE_RESTART_DISABLED
    // A build with the nightly restart compiled out never takes this path; the restarts around
    // it - the server command, the restart for a fault of the module, the update and the sensor
    // change - do.
    RUN_TEST(test_the_preventive_night_restart_stores_the_seed_before_restarting);
#endif
    RUN_TEST(test_the_restart_for_a_fault_of_the_module_stores_the_seed_before_restarting);
    RUN_TEST(test_the_firmware_update_restart_stores_the_seed_before_restarting);
    RUN_TEST(test_the_sensor_change_restart_stores_the_seed_before_restarting);
    RUN_TEST(test_a_module_mode_change_stores_the_seed_before_restarting);

    // The endurance bench that runs after all of this counts live allocations, so the stand does
    // not get to outlive its tests.
    g_stand.reset();
}
