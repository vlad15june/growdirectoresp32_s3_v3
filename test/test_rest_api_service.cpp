// The HTTP transport itself, executed rather than compiled.
//
// RestAPIService runs on the host because it is handed its HttpClientAdapter: a class that built its own
// adapter in its constructor would drag in <HTTPClient.h> and could only be
// compiled for the device. `nm .pio/build/native/program | grep -c ' RestAPIService::'` shows whether it is in the
// host build. The per-endpoint retry rules of this transport are executed here, not left to the
// board.
//
// What is replaced here is the connection and nothing else: Sim/SimHttpClient.h records the
// requests the transport makes and answers what the test told it to answer. The URLs, the retry
// counts, the failure reporting and the counters below are the production code's, not a copy.
//
// The claims:
//
//  1. A method that gave its retries to the outbox makes exactly one request per call - stated as
//     a number, so that a loop coming back is a failure and not a slowdown nobody notices.
//  2. The methods that still own a retry loop are named here with the count they actually make.
//     Moving one of those loops into the outbox is a change that has to show up here.
//  3. httpFailed is set when the server did not answer or did not answer 200, and only then. A
//     200 with a body the module cannot use is not a transport failure.
//  4. The URLs are what the backend expects, character for character. That is a contract with a
//     service we do not own, and nothing else asserts it.

#include <unity.h>

#include "Infrastructure/Controllers/HTTP/RestApiService.h"

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"

#include "Sim/SimHttpClient.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::SimHttpClient;

namespace {

// Kept short on purpose: init() copies the server URL and the token into BUFSIZE (40) char
// buffers, so a realistic-length URL here would be silently truncated and every URL assertion
// below would be asserting the truncation.
const char *const kServerUrl = "http://gd.test";
const char *const kToken = "TOK123";
const char *const kModuleUid = "UID-1";

struct Fixture {
    MockModuleConfig moduleConfig;
    MockSettings settings;
    MockLogger logger;
    MockDebugSystem debugSystem;
    ArduinoJsonConverter jsonConverter;
    SimHttpClient http;
    RestAPIService service;

    Fixture()
        : service(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter)
    {
        settings.setServerUrl(kServerUrl);
        settings.setToken(kToken);
        moduleConfig.setModuleUid(kModuleUid);
        service.init();

        // The server says nothing unless a test says otherwise.
        http.alwaysRespond(-1, "");
        hostDelayTotalMs = 0;
    }
};

std::vector<ModuleCommandUpdateDto> oneCommandUpdate()
{
    std::vector<ModuleCommandUpdateDto> updates;
    updates.push_back(ModuleCommandUpdateDto("cmd-1", "Done"));
    return updates;
}

} // namespace

// --- One request per call: the methods whose retries the outbox took ------------------------

void test_a_module_command_fetch_makes_one_request_when_the_server_is_silent(void)
{
    Fixture f;

    f.service.getModuleCommand();

    // Three, not one, would be the `while (tries < 3)` loop back - and with setTimeout(30000)
    // that is 90 s held on the caller and on the transport's own mutex.
    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_EQUAL_UINT32(30000, f.http.lastRequest().timeoutMs);
}

void test_a_module_command_report_makes_one_request_when_the_server_refuses(void)
{
    Fixture f;
    f.http.alwaysRespond(500, "");

    f.service.sendModuleCommandExecutionResult(oneCommandUpdate());

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_EQUAL_STRING("POST", f.http.lastRequest().method.c_str());
}

void test_a_rule_list_fetch_makes_one_request_when_the_server_is_silent(void)
{
    Fixture f;

    f.service.getRuleList();

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
}

void test_a_device_command_fetch_makes_one_request_when_the_server_is_silent(void)
{
    Fixture f;

    f.service.getDeviceCommand();

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
}

void test_a_diagnostic_send_costs_one_connection_check_and_one_post(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "");

    DiagnosticDto diagnostic;
    f.service.sendDiagnosticData(diagnostic);

    // Two requests, and both are the production path: sendDiagnosticData() pings
    // /TestConnection through isModuleOnline() before it posts anything.
    TEST_ASSERT_EQUAL_size_t(2, f.http.requestCount());
    TEST_ASSERT_EQUAL_STRING("GET", f.http.requests()[0].method.c_str());
    TEST_ASSERT_EQUAL_STRING("http://gd.test/api/v1/HardWare/TestConnection?token=TOK123&grow_director_uid=UID-1",
                             f.http.requests()[0].url.c_str());
    TEST_ASSERT_EQUAL_STRING("POST", f.http.requests()[1].method.c_str());
}

// --- One attempt per call: retries belong to the outbox ----------------------------------------------
//
// These two pin the single attempt of the transport - one request each, and no delay in the
// catalogue fetch - so that putting a retry loop back here has to change these lines rather
// than pass silently.

void test_the_structure_upload_makes_one_request_and_leaves_retries_to_the_outbox(void)
{
    Fixture f;

    ModuleStructure structure;
    f.service.sendToServerModuleStructure(structure);

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
}

void test_the_sensor_model_fetch_makes_one_request_and_sleeps_for_nothing(void)
{
    Fixture f;

    f.service.getSensorTypesList();

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    // The delay(200) per attempt went out with the loop. A caller that needs to try again now
    // pays that cost where it can be seen - at its own call site, or in the outbox.
    TEST_ASSERT_EQUAL_UINT32(0, hostDelayTotalMs);
}

// --- httpFailed: set when the request failed, and only then ----------------------------------

void test_http_failed_is_set_when_the_server_does_not_answer(void)
{
    Fixture f;
    bool httpFailed = false;

    f.service.getModuleCommand(&httpFailed);

    TEST_ASSERT_TRUE(httpFailed);
}

void test_http_failed_is_set_when_the_server_answers_other_than_200(void)
{
    Fixture f;
    f.http.alwaysRespond(500, "server on fire");
    bool httpFailed = false;

    f.service.getModuleCommand(&httpFailed);

    TEST_ASSERT_TRUE(httpFailed);
}

void test_http_failed_stays_clear_on_a_200_with_an_empty_body(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "");
    bool httpFailed = true;

    std::vector<ModuleCommandDto> commands = f.service.getModuleCommand(&httpFailed);

    // No commands and no failure are different answers, and the caller retries on one of them.
    TEST_ASSERT_FALSE(httpFailed);
    TEST_ASSERT_EQUAL_size_t(0, commands.size());
}

void test_http_failed_stays_clear_on_a_200_that_carries_a_command(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "[{\"uid\":\"cmd-1\",\"command\":\"ScanSensors\"}]");
    bool httpFailed = true;

    std::vector<ModuleCommandDto> commands = f.service.getModuleCommand(&httpFailed);

    TEST_ASSERT_FALSE(httpFailed);
    TEST_ASSERT_EQUAL_size_t(1, commands.size());
    TEST_ASSERT_EQUAL_STRING("cmd-1", commands[0].uid.c_str());
}

void test_a_rule_list_the_module_cannot_parse_is_reported_as_a_failure(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "not json at all");
    bool httpFailed = false;

    std::vector<Rule> rules = f.service.getRuleList(&httpFailed);

    // An unreadable 200 is not "this module has no rules": saying so would let the caller apply
    // an empty snapshot and delete every rule the module has.
    TEST_ASSERT_TRUE(httpFailed);
    TEST_ASSERT_EQUAL_size_t(0, rules.size());
}

// --- The URLs, which are a contract with the backend -----------------------------------------

void test_the_module_command_url_carries_the_endpoint_the_token_and_the_uid(void)
{
    Fixture f;

    f.service.getModuleCommand();

    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/module_command?token=TOK123&grow_director_uid=UID-1&statuses=New",
        f.http.lastRequest().url.c_str());
}

void test_the_module_command_report_url_and_body_are_what_the_server_expects(void)
{
    Fixture f;

    f.service.sendModuleCommandExecutionResult(oneCommandUpdate());

    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/module_command?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
    TEST_ASSERT_EQUAL_STRING("[{\"uid\":\"cmd-1\",\"status\":\"Done\"}]",
                             f.http.lastRequest().body.c_str());
    TEST_ASSERT_EQUAL_size_t(1, f.http.lastRequest().headers.size());
    TEST_ASSERT_EQUAL_STRING("Content-Type", f.http.lastRequest().headers[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("application/json", f.http.lastRequest().headers[0].second.c_str());
}

void test_the_rule_list_url_carries_the_endpoint_the_token_and_the_uid(void)
{
    Fixture f;

    f.service.getRuleList();

    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/rulelist?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
}

void test_the_device_command_url_asks_only_for_new_commands(void)
{
    Fixture f;

    f.service.getDeviceCommand();

    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/devices_command?token=TOK123&grow_director_uid=UID-1&status=new",
        f.http.lastRequest().url.c_str());
}

void test_the_structure_url_is_built_from_the_settings_the_service_was_given(void)
{
    Fixture f;

    ModuleStructure structure;
    f.service.sendToServerModuleStructure(structure);

    // This one goes through buildUrl(), which fills in the module UID from IModuleConfig rather
    // than from the caller. Same query string as the hand-rolled snprintf sites above.
    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/structure?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
}

void test_the_sensor_model_url_keeps_its_lower_case_hardware_segment(void)
{
    Fixture f;

    f.service.getSensorTypesList();

    // "Hardware", not "HardWare" as in every other endpoint here. Whether the backend cares is
    // not ours to guess: the path is frozen as it ships.
    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/Hardware/sensor_models?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
}

// --- Failure counters and the connection check -----------------------------------------------

void test_a_silent_server_is_counted_and_an_answer_clears_the_offline_count(void)
{
    Fixture f;

    f.service.getModuleCommand();
    TEST_ASSERT_EQUAL_INT(1, f.service.getApiSendFails());
    TEST_ASSERT_EQUAL_INT(1, f.service.getCountOffline());

    f.http.alwaysRespond(200, "[{\"uid\":\"cmd-1\",\"command\":\"ScanSensors\"}]");
    f.service.getModuleCommand();

    // getApiSendFails() is a running total and is not cleared by a success; the offline streak is.
    TEST_ASSERT_EQUAL_INT(1, f.service.getApiSendFails());
    TEST_ASSERT_EQUAL_INT(0, f.service.getCountOffline());
}

void test_a_rule_list_that_is_empty_is_a_successful_fetch(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "[]");
    bool httpFailed = true; // start wrong, so a transport that never writes it fails here

    std::vector<Rule> rules = f.service.getRuleList(&httpFailed);

    // This test asserted the opposite until the empty list became an outcome of its own. Running
    // this code for the first time froze the behaviour as it stood: rulesFromJson() answered "did
    // I build at least one rule", and httpFailed was wired to that answer, so a server correctly
    // saying "this module has no rules" was indistinguishable from an unreadable body. The cost
    // was four outbox attempts, 10.5 s of backoff and a failed synchronization recorded every
    // five minutes, for ever, on every account with no rules.
    char message[176];
    snprintf(message, sizeof(message),
             "a 200 with [] came back as httpFailed=%d over %d request(s); an empty list from a "
             "server that answered is an answer, not a transport failure",
             httpFailed ? 1 : 0, (int)f.http.requests().size());
    TEST_ASSERT_FALSE_MESSAGE(httpFailed, message);
    TEST_ASSERT_EQUAL_size_t(0, rules.size());

    // One request, and one only: the retry budget belongs to the outbox, and there is nothing to
    // retry here in the first place.
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, f.http.requests().size(), message);
    TEST_ASSERT_EQUAL_INT(0, f.service.getApiSendFails());
}

// The other half of the same distinction, and the guard that is easiest to lose here: a body the
// module cannot read must still be a failure. Without it a failed fetch reaches SyncRulesUseCase
// as an empty snapshot, and the module reports a successful sync for an answer it never understood.
void test_a_rule_list_that_is_not_a_list_is_still_a_failed_fetch(void)
{
    const char *const notLists[] = {"{\"rules\":[]}", "not json at all", "[{,]", "["};

    for (size_t i = 0; i < sizeof(notLists) / sizeof(notLists[0]); i++)
    {
        Fixture f;
        f.http.alwaysRespond(200, notLists[i]);
        bool httpFailed = false;

        std::vector<Rule> rules = f.service.getRuleList(&httpFailed);

        char message[176];
        snprintf(message, sizeof(message),
                 "a 200 carrying %s was accepted as a readable rule list; the module would record "
                 "a successful synchronization against a response it did not understand",
                 notLists[i]);
        TEST_ASSERT_TRUE_MESSAGE(httpFailed, message);
        TEST_ASSERT_EQUAL_size_t(0, rules.size());
    }
}

// A 200 with no body at all. getString() answers "" both when the response carried nothing and
// when the stream could not be read, so this is the same "I could not read it" as a malformed
// body - not "the server says there are no rules". A rule list that let it out
// of the bottom of the method would report a delivered empty list, while the sibling sensor-model method
// reports it as a failure.
void test_a_rule_list_with_no_body_at_all_is_a_failed_fetch(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "");
    bool httpFailed = false;

    std::vector<Rule> rules = f.service.getRuleList(&httpFailed);

    char message[192];
    snprintf(message, sizeof(message),
             "a 200 with no body came back as httpFailed=%d; a body the module could not read is "
             "not the server saying it has no rules",
             httpFailed ? 1 : 0);
    TEST_ASSERT_TRUE_MESSAGE(httpFailed, message);
    TEST_ASSERT_EQUAL_size_t(0, rules.size());
}

// The same guard, reached the other way: no converter to read the body with. Not a live risk on
// the device - globals.h always passes one - but it is the same `if`, and leaving it half
// asserted would let one half of the guard break unnoticed.
void test_a_rule_list_is_a_failed_fetch_when_there_is_no_converter_to_read_it(void)
{
    SimHttpClient http;
    MockModuleConfig moduleConfig;
    MockSettings settings;
    settings.setServerUrl(kServerUrl);
    settings.setToken(kToken);
    moduleConfig.setModuleUid(kModuleUid);
    RestAPIService service(&http, &moduleConfig, &settings);
    service.init();
    http.alwaysRespond(200, "[{\"uid\":\"rule-1\"}]");

    bool httpFailed = false;
    std::vector<Rule> rules = service.getRuleList(&httpFailed);

    TEST_ASSERT_TRUE_MESSAGE(httpFailed,
                             "a body that could not be read for want of a converter was reported "
                             "as a successful fetch of no rules");
    TEST_ASSERT_EQUAL_size_t(0, rules.size());
}

void test_a_sensor_model_catalog_that_is_empty_is_a_successful_fetch(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "[]");
    bool httpFailed = true;

    std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    char message[192];
    snprintf(message, sizeof(message),
             "a 200 with [] came back as httpFailed=%d over %d request(s); an account with no "
             "sensor models is an answer, and it is what handleEmptyServerResponse() exists for",
             httpFailed ? 1 : 0, (int)f.http.requests().size());
    TEST_ASSERT_FALSE_MESSAGE(httpFailed, message);
    TEST_ASSERT_EQUAL_size_t(0, types.size());
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, f.http.requests().size(), message);
}

void test_a_sensor_model_catalog_that_is_not_a_list_is_still_a_failed_fetch(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "garbage");
    bool httpFailed = false;

    std::vector<SensorType> types = f.service.getSensorTypesList(&httpFailed);

    // The brace scanner in ArduinoJsonConverter finds objects, it does not validate; a body with
    // no braces scans to zero objects and would look exactly like an empty catalog without the
    // shape check in front of it.
    TEST_ASSERT_TRUE_MESSAGE(httpFailed,
                             "a 200 carrying an unreadable body was accepted as an empty catalog");
    TEST_ASSERT_EQUAL_size_t(0, types.size());
}

void test_the_connection_check_calls_any_answer_online_including_a_500(void)
{
    Fixture f;
    f.http.alwaysRespond(500, "");

    // isModuleOnline() tests `httpResponseCode > 0`, so a server answering 500 - or 404 - counts
    // as reachable. Recorded as it is, because sendDiagnosticData() gates on it.
    TEST_ASSERT_TRUE(f.service.isModuleOnline());
    TEST_ASSERT_EQUAL_UINT32(2000, f.http.requests()[0].timeoutMs);
}

// --- Ownership of the adapter -----------------------------------------------------------------

void test_the_transport_closes_the_connection_it_was_lent_but_does_not_free_it(void)
{
    SimHttpClient http;
    MockModuleConfig moduleConfig;
    MockSettings settings;
    settings.setServerUrl(kServerUrl);
    settings.setToken(kToken);
    moduleConfig.setModuleUid(kModuleUid);

    {
        RestAPIService service(&http, &moduleConfig, &settings);
        service.init();
        service.isModuleOnline();
    }

    // The adapter outlives the transport and is still usable: it is the caller's, and the
    // destructor closes the connection without deleting it. Reading it here would be a
    // use-after-free if that ever changed back.
    TEST_ASSERT_EQUAL_size_t(1, http.requestCount());
    http.end();
    TEST_ASSERT_TRUE(http.endCount() > 0);
}

// ============================================================================================
// The seven methods no test executed at all.
//
// Without these tests, breaking the endpoint
// string inside queryModuleDeletion() leaves the whole suite green. Everything below freezes
// behaviour as it ships. Where the behaviour looks wrong it is still frozen and named in a
// comment, because changing it here would be a silent fix.
// ============================================================================================

// --- queryModuleDeletion(): the verdict behind the factory reset -----------------------------
//
// On DELETED, ModuleSetupService sets the mode back to USER_CAN_CONFIGURE, calls clearAllData()
// - rules, sensors, sensor types - and reboots. CheckModuleDeletionUseCase returns DELETED on
// `received200 && isDeletedFlag == 1`, and also on `allTries403`. So every field this method
// fills is one step away from erasing a greenhouse's configuration.

void test_the_deletion_check_asks_the_endpoint_the_backend_publishes(void)
{
    Fixture f;

    f.service.queryModuleDeletion();

    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/is_deleted?token=TOK123&grow_director_uid=UID-1",
        f.http.requests()[0].url.c_str());
    TEST_ASSERT_EQUAL_STRING("GET", f.http.requests()[0].method.c_str());
    // 10 s here, not the 30 s every other endpoint uses.
    TEST_ASSERT_EQUAL_UINT32(10000, f.http.requests()[0].timeoutMs);
}

void test_the_deletion_check_stops_at_its_first_200_and_still_sleeps(void)
{
    Fixture f;
    f.http.queueResponse(200, "{\"isDeleted\":false}");

    ModuleDeletionQueryResult result = f.service.queryModuleDeletion();

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_TRUE(result.received200);
    TEST_ASSERT_FALSE(result.queryFailed);
    TEST_ASSERT_EQUAL_INT(200, result.finalHttpCode);
    // delay(200) sits inside the loop before the counter, so even the answer that ends the loop
    // is followed by a 200 ms sleep on the calling task.
    TEST_ASSERT_EQUAL_UINT32(200, hostDelayTotalMs);
}

void test_the_deletion_check_tries_three_times_when_the_server_is_silent(void)
{
    Fixture f;

    ModuleDeletionQueryResult result = f.service.queryModuleDeletion();

    TEST_ASSERT_EQUAL_size_t(3, f.http.requestCount());
    TEST_ASSERT_EQUAL_UINT32(600, hostDelayTotalMs);
    TEST_ASSERT_FALSE(result.received200);
    TEST_ASSERT_FALSE(result.allTries403);
    TEST_ASSERT_TRUE(result.queryFailed);
    TEST_ASSERT_EQUAL_INT(-1, result.finalHttpCode);
    TEST_ASSERT_EQUAL_INT(-1, result.isDeletedFlag);
    TEST_ASSERT_EQUAL_INT(1, f.service.getApiSendFails());
}

void test_the_deletion_check_keeps_trying_until_it_gets_its_200(void)
{
    Fixture f;
    f.http.queueResponse(-1, "");
    f.http.queueResponse(500, "");
    f.http.queueResponse(200, "{\"isDeleted\":true}");

    ModuleDeletionQueryResult result = f.service.queryModuleDeletion();

    TEST_ASSERT_EQUAL_size_t(3, f.http.requestCount());
    TEST_ASSERT_TRUE(result.received200);
    TEST_ASSERT_EQUAL_INT(200, result.finalHttpCode);
    TEST_ASSERT_EQUAL_INT(1, result.isDeletedFlag);
}

void test_three_forbidden_answers_are_reported_as_a_sustained_403(void)
{
    Fixture f;
    f.http.queueResponse(403, "");
    f.http.queueResponse(403, "");
    f.http.queueResponse(403, "");

    ModuleDeletionQueryResult result = f.service.queryModuleDeletion();

    // CheckModuleDeletionUseCase turns this into DELETED without ever seeing a body: three 403s
    // are treated as "the server no longer knows this module".
    TEST_ASSERT_EQUAL_size_t(3, f.http.requestCount());
    TEST_ASSERT_TRUE(result.allTries403);
    TEST_ASSERT_FALSE(result.received200);
    TEST_ASSERT_FALSE(result.queryFailed);
    TEST_ASSERT_EQUAL_INT(403, result.finalHttpCode);
}

void test_two_forbidden_answers_and_a_silence_are_not_a_sustained_403(void)
{
    Fixture f;
    f.http.queueResponse(403, "");
    f.http.queueResponse(403, "");
    f.http.queueResponse(-1, "");

    ModuleDeletionQueryResult result = f.service.queryModuleDeletion();

    // The count has to reach exactly three. One dropped connection in the middle of a genuine
    // revocation turns the verdict into UNKNOWN, which is the safe direction.
    TEST_ASSERT_FALSE(result.allTries403);
    TEST_ASSERT_TRUE(result.queryFailed);
}

void test_query_failed_reports_the_last_attempt_and_not_the_worst_one(void)
{
    Fixture f;
    f.http.queueResponse(-1, "");
    f.http.queueResponse(-1, "");
    f.http.queueResponse(500, "");

    ModuleDeletionQueryResult result = f.service.queryModuleDeletion();

    // Two of three attempts never reached the server, and queryFailed is false: the field is
    // `httpResponseCode <= 0` evaluated after the loop, so it only ever describes attempt three.
    TEST_ASSERT_FALSE(result.queryFailed);
    TEST_ASSERT_EQUAL_INT(500, result.finalHttpCode);
    TEST_ASSERT_FALSE(result.received200);
}

void test_a_body_saying_deleted_sets_the_flag(void)
{
    Fixture f;
    f.http.queueResponse(200, "{\"isDeleted\":true}");

    TEST_ASSERT_EQUAL_INT(1, f.service.queryModuleDeletion().isDeletedFlag);
}

void test_a_body_saying_not_deleted_clears_the_flag(void)
{
    Fixture f;
    f.http.queueResponse(200, "{\"isDeleted\":false}");

    TEST_ASSERT_EQUAL_INT(0, f.service.queryModuleDeletion().isDeletedFlag);
}

void test_a_body_without_the_field_leaves_the_flag_unknown(void)
{
    Fixture f;
    f.http.queueResponse(200, "{\"status\":\"ok\",\"deleted\":true}");

    // No `"isDeleted"` substring, so the search never starts and the flag keeps its -1. The
    // use case reads that as UNKNOWN, which is why a renamed field fails safe.
    TEST_ASSERT_EQUAL_INT(-1, f.service.queryModuleDeletion().isDeletedFlag);
}

void test_an_empty_body_with_a_200_leaves_the_flag_unknown(void)
{
    Fixture f;
    f.http.queueResponse(200, "");

    ModuleDeletionQueryResult result = f.service.queryModuleDeletion();

    TEST_ASSERT_TRUE(result.received200);
    TEST_ASSERT_EQUAL_INT(-1, result.isDeletedFlag);
}

void test_the_word_true_before_the_field_does_not_decide_the_verdict(void)
{
    Fixture f;
    f.http.queueResponse(200, "{\"name\":\"true-north\",\"isDeleted\":false}");

    // The search is anchored at the field, so a "true" earlier in the body is out of reach.
    TEST_ASSERT_EQUAL_INT(0, f.service.queryModuleDeletion().isDeletedFlag);
}

void test_the_word_true_after_the_field_decides_the_verdict_whatever_it_belongs_to(void)
{
    Fixture f;
    f.http.queueResponse(200, "{\"isDeleted\":null,\"archived\":true}");

    // Frozen, not fixed - and this is the sharp edge of parsing by substring. `isDeleted` is
    // null here; the 1 comes from `archived`. Through CheckModuleDeletionUseCase that is a
    // verdict of DELETED, and through ModuleSetupService a factory reset: mode back to
    // USER_CAN_CONFIGURE, clearAllData(), reboot. Any field the backend ever adds after
    // isDeleted whose value is `true`, with no `false` between them, does this.
    TEST_ASSERT_EQUAL_INT(1, f.service.queryModuleDeletion().isDeletedFlag);
}

// --- sendFirmwareVersion() --------------------------------------------------------------------

void test_the_firmware_report_url_and_body_are_what_the_server_expects(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "");

    TEST_ASSERT_TRUE(f.service.sendFirmwareVersion());
    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/firmware?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
    // Two spellings of the same identifier in one request: snake_case in the query string,
    // camelCase in the body, and a space after each colon. Contract, frozen as it ships.
    TEST_ASSERT_EQUAL_STRING("{\"version\": \"2.1.1\",\"growdirectorUid\": \"UID-1\"}",
                             f.http.lastRequest().body.c_str());
}

void test_the_firmware_report_retries_three_times_without_pausing(void)
{
    Fixture f;

    TEST_ASSERT_FALSE(f.service.sendFirmwareVersion());
    TEST_ASSERT_EQUAL_size_t(3, f.http.requestCount());
    // The only retry loop in this file with no delay() in it: three attempts of setTimeout(30000)
    // back to back. It runs on the startup path, holding _apiMutex for up to 90 s.
    TEST_ASSERT_EQUAL_UINT32(0, hostDelayTotalMs);
    TEST_ASSERT_EQUAL_INT(1, f.service.getApiSendFails());
}

// --- sendAnalogInputDeviceCommandExecutionResult() ---------------------------------------------

void test_the_analog_command_report_posts_its_message_verbatim(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "");

    TEST_ASSERT_TRUE(f.service.sendAnalogInputDeviceCommandExecutionResult(
        "[{\"uid\":\"dev-1\",\"status\":\"Done\"}]"));

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    // Same endpoint as sendDeviceCommandExecutionResult(), with a different retry policy.
    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/devices_command?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
    TEST_ASSERT_EQUAL_STRING("[{\"uid\":\"dev-1\",\"status\":\"Done\"}]",
                             f.http.lastRequest().body.c_str());
}

void test_the_analog_command_report_still_owns_a_three_attempt_loop(void)
{
    Fixture f;

    TEST_ASSERT_FALSE(f.service.sendAnalogInputDeviceCommandExecutionResult(
        "[{\"uid\":\"dev-1\",\"status\":\"Done\"}]"));

    TEST_ASSERT_EQUAL_size_t(3, f.http.requestCount());
    TEST_ASSERT_EQUAL_UINT32(600, hostDelayTotalMs);
}

void test_a_short_analog_message_is_dropped_without_a_request_or_a_count(void)
{
    Fixture f;

    TEST_ASSERT_FALSE(f.service.sendAnalogInputDeviceCommandExecutionResult("[]"));

    // `strlen(msg) < 10` refuses the send. Nothing goes out, and nothing is counted either:
    // the caller gets false and the failure counters stay at zero, so a run of dropped reports
    // is invisible to the diagnostic path.
    TEST_ASSERT_EQUAL_size_t(0, f.http.requestCount());
    TEST_ASSERT_EQUAL_INT(0, f.service.getApiSendFails());
    TEST_ASSERT_EQUAL_INT(0, f.service.getCountOffline());
}

// --- sendDeviceCommandExecutionResult() ---------------------------------------------------------

void test_the_device_command_report_sends_what_the_converter_produced(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "");

    DeviceCommandUpdateDto update;
    update.uid = "dev-1";
    update.status = "Done";

    TEST_ASSERT_TRUE(f.service.sendDeviceCommandExecutionResult(update));

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/devices_command?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
    // The claim is about the transport, not about the JSON shape: whatever the converter
    // produced is what goes on the wire, unmodified.
    TEST_ASSERT_EQUAL_STRING(f.jsonConverter.deviceCommandUpdateToJson(update).c_str(),
                             f.http.lastRequest().body.c_str());
    TEST_ASSERT_EQUAL_STRING("application/json", f.http.lastRequest().headers[0].second.c_str());
}

void test_an_empty_device_command_report_is_still_sent(void)
{
    Fixture f;
    f.http.alwaysRespond(500, "");

    DeviceCommandUpdateDto update;
    TEST_ASSERT_FALSE(f.service.sendDeviceCommandExecutionResult(update));

    // The `jsonMsg.length() < 10` guard above cannot fire: deviceCommandUpdateToJson() always
    // emits at least `[{"uid":"","status":"","errorMessage":""}]`. Frozen to say so - a
    // default-constructed update is posted, not dropped.
    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_EQUAL_INT(1, f.service.getApiSendFails());
}

// --- sendModuleUpdateData() ----------------------------------------------------------------------

void test_the_module_update_url_and_body_are_what_the_server_expects(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "");

    UpdateDto update;
    update.growdirectorUid = kModuleUid;
    update.email = "grower@gd.test";

    TEST_ASSERT_TRUE(f.service.sendModuleUpdateData(update));

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/update?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
    TEST_ASSERT_EQUAL_STRING(f.jsonConverter.updateToJson(update).c_str(),
                             f.http.lastRequest().body.c_str());
}

void test_a_refused_module_update_is_one_request_and_a_counted_failure(void)
{
    Fixture f;
    f.http.alwaysRespond(503, "");

    UpdateDto update;
    TEST_ASSERT_FALSE(f.service.sendModuleUpdateData(update));

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_EQUAL_INT(1, f.service.getApiSendFails());
}

// --- sendRuleStateCommand() and sendByUidRuleStateCommand() ---------------------------------------

void test_the_rule_state_url_and_body_are_what_the_server_expects(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "");

    TEST_ASSERT_TRUE(f.service.sendRuleStateCommand("{\"state\":\"fired\"}"));

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/rules/state?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"fired\"}", f.http.lastRequest().body.c_str());
    // Closes the previous connection before opening its own, then closes its own: two end()s.
    TEST_ASSERT_EQUAL_INT(2, f.http.endCount());
}

void test_the_by_uid_rule_state_url_carries_the_rule_in_its_path(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "");

    TEST_ASSERT_TRUE(f.service.sendByUidRuleStateCommand("rule-7", "{\"state\":\"idle\"}"));

    TEST_ASSERT_EQUAL_size_t(1, f.http.requestCount());
    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/rules/rule-7/state?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"idle\"}", f.http.lastRequest().body.c_str());
    // One end(), not two: unlike its sibling above this method opens without closing first.
    // Frozen as an observation, not endorsed.
    TEST_ASSERT_EQUAL_INT(1, f.http.endCount());
}

void test_a_refused_rule_state_command_is_a_counted_failure(void)
{
    Fixture f;
    f.http.alwaysRespond(400, "");

    TEST_ASSERT_FALSE(f.service.sendRuleStateCommand("{\"state\":\"fired\"}"));
    TEST_ASSERT_FALSE(f.service.sendByUidRuleStateCommand("rule-7", "{\"state\":\"idle\"}"));

    TEST_ASSERT_EQUAL_size_t(2, f.http.requestCount());
    TEST_ASSERT_EQUAL_INT(2, f.service.getApiSendFails());
}

// --- what these requests leave on the log --------------------------------------------------------

namespace {

// Every method of this transport that writes the URL it is about to call. The list is the point:
// a new one added without a thought for what the URL carries is a line short of this walk.
void everyRequestThatWritesItsUrl(Fixture &f)
{
    ModuleStructure structure;
    f.service.sendToServerModuleStructure(structure);
    f.service.sendFirmwareVersion();

    DeviceCommandUpdateDto deviceUpdate;
    deviceUpdate.uid = "dev-1";
    deviceUpdate.status = "Done";
    f.service.sendDeviceCommandExecutionResult(deviceUpdate);
    f.service.sendAnalogInputDeviceCommandExecutionResult("[{\"uid\":\"dev-1\",\"status\":\"Done\"}]");
    f.service.sendModuleCommandExecutionResult(oneCommandUpdate());

    f.service.getDeviceCommand();
    f.service.getModuleCommand();
    f.service.getRuleList();
    f.service.getSensorTypesList();
    f.service.queryModuleDeletion();
}

std::string everythingLogged(const Fixture &f)
{
    std::string all;
    for (size_t i = 0; i < f.logger.messages.size(); i++)
    {
        all += "\n      | " + f.logger.messages[i];
    }
    return all.empty() ? std::string(" (none)") : all;
}

} // namespace

/**
 * The account token is in the query of every URL this transport builds, and it is a long-lived
 * secret: whoever has it can speak to the backend as this module's account. It goes on the wire
 * and nowhere else - not onto a line that a serial capture, a support bundle or, one channel
 * switch away, an MQTT topic would carry.
 */
void test_no_log_line_repeats_the_account_token(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "[]");

    everyRequestThatWritesItsUrl(f);

    const std::string secret = std::string("token=") + kToken;
    const std::string tape = "the transport wrote:" + everythingLogged(f);
    for (size_t i = 0; i < f.logger.messages.size(); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(f.logger.messages[i].find(secret) == std::string::npos,
                                 tape.c_str());
    }
}

/**
 * And the lines are still there, saying which request they are about. Without this the assertion
 * above would pass on a transport that stopped logging - or on a fixture that captured nothing.
 */
void test_the_request_lines_still_name_the_endpoint_they_are_about(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "[]");

    everyRequestThatWritesItsUrl(f);

    const std::string tape = "the transport wrote:" + everythingLogged(f);
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("/api/v1/HardWare/structure"), tape.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("/api/v1/HardWare/firmware"), tape.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("/api/v1/HardWare/devices_command"), tape.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("/api/v1/HardWare/module_command"), tape.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("/api/v1/HardWare/rulelist"), tape.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("/api/v1/Hardware/sensor_models"), tape.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.logger.hasMessageContaining("/api/v1/HardWare/is_deleted"), tape.c_str());
}

/**
 * The wire is untouched: what the log stops saying, the request still carries.
 */
void test_the_token_the_log_omits_is_still_on_the_wire(void)
{
    Fixture f;
    f.http.alwaysRespond(200, "[]");

    f.service.getRuleList();

    TEST_ASSERT_EQUAL_STRING(
        "http://gd.test/api/v1/HardWare/rulelist?token=TOK123&grow_director_uid=UID-1",
        f.http.lastRequest().url.c_str());
}

/**
 * One line is one line. A format string ending in "\n" adds a blank row to the serial capture,
 * because the logger writes the newline of its own.
 */
void test_the_failed_ping_is_one_line_with_nothing_after_it(void)
{
    Fixture f;

    TEST_ASSERT_FALSE(f.service.isModuleOnline());

    const std::string tape = "the ping wrote:" + everythingLogged(f);
    bool said = false;
    for (size_t i = 0; i < f.logger.messages.size(); i++)
    {
        if (f.logger.messages[i].find("PING_ERROR") != std::string::npos)
        {
            said = true;
            TEST_ASSERT_EQUAL_STRING_MESSAGE("PING_ERROR: HTTP Response code: -1",
                                             f.logger.messages[i].c_str(), tape.c_str());
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(said, tape.c_str());
}

// --- the diagnostic counters DiagnosticService reads and resets ------------------------------------

void test_the_failure_counters_can_be_read_and_reset(void)
{
    Fixture f;

    f.service.getRuleList();
    f.service.getRuleList();

    TEST_ASSERT_EQUAL_INT(2, f.service.getApiSendFails());
    TEST_ASSERT_EQUAL_FLOAT(2.0f, f.service.getApiSendFailRatio());
    TEST_ASSERT_EQUAL_INT(2, f.service.getCountOffline());

    f.service.resetApiSendFails();
    f.service.resetApiSendFailRatio();
    f.service.resetCountOffline();

    TEST_ASSERT_EQUAL_INT(0, f.service.getApiSendFails());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.service.getApiSendFailRatio());
    TEST_ASSERT_EQUAL_INT(0, f.service.getCountOffline());
}

extern "C" void run_rest_api_service_tests(void)
{
    RUN_TEST(test_a_module_command_fetch_makes_one_request_when_the_server_is_silent);
    RUN_TEST(test_a_module_command_report_makes_one_request_when_the_server_refuses);
    RUN_TEST(test_a_rule_list_fetch_makes_one_request_when_the_server_is_silent);
    RUN_TEST(test_a_device_command_fetch_makes_one_request_when_the_server_is_silent);
    RUN_TEST(test_a_diagnostic_send_costs_one_connection_check_and_one_post);

    RUN_TEST(test_the_structure_upload_makes_one_request_and_leaves_retries_to_the_outbox);
    RUN_TEST(test_the_sensor_model_fetch_makes_one_request_and_sleeps_for_nothing);

    RUN_TEST(test_http_failed_is_set_when_the_server_does_not_answer);
    RUN_TEST(test_http_failed_is_set_when_the_server_answers_other_than_200);
    RUN_TEST(test_http_failed_stays_clear_on_a_200_with_an_empty_body);
    RUN_TEST(test_http_failed_stays_clear_on_a_200_that_carries_a_command);
    RUN_TEST(test_a_rule_list_the_module_cannot_parse_is_reported_as_a_failure);

    RUN_TEST(test_the_module_command_url_carries_the_endpoint_the_token_and_the_uid);
    RUN_TEST(test_the_module_command_report_url_and_body_are_what_the_server_expects);
    RUN_TEST(test_the_rule_list_url_carries_the_endpoint_the_token_and_the_uid);
    RUN_TEST(test_the_device_command_url_asks_only_for_new_commands);
    RUN_TEST(test_the_structure_url_is_built_from_the_settings_the_service_was_given);
    RUN_TEST(test_the_sensor_model_url_keeps_its_lower_case_hardware_segment);

    RUN_TEST(test_a_silent_server_is_counted_and_an_answer_clears_the_offline_count);
    RUN_TEST(test_a_rule_list_that_is_empty_is_a_successful_fetch);
    RUN_TEST(test_a_rule_list_that_is_not_a_list_is_still_a_failed_fetch);
    RUN_TEST(test_a_rule_list_with_no_body_at_all_is_a_failed_fetch);
    RUN_TEST(test_a_rule_list_is_a_failed_fetch_when_there_is_no_converter_to_read_it);
    RUN_TEST(test_a_sensor_model_catalog_that_is_empty_is_a_successful_fetch);
    RUN_TEST(test_a_sensor_model_catalog_that_is_not_a_list_is_still_a_failed_fetch);
    RUN_TEST(test_the_connection_check_calls_any_answer_online_including_a_500);

    RUN_TEST(test_the_transport_closes_the_connection_it_was_lent_but_does_not_free_it);

    // The deletion check, whose verdict reaches clearAllData().
    RUN_TEST(test_the_deletion_check_asks_the_endpoint_the_backend_publishes);
    RUN_TEST(test_the_deletion_check_stops_at_its_first_200_and_still_sleeps);
    RUN_TEST(test_the_deletion_check_tries_three_times_when_the_server_is_silent);
    RUN_TEST(test_the_deletion_check_keeps_trying_until_it_gets_its_200);
    RUN_TEST(test_three_forbidden_answers_are_reported_as_a_sustained_403);
    RUN_TEST(test_two_forbidden_answers_and_a_silence_are_not_a_sustained_403);
    RUN_TEST(test_query_failed_reports_the_last_attempt_and_not_the_worst_one);
    RUN_TEST(test_a_body_saying_deleted_sets_the_flag);
    RUN_TEST(test_a_body_saying_not_deleted_clears_the_flag);
    RUN_TEST(test_a_body_without_the_field_leaves_the_flag_unknown);
    RUN_TEST(test_an_empty_body_with_a_200_leaves_the_flag_unknown);
    RUN_TEST(test_the_word_true_before_the_field_does_not_decide_the_verdict);
    RUN_TEST(test_the_word_true_after_the_field_decides_the_verdict_whatever_it_belongs_to);

    RUN_TEST(test_the_firmware_report_url_and_body_are_what_the_server_expects);
    RUN_TEST(test_the_firmware_report_retries_three_times_without_pausing);

    RUN_TEST(test_the_analog_command_report_posts_its_message_verbatim);
    RUN_TEST(test_the_analog_command_report_still_owns_a_three_attempt_loop);
    RUN_TEST(test_a_short_analog_message_is_dropped_without_a_request_or_a_count);

    RUN_TEST(test_the_device_command_report_sends_what_the_converter_produced);
    RUN_TEST(test_an_empty_device_command_report_is_still_sent);

    RUN_TEST(test_the_module_update_url_and_body_are_what_the_server_expects);
    RUN_TEST(test_a_refused_module_update_is_one_request_and_a_counted_failure);

    RUN_TEST(test_the_rule_state_url_and_body_are_what_the_server_expects);
    RUN_TEST(test_the_by_uid_rule_state_url_carries_the_rule_in_its_path);
    RUN_TEST(test_a_refused_rule_state_command_is_a_counted_failure);

    RUN_TEST(test_no_log_line_repeats_the_account_token);
    RUN_TEST(test_the_request_lines_still_name_the_endpoint_they_are_about);
    RUN_TEST(test_the_token_the_log_omits_is_still_on_the_wire);
    RUN_TEST(test_the_failed_ping_is_one_line_with_nothing_after_it);

    RUN_TEST(test_the_failure_counters_can_be_read_and_reset);
}
