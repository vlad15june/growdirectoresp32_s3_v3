// What a field log says about the WiFi link, and what it stops saying when the link is dancing.
//
// A module that goes quiet is either off the network or on it and broken, and until now the log
// could not tell an installer which: the diagnostics poll the link every few seconds and say
// "still down", but the reason the radio gives for a drop lives in one system event and is gone
// after it. That reason is the answer to most of the questions a field log is opened with - 201 is
// a network that is not there, 15 a password that is wrong, 8 an access point that sent the module
// away, 200 a beacon that stopped coming.
//
// These tests are of WifiLinkLogUseCase, which is what the radio's events are reported to. What is
// replaced is the radio: on the device WifiAdapter::stationEventCallback() turns
// ARDUINO_EVENT_WIFI_STA_GOT_IP and ARDUINO_EVENT_WIFI_STA_DISCONNECTED into the two calls made
// here, and nothing else calls them.
//
// ### Reproducing the red
//
// This is new code, so there is no revision of it that these tests fail against. What each of them
// is worth is shown by a mutation instead, and each mutation is named above the test it turns red.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Core/Domain/UseCases/Network/WifiLinkLogUseCase.h"

#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"

#include <string>
#include <vector>

namespace {

/** @brief The link log with a clock a test can move, and nothing else standing in. */
struct LinkUnderTest
{
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockArduinoAdapter arduino;
    WifiLinkLogUseCase linkLog;

    LinkUnderTest() : linkLog(&logger, &debugSystem, &arduino)
    {
        // The debug feature the WiFi lines go out under ships enabled, as it is on a module in the
        // field.
        debugSystem.featureEnabled = true;
        arduino.setMillis(0);
    }

    void at(unsigned long ms) { arduino.setMillis(ms); }

    /** @brief Lines this log wrote - the ones a field log is read for. */
    std::vector<std::string> linesSaying(const char* needle) const
    {
        std::vector<std::string> found;
        for (size_t i = 0; i < logger.messages.size(); i++)
        {
            if (logger.messages[i].find(needle) != std::string::npos)
            {
                found.push_back(logger.messages[i]);
            }
        }
        return found;
    }

    size_t countSaying(const char* needle) const { return linesSaying(needle).size(); }
};

/**
 * The line a module gives when it joins its network: which network, how well it hears it, and what
 * address it was given. All three come off the association the radio just made rather than off a
 * poll made later, so they describe the link that is actually up.
 *
 * Mutation: drop the rssi from the format string. The comparison below is of the whole line.
 */
void test_wifi_a_link_that_comes_up_is_said_whole(void)
{
    LinkUnderTest scene;

    scene.at(11500);
    scene.linkLog.linkUp("greenhouse-ap", -58, "192.168.1.100");

    const std::vector<std::string> lines = scene.linesSaying("WIFI: connected");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)lines.size(),
                              "a link coming up did not produce exactly one line");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("WIFI: connected greenhouse-ap rssi=-58 ip=192.168.1.100",
                                     lines[0].c_str(),
                                     "the line a joined module writes is not the line it is "
                                     "supposed to write");
}

/**
 * And the line that matters more: the drop, with the reason code the radio gave for it and the
 * length of the link that ended. The reason is the whole point - it exists in the event and
 * nowhere else - and how long the link stood is what tells a module that fell over once from one
 * that has been falling over all day.
 *
 * Mutation: log the reason without the length, or measure the length from the last event rather
 * than from the moment the link came up.
 */
void test_wifi_a_link_that_goes_down_carries_the_reason_and_its_length(void)
{
    LinkUnderTest scene;

    scene.at(20000);
    scene.linkLog.linkUp("greenhouse-ap", -58, "192.168.1.100");

    // An hour later the access point stops sending beacons.
    scene.at(20000 + 3600000);
    scene.linkLog.linkDown(200);

    const std::vector<std::string> lines = scene.linesSaying("WIFI: disconnected");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)lines.size(),
                              "a link going down did not produce exactly one line");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("WIFI: disconnected reason=200 after 3600000 ms",
                                     lines[0].c_str(),
                                     "the line a dropped link writes does not carry the reason and "
                                     "the length of the link that ended");
}

/**
 * A station that cannot associate says so every couple of seconds for as long as it keeps trying,
 * and every one of those events is the same event. One line for the drop, and then nothing until
 * something actually changes - otherwise the mechanism that exists to make a field log readable is
 * the loudest thing in it.
 *
 * Mutation: drop the state check at the top of linkDown() or of linkUp().
 */
void test_wifi_the_radio_saying_the_same_thing_again_says_nothing(void)
{
    LinkUnderTest scene;

    scene.at(30000);
    scene.linkLog.linkUp("greenhouse-ap", -58, "192.168.1.100");

    // The link drops, and the radio retries the association every two seconds for ten minutes.
    unsigned long now = 60000;
    scene.at(now);
    scene.linkLog.linkDown(201);
    for (int retry = 0; retry < 300; retry++)
    {
        now += 2000;
        scene.at(now);
        scene.linkLog.linkDown(201);
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)scene.countSaying("WIFI: disconnected"),
                              "the radio retrying an association filled the log");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)scene.countSaying("WIFI: link flapping"),
                              "a link that never changed state was reported as flapping");

    // And the same on the other side: an address renewed on a link that never went down.
    now += 2000;
    scene.at(now);
    scene.linkLog.linkUp("greenhouse-ap", -60, "192.168.1.100");
    for (int renewal = 0; renewal < 20; renewal++)
    {
        now += 3600000;
        scene.at(now);
        scene.linkLog.linkUp("greenhouse-ap", -60, "192.168.1.100");
    }

    TEST_ASSERT_EQUAL_MESSAGE(2, (int)scene.countSaying("WIFI: connected"),
                              "an address renewed on a link that was already up was reported as a "
                              "new link");
}

/**
 * The commonest failure of an installation: the module is powered, the network name is wrong or
 * the network is not there, and the module never joins anything. Nothing changes state after that
 * first event, so the log gets one line - and that line has to carry the reason, or the whole
 * installation is diagnosed by guessing.
 *
 * Mutation: return early from linkDown() when the link was never up.
 */
void test_wifi_a_module_that_never_joined_still_gives_the_reason(void)
{
    LinkUnderTest scene;

    unsigned long now = 8000;
    for (int attempt = 0; attempt < 100; attempt++)
    {
        scene.at(now);
        scene.linkLog.linkDown(201);
        now += 2000;
    }

    const std::vector<std::string> lines = scene.linesSaying("WIFI: disconnected");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)lines.size(),
                              "a module that never joined said nothing about why, or said it over "
                              "and over");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("WIFI: disconnected reason=201 after no link", lines[0].c_str(),
                                     "the one line a module that never joined writes does not name "
                                     "the reason");
}

/**
 * The pair an installer is looking for, and the thing the flap guard must not eat: one drop and
 * the recovery that follows it, seconds apart. Both are said in full.
 *
 * Mutation: hold a change back as soon as it lands inside FLAP_GAP_MS of the one before it, rather
 * than after FLAP_BURST of them in a row.
 */
void test_wifi_a_drop_and_the_recovery_after_it_are_both_said(void)
{
    LinkUnderTest scene;

    scene.at(60000);
    scene.linkLog.linkUp("greenhouse-ap", -58, "192.168.1.100");

    scene.at(3600000);
    scene.linkLog.linkDown(8);
    scene.at(3608000);
    scene.linkLog.linkUp("greenhouse-ap", -71, "192.168.1.100");

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)scene.countSaying("WIFI: disconnected"),
                              "the drop was not said");
    TEST_ASSERT_EQUAL_MESSAGE(2, (int)scene.countSaying("WIFI: connected"),
                              "the recovery eight seconds after the drop was not said - it is the "
                              "half of the pair that says the outage ended");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)scene.countSaying("WIFI: link flapping"),
                              "one drop and its recovery were called a flap");
}

/**
 * A module at the edge of its cell: the link comes and goes every twenty seconds, all night. Every
 * change is real and the first few are worth a line each; after that they are worth one line every
 * five minutes, which still carries the reason and how many changes it stands for.
 *
 * Mutation: never hold a change back (return false from holdThisChangeBack()). Two hundred and
 * forty changes then write two hundred and forty lines.
 */
void test_wifi_a_flapping_link_says_less_and_still_says_why(void)
{
    LinkUnderTest scene;

    unsigned long now = 60000;
    scene.at(now);
    scene.linkLog.linkUp("greenhouse-ap", -84, "192.168.1.100");

    // Eighty minutes of a link changing state every twenty seconds: 240 changes.
    bool up = true;
    int changes = 0;
    for (int i = 0; i < 240; i++)
    {
        now += 20000;
        scene.at(now);
        if (up)
        {
            scene.linkLog.linkDown(200);
        }
        else
        {
            scene.linkLog.linkUp("greenhouse-ap", -84, "192.168.1.100");
        }
        up = !up;
        changes++;
    }

    const size_t said = scene.countSaying("WIFI: connected") +
                        scene.countSaying("WIFI: disconnected") +
                        scene.countSaying("WIFI: link flapping");
    TEST_ASSERT_EQUAL_MESSAGE(240, changes, "the scene did not produce the changes it claims to");
    // Eighty minutes at one line every five is sixteen, plus the handful said in full before the
    // log gave up on them. What matters is that it is not two hundred and forty.
    TEST_ASSERT_TRUE_MESSAGE(said <= 25,
                             "a flapping link wrote a line for every change it made");
    TEST_ASSERT_TRUE_MESSAGE(said >= 5,
                             "a flapping link said nothing at all - a module that spends the night "
                             "off and on the network has to be visible in its log");

    const std::vector<std::string> flapping = scene.linesSaying("WIFI: link flapping");
    TEST_ASSERT_TRUE_MESSAGE(!flapping.empty(),
                             "nothing in the log says the link was flapping");
    TEST_ASSERT_TRUE_MESSAGE(flapping[0].find("last reason=200") != std::string::npos,
                             "the line that stands for the changes held back does not carry the "
                             "reason the radio gave for them");
}

/**
 * And what a link that settles owes the log: the count of what was held back while it danced, so
 * that a person reading the log is not left thinking the module was quietly connected all along.
 *
 * Mutation: drop the sayWhatWasHeldBack() call, or reset the count without saying it.
 */
void test_wifi_a_link_that_settles_says_what_was_held_back(void)
{
    LinkUnderTest scene;

    unsigned long now = 60000;
    scene.at(now);
    scene.linkLog.linkUp("greenhouse-ap", -84, "192.168.1.100");

    // Forty-one more changes, which leaves the link down: forty-two in all, of which the first
    // five are said in full and the other thirty-seven are held back.
    bool up = true;
    for (int i = 0; i < 41; i++)
    {
        now += 20000;
        scene.at(now);
        if (up)
        {
            scene.linkLog.linkDown(200);
        }
        else
        {
            scene.linkLog.linkUp("greenhouse-ap", -84, "192.168.1.100");
        }
        up = !up;
    }

    TEST_ASSERT_TRUE_MESSAGE(scene.countSaying("WIFI: link flapping") > 0,
                             "the scene never got as far as holding anything back");

    // Then the link is left alone for an hour and comes back for good.
    now += 3600000;
    scene.at(now);
    scene.linkLog.linkUp("greenhouse-ap", -55, "192.168.1.100");

    const std::vector<std::string> settled = scene.linesSaying("WIFI: link settled");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)settled.size(),
                              "a link that settled after flapping did not account for what was not "
                              "said");
    TEST_ASSERT_TRUE_MESSAGE(settled[0].find("37 change(s)") != std::string::npos,
                             "the account of what was held back does not match the changes the "
                             "link made");

    const std::vector<std::string> connected = scene.linesSaying("WIFI: connected");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("WIFI: connected greenhouse-ap rssi=-55 ip=192.168.1.100",
                                     connected[connected.size() - 1].c_str(),
                                     "the change that ended the flapping was not said in full");
}

/**
 * The debug feature the lines go out under is one an installer can turn off, and turning it off
 * has to be the end of it: the log is written from the radio's event task, and a feature nobody
 * asked for must not cost that task a formatted buffer.
 *
 * Mutation: format the line before the feature is checked.
 */
void test_wifi_a_link_log_nobody_asked_for_says_nothing(void)
{
    LinkUnderTest scene;
    scene.debugSystem.featureEnabled = false;

    scene.at(60000);
    scene.linkLog.linkUp("greenhouse-ap", -58, "192.168.1.100");
    scene.at(120000);
    scene.linkLog.linkDown(200);

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)scene.countSaying("WIFI:"),
                              "the link log wrote lines with its debug feature off");
}

} // namespace

extern "C" int run_wifi_link_log_tests(void)
{
    RUN_TEST(test_wifi_a_link_that_comes_up_is_said_whole);
    RUN_TEST(test_wifi_a_link_that_goes_down_carries_the_reason_and_its_length);
    RUN_TEST(test_wifi_the_radio_saying_the_same_thing_again_says_nothing);
    RUN_TEST(test_wifi_a_module_that_never_joined_still_gives_the_reason);
    RUN_TEST(test_wifi_a_drop_and_the_recovery_after_it_are_both_said);
    RUN_TEST(test_wifi_a_flapping_link_says_less_and_still_says_why);
    RUN_TEST(test_wifi_a_link_that_settles_says_what_was_held_back);
    RUN_TEST(test_wifi_a_link_log_nobody_asked_for_says_nothing);

    return 0;
}
