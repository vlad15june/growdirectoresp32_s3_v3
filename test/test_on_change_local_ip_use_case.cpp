#include <unity.h>
#include "Core/Domain/UseCases/ModuleMode/OnChangeLocalIpUseCase.h"
#include "Mock/Application/MockOutbox.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/UseCases/Rules/MockSyncRulesUseCase.h"
#include "Mock/UseCases/SensorTypes/MockSyncSensorTypesUseCase.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"

#include <cstdio>

// Global test variables
static MockLogger g_mockLogger;
static MockDebugSystem g_mockDebugSystem;
static MockWifiAdapter g_mockWifiAdapter;
static MockWiFiManager g_mockWifiManager;
static MockModuleConfig g_mockModuleConfig;
static MockSettings g_mockSettings;
static MockSyncRulesUseCase g_mockSyncRulesUseCase;
static MockSyncSensorTypesUseCase g_mockSyncSensorTypesUseCase;
static MockOutbox* g_outbox;
static ModuleStructure g_moduleStructure;
static OnChangeLocalIpUseCase* g_onChangeLocalIpUseCase;

// Deliver, or fail to deliver, the structure whose ticket the use case is waiting for. The control
// loop does this by draining the completion queue; here the test plays that part.
static void completeStructure(uint32_t ticket, bool delivered) {
    OutboxCompletion completion;
    completion.kind = OutboxKind::MODULE_STRUCTURE;
    completion.ticket = ticket;
    completion.delivered = delivered;
    completion.attempts = delivered ? 1 : 4;
    g_onChangeLocalIpUseCase->onOutboxCompletion(completion);
}

// Test setup and teardown
void on_change_local_ip_use_case_setup(void) {
    g_mockLogger.reset();
    g_mockDebugSystem.reset();
    g_mockWifiAdapter.reset();
    g_mockWifiManager.reset();
    g_mockModuleConfig.reset();
    g_mockSettings.reset();
    g_mockSyncRulesUseCase.reset();
    g_mockSyncSensorTypesUseCase.reset();
    g_outbox = new MockOutbox();

    // Inject settings into module config
    g_mockModuleConfig.setSettings(&g_mockSettings);

    // Enable network debugging for log messages
    g_mockDebugSystem.featureEnabled = false; // Disable to test if logging causes crash

    // Setup a basic module structure with one node
    g_moduleStructure.nodes.clear();

    // Create a Node with explicit initialization
    Node testNode;
    testNode.type = "esp32";
    testNode.subtype = "relay";
    testNode.uid = "node1";
    testNode.firmware = "2.1.1";
    testNode.ip = "old_ip";
    testNode.ssid = "old_ssid";

    // Add the node to the module structure
    g_moduleStructure.nodes.push_back(testNode);

    g_onChangeLocalIpUseCase = new OnChangeLocalIpUseCase(
        g_mockWifiAdapter,
        g_mockWifiManager,
        g_moduleStructure,
        g_mockModuleConfig,
        g_mockSyncRulesUseCase,
        g_mockSyncSensorTypesUseCase,
        g_mockLogger,
        g_mockDebugSystem);
    g_onChangeLocalIpUseCase->setOutbox(g_outbox);
}

void on_change_local_ip_use_case_teardown(void) {
    if (g_onChangeLocalIpUseCase != nullptr) {
        delete g_onChangeLocalIpUseCase;
        g_onChangeLocalIpUseCase = nullptr;
    }
    if (g_outbox != nullptr) {
        delete g_outbox;
        g_outbox = nullptr;
    }
}

// Test functions
void test_perform_when_ip_is_zero_ip(void) {
    on_change_local_ip_use_case_setup();

    // Set IP to 0.0.0.0
    g_mockWifiAdapter.setLocalIP("0.0.0.0");

    // Execute the use case
    g_onChangeLocalIpUseCase->perform();

    // Verify no operations were performed on module structure or the outbox
    TEST_ASSERT_EQUAL_UINT32(0, g_outbox->moduleStructureSends);
    TEST_ASSERT_EQUAL_STRING("old_ip", g_moduleStructure.nodes[0].ip.c_str());

    on_change_local_ip_use_case_teardown();
}

void test_perform_when_wifi_not_connected(void) {
    on_change_local_ip_use_case_setup();

    // Set a valid IP but WiFi not connected
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(false);

    // Execute the use case
    g_onChangeLocalIpUseCase->perform();

    // Verify no operations were performed on module structure or the outbox
    TEST_ASSERT_EQUAL_UINT32(0, g_outbox->moduleStructureSends);
    TEST_ASSERT_EQUAL_STRING("old_ip", g_moduleStructure.nodes[0].ip.c_str());

    on_change_local_ip_use_case_teardown();
}

void test_perform_with_same_ip_no_change(void) {
    on_change_local_ip_use_case_setup();

    // First set one IP to initialize the internal _oldIpStr
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);

    g_onChangeLocalIpUseCase->perform();
    completeStructure(1, true);

    // Call again with the same IP
    g_onChangeLocalIpUseCase->perform();

    // Verify nothing further was submitted as the IP hasn't changed
    TEST_ASSERT_EQUAL_UINT32(1, g_outbox->moduleStructureSends);

    on_change_local_ip_use_case_teardown();
}

void test_perform_with_new_ip_updates_structure(void) {
    on_change_local_ip_use_case_setup();

    // Set a valid IP and connected WiFi
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);

    // Execute the use case
    g_onChangeLocalIpUseCase->perform();

    // Verify module structure was updated
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", g_moduleStructure.nodes[0].ip.c_str());
    TEST_ASSERT_EQUAL_STRING("TestSSID", g_moduleStructure.nodes[0].ssid.c_str());

    // Verify the structure went to the outbox, addressed to both transports as before
    TEST_ASSERT_EQUAL_UINT32(1, g_outbox->moduleStructureSends);
    TEST_ASSERT_EQUAL(ApiPriority::BOTH, g_outbox->lastModuleStructurePriority);

    on_change_local_ip_use_case_teardown();
}

void test_perform_with_changing_ip(void) {
    on_change_local_ip_use_case_setup();

    // First set one IP
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);
    g_onChangeLocalIpUseCase->perform();
    completeStructure(1, true);

    // Now change the IP
    g_mockWifiAdapter.setLocalIP("192.168.1.101");
    g_onChangeLocalIpUseCase->perform();

    // Verify module structure was updated with new IP
    TEST_ASSERT_EQUAL_STRING("192.168.1.101", g_moduleStructure.nodes[0].ip.c_str());

    // Verify the second structure was submitted too
    TEST_ASSERT_EQUAL_UINT32(2, g_outbox->moduleStructureSends);

    on_change_local_ip_use_case_teardown();
}

// --- what perform() must no longer cost the control loop -------------------------------------

// The reason the whole change exists: perform() runs on every control-loop iteration, so whatever
// it does when the address changed is what the loop pays. It must submit and return - and the rules
// and the catalog are asked for once the structure has settled, not beside it: asked for in the
// same iteration they share the queue with the structure, and the outbox's rotation may take either
// of them first.
void test_perform_hands_the_structure_over_and_the_syncs_follow_its_completion(void) {
    on_change_local_ip_use_case_setup();

    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);

    g_onChangeLocalIpUseCase->perform();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, g_outbox->moduleStructureSends,
                                     "the structure did not go through the outbox");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockSyncRulesUseCase.forceSyncCallCount,
                                  "rules were force-synced beside the structure, before it settled");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockSyncSensorTypesUseCase.forceSyncCallCount,
                                  "sensor types were force-synced beside the structure, before it settled");

    completeStructure(1, true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockSyncRulesUseCase.forceSyncCallCount,
                                  "rules were not force-synced once the structure settled");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockSyncSensorTypesUseCase.forceSyncCallCount,
                                  "sensor types were not force-synced once the structure settled");

    on_change_local_ip_use_case_teardown();
}

// A registered module whose structure the server did not take keeps working on what it holds, and
// asks for the rules and the catalog all the same; a module that is not registered yet asks for
// nothing until its structure is delivered, and is not restarted.
void test_the_syncs_follow_an_undelivered_structure_only_on_a_registered_module(void) {
    on_change_local_ip_use_case_setup();
    g_mockSettings.setWasInitialSyncCompleted(1);
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);

    g_onChangeLocalIpUseCase->perform();
    completeStructure(1, false);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockSyncRulesUseCase.forceSyncCallCount,
                                  "a registered module did not ask for its rules after an undelivered structure");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_mockSyncSensorTypesUseCase.forceSyncCallCount,
                                  "a registered module did not ask for its catalog after an undelivered structure");
    on_change_local_ip_use_case_teardown();

    on_change_local_ip_use_case_setup();
    g_mockSettings.setWasInitialSyncCompleted(0);
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);

    g_onChangeLocalIpUseCase->perform();
    completeStructure(1, false);
    TEST_ASSERT_FALSE_MESSAGE(g_mockModuleConfig.getRequestedModuleMode() == ModuleModeEnum::CONFIGURED_NEED_RESTART,
                              "a module not registered yet was restarted for an undelivered structure");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockSyncRulesUseCase.forceSyncCallCount,
                                  "a module not registered yet asked for its rules");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_mockSyncSensorTypesUseCase.forceSyncCallCount,
                                  "a module not registered yet asked for its catalog");
    on_change_local_ip_use_case_teardown();
}

// perform() is called at the loop period, so without an in-flight guard one address change would
// put one structure per iteration into the outbox and fill the event ring on the spot.
void test_one_structure_travels_at_a_time(void) {
    on_change_local_ip_use_case_setup();

    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);

    for (int iteration = 0; iteration < 500; iteration++) {
        g_onChangeLocalIpUseCase->perform();
    }

    char message[176];
    snprintf(message, sizeof(message),
             "500 loop iterations with one unchanged address submitted %u structures; exactly one "
             "may be travelling at a time",
             (unsigned)g_outbox->moduleStructureSends);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, g_outbox->moduleStructureSends, message);

    on_change_local_ip_use_case_teardown();
}

// --- what perform() writes to the log -------------------------------------------------------------

// How many lines the use case wrote that contain this text, since the counter was last cleared.
static size_t linesContaining(const char* text) {
    size_t count = 0;
    for (size_t i = 0; i < g_mockLogger.messages.size(); i++) {
        if (g_mockLogger.messages[i].find(text) != std::string::npos) {
            count++;
        }
    }
    return count;
}

// perform() runs at the loop period, and a structure is in flight for as long as the server takes
// to answer - seconds, and minutes when it does not answer. The change of address is news once,
// when the structure that reports it is handed over; every iteration after that finds the same
// address still unreported, and saying so again is the loop writing to the console at its own rate.
void test_an_address_change_is_said_once_while_its_structure_travels(void) {
    on_change_local_ip_use_case_setup();
    g_mockDebugSystem.featureEnabled = true;
    g_mockLogger.messages.clear();

    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);

    for (int iteration = 0; iteration < 1000; iteration++) {
        g_onChangeLocalIpUseCase->perform();
    }

    char message[160];
    snprintf(message, sizeof(message),
             "1000 iterations with one structure in flight wrote %u 'IP changed' lines; one structure "
             "is one change",
             (unsigned)linesContaining("IP changed"));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)linesContaining("IP changed"), message);
    TEST_ASSERT_EQUAL_UINT32(1, g_outbox->moduleStructureSends);

    // The next structure is the next change: undelivered, the address is offered again and said
    // again - once.
    completeStructure(1, false);
    for (int iteration = 0; iteration < 1000; iteration++) {
        g_onChangeLocalIpUseCase->perform();
    }
    TEST_ASSERT_EQUAL_UINT32(2, g_outbox->moduleStructureSends);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)linesContaining("IP changed"),
                                     "a structure offered again did not say the change again, or "
                                     "said it more than once");

    on_change_local_ip_use_case_teardown();
}

// A module without an address asks for one on every iteration; the absence is news when it begins.
void test_a_missing_address_is_said_once_until_one_comes(void) {
    on_change_local_ip_use_case_setup();
    g_mockDebugSystem.featureEnabled = true;
    g_mockLogger.messages.clear();

    g_mockWifiAdapter.setLocalIP("0.0.0.0");
    g_mockWifiAdapter.setConnected(false);

    for (int iteration = 0; iteration < 500; iteration++) {
        g_onChangeLocalIpUseCase->perform();
    }

    char message[160];
    snprintf(message, sizeof(message),
             "500 iterations without an address wrote %u 'No valid IP' lines; the absence is said "
             "when it begins",
             (unsigned)linesContaining("No valid IP"));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)linesContaining("No valid IP"), message);

    // An address comes and goes: the second absence is a second edge.
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);
    g_onChangeLocalIpUseCase->perform();
    g_mockWifiAdapter.setLocalIP("0.0.0.0");
    g_mockWifiAdapter.setConnected(false);
    for (int iteration = 0; iteration < 500; iteration++) {
        g_onChangeLocalIpUseCase->perform();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)linesContaining("No valid IP"),
                                     "an address that went away a second time was not said again "
                                     "once");

    on_change_local_ip_use_case_teardown();
}

// The same for an address the adapter still holds while the link is down.
void test_a_link_that_is_down_is_said_once_until_it_comes_back(void) {
    on_change_local_ip_use_case_setup();
    g_mockDebugSystem.featureEnabled = true;
    g_mockLogger.messages.clear();

    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(false);

    for (int iteration = 0; iteration < 500; iteration++) {
        g_onChangeLocalIpUseCase->perform();
    }

    char message[160];
    snprintf(message, sizeof(message),
             "500 iterations with the link down wrote %u 'WiFi not connected' lines; the state is "
             "said when it begins",
             (unsigned)linesContaining("WiFi not connected"));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)linesContaining("WiFi not connected"), message);
    TEST_ASSERT_EQUAL_UINT32(0, g_outbox->moduleStructureSends);

    // The link comes back and the structure goes; the link drops again before it is confirmed.
    g_mockWifiAdapter.setConnected(true);
    g_onChangeLocalIpUseCase->perform();
    TEST_ASSERT_EQUAL_UINT32(1, g_outbox->moduleStructureSends);
    completeStructure(1, false);
    g_mockWifiAdapter.setConnected(false);
    for (int iteration = 0; iteration < 500; iteration++) {
        g_onChangeLocalIpUseCase->perform();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)linesContaining("WiFi not connected"),
                                     "a link that dropped a second time was not said again once");

    on_change_local_ip_use_case_teardown();
}

// --- what an undelivered structure means -------------------------------------------------------

// A module the server has never seen cannot work until its structure - which is what registers it -
// is delivered. An undelivered one does not restart it: the structure is offered again after 1, 2 and
// 5 minutes and then every 10, each pause counted from the completion that said undelivered and never
// cut short. Delivered, the module is registered and asks for its rules and its catalog.
//
// Mutations: ask for a restart again; offer the structure again at once.
void test_an_unregistered_module_offers_its_structure_again_after_a_growing_pause_and_never_restarts(void) {
    on_change_local_ip_use_case_setup();
    MockArduinoAdapter clock;
    clock.currentMillis = 1000;
    g_onChangeLocalIpUseCase->setClock(&clock);

    g_mockSettings.setWasInitialSyncCompleted(0);
    g_mockModuleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
    g_mockWifiAdapter.setLocalIP("192.168.1.200");
    g_mockWifiAdapter.setConnected(true);

    const uint32_t pausesMs[] = {60000, 120000, 300000, 600000, 600000};
    for (uint32_t round = 0; round < sizeof(pausesMs) / sizeof(pausesMs[0]); round++) {
        g_onChangeLocalIpUseCase->perform();
        TEST_ASSERT_EQUAL_UINT32(round + 1, g_outbox->moduleStructureSends);
        completeStructure(round + 1, false);
        TEST_ASSERT_EQUAL_MESSAGE(ModuleModeEnum::CONFIGURED_DONE, g_mockModuleConfig.getRequestedModuleMode(),
                                  "an unregistered module asked for a restart for an undelivered structure");

        char message[128];
        snprintf(message, sizeof(message), "structure %u was offered again before its pause of %u ms had run",
                 (unsigned)(round + 1), (unsigned)pausesMs[round]);
        clock.currentMillis += pausesMs[round] - 1;
        g_onChangeLocalIpUseCase->perform();
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(round + 1, g_outbox->moduleStructureSends, message);
        clock.currentMillis += 1;
    }
    TEST_ASSERT_EQUAL_INT(0, g_mockSyncRulesUseCase.forceSyncCallCount);

    g_onChangeLocalIpUseCase->perform();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, g_outbox->moduleStructureSends, "the pause ran and the structure was not offered");
    completeStructure(6, true);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, g_mockSettings.getWasInitialSyncCompleted(),
                                    "a delivered structure did not register the module");
    TEST_ASSERT_EQUAL_INT(1, g_mockSyncRulesUseCase.forceSyncCallCount);
    TEST_ASSERT_EQUAL_INT(1, g_mockSyncSensorTypesUseCase.forceSyncCallCount);

    on_change_local_ip_use_case_teardown();
}

// A module that has been running for months does not reboot because a DHCP lease renewal could not
// be reported. The behaviour this rules out: a restart on any failed send.
void test_a_registered_module_keeps_running_when_the_structure_is_not_delivered(void) {
    on_change_local_ip_use_case_setup();

    g_mockSettings.setWasInitialSyncCompleted(1);
    g_mockModuleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
    g_mockWifiAdapter.setLocalIP("192.168.1.200");
    g_mockWifiAdapter.setConnected(true);

    g_onChangeLocalIpUseCase->perform();
    completeStructure(1, false);

    TEST_ASSERT_EQUAL_MESSAGE(ModuleModeEnum::CONFIGURED_DONE,
                              g_mockModuleConfig.getRequestedModuleMode(),
                              "a registered module asked for a restart over an unreported address");

    on_change_local_ip_use_case_teardown();
}

// The defect this pins: the old code moved _oldIpStr forward before the send,
// so after a failure ipChanged was false forever and the structure was never offered again.
void test_an_undelivered_structure_is_offered_again(void) {
    on_change_local_ip_use_case_setup();

    g_mockSettings.setWasInitialSyncCompleted(1);
    g_mockWifiAdapter.setLocalIP("192.168.1.200");
    g_mockWifiAdapter.setConnected(true);

    // Three rounds of: the loop offers the structure, the outbox spends its budget, it fails.
    for (int round = 1; round <= 3; round++) {
        g_onChangeLocalIpUseCase->perform();
        completeStructure((uint32_t)round, false);
    }

    char message[192];
    snprintf(message, sizeof(message),
             "after three failed deliveries the address had gone out %u times; an unreported "
             "address must keep being offered",
             (unsigned)g_outbox->moduleStructureSends);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, g_outbox->moduleStructureSends, message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", g_onChangeLocalIpUseCase->reportedIp(),
                                     "an address the server never confirmed was recorded as reported");

    // And when the link comes back, the very next offer is delivered and the address settles.
    g_onChangeLocalIpUseCase->perform();
    TEST_ASSERT_EQUAL_UINT32(4, g_outbox->moduleStructureSends);
    completeStructure(4, true);
    TEST_ASSERT_EQUAL_STRING("192.168.1.200", g_onChangeLocalIpUseCase->reportedIp());

    // Settled means settled: no further structures for an address that has not changed.
    g_onChangeLocalIpUseCase->perform();
    TEST_ASSERT_EQUAL_UINT32(4, g_outbox->moduleStructureSends);

    on_change_local_ip_use_case_teardown();
}

// wasInitialSyncCompleted is set on the confirmation, not on the submission - otherwise a module
// whose first structure never arrived would count itself registered and stop asking for a restart.
void test_initial_sync_is_marked_complete_only_on_delivery(void) {
    on_change_local_ip_use_case_setup();

    g_mockSettings.setWasInitialSyncCompleted(0);
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);

    g_onChangeLocalIpUseCase->perform();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, g_mockSettings.getWasInitialSyncCompleted(),
                                    "the module counted itself registered on submission alone");

    completeStructure(1, true);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, g_mockSettings.getWasInitialSyncCompleted(),
                                    "a delivered structure did not complete the initial sync");

    on_change_local_ip_use_case_teardown();
}

// Structures are also submitted by CommandService. Somebody else's delivery is not confirmation
// that this module's address arrived.
void test_another_producers_structure_completion_is_ignored(void) {
    on_change_local_ip_use_case_setup();

    g_mockSettings.setWasInitialSyncCompleted(1);
    g_mockWifiAdapter.setLocalIP("192.168.1.100");
    g_mockWifiAdapter.setConnected(true);

    g_onChangeLocalIpUseCase->perform();   // ticket 1
    completeStructure(7, true);            // somebody else's ticket

    TEST_ASSERT_EQUAL_STRING_MESSAGE("", g_onChangeLocalIpUseCase->reportedIp(),
                                     "another producer's delivery was taken as our confirmation");

    completeStructure(1, true);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", g_onChangeLocalIpUseCase->reportedIp());

    on_change_local_ip_use_case_teardown();
}

// Main test runner function
extern "C" int run_on_change_local_ip_use_case_tests(void) {
    printf("\n----- Running OnChangeLocalIpUseCase Tests -----\n");
    fflush(stdout);

    RUN_TEST(test_perform_when_ip_is_zero_ip);
    RUN_TEST(test_perform_when_wifi_not_connected);
    RUN_TEST(test_perform_with_same_ip_no_change);
    RUN_TEST(test_perform_with_new_ip_updates_structure);
    RUN_TEST(test_perform_with_changing_ip);
    RUN_TEST(test_perform_hands_the_structure_over_and_the_syncs_follow_its_completion);
    RUN_TEST(test_the_syncs_follow_an_undelivered_structure_only_on_a_registered_module);
    RUN_TEST(test_one_structure_travels_at_a_time);
    RUN_TEST(test_an_address_change_is_said_once_while_its_structure_travels);
    RUN_TEST(test_a_missing_address_is_said_once_until_one_comes);
    RUN_TEST(test_a_link_that_is_down_is_said_once_until_it_comes_back);
    RUN_TEST(test_an_unregistered_module_offers_its_structure_again_after_a_growing_pause_and_never_restarts);
    RUN_TEST(test_a_registered_module_keeps_running_when_the_structure_is_not_delivered);
    RUN_TEST(test_an_undelivered_structure_is_offered_again);
    RUN_TEST(test_initial_sync_is_marked_complete_only_on_delivery);
    RUN_TEST(test_another_producers_structure_completion_is_ignored);

    printf("\n----- OnChangeLocalIpUseCase Tests Complete -----\n");
    return 0;
}
