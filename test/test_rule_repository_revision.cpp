// Every mutator of the rule repository must move the revision - and no read may.
//
// The evaluation tick trusts RuleRepository::rulesRevision() as the complete change signal
// for its registry snapshot: a mutation that slips past the revision leaves the module
// evaluating a dead list until some other write or a reboot. The revision derives from the
// base repository's cache generation, which is bumped in invalidateCache() - the single
// funnel of all write paths - so slipping past takes deliberate effort; this test makes that
// effort visible by walking every public mutator, including the state/mode/error updaters
// and the cloud synchronization, and requiring a revision change from each. The reverse
// invariant matters just as much: reads must leave the revision alone, because a revision
// that moves on a read would force the snapshot to be re-copied every tick and the steady
// second would allocate again.

#include <unity.h>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"

namespace {

const char* kRuleAUid = "rule-revision-a-aaaaaaaa-bbbb-cccc-0001";
const char* kRuleBUid = "rule-revision-b-aaaaaaaa-bbbb-cccc-0002";

Rule makeRule(const char* uid) {
    Rule rule;
    rule.uid = uid;
    rule.version = 1;
    rule.deviceUid = "device-revision-aaaaaaaa-bbbb-cccc-001";
    rule.sensorUid = "sensor-revision-aaaaaaaa-bbbb-cccc-001";
    rule.ruleType = RuleType::SENSOR;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    return rule;
}

struct RevisionFixture {
    MockSpiffsRuleDao dao;
    MockNTPTimeAdapter ntp;
    RuleRepository repo;
    uint32_t lastRevision;

    RevisionFixture() : repo(&dao, &ntp), lastRevision(repo.rulesRevision()) {}

    void expectMoved(const char* mutator) {
        uint32_t revision = repo.rulesRevision();
        char message[96];
        snprintf(message, sizeof(message), "%s must change rulesRevision()", mutator);
        TEST_ASSERT_TRUE_MESSAGE(revision != lastRevision, message);
        lastRevision = revision;
    }

    void expectUnmoved(const char* reader) {
        uint32_t revision = repo.rulesRevision();
        char message[96];
        snprintf(message, sizeof(message), "%s must not change rulesRevision()", reader);
        TEST_ASSERT_TRUE_MESSAGE(revision == lastRevision, message);
    }
};

}  // namespace

void test_every_rule_repository_mutator_moves_the_revision(void) {
    RevisionFixture f;

    TEST_ASSERT_TRUE_MESSAGE(f.repo.save(makeRule(kRuleAUid)), "save must succeed");
    f.expectMoved("save");

    TEST_ASSERT_TRUE(f.repo.updateRuleState(kRuleAUid, RuleState::UNSYNCHRONIZED));
    f.expectMoved("updateRuleState");

    TEST_ASSERT_TRUE(f.repo.updateRuleError(kRuleAUid, RuleState::ERROR, "sensor_unreachable", "reason"));
    f.expectMoved("updateRuleError");

    TEST_ASSERT_TRUE(f.repo.updateRuleMode(kRuleAUid, RuleMode::OFF, "user"));
    f.expectMoved("updateRuleMode");

    std::vector<Rule> incoming;
    incoming.push_back(makeRule(kRuleBUid));
    TEST_ASSERT_EQUAL(1, f.repo.synchronizeRules(incoming));
    f.expectMoved("synchronizeRules");

    std::vector<Rule> both;
    both.push_back(makeRule(kRuleAUid));
    both.push_back(makeRule(kRuleBUid));
    TEST_ASSERT_EQUAL(2, f.repo.saveAll(both));
    f.expectMoved("saveAll");

    TEST_ASSERT_TRUE(f.repo.remove(kRuleBUid));
    f.expectMoved("remove");

    TEST_ASSERT_EQUAL(1, f.repo.removeAll());
    f.expectMoved("removeAll");

    TEST_ASSERT_TRUE(f.repo.clear());
    f.expectMoved("clear");
}

void test_rule_repository_reads_leave_the_revision_alone(void) {
    RevisionFixture f;
    f.repo.save(makeRule(kRuleAUid));
    f.lastRevision = f.repo.rulesRevision();

    f.repo.getAll();
    f.expectUnmoved("getAll");

    f.repo.getActiveRules();
    f.expectUnmoved("getActiveRules");

    f.repo.get(kRuleAUid);
    f.expectUnmoved("get");

    f.repo.count();
    f.expectUnmoved("count");

    f.repo.exists(kRuleAUid);
    f.expectUnmoved("exists");

    f.repo.getRulesNeedingSynchronization();
    f.expectUnmoved("getRulesNeedingSynchronization");
}

extern "C" void run_rule_repository_revision_tests(void) {
    RUN_TEST(test_every_rule_repository_mutator_moves_the_revision);
    RUN_TEST(test_rule_repository_reads_leave_the_revision_alone);
}
