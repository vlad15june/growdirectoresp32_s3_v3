// What the module does with an answer that does not carry every rule of it.
//
// A rule snapshot is authoritative: SyncRulesUseCase hands the uids it received to
// IRuleRepository::synchronizeRules() as the complete set, and the merge marks every stored rule
// missing from that set DELETING - after which the next evaluation tick switches the rule's device
// off with the reason `rule_manualy_deleted` and purges the rule. That is the right answer when
// the server really did delete the rule, and it is a greenhouse turning itself off when the rule
// is missing for any other reason.
//
// Reading the answer one rule at a time creates exactly those other reasons, and this file is
// about them:
//
//   - a rule the module cannot parse. The rules around it are read, so the answer arrives one
//     rule short - which is indistinguishable, to the merge, from the server having deleted it;
//   - a rule larger than the module will accept, for the same reason;
//   - a rule past the number this module keeps. The refusal has to happen at intake, before the
//     module has built the rule it is about to refuse, and it must not look like a deletion
//     either.
//
// The answer in all three cases is the same and it is not "retire it": an answer the module could
// not read whole is not a statement about which rules exist. The rules the module holds stay
// exactly as they are, with their versions and their devices; the rules that did arrive are
// applied; the gap is counted and the server is told. Deletion still works - the backend sends it
// as an element with state Deleting and keeps the rule until the module acknowledges it
// (RuleDomainService.GetAllWithInnersForGrowDirectorProAsync filters on IsDeleted, which is set
// only by the acknowledgement), so a deletion does not depend on absence at all.
//
// What runs here is production code from the socket down to the device pin: the real
// RestAPIService reading a body the real ArduinoJsonConverter parses, the real SyncRulesUseCase,
// the real RuleRepository over the real SpiffsRuleDao, and the real EvaluateRulesUseCase driving a
// real device. Replaced: the socket, the flash device and the clock.

#include <unity.h>

#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Core/Domain/UseCases/Rules/SyncRulesUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Json/RuleStreamReader.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Core/Domain/Rules/MockRuleStateNotifier.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include "Sim/SimHttpClient.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::SimHttpClient;

namespace {

const char* const SERVER_URL = "http://gd.test";
const char* const TOKEN = "TOK123";
const char* const MODULE_UID = "UID-1";

const char* const DEVICE_A = "7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71";
const char* const SENSOR_A = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";

Rule makeRule(size_t index)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a%04x", (unsigned)index);

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
    rule.timeToSetPoint = 10;
    return rule;
}

/** @brief A rule object that is balanced JSON and still not a rule this module can read. */
std::string unreadableRuleJson(size_t index, int version)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a%04x", (unsigned)index);

    // A member with no value: the braces balance, so the array is cut into the same objects as
    // ever, and the parser refuses this one on its own.
    std::string json = "{\"uid\":\"";
    json += uid;
    json += "\",\"version\":";
    json += std::to_string(version);
    json += ",\"deviceUid\":\"";
    json += DEVICE_A;
    json += "\",\"ruleType\":\"sensor\",\"valueLow\":}";
    return json;
}

/** @brief A rule object bigger than the module will take, and otherwise perfectly good. */
std::string oversizedRuleJson(size_t index, IJsonConverter& converter)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a%04x", (unsigned)index);

    Rule rule;
    rule.uid = uid;
    // A version above the stored one, so that a module which took this rule anyway would replace
    // what it holds - which is what makes the refusal observable in the registry and not only in
    // the log.
    rule.version = 5;
    rule.deviceUid = DEVICE_A;
    rule.ruleType = RuleType::SCHEDULE;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    // Windows enough to run past the cap on one rule, whatever that cap is.
    const size_t slots = RuleStreamReader::MAX_RULE_BYTES / 45 + 40;
    for (size_t i = 0; i < slots; i++)
    {
        rule.schedule.push_back(ScheduleItem(1787000000 + (time_t)i * 3600, 600));
    }
    std::string json = converter.ruleToJson(rule);
    // The converter itself must not be what shortens it - if it did, this scenario would not be
    // the one the test claims to run.
    while (json.size() <= RuleStreamReader::MAX_RULE_BYTES)
    {
        json.insert(json.size() - 1, ",\"padding" + std::to_string(json.size()) + "\":\"" +
                                         std::string(64, 'x') + "\"");
    }
    return json;
}

int occurrences(const std::string& haystack, const std::string& needle)
{
    int count = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size()))
    {
        count++;
    }
    return count;
}

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

/** @brief A module from the socket to the device pin, wired the way globals.h wires it. */
struct ModuleStand
{
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockNTPTimeAdapter ntpTimeAdapter;
    MockArduinoAdapter arduino;
    MockSettings settings;
    MockModuleConfig moduleConfig;

    SimHttpClient http;
    RestAPIService restApi;
    HybridAPIService api;

    SpiffsRuleDao dao;
    RuleRepository repository;
    SyncRulesUseCase sync;

    DeviceDigitalOut device;
    OneDeviceModel deviceModel;
    MockSensorValueModel sensorValues;
    MockRuleStateNotifier notifier;
    EvaluateRulesUseCase evaluate;

    ModuleStand()
        : moduleConfig(&logger, &debugSystem, &settings),
          restApi(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter),
          api(&restApi, nullptr, &moduleConfig, &logger, &debugSystem),
          dao(&spiffs, &jsonConverter, &debugSystem, &logger),
          repository(&dao, &ntpTimeAdapter, &debugSystem, &logger),
          sync(&api, &repository, &arduino, &ntpTimeAdapter, &jsonConverter, &logger, &debugSystem),
          evaluate(&deviceModel, &repository, &arduino, &ntpTimeAdapter, &sensorValues, nullptr,
                   nullptr, &notifier)
    {
        debugSystem.featureEnabled = true;
        ntpTimeAdapter.setEpochTime(1787000000);
        moduleConfig.setModuleUid(MODULE_UID);
        settings.setServerUrl(SERVER_URL);
        settings.setToken(TOKEN);
        restApi.init();
        sync.setSettings(&settings);

        device.deviceUid = DEVICE_A;
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = DEVICE_STATE_OFF;
        deviceModel.device = &device;

        sensorValues.values[SENSOR_A] = 25.0f;
    }

    /** @brief Rules an earlier synchronization left on flash. */
    void seed(const std::vector<Rule>& rules)
    {
        TEST_ASSERT_TRUE(dao.saveAll(rules));
        repository.refreshCache();
        TEST_ASSERT_EQUAL_INT((int)rules.size(), repository.count());
    }

    /** @brief What the server answers the next pull with, as bytes on the wire. */
    void publish(const std::vector<std::string>& elements)
    {
        std::string body = "[";
        for (size_t i = 0; i < elements.size(); i++)
        {
            if (i > 0) body += ",";
            body += elements[i];
        }
        body += "]";
        http.alwaysRespond(200, body.c_str());
    }

    /** @brief One pull, applied and acknowledged. */
    void runOneSync()
    {
        TEST_ASSERT_TRUE(sync.forceSync());
        sync.loop();
    }

    /** @brief Is the device being driven by a rule right now? */
    bool deviceDriven() const
    {
        return device.state != DEVICE_STATE_OFF && device.state != DEVICE_STATE_NONE;
    }

    /** @brief One evaluation tick with the sensor answering. */
    void tick(unsigned long atMs)
    {
        sensorValues.lastReadTimes[SENSOR_A] = (uint32_t)atMs;
        arduino.setMillis(atMs);
        evaluate.loop();
    }

    /** @brief The body of the last POST the module made - the acknowledgement. */
    const std::string& lastPostBody() const
    {
        for (size_t i = http.requests().size(); i > 0; i--)
        {
            if (http.requests()[i - 1].method == "POST")
            {
                return http.requests()[i - 1].body;
            }
        }
        return _empty;
    }

private:
    std::string _empty;
};

} // namespace

// --------------------------------------------------------------------------------------------
// 1. A rule the module could not read is not a rule the server deleted
// --------------------------------------------------------------------------------------------

static void test_a_rule_the_module_cannot_read_does_not_retire_the_one_it_holds(void)
{
    ModuleStand stand;

    std::vector<Rule> stored;
    stored.push_back(makeRule(0));
    stored.push_back(makeRule(1));
    stored.push_back(makeRule(2));
    stand.seed(stored);

    // The rule in the middle drives the device, and it is running.
    stand.tick(500000);
    TEST_ASSERT_TRUE_MESSAGE(stand.deviceDriven(),
                             "the scenario did not start with a running rule");

    // The pull carries all three rules, and the middle one is one this module cannot read.
    std::vector<std::string> published;
    published.push_back(stand.jsonConverter.ruleToJson(stored[0]));
    published.push_back(unreadableRuleJson(1, 3));
    published.push_back(stand.jsonConverter.ruleToJson(stored[2]));
    stand.publish(published);

    stand.runOneSync();
    stand.tick(501000);

    const Utils::Optional<Rule> held = stand.repository.get(stored[1].uid.c_str());
    char message[288];
    snprintf(message, sizeof(message),
             "the module holds %d rules after an answer it could not read one rule of; the rule "
             "it could not read reads as state %d and its device is %s",
             stand.repository.count(), held.has_value() ? (int)held.value().state : -1,
             stand.deviceDriven() ? "still driven" : "switched off");

    TEST_ASSERT_TRUE_MESSAGE(held.has_value(), message);
    TEST_ASSERT_TRUE_MESSAGE(held.value().state != RuleState::DELETING, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, held.value().version,
                                  "the rule the module kept is not the rule it had");
    TEST_ASSERT_TRUE_MESSAGE(stand.deviceDriven(), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, stand.repository.count(), message);

    // The rules that did arrive are applied - a gap is not a reason to stop synchronizing.
    TEST_ASSERT_TRUE(stand.repository.get(stored[0].uid.c_str()).has_value());
    TEST_ASSERT_TRUE(stand.repository.get(stored[2].uid.c_str()).has_value());

    // And the gap is not silent.
    TEST_ASSERT_TRUE_MESSAGE(stand.logger.hasMessageContaining("could not be read and were skipped"),
                             "a rule left out of the answer was left out without a word");
}

// The same for a rule that arrived whole and is larger than this module takes: the module cannot
// run it, and that is still not a statement that the server deleted it.
static void test_a_rule_too_large_to_take_does_not_retire_the_one_it_holds(void)
{
    ModuleStand stand;

    std::vector<Rule> stored;
    stored.push_back(makeRule(0));
    stored.push_back(makeRule(1));
    stand.seed(stored);
    stand.tick(500000);
    TEST_ASSERT_TRUE_MESSAGE(stand.deviceDriven(),
                             "the scenario did not start with a running rule");

    std::vector<std::string> published;
    published.push_back(stand.jsonConverter.ruleToJson(stored[0]));
    published.push_back(oversizedRuleJson(1, stand.jsonConverter));
    stand.publish(published);

    stand.runOneSync();
    stand.tick(501000);

    const Utils::Optional<Rule> held = stand.repository.get(stored[1].uid.c_str());
    char message[256];
    snprintf(message, sizeof(message),
             "after an answer carrying a rule larger than the module takes, the module holds %d "
             "rules and the device is %s",
             stand.repository.count(),
             stand.deviceDriven() ? "still driven" : "switched off");

    TEST_ASSERT_TRUE_MESSAGE(held.has_value(), message);
    TEST_ASSERT_TRUE_MESSAGE(held.value().state != RuleState::DELETING, message);
    TEST_ASSERT_TRUE_MESSAGE(stand.deviceDriven(), message);

    // And the rule the module keeps is its own, not the one it refused: the oversized copy is a
    // schedule rule, so a module that took it anyway is holding a different rule under that uid.
    TEST_ASSERT_TRUE_MESSAGE(held.value().ruleType == RuleType::SENSOR,
                             "the module stored a rule it was supposed to refuse");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, held.value().version,
                                  "the module stored a rule it was supposed to refuse");
    TEST_ASSERT_EQUAL_INT_MESSAGE(20, (int)held.value().valueHigh,
                                  "the module stored a rule it was supposed to refuse");
    TEST_ASSERT_TRUE_MESSAGE(stand.logger.hasMessageContaining("were refused"),
                             "the rule the module would not take was refused without a word");
}

// And the case the sweep exists for still works: a deletion travels as an element of the answer,
// which is how the backend sends it, and it goes all the way to the device being switched off.
static void test_a_rule_the_server_deletes_is_still_deleted(void)
{
    ModuleStand stand;

    std::vector<Rule> stored;
    stored.push_back(makeRule(0));
    stand.seed(stored);
    stand.tick(500000);
    TEST_ASSERT_TRUE_MESSAGE(stand.deviceDriven(),
                             "the scenario did not start with a running rule");

    Rule deleted = makeRule(0);
    deleted.state = RuleState::DELETING;
    std::vector<std::string> published;
    published.push_back(stand.jsonConverter.ruleToJson(deleted));
    stand.publish(published);

    stand.runOneSync();
    stand.tick(501000);

    TEST_ASSERT_FALSE_MESSAGE(stand.repository.exists(stored[0].uid.c_str()),
                              "a rule the server deleted stayed in the registry");
    TEST_ASSERT_EQUAL_INT_MESSAGE(DEVICE_STATE_OFF, stand.device.state,
                                  "a rule the server deleted kept driving its device");
}

// The sweep by absence still works too, when the answer is one the module read whole.
static void test_a_rule_absent_from_a_complete_answer_is_still_retired(void)
{
    ModuleStand stand;

    std::vector<Rule> stored;
    stored.push_back(makeRule(0));
    stored.push_back(makeRule(1));
    stand.seed(stored);

    std::vector<std::string> published;
    published.push_back(stand.jsonConverter.ruleToJson(stored[0]));
    stand.publish(published);

    stand.runOneSync();

    const Utils::Optional<Rule> gone = stand.repository.get(stored[1].uid.c_str());
    TEST_ASSERT_TRUE(gone.has_value());
    TEST_ASSERT_TRUE_MESSAGE(gone.value().state == RuleState::DELETING,
                             "a rule the module read a complete answer without was not retired");
}

// --------------------------------------------------------------------------------------------
// 2. The number of rules is refused at intake, not after the answer is in memory
// --------------------------------------------------------------------------------------------

static void test_an_answer_longer_than_the_module_keeps_is_cut_at_intake(void)
{
    ModuleStand stand;

    // Far more rules than this module keeps - the shape the cap exists for.
    const size_t published = 100;
    std::vector<std::string> elements;
    for (size_t i = 0; i < published; i++)
    {
        elements.push_back(stand.jsonConverter.ruleToJson(makeRule(i)));
    }
    stand.publish(elements);

    bool httpFailed = true;
    const std::vector<Rule> read = stand.restApi.getRuleList(&httpFailed);

    char message[256];
    snprintf(message, sizeof(message),
             "the transport built %d rules out of an answer carrying %u: the rules past what this "
             "module keeps are refused before they are built, not after",
             (int)read.size(), (unsigned)published);
    TEST_ASSERT_FALSE_MESSAGE(httpFailed, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)RuleRepository::MAX_RULES, (int)read.size(), message);
    TEST_ASSERT_TRUE_MESSAGE(stand.logger.hasMessageContaining("were refused"),
                             "the rules the module did not take were refused without a word");
}

// And the acknowledgement stays as short as the module's own registry, however long the answer
// was: the report is one entry per rule the module read, not per rule the server sent.
static void test_the_report_does_not_grow_with_the_answer(void)
{
    ModuleStand stand;

    std::vector<std::string> elements;
    for (size_t i = 0; i < 100; i++)
    {
        elements.push_back(stand.jsonConverter.ruleToJson(makeRule(i)));
    }
    stand.publish(elements);

    stand.runOneSync();

    const std::string report = stand.lastPostBody();
    char message[256];
    snprintf(message, sizeof(message),
             "the acknowledgement of a 100-rule answer named %d rules and was %d bytes long",
             occurrences(report, "\"uid\":\""), (int)report.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)RuleRepository::MAX_RULES,
                                  occurrences(report, "\"uid\":\""), message);
}

// A cut answer is not a statement about which rules exist either: the module keeps what it holds.
static void test_an_answer_cut_at_intake_retires_nothing(void)
{
    ModuleStand stand;

    std::vector<Rule> stored;
    for (size_t i = 0; i < 4; i++)
    {
        stored.push_back(makeRule(100 + i));
    }
    stand.seed(stored);
    stand.tick(500000);
    TEST_ASSERT_TRUE_MESSAGE(stand.deviceDriven(),
                             "the scenario did not start with a running rule");

    // The server publishes a set of its own, longer than the module keeps, and none of it names
    // the rules the module is running.
    std::vector<std::string> elements;
    for (size_t i = 0; i < RuleRepository::MAX_RULES + 10; i++)
    {
        elements.push_back(stand.jsonConverter.ruleToJson(makeRule(i)));
    }
    stand.publish(elements);

    stand.runOneSync();
    stand.tick(501000);

    char message[256];
    snprintf(message, sizeof(message),
             "the module holds %d rules after an answer it had to cut, and its device is %s",
             stand.repository.count(),
             stand.deviceDriven() ? "still driven" : "switched off");
    for (size_t i = 0; i < stored.size(); i++)
    {
        const Utils::Optional<Rule> held = stand.repository.get(stored[i].uid.c_str());
        TEST_ASSERT_TRUE_MESSAGE(held.has_value(), message);
        TEST_ASSERT_TRUE_MESSAGE(held.value().state != RuleState::DELETING, message);
    }
    TEST_ASSERT_TRUE_MESSAGE(stand.deviceDriven(), message);
}

extern "C" void run_rule_snapshot_gaps_tests(void)
{
    printf("\n----- Running Rule Snapshot Gaps Tests -----\n");

    RUN_TEST(test_a_rule_the_module_cannot_read_does_not_retire_the_one_it_holds);
    RUN_TEST(test_a_rule_too_large_to_take_does_not_retire_the_one_it_holds);
    RUN_TEST(test_a_rule_the_server_deletes_is_still_deleted);
    RUN_TEST(test_a_rule_absent_from_a_complete_answer_is_still_retired);
    RUN_TEST(test_an_answer_longer_than_the_module_keeps_is_cut_at_intake);
    RUN_TEST(test_the_report_does_not_grow_with_the_answer);
    RUN_TEST(test_an_answer_cut_at_intake_retires_nothing);

    printf("\n----- Rule Snapshot Gaps Tests Complete -----\n");
}
