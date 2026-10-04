// A rule whose sensor comes and goes must not write to flash for it.
//
// state / errorType / modeReason say how a rule is doing right now, but they are fields of the
// persisted rule, so every transition of them would travel the full storage path:
// updateRuleError() -> the DAO reads the whole rule file to find the rule -> save() -> the whole
// file serialized and rewritten -> invalidateCache(), and the next evaluation tick would read the file
// back. One transition would cost one rewrite and three file reads; a sensor that answers every other
// sweep would buy that twice per flap, for ever.
//
// Those three fields now live in a fixed runtime table held by the repository, with one slot per
// rule the stored format can hold - so every rule the module has can differ from its stored copy
// at once, which is what a whole bus going quiet does. The mode does not move to the table: it is what the rule
// was told to do, and a rule held for user confirmation has to stay held across a reboot - so
// updateRuleMode() still writes, and writes the rule whole, with the runtime state the module
// currently holds.
//
// ### Reproducing the red
//
// In RuleRepository::updateRuleError(), go back to reading the rule through the DAO and storing
// it with Repository<Rule>::save(): the write-counting tests fail (20 rewrites where none are
// allowed, 60 file reads where none are allowed), and the end-to-end test fails with them.
//
// To see the reader tests bite, apply the overlay in get()/getAll() only and leave
// getActiveRules(), getRulesNeedingSynchronization() and getRulesBySensorUid() reading
// Repository<Rule>::getAll() again - the rule set the evaluation tick filters is taken there.
// To see the revision test bite, drop the stateOverlayRevision term from rulesRevision(): the
// evaluation tick then keeps a snapshot that no longer matches the repository, and the end-to-end
// test stops seeing recoveries.
//
// ### What this file does not claim
//
// No field, name or type in rules.json changes here, but one value does, and it is worth being
// precise about which. RuleState::ERROR has no spelling in the format at all - the converter
// writes it as "Unsynchronized" - so the module cannot read an ERROR state back from storage;
// that is why the state is held in RAM and not why anything is
// written differently. RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM is the value that changes:
// the writers spelled it "Auto", a mode this firmware does not have, and no reader accepts that
// word - so the latch a protective shutdown sets came back off the file as OFF. The reader
// already accepts "WaitingForUserConfirm", and it is what the server sends, so writing it is the
// two halves agreeing rather than a new term.

#include <unity.h>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "Mock/ArduinoMock.h"

#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Core/Domain/Rules/MockRuleStateNotifier.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

namespace {

const char* const RULES_FILE = "/data/rules.json";

// Backend UIDs are GUID-sized; a short one would sit in the small-string buffer and hide the
// copies an overlay lookup makes.
const char* const RULE_A = "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a1b34";
const char* const RULE_B = "5d8e2f40-9a16-4c73-8e91-4f7a0b2c3d56";
const char* const SENSOR_A = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";
const char* const DEVICE_A = "7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71";

// How many times the rule goes into error and comes back. Every one of them would be a rewrite of the
// whole rule file without the overlay.
const int FLAP_TRANSITIONS = 20;

// Reads the whole file per transition without the overlay: one for the DAO lookup, one for the
// save, one for the cache reload the invalidation forces on the next reader.
const int FILE_READS_PER_TRANSITION = 3;

// Slots in the repository's runtime table - RuleRepository::STATE_OVERLAY_SLOTS, which is
// private. One per rule the registry will hold, so no rule can be left without one; the test below
// pins that against the registry's own capacity.
const int RULE_STATE_SLOTS = (int)RuleRepository::MAX_RULES;

Rule makeRule(const char* uid)
{
    Rule rule;
    rule.uid = uid;
    rule.version = 3;
    rule.deviceUid = DEVICE_A;
    rule.sensorUid = SENSOR_A;
    rule.ruleType = RuleType::SENSOR;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.protectiveShutdown = true;
    rule.timeToSetPoint = 10;
    return rule;
}

/** @brief The rule registry over its real storage path, wired the way globals.h wires it. */
struct RegistryStand
{
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockNTPTimeAdapter ntpTimeAdapter;

    SpiffsRuleDao dao;
    RuleRepository repository;

    RegistryStand()
        : dao(&spiffs, &jsonConverter, &debugSystem, &logger),
          repository(&dao, &ntpTimeAdapter, &debugSystem, &logger)
    {
        debugSystem.featureEnabled = false;
        ntpTimeAdapter.setEpochTime(1787000000);
    }

    /** @brief Put a rule in the file the way a server sync does, then start counting. */
    void seed(const Rule& rule)
    {
        TEST_ASSERT_TRUE(dao.saveRule(rule));
        repository.refreshCache();
        spiffs.resetWriteStats();
        spiffs.resetFileReadCount();
    }

    std::string storedFile() { return spiffs.getFileContent(RULES_FILE); }

    /** @brief The rule as a module that just booted on this file would read it. */
    Rule storedRule(const char* uid)
    {
        Rule stored;
        TEST_ASSERT_TRUE(dao.getRule(uid, stored));
        return stored;
    }
};

/** @brief The rule as the evaluation tick's registry snapshot reports it. */
Rule fromSnapshot(IRuleRepository& repository, const char* uid)
{
    for (const auto& rule : repository.getAll())
    {
        if (rule.uid == uid)
        {
            return rule;
        }
    }
    TEST_FAIL_MESSAGE("the rule is not in the registry snapshot");
    return Rule();
}

bool isInActiveSet(IRuleRepository& repository, const char* uid)
{
    for (const auto& rule : repository.getActiveRules())
    {
        if (rule.uid == uid)
        {
            return true;
        }
    }
    return false;
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The transitions stop writing
// --------------------------------------------------------------------------------------------

static void test_flapping_rule_state_writes_nothing_to_flash(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    for (int i = 0; i < FLAP_TRANSITIONS; i++)
    {
        const bool unreachable = (i % 2) == 0;
        if (unreachable)
        {
            TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                              "sensor_unreachable", ""));
        }
        else
        {
            TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::SYNCHRONIZED,
                                                              "", ""));
        }

        // Checked on errorType, not on state: this test is about what the transitions cost, and
        // the count below only means something if every transition did reach the readers. The
        // state is the subject of test_the_registry_snapshot_carries_the_live_state.
        TEST_ASSERT_EQUAL_STRING_MESSAGE(unreachable ? "sensor_unreachable" : "",
                                         fromSnapshot(stand.repository, RULE_A).errorType.c_str(),
                                         "the transition did not reach the readers");
    }

    char message[192];
    snprintf(message, sizeof(message),
             "%d rule state transitions rewrote the rule file %d times; a runtime state must cost "
             "no flash writes at all",
             FLAP_TRANSITIONS, (int)stand.spiffs.getWriteChunkCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.spiffs.getWriteChunkCount(), message);
}

static void test_flapping_rule_state_does_not_read_the_file_back(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    // One read settles the cache; from here the tick asks the same question every second.
    TEST_ASSERT_EQUAL_INT(1, (int)stand.repository.getActiveRules().size());
    const uint32_t generationBefore = stand.repository.generation();
    stand.spiffs.resetFileReadCount();

    for (int i = 0; i < FLAP_TRANSITIONS; i++)
    {
        TEST_ASSERT_TRUE(stand.repository.updateRuleError(
            RULE_A, (i % 2) == 0 ? RuleState::ERROR : RuleState::SYNCHRONIZED,
            (i % 2) == 0 ? "sensor_unreachable" : "", ""));
        stand.repository.getActiveRules();
    }

    char message[224];
    snprintf(message, sizeof(message),
             "the rule file was read back from flash %d times across %d state transitions; without the "
             "overlay each transition cost %d such reads",
             (int)stand.spiffs.getFileReadCount(), FLAP_TRANSITIONS, FILE_READS_PER_TRANSITION);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.spiffs.getFileReadCount(), message);
    TEST_ASSERT_TRUE_MESSAGE(stand.repository.isCacheCurrent(),
                             "the cache was dropped by a rule state transition");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationBefore, stand.repository.generation(),
                                     "a rule state transition moved the storage generation, which "
                                     "is what makes the base repository drop its cache");
}

// The evaluation tick keeps its own copy of the rule set and re-takes it when rulesRevision()
// moves. That copy lives outside the repository, so a transition the repository keeps in RAM
// still has to be announced - otherwise the tick evaluates a rule set that no longer exists.
static void test_a_runtime_transition_moves_the_rule_revision(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    const uint32_t before = stand.repository.rulesRevision();
    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));
    const uint32_t afterError = stand.repository.rulesRevision();
    TEST_ASSERT_TRUE_MESSAGE(afterError != before,
                             "a state transition left the registry snapshot of the evaluation "
                             "tick looking current");

    TEST_ASSERT_TRUE(stand.repository.updateRuleState(RULE_A, RuleState::UNSYNCHRONIZED));
    TEST_ASSERT_TRUE_MESSAGE(stand.repository.rulesRevision() != afterError,
                             "a state transition left the registry snapshot of the evaluation "
                             "tick looking current");

    // And a reader still does not move it: a revision that moved on a read would make the tick
    // re-copy the whole rule set every second.
    const uint32_t afterState = stand.repository.rulesRevision();
    stand.repository.getAll();
    stand.repository.getActiveRules();
    TEST_ASSERT_EQUAL_UINT32(afterState, stand.repository.rulesRevision());
}

// --------------------------------------------------------------------------------------------
// 2. The readers - every path a rule takes out of the repository - see the live state
// --------------------------------------------------------------------------------------------

// The registry snapshot the evaluation tick copies: EvaluateRulesUseCase decides from
// rule.state which of the sensor-unreachable branches a rule is in.
static void test_the_registry_snapshot_carries_the_live_state(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", "waiting"));
    Rule seen = fromSnapshot(stand.repository, RULE_A);
    TEST_ASSERT_TRUE_MESSAGE(seen.state == RuleState::ERROR,
                             "the tick would keep evaluating a rule whose sensor is gone");
    TEST_ASSERT_EQUAL_STRING("sensor_unreachable", seen.errorType.c_str());
    TEST_ASSERT_EQUAL_STRING("waiting", seen.modeReason.c_str());

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::SYNCHRONIZED, "", ""));
    seen = fromSnapshot(stand.repository, RULE_A);
    TEST_ASSERT_TRUE_MESSAGE(seen.state == RuleState::SYNCHRONIZED,
                             "the rule would never be evaluated again after its sensor came back");
    TEST_ASSERT_EQUAL_STRING("", seen.errorType.c_str());
    TEST_ASSERT_EQUAL_STRING("", seen.modeReason.c_str());
}

static void test_get_carries_the_live_rule_state(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    TEST_ASSERT_TRUE(stand.repository.updateRuleState(RULE_A, RuleState::UNSYNCHRONIZED));
    TEST_ASSERT_TRUE(stand.repository.get(RULE_A).value().state == RuleState::UNSYNCHRONIZED);

    TEST_ASSERT_TRUE(stand.repository.updateRuleState(RULE_A, RuleState::SYNCHRONIZED));
    TEST_ASSERT_TRUE(stand.repository.get(RULE_A).value().state == RuleState::SYNCHRONIZED);
}

// getActiveRules() is the set the evaluation tick walks, and it filters on the runtime state
// itself: a rule held by a protective shutdown is dropped from it.
static void test_the_active_set_follows_the_live_state(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));
    TEST_ASSERT_TRUE(isInActiveSet(stand.repository, RULE_A));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "protective_shutdown", "protective_shutdown"));
    TEST_ASSERT_FALSE_MESSAGE(isInActiveSet(stand.repository, RULE_A),
                              "a rule held by a protective shutdown stayed in the active set");
    TEST_ASSERT_EQUAL_INT(0, (int)stand.repository.getActiveRulesByType(RuleType::SENSOR).size());

    // A rule in error over an unreachable sensor stays in the set - that is where the tick
    // notices the sensor answering again.
    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));
    TEST_ASSERT_TRUE_MESSAGE(isInActiveSet(stand.repository, RULE_A),
                             "the rule left the set the recovery branch reads");
}

// getRulesNeedingSynchronization() filters on the state as well.
static void test_the_unsynchronized_list_follows_the_live_state(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));
    TEST_ASSERT_EQUAL_INT(0, (int)stand.repository.getRulesNeedingSynchronization().size());

    TEST_ASSERT_TRUE(stand.repository.updateRuleState(RULE_A, RuleState::UNSYNCHRONIZED));
    TEST_ASSERT_EQUAL_INT(1, (int)stand.repository.getRulesNeedingSynchronization().size());

    TEST_ASSERT_TRUE(stand.repository.updateRuleState(RULE_A, RuleState::SYNCHRONIZED));
    TEST_ASSERT_EQUAL_INT(0, (int)stand.repository.getRulesNeedingSynchronization().size());
}

// The three remaining lists a rule can leave the repository in. Two of them read the file
// through the DAO instead of the cache, which is a separate way to miss the overlay.
static void test_every_rule_list_carries_the_live_state(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));

    std::vector<Rule> bySensor = stand.repository.getRulesBySensorUid(SENSOR_A);
    TEST_ASSERT_EQUAL_INT(1, (int)bySensor.size());
    TEST_ASSERT_TRUE_MESSAGE(bySensor[0].state == RuleState::ERROR,
                             "getRulesBySensorUid answered from storage");

    std::vector<Rule> byDevice = stand.repository.getRulesByDeviceUid(DEVICE_A);
    TEST_ASSERT_EQUAL_INT(1, (int)byDevice.size());
    TEST_ASSERT_TRUE_MESSAGE(byDevice[0].state == RuleState::ERROR,
                             "getRulesByDeviceUid answered from storage");

    std::vector<Rule> byType = stand.repository.getRulesByType(RuleType::SENSOR);
    TEST_ASSERT_EQUAL_INT(1, (int)byType.size());
    TEST_ASSERT_TRUE_MESSAGE(byType[0].state == RuleState::ERROR,
                             "getRulesByType answered from storage");
}

// A cache reload for an unrelated reason must not resurrect the value on flash: the runtime
// state outlives the cache it is read next to.
static void test_the_live_rule_state_survives_a_cache_reload(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));
    TEST_ASSERT_TRUE(stand.repository.save(makeRule(RULE_B)));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));

    // Saving another rule drops the cache; the next read comes from the file, where rule A is
    // still recorded as synchronized.
    Rule ruleB = makeRule(RULE_B);
    ruleB.version = 4;
    TEST_ASSERT_TRUE(stand.repository.save(ruleB));
    TEST_ASSERT_FALSE(stand.repository.isCacheCurrent());

    TEST_ASSERT_TRUE_MESSAGE(fromSnapshot(stand.repository, RULE_A).state == RuleState::ERROR,
                             "the live rule state was lost when the cache was reloaded");
    TEST_ASSERT_TRUE(stand.storedRule(RULE_A).state != RuleState::ERROR);

    // And a refresh - the documented way to pick up a change made behind the repository's back -
    // is not a way to lose it either.
    stand.repository.refreshCache();
    TEST_ASSERT_TRUE(fromSnapshot(stand.repository, RULE_A).state == RuleState::ERROR);
}

// --------------------------------------------------------------------------------------------
// 3. Everything else about the rule is stored exactly as before
// --------------------------------------------------------------------------------------------

// The mode is configuration: a rule handed back to the user by a protective shutdown must stay
// held across a reboot, so it still goes to flash - once, carrying the live state with it.
static void test_a_protective_shutdown_is_still_written(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    // The pair EvaluateRulesUseCase::triggerProtectiveShutdown() issues, in that order.
    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "protective_shutdown", "protective_shutdown"));
    TEST_ASSERT_TRUE(stand.repository.updateRuleMode(
        RULE_A, RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM, "protective_shutdown"));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.spiffs.getWriteChunkCount(),
                                     "a protective shutdown is a latch and must reach the file, "
                                     "and once is enough for it");

    TEST_ASSERT_TRUE(stand.storedFile().find("\"errorType\":\"protective_shutdown\"") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(stand.storedFile().find("\"modeReason\":\"protective_shutdown\"") !=
                     std::string::npos);

    // And the mode comes back as the mode it was: a rule waiting for the user, not a rule the
    // user switched off and not a rule that runs.
    TEST_ASSERT_TRUE_MESSAGE(
        stand.storedRule(RULE_A).mode == RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM,
        "the latch did not survive the file it was written to");

    // And the module still holds it out of the active set afterwards.
    TEST_ASSERT_FALSE(isInActiveSet(stand.repository, RULE_A));
}

// The latch on its own, through the round trip, because the two halves of it were not in step:
// the reader has always known the word "WaitingForUserConfirm" - it is what the server sends -
// but the writer spelled the mode "Auto", which no reader knows, so a rule stopped for the user
// came back off the file as a rule the user had switched off. It is the same active set either
// way, which is why nothing caught it; it is not the same rule, and not what the app is shown.
static void test_the_protective_shutdown_latch_survives_serialization(void)
{
    Rule latched = makeRule(RULE_A);
    latched.mode = RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM;
    latched.modeReason = "protective_shutdown";
    latched.errorType = "protective_shutdown";

    // The converter the device ships with, which is what writes the rule file.
    ArduinoJsonConverter converter;

    Rule readBack;
    TEST_ASSERT_TRUE(converter.ruleFromJson(converter.ruleToJson(latched), readBack));
    TEST_ASSERT_TRUE_MESSAGE(
        readBack.mode == RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM,
        "ArduinoJsonConverter lost the latch: a rule waiting for the user came back switched off");

    // The word is the server's, not a new one: it is what the parser above already accepts.
    TEST_ASSERT_TRUE(converter.ruleToJson(latched).find("\"mode\":\"WaitingForUserConfirm\"") !=
                     std::string::npos);

    // The two modes that were never in doubt still travel unchanged.
    Rule on = makeRule(RULE_A);
    Rule off = makeRule(RULE_A);
    off.mode = RuleMode::OFF;
    Rule roundTrip;
    TEST_ASSERT_TRUE(converter.ruleFromJson(converter.ruleToJson(on), roundTrip));
    TEST_ASSERT_TRUE(roundTrip.mode == RuleMode::ON);
    TEST_ASSERT_TRUE(converter.ruleFromJson(converter.ruleToJson(off), roundTrip));
    TEST_ASSERT_TRUE(roundTrip.mode == RuleMode::OFF);
}

// And the whole way round, over the real storage: the latch a protective shutdown sets is still
// a latch after the module restarts on that file.
static void test_the_latch_holds_the_rule_after_a_reboot(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "protective_shutdown", "protective_shutdown"));
    TEST_ASSERT_TRUE(stand.repository.updateRuleMode(
        RULE_A, RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM, "protective_shutdown"));

    const std::string persisted = stand.storedFile();

    RegistryStand rebooted;
    TEST_ASSERT_TRUE(rebooted.spiffs.writeToFile(RULES_FILE, persisted.c_str()));

    const Rule afterBoot = fromSnapshot(rebooted.repository, RULE_A);
    TEST_ASSERT_TRUE_MESSAGE(afterBoot.mode == RuleMode::RULE_MODE_WAITING_FOR_USER_CONFIRM,
                             "the rebooted module reads the rule as switched off rather than "
                             "waiting for the user");
    TEST_ASSERT_EQUAL_STRING("protective_shutdown", afterBoot.modeReason.c_str());
    TEST_ASSERT_FALSE_MESSAGE(isInActiveSet(rebooted.repository, RULE_A),
                              "the rule the shutdown stopped started running again after a reboot");
}

// A server snapshot cannot overwrite how a rule is doing, because the merge never looks at it: it
// decides out of the stored configuration - what the rule *is* - and the runtime state stays in
// the table, where the next tick can clear it. So a routine re-sync of a rule whose sensor is gone
// leaves the error exactly where it was and puts nothing about it on flash.
//
// What the server is told about that rule is a separate path and a separate file:
// test_rule_sync_reporting.cpp, where the report is built by reading this registry.
static void test_a_server_sync_sees_the_live_state(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", "waiting"));

    // The same version of the same rule coming back from the server, which is what every routine
    // pull carries.
    Rule fromServer = makeRule(RULE_A);
    fromServer.state = RuleState::SYNCHRONIZED;
    TEST_ASSERT_EQUAL_INT(1, stand.repository.synchronizeRules({fromServer}));

    const Rule seen = fromSnapshot(stand.repository, RULE_A);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("sensor_unreachable", seen.errorType.c_str(),
                                     "the server's copy of the rule overwrote the error the module "
                                     "holds for it");
    TEST_ASSERT_TRUE_MESSAGE(seen.state == RuleState::ERROR,
                             "the sync moved the rule out of the state the module holds it in");
    TEST_ASSERT_EQUAL_STRING("waiting", seen.modeReason.c_str());

    // And the runtime state stayed out of the file: it is what the next tick establishes again,
    // and writing it would be the flash traffic this table exists to remove.
    TEST_ASSERT_TRUE_MESSAGE(stand.storedFile().find("\"errorType\":\"sensor_unreachable\"") ==
                                 std::string::npos,
                             "a runtime error was written to flash by a routine synchronization");
    TEST_ASSERT_TRUE(stand.storedRule(RULE_A).state != RuleState::ERROR);
}

// The other half of the same statement, and the one a greenhouse notices: after that re-sync the
// rule is still a rule the evaluation tick acts on. A rule left in UNSYNCHRONIZED matches neither
// branch of EvaluateRulesUseCase::loop() - not SYNCHRONIZED, so it is not evaluated, and not
// ERROR, so it is never recovered either - and a rule whose sensor comes back during that window
// stays switched off until the sync after next.
static void test_a_routine_re_sync_leaves_the_rule_where_the_tick_can_recover_it(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));
    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));

    Rule fromServer = makeRule(RULE_A);
    fromServer.state = RuleState::SYNCHRONIZED;
    TEST_ASSERT_EQUAL_INT(1, stand.repository.synchronizeRules({fromServer}));

    const Rule seen = fromSnapshot(stand.repository, RULE_A);
    char message[224];
    snprintf(message, sizeof(message),
             "after a routine re-sync the rule reads state %d: the tick evaluates SYNCHRONIZED "
             "rules and recovers ERROR ones, and anything else is a rule nothing is done about "
             "until the next sync",
             (int)seen.state);
    TEST_ASSERT_TRUE_MESSAGE(seen.state == RuleState::ERROR, message);
    TEST_ASSERT_TRUE_MESSAGE(isInActiveSet(stand.repository, RULE_A),
                             "the rule left the set the recovery branch reads");
}

// The other direction of the same rule: what the sync decides is what the module then holds. The
// merge writes the whole rule set, so the runtime table is released against that decision - a
// rule the server deleted must stop being the rule the module remembered.
static void test_a_rule_deleted_by_the_server_while_its_sensor_is_gone_is_deleted(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));
    TEST_ASSERT_TRUE(stand.repository.save(makeRule(RULE_B)));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));
    TEST_ASSERT_TRUE(isInActiveSet(stand.repository, RULE_A));

    // The periodic pull, with A gone from the server's snapshot: SyncRulesUseCase passes the
    // full set of uids the server returned, and the merge marks everything absent DELETING.
    std::set<std::string> snapshotUids;
    snapshotUids.insert(RULE_B);
    TEST_ASSERT_EQUAL_INT(1, stand.repository.synchronizeRules({makeRule(RULE_B)}, true,
                                                               snapshotUids));

    TEST_ASSERT_TRUE_MESSAGE(fromSnapshot(stand.repository, RULE_A).state == RuleState::DELETING,
                             "the module went on reading the rule as the state it remembered, so "
                             "the deletion never reached the purge");
    TEST_ASSERT_FALSE_MESSAGE(isInActiveSet(stand.repository, RULE_A),
                              "a rule the server deleted stayed in the evaluated set");
}

// And the edit the server makes without bumping a version: switching the rule off. It has to
// reach the rule even while the rule is in error over a missing sensor.
static void test_a_mode_change_reaches_a_rule_whose_sensor_is_gone(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));
    TEST_ASSERT_TRUE(isInActiveSet(stand.repository, RULE_A));

    Rule fromServer = makeRule(RULE_A);
    fromServer.mode = RuleMode::OFF;
    TEST_ASSERT_EQUAL_INT(1, stand.repository.synchronizeRules({fromServer}));

    TEST_ASSERT_FALSE_MESSAGE(isInActiveSet(stand.repository, RULE_A),
                              "the rule the user switched off went on being evaluated");
    TEST_ASSERT_TRUE_MESSAGE(stand.storedRule(RULE_A).mode != RuleMode::ON,
                             "the mode the server sent never reached the file");
}

// A rule saved whole carries its own state: the server pushing a rule decides what the state is,
// and the overlay does not outvote the save.
static void test_an_explicit_save_replaces_the_live_state(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));
    TEST_ASSERT_TRUE(stand.repository.save(makeRule(RULE_A)));

    TEST_ASSERT_EQUAL_STRING_MESSAGE("", fromSnapshot(stand.repository, RULE_A).errorType.c_str(),
                                     "a stale runtime state survived the rule it belonged to");
}

// And it does not outlive the rule: a uid that comes back is a new rule, not the old state.
static void test_the_live_rule_state_does_not_outlive_the_rule(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));

    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));
    TEST_ASSERT_TRUE(stand.repository.remove(RULE_A));
    TEST_ASSERT_TRUE(stand.repository.save(makeRule(RULE_A)));

    TEST_ASSERT_EQUAL_STRING_MESSAGE("", fromSnapshot(stand.repository, RULE_A).errorType.c_str(),
                                     "the removed rule's error came back with its uid");
}

// The table is sized from the registry: the registry refuses a rule past its capacity, so every
// rule it does hold has a slot. This is the assertion that keeps the two in step - it fills the
// registry through the merge, one rule past what it will take, and fails if the registry ever
// grows past the table.
static void test_the_registry_holds_no_more_rules_than_the_table_has_slots(void)
{
    RegistryStand stand;

    std::vector<Rule> fromServer;
    for (int i = 0; i <= RULE_STATE_SLOTS; i++)
    {
        char uid[40];
        snprintf(uid, sizeof(uid), "%.34s%02d", RULE_A, i);
        fromServer.push_back(makeRule(uid));
    }

    stand.repository.synchronizeRules(fromServer);

    char message[224];
    snprintf(message, sizeof(message),
             "%d rules of the %d the server sent are in the registry, and the runtime table has "
             "%d slots: a rule the module holds with no slot to hold its state is exactly what "
             "this table is sized to prevent",
             stand.repository.count(), (int)fromServer.size(), RULE_STATE_SLOTS);
    TEST_ASSERT_EQUAL_INT_MESSAGE(RULE_STATE_SLOTS, stand.repository.count(), message);

    // And the capacity is where it is claimed to be, not below it: the rule that did not fit is
    // the last one, and every rule before it is running.
    for (int i = 0; i < RULE_STATE_SLOTS; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(stand.repository.exists(fromServer[i].uid.c_str()), message);
    }
    TEST_ASSERT_FALSE_MESSAGE(stand.repository.exists(fromServer[RULE_STATE_SLOTS].uid.c_str()),
                              message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stand.repository.refusedRuleCount(),
                                     "the rule that did not fit was refused without a count");
}

// The last resort, for a rule set that got past the registry anyway: a transition with no slot is
// not dropped, it is written to the file exactly the way every transition was written before this
// table existed - and it inherits exactly the behaviour that went with it. The file has no
// spelling for RuleState::ERROR, so such a rule reads back as UNSYNCHRONIZED and stops matching
// either branch of the evaluation tick until the next server sync. Unreachable through the
// registry, which refuses a rule past its capacity; reachable by a rule file written behind the
// repository's back - an older firmware's file, or a write straight into the DAO. Asserted here so
// that when it is reached, it is known what it costs.
static void test_a_transition_with_no_free_slot_falls_back_to_storage(void)
{
    RegistryStand stand;

    // Two rules more than the table holds, put into the file through the DAO: the registry itself
    // would refuse them, and that is what the test above says.
    const int ruleCount = RULE_STATE_SLOTS + 2;
    char uids[ruleCount][40];
    for (int i = 0; i < ruleCount; i++)
    {
        snprintf(uids[i], sizeof(uids[i]), "%.34s%02d", RULE_A, i);
        TEST_ASSERT_TRUE(stand.dao.saveRule(makeRule(uids[i])));
    }
    stand.repository.refreshCache();
    stand.spiffs.resetWriteStats();

    int inTable = 0;
    int overflowed = 0;
    for (int i = 0; i < ruleCount; i++)
    {
        TEST_ASSERT_TRUE(stand.repository.updateRuleError(uids[i], RuleState::ERROR,
                                                          "sensor_unreachable", ""));

        // The error type survives either way - the file does carry that field. The state does
        // not: it is the field the evaluation tick branches on, and it is the one the file
        // cannot hold.
        const Rule seen = fromSnapshot(stand.repository, uids[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("sensor_unreachable", seen.errorType.c_str(),
                                         "a transition was lost when the table filled up");
        if (seen.state == RuleState::ERROR)
        {
            inTable++;
        }
        else
        {
            char message[192];
            snprintf(message, sizeof(message),
                     "rule %d has no slot, so its state came back from the file - and the file "
                     "has only two answers, UNSYNCHRONIZED or the one it was saved with",
                     i);
            TEST_ASSERT_TRUE_MESSAGE(seen.state == RuleState::UNSYNCHRONIZED, message);
            overflowed++;
        }
    }

    char message[208];
    snprintf(message, sizeof(message),
             "%d rules went into error against a table of %d slots: %d kept a live state, %d fell "
             "back to storage, and the file was written %d times",
             ruleCount, RULE_STATE_SLOTS, inTable, overflowed,
             (int)stand.spiffs.getWriteChunkCount());

    TEST_ASSERT_EQUAL_INT_MESSAGE(RULE_STATE_SLOTS, inTable, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ruleCount - RULE_STATE_SLOTS, overflowed, message);

    // Only the ones past the budget may reach the file, and they reach it once each.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)(ruleCount - RULE_STATE_SLOTS),
                                     (uint32_t)stand.spiffs.getWriteChunkCount(), message);

    // And that is what the file route does, spelled out: a rule whose transition went to the
    // file matches neither branch of EvaluateRulesUseCase::loop() - not SYNCHRONIZED, so it is
    // not evaluated, and not ERROR, so it is never recovered either.
    const Rule pastBudget = fromSnapshot(stand.repository, uids[ruleCount - 1]);
    TEST_ASSERT_TRUE(pastBudget.state != RuleState::SYNCHRONIZED);
    TEST_ASSERT_TRUE(pastBudget.state != RuleState::ERROR);
}

// What a module reads at boot is the rule as it was last stored - the same mechanism as before,
// now without the runtime transitions in it.
static void test_a_reboot_starts_from_the_stored_rule(void)
{
    RegistryStand stand;
    stand.seed(makeRule(RULE_A));
    TEST_ASSERT_TRUE(stand.repository.updateRuleError(RULE_A, RuleState::ERROR,
                                                      "sensor_unreachable", ""));

    const std::string persisted = stand.storedFile();

    RegistryStand rebooted;
    TEST_ASSERT_TRUE(rebooted.spiffs.writeToFile(RULES_FILE, persisted.c_str()));

    const Rule afterBoot = fromSnapshot(rebooted.repository, RULE_A);
    TEST_ASSERT_TRUE_MESSAGE(afterBoot.state == RuleState::SYNCHRONIZED,
                             "a rebooted module did not start from the last stored rule");
    TEST_ASSERT_EQUAL_STRING("", afterBoot.errorType.c_str());
    TEST_ASSERT_TRUE_MESSAGE(isInActiveSet(rebooted.repository, RULE_A),
                             "the rule did not come back into the evaluated set after a reboot");
}

// --------------------------------------------------------------------------------------------
// 4. The whole path, from a sensor that answers every other tick to the reports the server gets
// --------------------------------------------------------------------------------------------

namespace {

class OneDeviceModel : public IDeviceModel
{
public:
    Device* device = nullptr;

    bool init() override { return true; }
    Device* getDeviceByUid(const char* uid) override
    {
        if (device && device->deviceUid && uid && std::string(device->deviceUid) == uid)
        {
            return device;
        }
        return nullptr;
    }
    std::vector<Device*> getAllDevices() override
    {
        std::vector<Device*> devices;
        if (device) devices.push_back(device);
        return devices;
    }
    int getDeviceCount() const override { return device ? 1 : 0; }
    bool addDevice(Device*) override { return true; }
    bool removeDevice(const char*) override { return true; }
    void clearDevices() override {}
};

/** @brief EvaluateRulesUseCase driving one sensor rule over the real rule storage. */
struct FlappingSensorModule
{
    RegistryStand stand;

    DeviceDigitalOut device;
    OneDeviceModel deviceModel;
    MockSensorValueModel sensorValues;
    MockArduinoAdapter arduino;
    MockRuleStateNotifier notifier;
    EvaluateRulesUseCase evaluate;

    FlappingSensorModule()
        : device(),
          evaluate(&deviceModel, &stand.repository, &arduino, &stand.ntpTimeAdapter,
                   &sensorValues, nullptr, nullptr, &notifier)
    {
        device.deviceUid = DEVICE_A;
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = DEVICE_STATE_OFF;
        deviceModel.device = &device;

        sensorValues.values[SENSOR_A] = 25.0f;
        stand.seed(makeRule(RULE_A));
    }

    /** @brief One evaluation tick: the sensor either answered recently or stopped answering. */
    void tick(unsigned long atMs, bool sensorAnswers)
    {
        // The freshness window is 5 minutes; a reading older than that is what the rule calls an
        // unreachable sensor.
        sensorValues.lastReadTimes[SENSOR_A] =
            sensorAnswers ? (uint32_t)atMs : (uint32_t)(atMs - 400000);
        arduino.setMillis(atMs);
        evaluate.loop();
    }
};

} // namespace

static void test_a_flapping_sensor_drives_the_rule_without_writing_to_flash(void)
{
    FlappingSensorModule module;

    unsigned long now = 500000;
    for (int i = 0; i < FLAP_TRANSITIONS; i++)
    {
        const bool answers = (i % 2) == 1;
        now += 1000;
        module.tick(now, answers);

        char message[160];
        snprintf(message, sizeof(message),
                 "tick %d: the rule did not follow the sensor (unreachable=%d recovered=%d)", i,
                 module.notifier.sensorUnreachableCount, module.notifier.ruleRecoveredCount);
        TEST_ASSERT_EQUAL_INT_MESSAGE(i / 2 + 1, module.notifier.sensorUnreachableCount, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE((i + 1) / 2, module.notifier.ruleRecoveredCount, message);
    }

    char message[224];
    snprintf(message, sizeof(message),
             "a sensor that came and went %d times rewrote the rule file %d times and read it back "
             "%d times; neither belongs to a runtime state",
             FLAP_TRANSITIONS, (int)module.stand.spiffs.getWriteChunkCount(),
             (int)module.stand.spiffs.getFileReadCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.stand.spiffs.getWriteChunkCount(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.stand.spiffs.getFileReadCount(), message);

    // The file still says what the last save said, and it is still a complete rule record.
    TEST_ASSERT_TRUE(module.stand.storedRule(RULE_A).state == RuleState::SYNCHRONIZED);
    TEST_ASSERT_TRUE(module.stand.storedFile().find("\"state\":\"Synchronized\"") !=
                     std::string::npos);
}

// The dead zone, over the whole path: a rule whose sensor is gone, a routine pull in the middle of
// it, and the sensor answering again on the next tick. The recovery branch of the tick reads
// RuleState::ERROR, so a sync that moves the rule anywhere else costs the greenhouse a rule that
// stays switched off with a working sensor until the sync after next.
static void test_a_rule_recovers_after_a_routine_sync_passed_over_it(void)
{
    FlappingSensorModule module;

    module.tick(501000, false);
    TEST_ASSERT_EQUAL_INT(1, module.notifier.sensorUnreachableCount);

    // The periodic pull, carrying the rule exactly as the server holds it: same version, same
    // mode. Nothing about the rule changed, and nothing about it should move.
    Rule fromServer = makeRule(RULE_A);
    fromServer.state = RuleState::SYNCHRONIZED;
    TEST_ASSERT_EQUAL_INT(1, module.stand.repository.synchronizeRules({fromServer}));

    // The sync writes the rule file - that is what a sync does. What the recovery on the next tick
    // costs is measured from here.
    module.stand.spiffs.resetWriteStats();

    // The sensor answers again on the very next tick.
    module.tick(502000, true);

    char message[224];
    snprintf(message, sizeof(message),
             "the rule was not recovered on the tick after its sensor came back (recovered=%d, "
             "state read as %d): a synchronization passed over it and left it matching neither "
             "branch of the evaluation tick",
             module.notifier.ruleRecoveredCount,
             (int)fromSnapshot(module.stand.repository, RULE_A).state);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.notifier.ruleRecoveredCount, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.stand.spiffs.getWriteChunkCount(),
                                     "the recovery after a sync cost a flash write");
}

// The whole path for the deletion, because the repository is only half of it: what marks a rule
// DELETING is the sync, and what acts on it is the next evaluation tick. A rule the server
// deleted while its sensor was gone has to leave the registry and let go of its device.
static void test_a_rule_deleted_while_unreachable_is_purged_by_the_next_tick(void)
{
    FlappingSensorModule module;

    // The sensor stops answering: the rule goes into error and drops its device.
    module.tick(501000, false);
    TEST_ASSERT_EQUAL_INT(1, module.notifier.sensorUnreachableCount);
    TEST_ASSERT_EQUAL_INT(1, module.stand.repository.count());

    // The server drops the rule, and the periodic pull brings a snapshot without it. An empty
    // snapshot means nothing to merge, so the account still has one other rule.
    std::set<std::string> snapshotUids;
    snapshotUids.insert(RULE_B);
    module.stand.repository.synchronizeRules({makeRule(RULE_B)}, true, snapshotUids);

    // The next tick purges it - it cannot, if the module still reads the rule as the state it
    // remembered rather than the state the sync wrote.
    module.tick(502000, false);

    char message[176];
    snprintf(message, sizeof(message),
             "the rule set still holds %d rules after the server deleted one of them",
             module.stand.repository.count());
    TEST_ASSERT_FALSE_MESSAGE(module.stand.repository.exists(RULE_A), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DEVICE_STATE_OFF, module.device.state,
                                  "the deleted rule kept driving its device");
}

extern "C" void run_rule_state_overlay_tests(void)
{
    printf("\n----- Running Rule State Overlay Tests -----\n");

    RUN_TEST(test_flapping_rule_state_writes_nothing_to_flash);
    RUN_TEST(test_flapping_rule_state_does_not_read_the_file_back);
    RUN_TEST(test_a_runtime_transition_moves_the_rule_revision);

    RUN_TEST(test_the_registry_snapshot_carries_the_live_state);
    RUN_TEST(test_get_carries_the_live_rule_state);
    RUN_TEST(test_the_active_set_follows_the_live_state);
    RUN_TEST(test_the_unsynchronized_list_follows_the_live_state);
    RUN_TEST(test_every_rule_list_carries_the_live_state);
    RUN_TEST(test_the_live_rule_state_survives_a_cache_reload);

    RUN_TEST(test_a_protective_shutdown_is_still_written);
    RUN_TEST(test_the_protective_shutdown_latch_survives_serialization);
    RUN_TEST(test_the_latch_holds_the_rule_after_a_reboot);
    RUN_TEST(test_a_server_sync_sees_the_live_state);
    RUN_TEST(test_a_routine_re_sync_leaves_the_rule_where_the_tick_can_recover_it);
    RUN_TEST(test_a_rule_deleted_by_the_server_while_its_sensor_is_gone_is_deleted);
    RUN_TEST(test_a_mode_change_reaches_a_rule_whose_sensor_is_gone);
    RUN_TEST(test_an_explicit_save_replaces_the_live_state);
    RUN_TEST(test_the_live_rule_state_does_not_outlive_the_rule);
    RUN_TEST(test_the_registry_holds_no_more_rules_than_the_table_has_slots);
    RUN_TEST(test_a_transition_with_no_free_slot_falls_back_to_storage);
    RUN_TEST(test_a_reboot_starts_from_the_stored_rule);

    RUN_TEST(test_a_flapping_sensor_drives_the_rule_without_writing_to_flash);
    RUN_TEST(test_a_rule_recovers_after_a_routine_sync_passed_over_it);
    RUN_TEST(test_a_rule_deleted_while_unreachable_is_purged_by_the_next_tick);
}
