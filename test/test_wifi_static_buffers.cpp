// What the radio is told before it is first started, and in which order.
//
// How much memory the WiFi driver may hold is decided once, when it is initialised - and it is
// initialised by the first call that puts the radio into a mode. The request that makes it read
// the counts this build was configured with is read at that same moment: asked for afterwards it
// is a warning in a log nobody sees, and the driver keeps the core's own numbers - thirty-two
// dynamic receive buffers of about 1.6 KB, which is more than this firmware has free heap.
//
// So the order is the whole of the behaviour, and it is what these tests hold: the request first,
// the mode after it. What is replaced is the radio - the core's WiFi object is a singleton talking
// to a driver that does not exist on a host - by one that writes down what it was told. The
// sequence itself is the production one: WifiAdapter's constructor runs the same function on the
// device, with the core's WiFi object and with the build's own answer to "were the counts
// configured for this image".
//
// ### Reproducing the red
//
// Without the driver buffer profile the file does not compile: there is no
// WifiDriverBufferProfile.h and no sequence to call. What each test is worth is shown by a
// mutation of that sequence instead, named above the test it turns red.

#include <unity.h>

#include "Infrastructure/Adapters/WiFi/WifiDriverBufferProfile.h"

#include <string>
#include <vector>

namespace {

/** @brief A radio that does nothing and remembers everything it was told, in order. */
struct RecordingRadio
{
    enum Mode
    {
        Off = 0,
        Station = 1
    };

    std::vector<std::string> calls;

    void useStaticBuffers(bool keepTheConfiguredCounts)
    {
        calls.push_back(keepTheConfiguredCounts ? "useStaticBuffers(true)" : "useStaticBuffers(false)");
    }

    void mode(Mode wanted) { calls.push_back(wanted == Off ? "mode(off)" : "mode(station)"); }

    void setSleep(bool sleeping) { calls.push_back(sleeping ? "setSleep(true)" : "setSleep(false)"); }

    /** @brief Where a call is in the order, or -1 if it was never made. */
    int at(const char* call) const
    {
        for (size_t i = 0; i < calls.size(); i++)
        {
            if (calls[i] == call)
            {
                return (int)i;
            }
        }
        return -1;
    }
};

/**
 * Turns red on: the request moved below radio.mode(off) in prepareRadioBeforeFirstStart(), which
 * is where the driver is initialised and the counts are taken - the mutation the order exists
 * against.
 */
void test_wifi_the_configured_buffer_counts_are_asked_for_before_the_radio_is_first_brought_up(void)
{
    RecordingRadio radio;

    prepareRadioBeforeFirstStart(radio, RecordingRadio::Off, true);

    TEST_ASSERT_EQUAL_MESSAGE(0, radio.at("useStaticBuffers(true)"),
                              "an image built with the shrunk buffer counts did not ask the core "
                              "to keep them, or asked after the radio had been put into a mode - "
                              "the driver reads the request only when it is initialised");
    TEST_ASSERT_EQUAL_MESSAGE(1, radio.at("mode(off)"),
                              "the radio was not put into the off mode after the request");
    TEST_ASSERT_EQUAL_MESSAGE(2, radio.at("setSleep(false)"),
                              "modem sleep was not taken off before the first station start");
    TEST_ASSERT_EQUAL_MESSAGE(3, (int)radio.calls.size(),
                              "something else was said to the radio before its first start");
}

/**
 * Turns red on: the request made unconditionally (the `if` dropped), which would hand the factory
 * buffer counts to every image, the production one included.
 */
void test_wifi_an_image_without_the_profile_leaves_the_cores_buffer_counts_alone(void)
{
    RecordingRadio radio;

    // The expression the adapter's constructor passes; in a host build - as in a production image -
    // the profile is not compiled in.
    prepareRadioBeforeFirstStart(radio, RecordingRadio::Off, kWifiBufferCountsComeFromTheBuild);

    TEST_ASSERT_FALSE_MESSAGE(kWifiBufferCountsComeFromTheBuild,
                              "a build without GD_WIFI_STATIC_BUFFERS claims the counts were "
                              "configured for it");
    TEST_ASSERT_EQUAL_MESSAGE(-1, radio.at("useStaticBuffers(true)"),
                              "an image without the shrunk counts asked the driver to use the "
                              "ones the build carries anyway");
    TEST_ASSERT_EQUAL_MESSAGE(0, radio.at("mode(off)"),
                              "the radio was not put into the off mode");
    TEST_ASSERT_EQUAL_MESSAGE(1, radio.at("setSleep(false)"),
                              "modem sleep was not taken off before the first station start");
    TEST_ASSERT_EQUAL_MESSAGE(2, (int)radio.calls.size(),
                              "something else was said to the radio before its first start");
}

} // namespace

extern "C" int run_wifi_static_buffers_tests(void)
{
    RUN_TEST(test_wifi_the_configured_buffer_counts_are_asked_for_before_the_radio_is_first_brought_up);
    RUN_TEST(test_wifi_an_image_without_the_profile_leaves_the_cores_buffer_counts_alone);

    return 0;
}
