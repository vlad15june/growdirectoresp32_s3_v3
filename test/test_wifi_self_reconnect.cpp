// The link supervision carrying out what the troubleshooting decides, and where each act runs.
//
// The decisions themselves are LinkTroubleshooting's and are tested on their own
// (test_link_troubleshooting.cpp). What is tested here is the class between the module and the
// machine: which readings become which events, that the radio restart and the probe go to the
// network task and their ends come back from it, that a restart asked for with the station associated
// is the one that takes the association down, that the module is restarted - with its technical boot
// - only for a fault its parts reported, and that the outage is kept for the report.
//
// The outbox is a mock that records what was asked for, and the test hands the answers back as
// LoopServiceConfigureDone's drain does. The adapter's own reconnect() is asserted never to run on the
// caller while an outbox is wired: that call held the control loop for the seconds an association
// takes.

#include <unity.h>
#include "Application/Services/Net/LinkSupervisor.h"
#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Mock/Application/MockOutbox.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Infrastructure/Adapters/Http/MeteredTlsClient.h"

#include <cerrno>
#include <string>

static MockRestAPIService wr_mockRestApi;
static MockModuleConfig wr_mockModuleConfig;
static MockLogger wr_mockLogger;
static MockDebugSystem wr_mockDebugSystem;
static MockSettings wr_mockSettings;
static MockNTPTimeAdapter wr_mockTimeAdapter;
static MockWifiAdapter wr_mockWifiAdapter;
static MockArduinoAdapter wr_mockArduinoAdapter;
static MockMqttManager wr_mockMqttManager;
static ModuleStatusBoard* wr_board;
static LinkSupervisor* wr_linkSupervisor;
static MockOutbox* wr_outbox;

static const unsigned long TICK_MS = LinkSupervisor::TICK_INTERVAL_MS;
static unsigned long wr_now = 0;

static void wr_setup(void) {
    wr_mockRestApi.reset();
    wr_mockModuleConfig.reset();
    wr_mockLogger.reset();
    // reset() keeps the recorded lines; the tape assertions below are about this test's lines.
    wr_mockLogger.messages.clear();
    wr_mockDebugSystem.reset();
    wr_mockSettings.reset();
    wr_mockTimeAdapter.reset();
    wr_mockWifiAdapter.reset();
    wr_mockArduinoAdapter.reset();
    wr_mockMqttManager.reset();

    wr_mockDebugSystem.registerFeature(DebugFeatures::SETUP_DEBUG);
    wr_mockDebugSystem.setDebugFeatureState(DebugFeatures::SETUP_DEBUG, true);

    wr_mockModuleConfig.setSettings(&wr_mockSettings);
    wr_mockModuleConfig.setModuleUid("wifi-reconnect-uid");
    wr_mockModuleConfig.setModuleType(MODULE_TYPE_SENSOR);

    wr_mockWifiAdapter.setConnected(true);
    wr_mockMqttManager.isConnectedReturnValue = true;

    wr_now = 0;
    wr_mockArduinoAdapter.setMillis(wr_now);

    wr_board = new ModuleStatusBoard();
    wr_linkSupervisor = new LinkSupervisor(
        &wr_mockRestApi,
        &wr_mockModuleConfig,
        wr_mockLogger,
        wr_mockDebugSystem,
        &wr_mockWifiAdapter,
        &wr_mockTimeAdapter,
        &wr_mockArduinoAdapter,
        &wr_mockMqttManager
    );
    wr_linkSupervisor->setStatusBoard(wr_board);

    wr_outbox = new MockOutbox();
    wr_linkSupervisor->setOutbox(wr_outbox);
}

static void wr_teardown(void) {
    delete wr_linkSupervisor;
    wr_linkSupervisor = nullptr;
    delete wr_outbox;
    wr_outbox = nullptr;
    delete wr_board;
    wr_board = nullptr;
}

/** One supervision tick, thirty seconds after the last. */
static void wr_tick(void) {
    wr_now += TICK_MS;
    wr_mockArduinoAdapter.setMillis(wr_now);
    wr_linkSupervisor->superviseOnce();
}

static void wr_ticksFor(unsigned long ms) {
    for (unsigned long elapsed = 0; elapsed < ms; elapsed += TICK_MS) {
        wr_tick();
    }
}

/** The network task ends the restart it was handed: associated or not. */
static void wr_restartEnds(bool associated) {
    OutboxCompletion completion;
    completion.kind = OutboxKind::WIFI_RECONNECT;
    completion.ticket = wr_outbox->lastIssuedTicket();
    completion.delivered = associated;
    completion.attempts = 1;
    wr_linkSupervisor->onOutboxCompletion(completion);
}

/** The network task ends the probe it was handed, with what it found. */
static void wr_probeEnds(LinkReach gateway, LinkReach publicAddress) {
    LinkProbeResult found;
    found.gateway = gateway;
    found.publicAddress = publicAddress;
    OutboxCompletion completion;
    completion.kind = OutboxKind::LINK_PROBE;
    completion.ticket = wr_outbox->lastIssuedTicket();
    completion.delivered = true;
    completion.attempts = 1;
    completion.linkProbe = found.encode();
    wr_linkSupervisor->onOutboxCompletion(completion);
}

static void wr_servicesFail(void) {
    wr_mockMqttManager.isConnectedReturnValue = false;
    wr_mockRestApi.countOffline = 6;
}

static bool wr_tapeSays(const char* needle) {
    for (size_t i = 0; i < wr_mockLogger.messages.size(); i++) {
        if (wr_mockLogger.messages[i].find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static void test_a_lost_association_asks_the_network_task_for_one_restart_for_the_lost_link(void) {
    wr_setup();

    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->wifiReconnectRequests);

    wr_mockWifiAdapter.setConnected(false);
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(1, wr_outbox->wifiReconnectRequests);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, wr_outbox->wifiReconnectForALostLinkRequests,
                                     "a restart for a lost association was asked for unconditionally");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wr_mockWifiAdapter.reconnectCallCount,
                                  "the restart ran on the caller although an outbox was wired");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, wr_outbox->linkProbeRequests, "a probe was asked with no association");

    // Still down while the restart runs: nothing more is asked.
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(1, wr_outbox->wifiReconnectRequests);

    wr_teardown();
}

static void test_a_stable_link_asks_for_nothing(void) {
    wr_setup();

    wr_ticksFor(60 * 60 * 1000);
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->wifiReconnectRequests);
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->linkProbeRequests);
    TEST_ASSERT_EQUAL_INT(0, wr_mockWifiAdapter.reconnectCallCount);
    TEST_ASSERT_FALSE(wr_mockModuleConfig.wasModified("requestedModuleMode"));

    wr_teardown();
}

/** The next restart follows the end of the one before after 1, then 2, 5 and 10 minutes. */
static void test_the_restarts_follow_the_end_of_the_last_after_a_growing_pause(void) {
    wr_setup();
    wr_mockWifiAdapter.setConnected(false);
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(1, wr_outbox->wifiReconnectRequests);

    const unsigned long pauses[] = {60000, 120000, 300000, 600000, 600000};
    for (size_t i = 0; i < sizeof(pauses) / sizeof(pauses[0]); i++) {
        wr_restartEnds(false);
        const unsigned long endedAt = wr_now;
        while (wr_outbox->wifiReconnectRequests == i + 1 && wr_now - endedAt < pauses[i] + TICK_MS) {
            wr_tick();
        }
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)(i + 2), wr_outbox->wifiReconnectRequests,
                                         "the next restart was not asked for after its pause");
        TEST_ASSERT_TRUE_MESSAGE(wr_now - endedAt >= pauses[i], "a restart came before its pause was over");
    }
    TEST_ASSERT_EQUAL_INT(0, wr_mockWifiAdapter.reconnectCallCount);

    wr_teardown();
}

static void test_the_association_back_ends_the_outage_and_the_next_loss_restarts_at_once(void) {
    wr_setup();

    wr_mockWifiAdapter.setConnected(false);
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(1, wr_outbox->wifiReconnectRequests);

    // The network task brings the link back; the next tick sees the services working again.
    wr_mockWifiAdapter.setConnected(true);
    wr_restartEnds(true);
    wr_tick();
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::OK, wr_linkSupervisor->troublePhase());
    TEST_ASSERT_TRUE(wr_tapeSays("LINK_CHECK: LINK_DOWN -> OK (services restored)"));

    LinkTroubleshooting::Outage outage;
    TEST_ASSERT_TRUE_MESSAGE(wr_linkSupervisor->lastOutage(outage), "the outage was not kept for the report");
    TEST_ASSERT_EQUAL_STRING("router", LinkTroubleshooting::causeName(outage.cause));
    TEST_ASSERT_EQUAL_UINT32(TICK_MS, outage.startedAtMs);
    TEST_ASSERT_EQUAL_UINT32(TICK_MS, outage.durationMs);

    wr_mockWifiAdapter.setConnected(false);
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(2, wr_outbox->wifiReconnectRequests);

    wr_teardown();
}

/** The module's own access point up: the link is down on purpose, and nothing is decided. */
static void test_an_access_point_in_service_is_never_troubleshot(void) {
    wr_setup();
    wr_mockWifiAdapter.setAccessPointActive(true);
    wr_mockWifiAdapter.setConnected(false);
    wr_servicesFail();

    wr_ticksFor(2 * 60 * 60 * 1000);
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->wifiReconnectRequests);
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->linkProbeRequests);
    TEST_ASSERT_EQUAL_INT(0, wr_mockWifiAdapter.reconnectCallCount);
    TEST_ASSERT_FALSE(wr_mockModuleConfig.wasModified("requestedModuleMode"));
    TEST_ASSERT_TRUE(wr_tapeSays("WIFI_RECONNECT: skip (access point active)"));

    wr_teardown();
}

/** Hours with no association restart the radio, never the module. */
static void test_hours_without_an_association_never_restart_the_module(void) {
    wr_setup();
    wr_mockWifiAdapter.setConnected(false);
    wr_servicesFail();

    for (int i = 0; i < 4 * 60 * 2; i++) {
        wr_tick();
        if (wr_outbox->wifiReconnectRequests > 0) {
            wr_restartEnds(false);
        }
    }

    TEST_ASSERT_TRUE(wr_outbox->wifiReconnectRequests >= 5);
    TEST_ASSERT_FALSE(wr_mockModuleConfig.wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL_UINT8(0, wr_mockSettings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(0, wr_mockSettings.getBootType());

    wr_teardown();
}

/**
 * The end of a restart is reported when the network task reports it, and only an end with the ticket
 * of the restart in flight counts as its end.
 */
static void test_the_restart_ends_on_its_own_completion_and_not_before(void) {
    wr_setup();

    wr_mockWifiAdapter.setConnected(false);
    wr_tick();
    TEST_ASSERT_TRUE(wr_tapeSays("WIFI_RECONNECT: restart handed to the network task"));
    TEST_ASSERT_FALSE_MESSAGE(wr_tapeSays("WIFI_RECONNECT: restart result="),
                              "the outcome was reported before the restart ran");

    OutboxCompletion other;
    other.kind = OutboxKind::DIAGNOSTIC;
    other.ticket = wr_outbox->lastIssuedTicket();
    other.delivered = true;
    wr_linkSupervisor->onOutboxCompletion(other);
    TEST_ASSERT_FALSE(wr_tapeSays("WIFI_RECONNECT: restart result="));

    OutboxCompletion anotherRestart;
    anotherRestart.kind = OutboxKind::WIFI_RECONNECT;
    anotherRestart.ticket = wr_outbox->lastIssuedTicket() + 1;
    anotherRestart.delivered = true;
    wr_linkSupervisor->onOutboxCompletion(anotherRestart);
    TEST_ASSERT_FALSE_MESSAGE(wr_tapeSays("WIFI_RECONNECT: restart result="),
                              "a restart completion with another ticket was taken as the answer");

    // Until its end the pause does not start: an hour later nothing more has been asked.
    wr_ticksFor(30 * 60 * 1000);
    TEST_ASSERT_EQUAL_UINT32(1, wr_outbox->wifiReconnectRequests);

    wr_restartEnds(false);
    TEST_ASSERT_TRUE(wr_tapeSays("WIFI_RECONNECT: restart result=0"));

    wr_teardown();
}

/**
 * A restart the network task did not run - the link it was asked for had come back by itself - is
 * not reported as a restart: the tape says it was not run, and carries no result line that would
 * read as a radio restarted. The machine still hears its end, so that the next decision is not held.
 */
static void test_a_restart_the_network_task_did_not_run_is_not_reported_as_one(void) {
    wr_setup();

    wr_mockWifiAdapter.setConnected(false);
    wr_tick();
    TEST_ASSERT_TRUE(wr_tapeSays("WIFI_RECONNECT: restart handed to the network task"));

    OutboxCompletion notRun;
    notRun.kind = OutboxKind::WIFI_RECONNECT;
    notRun.ticket = wr_outbox->lastIssuedTicket();
    notRun.delivered = true;
    notRun.attempts = 0;
    wr_linkSupervisor->onOutboxCompletion(notRun);

    TEST_ASSERT_TRUE_MESSAGE(wr_tapeSays("WIFI_RECONNECT: restart not run"),
                             "a restart that was not run was not reported as such");
    TEST_ASSERT_FALSE_MESSAGE(wr_tapeSays("WIFI_RECONNECT: restart result="),
                              "a restart that was not run was reported with a result, as one that took place");

    // The end was heard: the machine is not waiting for a restart any more.
    wr_mockWifiAdapter.setConnected(true);
    wr_tick();
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, wr_outbox->wifiReconnectRequests,
                                     "a restart was asked for again on a link that is up");

    wr_teardown();
}

/**
 * The broker dead with the station associated: a minute of patience, then a probe of the link on the
 * network task. A gateway that answers leaves the radio alone, for as long as the broker stays dead.
 */
static void test_a_dead_broker_behind_a_gateway_that_answers_is_probed_and_the_radio_left_alone(void) {
    wr_setup();
    wr_mockMqttManager.isConnectedReturnValue = false;

    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->linkProbeRequests);
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, wr_outbox->linkProbeRequests, "the gateway was asked before a minute");
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(1, wr_outbox->linkProbeRequests);
    TEST_ASSERT_TRUE(wr_tapeSays("LINK_CHECK: probe handed to the network task"));

    wr_probeEnds(LinkReach::ANSWERED, LinkReach::ANSWERED);
    TEST_ASSERT_TRUE(wr_tapeSays("LINK_CHECK: probe gateway=answered public=answered server_answered=0"));
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::UPSTREAM_DOWN, wr_linkSupervisor->troublePhase());

    for (int i = 0; i < 2 * 60 * 2; i++) {
        const uint32_t asked = wr_outbox->linkProbeRequests;
        wr_tick();
        if (wr_outbox->linkProbeRequests != asked) {
            wr_probeEnds(LinkReach::ANSWERED, LinkReach::ANSWERED);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->wifiReconnectRequests);
    TEST_ASSERT_FALSE(wr_mockModuleConfig.wasModified("requestedModuleMode"));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1 + 24, wr_outbox->linkProbeRequests, "the gateway was not asked every five minutes");

    wr_teardown();
}

/**
 * A gateway silent twice with the station associated: the restart is meant to take that association
 * down, so it is asked for unconditionally - one asked "for a lost link" would be skipped by the
 * network task on a station that is associated.
 */
static void test_a_silent_gateway_restarts_an_associated_radio_unconditionally(void) {
    wr_setup();
    wr_servicesFail();

    wr_ticksFor(3 * TICK_MS);
    TEST_ASSERT_EQUAL_UINT32(1, wr_outbox->linkProbeRequests);
    wr_probeEnds(LinkReach::SILENT, LinkReach::SILENT);
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->wifiReconnectRequests);

    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(2, wr_outbox->linkProbeRequests);
    wr_probeEnds(LinkReach::SILENT, LinkReach::SILENT);
    TEST_ASSERT_EQUAL_UINT32(1, wr_outbox->wifiReconnectRequests);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, wr_outbox->wifiReconnectForALostLinkRequests,
                                     "a restart for a silent gateway was asked as one for a lost link");
    TEST_ASSERT_TRUE(wr_tapeSays("LINK_CHECK: PROBING -> LINK_DOWN (gateway silent)"));

    wr_teardown();
}

/** A probe's end with another ticket, or of another kind, is not the probe's end. */
static void test_only_the_probe_in_flight_ends_the_probe(void) {
    wr_setup();
    wr_servicesFail();
    wr_ticksFor(3 * TICK_MS);
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::PROBING, wr_linkSupervisor->troublePhase());

    OutboxCompletion stale;
    stale.kind = OutboxKind::LINK_PROBE;
    stale.ticket = wr_outbox->lastIssuedTicket() + 7;
    wr_linkSupervisor->onOutboxCompletion(stale);
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::PROBING, wr_linkSupervisor->troublePhase());

    wr_probeEnds(LinkReach::ANSWERED, LinkReach::SILENT);
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::UPSTREAM_DOWN, wr_linkSupervisor->troublePhase());

    wr_teardown();
}

/**
 * An outage behind a gateway that answers and a public address that stays silent, taken to its end:
 * the services fail, the probe finds the gateway and not the internet, the services come back. What the
 * outage is recorded as - services or internet - is whether the server answered a request with a
 * status, any status, since the outage began.
 */
static LinkTroubleshooting::Cause wr_causeOfAnOutageBehindASilentInternet(bool serverAnswersDuringIt) {
    wr_tick();
    wr_servicesFail();
    wr_tick();
    const unsigned long outageBegan = wr_now;
    if (serverAnswersDuringIt) {
        wr_board->noteRequestOutcome(outageBegan + 5000, 503);
    }
    const uint32_t asked = wr_outbox->linkProbeRequests;
    for (int i = 0; i < 4 && wr_outbox->linkProbeRequests == asked; i++) {
        wr_tick();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(asked + 1, wr_outbox->linkProbeRequests, "the link was never probed");
    wr_probeEnds(LinkReach::ANSWERED, LinkReach::SILENT);
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::UPSTREAM_DOWN, wr_linkSupervisor->troublePhase());

    wr_mockMqttManager.isConnectedReturnValue = true;
    wr_mockRestApi.countOffline = 0;
    wr_tick();
    LinkTroubleshooting::Outage outage;
    TEST_ASSERT_TRUE_MESSAGE(wr_linkSupervisor->lastOutage(outage), "the outage was not recorded");
    TEST_ASSERT_EQUAL_UINT32(outageBegan, outage.startedAtMs);
    return outage.cause;
}

/**
 * The server answered with a status during the outage: what failed is past the server's front door,
 * whatever the silent public address says - the outage is the services'.
 *
 * Mutation: do not read the server's answer.
 */
static void test_a_server_that_answered_during_the_outage_makes_it_the_services(void) {
    wr_setup();
    TEST_ASSERT_EQUAL_MESSAGE(LinkTroubleshooting::Cause::SERVICES, wr_causeOfAnOutageBehindASilentInternet(true),
                              "an outage the server answered during was not recorded as the services'");
    wr_teardown();
}

/**
 * The server answered before the outage began and not since: that answer says nothing about this
 * outage, and with the public address silent the outage is the internet's.
 *
 * Mutation: read the server's answers since the boot instead of since the outage began.
 */
static void test_a_server_answer_from_before_the_outage_does_not_make_it_the_services(void) {
    wr_setup();
    wr_tick();
    wr_board->noteRequestOutcome(wr_now, 200);
    TEST_ASSERT_EQUAL_MESSAGE(LinkTroubleshooting::Cause::INTERNET, wr_causeOfAnOutageBehindASilentInternet(false),
                              "an answer from before the outage was read as the server answering during it");
    wr_teardown();
}

/**
 * A network task the heap refuses, reported by the outbox and still refused: a fault of the module.
 * With the services failing it restarts the module after the hold - as a technical boot that skips the
 * discovery, as the offline restart always did.
 */
static void test_a_refused_network_task_that_holds_restarts_the_module(void) {
    wr_setup();
    wr_board->noteNetworkTask(false);
    wr_servicesFail();

    wr_tick();
    TEST_ASSERT_TRUE(wr_tapeSays("LINK_CHECK: module faults: network-task"));
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::MODULE_FAULT, wr_linkSupervisor->troublePhase());
    const unsigned long faultAt = wr_now;

    while (!wr_mockModuleConfig.wasModified("requestedModuleMode") && wr_now - faultAt < 2 * LinkTroubleshooting::FAULT_HOLD_MS) {
        wr_tick();
    }
    TEST_ASSERT_TRUE_MESSAGE(wr_mockModuleConfig.wasModified("requestedModuleMode"), "the fault never restarted the module");
    TEST_ASSERT_TRUE(wr_now - faultAt >= LinkTroubleshooting::FAULT_HOLD_MS);
    TEST_ASSERT_TRUE(wr_now - faultAt < LinkTroubleshooting::FAULT_HOLD_MS + TICK_MS);
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_NEED_RESTART, wr_mockModuleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(1, wr_mockSettings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_TECHNICAL, wr_mockSettings.getBootType());
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->wifiReconnectRequests);

    wr_teardown();
}

/**
 * A radio restart whose driver would not start - the station cannot associate until the module
 * restarts - is a fault of the module: no more radio restarts, and the module restarted after the hold.
 *
 * Mutation: do not read the radio's start from the status board.
 */
static void test_a_radio_that_would_not_start_restarts_the_module_after_the_hold(void) {
    wr_setup();
    wr_mockWifiAdapter.setConnected(false);
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32(1, wr_outbox->wifiReconnectRequests);

    // The restart ran, and the driver refused to start the station.
    wr_board->noteRadioStart(false);
    wr_restartEnds(false);
    wr_tick();
    TEST_ASSERT_TRUE(wr_tapeSays("LINK_CHECK: module faults: radio-start"));
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::MODULE_FAULT, wr_linkSupervisor->troublePhase());

    wr_ticksFor(LinkTroubleshooting::FAULT_HOLD_MS + TICK_MS);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, wr_outbox->wifiReconnectRequests,
                                     "the radio was restarted again on a driver that would not start");
    TEST_ASSERT_TRUE_MESSAGE(wr_mockModuleConfig.wasModified("requestedModuleMode"),
                             "a radio that would not start did not restart the module");

    wr_teardown();
}

/** The same fault created at last before the hold: nothing is restarted. */
static void test_a_network_task_created_before_the_hold_restarts_nothing(void) {
    wr_setup();
    wr_board->noteNetworkTask(false);
    wr_servicesFail();
    wr_ticksFor(5 * 60 * 1000);
    wr_board->noteNetworkTask(true);
    wr_ticksFor(LinkTroubleshooting::FAULT_HOLD_MS * 2);
    TEST_ASSERT_FALSE(wr_mockModuleConfig.wasModified("requestedModuleMode"));
    TEST_ASSERT_TRUE(wr_tapeSays("LINK_CHECK: MODULE_FAULT -> SUSPECT (fault cleared)"));

    wr_teardown();
}

/**
 * The association lost on the tick a fault of the module is first reported: the fault is read first,
 * so the loss goes straight to the module's fault - a radio restart would only ask the heap for what a
 * driver needs, on a module that is already short of it.
 *
 * Mutation: feed the association's edge before the fault's.
 */
static void test_an_association_lost_with_a_fault_reported_asks_for_no_radio_restart(void) {
    wr_setup();
    wr_tick();
    wr_board->noteNetworkTask(false);
    wr_mockWifiAdapter.setConnected(false);
    wr_tick();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, wr_outbox->wifiReconnectRequests,
                                     "the radio was restarted on a module with a fault of its own");
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::MODULE_FAULT, wr_linkSupervisor->troublePhase());

    wr_teardown();
}

/**
 * The module's TLS client opening its connections through the stand-in of the core, on the
 * supervision's clock: each one failing with -1 and leaving this errno - ENFILE for a socket the stack
 * refused, EINPROGRESS for one that opened and whose TCP connect failed.
 */
static void wr_connectsThatFail(MeteredTlsClient& client, int stackErrno, int count) {
    WiFiClientSecure::hostNextConnectResult = 0;
    WiFiClientSecure::hostNextTlsError = -1;
    WiFiClientSecure::hostNextStackErrno = stackErrno;
    for (int i = 0; i < count; i++) {
        hostUptimeMs = wr_now;
        client.connect("gd.test", 443);
    }
}

/** Each probe the supervision hands over ends at once: the gateway answered, and so did the internet. */
static void wr_tickAnsweringProbes(void) {
    const uint32_t asked = wr_outbox->linkProbeRequests;
    wr_tick();
    if (wr_outbox->linkProbeRequests != asked) {
        wr_probeEnds(LinkReach::ANSWERED, LinkReach::ANSWERED);
    }
}

/**
 * Sockets the module's own stack refuses, three in a row before every tick, while the services fail:
 * from the TLS client through the status board, a fault of the module. After the hold the module is
 * restarted, as a technical boot that skips the discovery, and the radio never is.
 *
 * Mutation: do not read the refused sockets from the status board.
 */
static void test_sockets_refused_in_a_row_restart_the_module_after_the_hold(void) {
    const unsigned long hostUptimeBefore = hostUptimeMs;
    WiFiClientSecure::hostReset();
    wr_setup();
    MeteredTlsClient client;
    client.setStatusBoard(wr_board);
    wr_servicesFail();

    wr_connectsThatFail(client, ENFILE, 3);
    wr_tick();
    TEST_ASSERT_TRUE_MESSAGE(wr_tapeSays("LINK_CHECK: module faults: socket"),
                             "three refused sockets were not read as a fault of the module");
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::MODULE_FAULT, wr_linkSupervisor->troublePhase());
    const unsigned long faultAt = wr_now;

    while (!wr_mockModuleConfig.wasModified("requestedModuleMode") && wr_now - faultAt < 2 * LinkTroubleshooting::FAULT_HOLD_MS) {
        wr_connectsThatFail(client, ENFILE, 3);
        wr_tick();
    }
    TEST_ASSERT_TRUE_MESSAGE(wr_mockModuleConfig.wasModified("requestedModuleMode"),
                             "sockets refused for the whole hold did not restart the module");
    TEST_ASSERT_TRUE(wr_now - faultAt >= LinkTroubleshooting::FAULT_HOLD_MS);
    TEST_ASSERT_TRUE(wr_now - faultAt < LinkTroubleshooting::FAULT_HOLD_MS + TICK_MS);
    TEST_ASSERT_EQUAL(ModuleModeEnum::CONFIGURED_NEED_RESTART, wr_mockModuleConfig.getRequestedModuleMode());
    TEST_ASSERT_EQUAL_UINT8(1, wr_mockSettings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_TECHNICAL, wr_mockSettings.getBootType());
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->wifiReconnectRequests);

    wr_teardown();
    WiFiClientSecure::hostReset();
    hostUptimeMs = hostUptimeBefore;
}

/**
 * The same outage twice over the length of the hold - once with every socket opening and every TCP
 * connect behind it failing, once with a socket that opens and two refusals after it before every
 * tick, so that every tick reads two in a row - is not a fault of the module: the module is never
 * restarted.
 *
 * Mutations: count a failed TCP connect as a refused socket; do not start the count again on a socket
 * that opened; a threshold below three.
 */
static void test_failed_tcp_connects_and_refusals_short_of_three_in_a_row_restart_nothing(void) {
    const unsigned long hostUptimeBefore = hostUptimeMs;
    WiFiClientSecure::hostReset();
    wr_setup();
    MeteredTlsClient client;
    client.setStatusBoard(wr_board);
    wr_servicesFail();

    for (unsigned long elapsed = 0; elapsed < 2 * LinkTroubleshooting::FAULT_HOLD_MS; elapsed += TICK_MS) {
        wr_connectsThatFail(client, EINPROGRESS, 4);
        wr_tickAnsweringProbes();
    }
    for (unsigned long elapsed = 0; elapsed < 2 * LinkTroubleshooting::FAULT_HOLD_MS; elapsed += TICK_MS) {
        wr_connectsThatFail(client, EINPROGRESS, 1);
        wr_connectsThatFail(client, ENFILE, 2);
        wr_tickAnsweringProbes();
    }
    TEST_ASSERT_FALSE_MESSAGE(wr_tapeSays("module faults: socket"),
                              "failed TCP connects, or refusals never three in a row, were read as a fault");
    TEST_ASSERT_FALSE_MESSAGE(wr_mockModuleConfig.wasModified("requestedModuleMode"),
                              "the module was restarted without a fault of its own");

    wr_teardown();
    WiFiClientSecure::hostReset();
    hostUptimeMs = hostUptimeBefore;
}

/**
 * A largest free block below a handshake's peak - 9 716 B, the lowest device logs show with the
 * requests still answered - forecasts nothing the module can act on, and is no fault of it: what fails
 * for want of memory is caught where it fails (a handshake, a socket, a task, the radio's start). An
 * association lost on such a heap restarts the radio at once, and the module is not restarted.
 */
static void test_a_fragmented_heap_is_no_fault_of_the_module(void) {
    wr_setup();
    wr_mockArduinoAdapter.maxAllocHeapValue = 9716;
    wr_tick();

    wr_mockWifiAdapter.setConnected(false);
    wr_tick();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, wr_outbox->wifiReconnectRequests,
                                     "an association lost on a fragmented heap did not restart the radio");
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::LINK_DOWN, wr_linkSupervisor->troublePhase());

    wr_ticksFor(2 * LinkTroubleshooting::FAULT_HOLD_MS);
    TEST_ASSERT_FALSE_MESSAGE(wr_mockModuleConfig.wasModified("requestedModuleMode"),
                              "a fragmented heap restarted the module");

    wr_teardown();
}

/** A handshake short of memory counts for three ticks: a module that stopped trying is not at fault. */
static void test_a_handshake_short_of_memory_counts_while_it_is_fresh(void) {
    wr_setup();
    wr_servicesFail();
    wr_board->noteTlsConnect(wr_now, ModuleStatusBoard::TlsConnect::OUT_OF_MEMORY);
    wr_tick();
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::MODULE_FAULT, wr_linkSupervisor->troublePhase());

    // Not reported again: three ticks on it is no longer a fault, and nothing was restarted.
    wr_ticksFor(3 * TICK_MS);
    TEST_ASSERT_TRUE(wr_linkSupervisor->troublePhase() != LinkTroubleshooting::Phase::MODULE_FAULT);
    TEST_ASSERT_FALSE(wr_mockModuleConfig.wasModified("requestedModuleMode"));

    wr_teardown();
}

/**
 * Sockets refused three in a row count for three ticks after the last of them: a module that stopped
 * opening connections says nothing about its sockets, and is not restarted for them.
 *
 * Mutation: let the refusals count however old they are.
 */
static void test_sockets_refused_count_while_they_are_fresh(void) {
    wr_setup();
    wr_servicesFail();
    for (int i = 0; i < 3; i++) {
        wr_board->noteSocketOpen(wr_now, ModuleStatusBoard::SocketOpen::REFUSED);
    }
    wr_tick();
    TEST_ASSERT_EQUAL(LinkTroubleshooting::Phase::MODULE_FAULT, wr_linkSupervisor->troublePhase());

    wr_ticksFor(3 * TICK_MS);
    TEST_ASSERT_TRUE_MESSAGE(wr_linkSupervisor->troublePhase() != LinkTroubleshooting::Phase::MODULE_FAULT,
                             "refusals three ticks old still counted as a fault of the module");
    wr_ticksFor(LinkTroubleshooting::FAULT_HOLD_MS);
    TEST_ASSERT_FALSE(wr_mockModuleConfig.wasModified("requestedModuleMode"));

    wr_teardown();
}

/** Without an outbox the restart runs here and ends at once; a probe cannot run and decides nothing. */
static void test_without_an_outbox_the_restart_runs_on_the_caller_and_no_probe_decides(void) {
    wr_setup();
    wr_linkSupervisor->setOutbox(nullptr);

    wr_mockWifiAdapter.reconnectSuccess = false;
    wr_mockWifiAdapter.setConnected(false);
    wr_tick();
    TEST_ASSERT_EQUAL_INT(1, wr_mockWifiAdapter.reconnectCallCount);
    TEST_ASSERT_EQUAL_UINT32(0, wr_outbox->wifiReconnectRequests);
    TEST_ASSERT_TRUE(wr_tapeSays("WIFI_RECONNECT: restart result=0"));

    wr_teardown();

    wr_setup();
    wr_linkSupervisor->setOutbox(nullptr);
    wr_servicesFail();
    wr_ticksFor(10 * 60 * 1000);
    TEST_ASSERT_TRUE(wr_tapeSays("LINK_CHECK: probe gateway=unavailable"));
    TEST_ASSERT_EQUAL_INT(0, wr_mockWifiAdapter.reconnectCallCount);
    TEST_ASSERT_FALSE(wr_mockModuleConfig.wasModified("requestedModuleMode"));

    wr_teardown();
}

extern "C" void run_wifi_self_reconnect_tests(void) {
    RUN_TEST(test_a_lost_association_asks_the_network_task_for_one_restart_for_the_lost_link);
    RUN_TEST(test_a_stable_link_asks_for_nothing);
    RUN_TEST(test_the_restarts_follow_the_end_of_the_last_after_a_growing_pause);
    RUN_TEST(test_the_association_back_ends_the_outage_and_the_next_loss_restarts_at_once);
    RUN_TEST(test_an_access_point_in_service_is_never_troubleshot);
    RUN_TEST(test_hours_without_an_association_never_restart_the_module);
    RUN_TEST(test_the_restart_ends_on_its_own_completion_and_not_before);
    RUN_TEST(test_a_restart_the_network_task_did_not_run_is_not_reported_as_one);
    RUN_TEST(test_a_dead_broker_behind_a_gateway_that_answers_is_probed_and_the_radio_left_alone);
    RUN_TEST(test_a_silent_gateway_restarts_an_associated_radio_unconditionally);
    RUN_TEST(test_only_the_probe_in_flight_ends_the_probe);
    RUN_TEST(test_a_server_that_answered_during_the_outage_makes_it_the_services);
    RUN_TEST(test_a_server_answer_from_before_the_outage_does_not_make_it_the_services);
    RUN_TEST(test_a_refused_network_task_that_holds_restarts_the_module);
    RUN_TEST(test_a_network_task_created_before_the_hold_restarts_nothing);
    RUN_TEST(test_a_radio_that_would_not_start_restarts_the_module_after_the_hold);
    RUN_TEST(test_an_association_lost_with_a_fault_reported_asks_for_no_radio_restart);
    RUN_TEST(test_sockets_refused_in_a_row_restart_the_module_after_the_hold);
    RUN_TEST(test_failed_tcp_connects_and_refusals_short_of_three_in_a_row_restart_nothing);
    RUN_TEST(test_a_fragmented_heap_is_no_fault_of_the_module);
    RUN_TEST(test_a_handshake_short_of_memory_counts_while_it_is_fresh);
    RUN_TEST(test_sockets_refused_count_while_they_are_fresh);
    RUN_TEST(test_without_an_outbox_the_restart_runs_on_the_caller_and_no_probe_decides);
}
