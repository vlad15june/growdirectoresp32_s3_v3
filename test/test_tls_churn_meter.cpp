// What a TLS handshake costs, and the three places that number has to survive: the accounting
// itself, the allocator the TLS stack was given, and the client that opens the window around a
// handshake.
//
// Four groups follow, in that order plus one for the text of the line itself. The first drives
// TlsChurnMeter directly - it takes byte counts and timestamps and holds no platform header, so a
// test can state a whole handshake in a dozen lines and assert the arithmetic to the byte; the
// second pins the line character for character. The last two are the glue, and they are here
// because glue that no test executes is the way a measurement ends up measuring nothing: the pool
// is driven through the calloc/free pair it hands mbedtls, and the client through the connect()
// overloads the core calls on it - the same entry points the device uses.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Core/Domain/Diagnostics/TlsChurnMeter.h"
#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"
#include "Infrastructure/Adapters/Http/MeteredTlsClient.h"
#include "Infrastructure/Adapters/Http/TlsMemoryPool.h"

namespace
{

// --- the accounting -------------------------------------------------------------------------

// A handshake takes several things at once and gives them back in another order. What the heap
// felt is the largest total held at one moment - not the sum of the requests, which no single
// moment ever needed, and not what is left at the end, which is the resident cost of the
// connection and a different question.
void test_the_peak_is_the_largest_total_held_not_the_sum(void)
{
    TlsChurnMeter meter;

    meter.beginHandshake(1000);
    meter.recordAllocated(10000);
    meter.recordAllocated(20000);   // 30 000 held at once - the worst moment
    meter.recordFreed(20000);
    meter.recordAllocated(5000);    // 15 000 held, and this is what stays
    const TlsChurnMeter::Episode episode = meter.endHandshake(1980, true);

    TEST_ASSERT_EQUAL_UINT32(30000, episode.peakBytes);
    TEST_ASSERT_EQUAL_UINT32(15000, episode.liveBytesAfter);
    TEST_ASSERT_EQUAL_UINT32(980, episode.durationMs);
    TEST_ASSERT_EQUAL_UINT32(1, episode.index);
    TEST_ASSERT_TRUE(episode.completed);
    TEST_ASSERT_EQUAL_UINT32(0, episode.unpairedEvents);
    // The same numbers are readable afterwards, which is what a later report would use.
    TEST_ASSERT_EQUAL_UINT32(30000, meter.lastEpisodePeakBytes());
    TEST_ASSERT_EQUAL_UINT32(30000, meter.peakBytes());
    TEST_ASSERT_EQUAL_UINT32(15000, meter.liveBytes());
    TEST_ASSERT_EQUAL_UINT32(1, meter.handshakeCount());
}

// A window is the cost of one handshake, so it starts again with the next one - otherwise every
// line after the worst handshake of the run repeats that handshake's number. The since-boot peak
// is the one that does not reset.
void test_each_window_starts_from_what_is_held_now(void)
{
    TlsChurnMeter meter;

    meter.beginHandshake(0);
    meter.recordAllocated(50000);
    meter.recordFreed(50000);
    meter.endHandshake(1000, true);

    // A second connection, opened while nothing is held: a far cheaper one.
    meter.beginHandshake(2000);
    meter.recordAllocated(4000);
    const TlsChurnMeter::Episode second = meter.endHandshake(2400, true);

    TEST_ASSERT_EQUAL_UINT32(4000, second.peakBytes);
    TEST_ASSERT_EQUAL_UINT32(2, second.index);
    // ...and the worst of the run is still the first one.
    TEST_ASSERT_EQUAL_UINT32(50000, second.globalPeakBytes);
    TEST_ASSERT_EQUAL_UINT32(50000, meter.peakBytes());

    meter.recordFreed(4000);
}

// A handshake performed while another connection is up is charged with what that connection
// holds: that total is what the heap has to find room next to, and a window that started from
// zero would understate exactly the case that hurts.
void test_a_window_includes_what_was_already_held_when_it_opened(void)
{
    TlsChurnMeter meter;

    meter.recordAllocated(9000);    // a connection already up

    meter.beginHandshake(0);
    meter.recordAllocated(1000);
    const TlsChurnMeter::Episode episode = meter.endHandshake(100, true);

    TEST_ASSERT_EQUAL_UINT32(10000, episode.peakBytes);
    TEST_ASSERT_EQUAL_UINT32(10000, episode.liveBytesAfter);
}

// The TLS stack allocates through the C library until the shim is installed and releases those
// blocks through the shim afterwards, so releases with nothing behind them do arrive. Subtracting
// past zero would wrap the total into hundreds of megabytes and every peak after it would be that
// number; the clamp keeps the total usable, and the count says the line is not to be trusted.
void test_a_release_without_a_request_clamps_and_is_counted(void)
{
    TlsChurnMeter meter;

    meter.recordAllocated(1000);
    meter.recordFreed(5000);

    TEST_ASSERT_EQUAL_UINT32(0, meter.liveBytes());
    TEST_ASSERT_EQUAL_UINT32(1, meter.unpairedEvents());

    // And the total is usable again straight away rather than poisoned.
    meter.beginHandshake(0);
    meter.recordAllocated(700);
    const TlsChurnMeter::Episode episode = meter.endHandshake(10, true);
    TEST_ASSERT_EQUAL_UINT32(700, episode.peakBytes);
    TEST_ASSERT_EQUAL_UINT32(1, episode.unpairedEvents);
}

// The other end of the same decision. A total that wrapped would report either four gigabytes or
// a few hundred bytes, and both read as a plausible measurement.
void test_a_total_that_would_wrap_saturates_and_is_counted(void)
{
    TlsChurnMeter meter;

    meter.recordAllocated(0xFFFFFF00u);
    meter.recordAllocated(0x200u);

    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, meter.liveBytes());
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, meter.peakBytes());
    TEST_ASSERT_EQUAL_UINT32(1, meter.unpairedEvents());
}

// The module runs for weeks and the millisecond counter wraps every 49 days. A duration computed
// as a signed difference of two unsigned readings around that point is a negative number printed
// as four billion milliseconds.
void test_a_window_measured_across_the_millisecond_wraparound(void)
{
    TlsChurnMeter meter;

    meter.beginHandshake(0xFFFFFF00u);
    const TlsChurnMeter::Episode episode = meter.endHandshake(0x00000100u, true);

    TEST_ASSERT_EQUAL_UINT32(512, episode.durationMs);
}

// A window is a handshake's cost, so it stops being written to when the handshake ends. Between
// connections the TLS stack still allocates - a request on a kept-alive connection, an OTA body -
// and a reader asking then must get the last handshake's number, not the traffic since.
void test_the_last_window_peak_stops_when_the_window_closes(void)
{
    TlsChurnMeter meter;

    meter.beginHandshake(0);
    meter.recordAllocated(10000);
    const TlsChurnMeter::Episode episode = meter.endHandshake(100, true);
    TEST_ASSERT_EQUAL_UINT32(10000, episode.peakBytes);

    // Well past the worst the handshake reached, and outside any window.
    meter.recordAllocated(40000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(10000, meter.lastEpisodePeakBytes(),
                                     "the closed window kept collecting after the handshake");
    // The since-boot peak is the one that does follow everything.
    TEST_ASSERT_EQUAL_UINT32(50000, meter.peakBytes());

    meter.recordFreed(50000);
}

// Closing a window that was never opened would report a duration measured from whenever the
// previous handshake started - a plausible-looking number with nothing behind it.
void test_closing_a_window_that_was_never_opened_is_counted(void)
{
    TlsChurnMeter meter;

    const TlsChurnMeter::Episode episode = meter.endHandshake(5000, true);

    TEST_ASSERT_EQUAL_UINT32(1, episode.unpairedEvents);
    TEST_ASSERT_EQUAL_UINT32(1, meter.unpairedEvents());
}

// Two connections handshaking at once would interleave into one window and both lines would be a
// mixture. The outer window is kept, and the event is counted for the same reason as the clamp.
void test_a_second_window_opened_inside_the_first_is_counted(void)
{
    TlsChurnMeter meter;

    meter.beginHandshake(1000);
    meter.recordAllocated(3000);
    meter.beginHandshake(1500);
    const TlsChurnMeter::Episode episode = meter.endHandshake(2000, true);

    TEST_ASSERT_EQUAL_UINT32(1000, episode.durationMs);
    TEST_ASSERT_EQUAL_UINT32(3000, episode.peakBytes);
    TEST_ASSERT_EQUAL_UINT32(1, episode.unpairedEvents);

    meter.recordFreed(3000);
}

// --- the line -------------------------------------------------------------------------------

// The line is the whole output of the meter, so its shape is pinned here rather than left to
// whatever the printf call happens to say.
void test_the_line_reports_what_the_window_measured(void)
{
    TlsChurnMeter::Episode episode;
    episode.index = 12;
    episode.durationMs = 980;
    episode.peakBytes = 58432;
    episode.liveBytesAfter = 9410;
    episode.globalPeakBytes = 61200;
    episode.unpairedEvents = 0;
    episode.completed = true;

    char line[TlsChurnMeter::kLineBytes];
    const size_t needed = TlsChurnMeter::formatEpisode(episode, line, sizeof(line));

    TEST_ASSERT_EQUAL_STRING("TLS_CHURN: handshake=full n=12 dur_ms=980 peak=58432 max=61200 "
                             "live_after=9410 unpaired=0",
                             line);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(line), (uint32_t)needed);
}

// A handshake that never completed spent memory too, and its cost is the interesting one: it is
// the case where the module ran out. It must not be readable as a successful connection.
void test_a_failed_handshake_is_labelled_as_one(void)
{
    TlsChurnMeter::Episode episode;
    episode.index = 13;
    episode.durationMs = 15020;
    episode.peakBytes = 41200;
    episode.liveBytesAfter = 0;
    episode.globalPeakBytes = 61200;
    episode.unpairedEvents = 2;
    episode.completed = false;

    char line[TlsChurnMeter::kLineBytes];
    TlsChurnMeter::formatEpisode(episode, line, sizeof(line));

    TEST_ASSERT_EQUAL_STRING("TLS_CHURN: handshake=failed n=13 dur_ms=15020 peak=41200 max=61200 "
                             "live_after=0 unpaired=2",
                             line);
}

// The widest line every field can produce has to fit the buffer the client puts on its stack.
void test_the_widest_line_fits_the_buffer_the_client_reserves(void)
{
    TlsChurnMeter::Episode episode;
    episode.index = 0xFFFFFFFFu;
    episode.durationMs = 0xFFFFFFFFu;
    episode.peakBytes = 0xFFFFFFFFu;
    episode.liveBytesAfter = 0xFFFFFFFFu;
    episode.globalPeakBytes = 0xFFFFFFFFu;
    episode.unpairedEvents = 0xFFFFFFFFu;
    episode.completed = false;

    char line[TlsChurnMeter::kLineBytes];
    const size_t needed = TlsChurnMeter::formatEpisode(episode, line, sizeof(line));

    TEST_ASSERT_TRUE_MESSAGE(needed < TlsChurnMeter::kLineBytes,
                             "the widest line does not fit kLineBytes");
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(line), (uint32_t)needed);
}

// And a buffer too small truncates instead of running past its end.
void test_the_line_does_not_overrun_a_short_buffer(void)
{
    TlsChurnMeter::Episode episode;
    episode.index = 1;
    episode.durationMs = 2;
    episode.peakBytes = 3;
    episode.liveBytesAfter = 4;
    episode.globalPeakBytes = 5;
    episode.unpairedEvents = 6;
    episode.completed = true;

    char scratch[32];
    memset(scratch, '#', sizeof(scratch));
    const size_t needed = TlsChurnMeter::formatEpisode(episode, scratch, 20);

    TEST_ASSERT_TRUE(needed > 19);
    TEST_ASSERT_EQUAL_CHAR('\0', scratch[19]);
    TEST_ASSERT_EQUAL_CHAR('#', scratch[20]);
    TEST_ASSERT_EQUAL_STRING("TLS_CHURN: handshak", scratch);
}

// --- the allocator the TLS stack was given ----------------------------------------------------

// Below the slot window, so it goes to the heap the way the handshake scratch does.
const size_t kScratchSize = 2280;
// Inside the slot window: one of the two record buffers the pool exists for.
const size_t kRecordSize = 16384;

// What mbedtls does on the device, through the very pair of function pointers install() gave it.
void test_the_pool_charges_the_meter_for_what_it_takes_from_the_heap(void)
{
    TlsMemoryPool::install();
    TlsChurnMeter &meter = TlsChurnMeter::instance();

    const uint32_t liveBefore = meter.liveBytes();
    const uint32_t unpairedBefore = meter.unpairedEvents();

    void *scratch = mbedtlsInstalledCalloc(1, kScratchSize);
    TEST_ASSERT_NOT_NULL(scratch);

    const uint32_t liveHeld = meter.liveBytes();
    TEST_ASSERT_TRUE_MESSAGE(liveHeld >= liveBefore + kScratchSize,
                             "a heap allocation of the TLS stack was not charged to the meter");

    mbedtlsInstalledFree(scratch);

    // The release recovered the same number the request added, which is the only way the total
    // survives thousands of handshakes.
    TEST_ASSERT_EQUAL_UINT32(liveBefore, meter.liveBytes());
    TEST_ASSERT_EQUAL_UINT32(unpairedBefore, meter.unpairedEvents());
}

// A reserved slot is static memory: it costs the heap the same whether TLS runs or not, so
// charging it would add a constant to every reading and hide the thing being measured.
void test_a_reserved_slot_costs_the_meter_nothing(void)
{
    TlsMemoryPool::install();
    TlsChurnMeter &meter = TlsChurnMeter::instance();

    const uint32_t liveBefore = meter.liveBytes();

    void *record = mbedtlsInstalledCalloc(1, kRecordSize);
    TEST_ASSERT_NOT_NULL(record);
    TEST_ASSERT_EQUAL_UINT32(liveBefore, meter.liveBytes());

    mbedtlsInstalledFree(record);
    TEST_ASSERT_EQUAL_UINT32(liveBefore, meter.liveBytes());
}

// ...and a record buffer that found no free slot went to the heap after all, which is precisely
// the case the meter must see.
void test_a_pool_full_fallback_is_charged_to_the_meter(void)
{
    TlsMemoryPool::install();
    TlsChurnMeter &meter = TlsChurnMeter::instance();

    void *first = mbedtlsInstalledCalloc(1, kRecordSize);
    void *second = mbedtlsInstalledCalloc(1, kRecordSize);
    const uint32_t liveBefore = meter.liveBytes();

    void *third = mbedtlsInstalledCalloc(1, kRecordSize);
    TEST_ASSERT_NOT_NULL(third);
    TEST_ASSERT_TRUE_MESSAGE(meter.liveBytes() >= liveBefore + kRecordSize,
                             "a record buffer pushed to the heap was not charged to the meter");

    mbedtlsInstalledFree(third);
    TEST_ASSERT_EQUAL_UINT32(liveBefore, meter.liveBytes());

    mbedtlsInstalledFree(second);
    mbedtlsInstalledFree(first);
}

// --- the client that opens the window ---------------------------------------------------------

// What the stand-in TLS stack does with memory while the handshake runs: scratch taken and given
// back, and a smaller amount that a live connection keeps holding. Driven through the pool's own
// calloc/free pair, so the path under test is the device's.
void *g_residentAllocation = nullptr;
uint32_t g_liveAtWorstMoment = 0;

void tlsLikeHandshakeAllocations()
{
    void *chain = mbedtlsInstalledCalloc(1, 5000);
    void *scratch = mbedtlsInstalledCalloc(1, 2280);
    g_liveAtWorstMoment = TlsChurnMeter::instance().liveBytes();
    mbedtlsInstalledFree(scratch);
    mbedtlsInstalledFree(chain);

    // The session and the transform, which stay for as long as the connection does.
    g_residentAllocation = mbedtlsInstalledCalloc(1, 1400);
}

void test_a_handshake_prints_one_line_with_what_it_cost(void)
{
    TlsMemoryPool::install();
    WiFiClientSecure::hostReset();
    TlsChurnMeter &meter = TlsChurnMeter::instance();

    const uint32_t liveBefore = meter.liveBytes();
    const uint32_t handshakesBefore = meter.handshakeCount();

    WiFiClientSecure::hostNextConnectResult = 1;
    WiFiClientSecure::hostNextHandshakeMs = 980;
    WiFiClientSecure::hostDuringHandshake = &tlsLikeHandshakeAllocations;

    MeteredTlsClient client;
    Serial.printlnCalls = 0;
    const int result = client.connect("api.example.test", 443);

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, WiFiClientSecure::hostConnectCalls,
                                  "the handshake was not passed on to the TLS stack");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, Serial.printlnCalls,
                                  "a handshake did not produce exactly one line");

    // The substance: the transient really was larger than what the connection went on holding,
    // and what it went on holding is the resident block and nothing else.
    const uint32_t liveAfter = meter.liveBytes();
    TEST_ASSERT_TRUE_MESSAGE(g_liveAtWorstMoment > liveAfter,
                             "the window did not see the transient, only the end state");
    TEST_ASSERT_TRUE_MESSAGE(liveAfter > liveBefore,
                             "what the connection keeps holding was not counted");
    TEST_ASSERT_EQUAL_UINT32(handshakesBefore + 1, meter.handshakeCount());

    char expected[TlsChurnMeter::kLineBytes];
    snprintf(expected, sizeof(expected),
             "TLS_CHURN: handshake=full n=%lu dur_ms=980 peak=%lu max=%lu live_after=%lu "
             "unpaired=%lu",
             (unsigned long)(handshakesBefore + 1), (unsigned long)g_liveAtWorstMoment,
             (unsigned long)meter.peakBytes(), (unsigned long)liveAfter,
             (unsigned long)meter.unpairedEvents());
    TEST_ASSERT_EQUAL_STRING(expected, Serial.lastText.c_str());

    mbedtlsInstalledFree(g_residentAllocation);
    g_residentAllocation = nullptr;
    TEST_ASSERT_EQUAL_UINT32(liveBefore, meter.liveBytes());
}

// A connection that never came up: the line still says what the attempt cost, and says the
// attempt failed.
void test_a_failed_handshake_prints_the_line_too(void)
{
    TlsMemoryPool::install();
    WiFiClientSecure::hostReset();
    TlsChurnMeter &meter = TlsChurnMeter::instance();

    const uint32_t handshakesBefore = meter.handshakeCount();

    WiFiClientSecure::hostNextConnectResult = 0;
    WiFiClientSecure::hostNextHandshakeMs = 15020;

    MeteredTlsClient client;
    Serial.printlnCalls = 0;
    const int result = client.connect("api.example.test", 443);

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(1, Serial.printlnCalls);
    TEST_ASSERT_EQUAL_UINT32(handshakesBefore + 1, meter.handshakeCount());

    char expected[64];
    snprintf(expected, sizeof(expected), "TLS_CHURN: handshake=failed n=%lu dur_ms=15020 ",
             (unsigned long)(handshakesBefore + 1));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)Serial.lastText.rfind(expected, 0));
}

// The core's other connect family. Nothing in this firmware opens a connection by address today,
// but the overload is virtual and reaches the same handshake, so a caller that did would
// otherwise be measured as nothing at all.
void test_a_handshake_opened_by_address_is_measured_too(void)
{
    TlsMemoryPool::install();
    WiFiClientSecure::hostReset();
    TlsChurnMeter &meter = TlsChurnMeter::instance();

    const uint32_t handshakesBefore = meter.handshakeCount();

    WiFiClientSecure::hostNextConnectResult = 1;
    WiFiClientSecure::hostNextHandshakeMs = 640;
    WiFiClientSecure::hostDuringHandshake = &tlsLikeHandshakeAllocations;

    MeteredTlsClient client;
    Serial.printlnCalls = 0;
    const int result = client.connect(IPAddress(10, 0, 0, 7), 443);

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_FALSE_MESSAGE(WiFiClientSecure::hostLastConnectByName,
                              "the address overload did not reach the TLS stack");
    TEST_ASSERT_TRUE(IPAddress(10, 0, 0, 7) == WiFiClientSecure::hostLastAddress);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, Serial.printlnCalls,
                                  "a handshake opened by address produced no line");
    TEST_ASSERT_EQUAL_UINT32(handshakesBefore + 1, meter.handshakeCount());

    char expected[64];
    snprintf(expected, sizeof(expected), "TLS_CHURN: handshake=full n=%lu dur_ms=640 ",
             (unsigned long)(handshakesBefore + 1));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)Serial.lastText.rfind(expected, 0));

    mbedtlsInstalledFree(g_residentAllocation);
    g_residentAllocation = nullptr;
}

// The last link: the connection the HTTP adapter hands to HTTPClient is the metered one. Without
// this the meter would be correct and never reached, because the adapter is where the firmware's
// only https connections are opened.
void test_the_http_adapter_hands_http_client_a_metered_connection(void)
{
    HTTPClient::hostReset();
    HTTPClient::hostRespondWith(200, "{}");

    HttpClientAdapter adapter;
    TEST_ASSERT_TRUE(adapter.begin("https://api.example.test/v1/module"));

    WiFiClient *handedOver = adapter.getStreamPtr();
    TEST_ASSERT_NOT_NULL_MESSAGE(handedOver, "the adapter opened no secure connection");
    TEST_ASSERT_NOT_NULL_MESSAGE(dynamic_cast<MeteredTlsClient *>(handedOver),
                                 "HTTPClient was given a connection nothing measures");

    adapter.end();
    HTTPClient::hostReset();
}

// A server that takes the TCP connect and never finishes the handshake holds the adapter's connection
// for the adapter's bound of 20 s, not for WiFiClientSecure's default of 120 s: the connect gives up,
// and the line says how long the attempt took.
void test_the_http_adapter_gives_a_silent_handshake_twenty_seconds(void)
{
    TlsMemoryPool::install();
    WiFiClientSecure::hostReset();
    HTTPClient::hostReset();
    HTTPClient::hostRespondWith(200, "{}");

    HttpClientAdapter adapter;
    TEST_ASSERT_TRUE(adapter.begin("https://api.example.test/v1/module"));
    WiFiClient *handedOver = adapter.getStreamPtr();
    TEST_ASSERT_NOT_NULL_MESSAGE(handedOver, "the adapter opened no secure connection");

    // Silent for ten minutes: only the client's own bound ends the handshake.
    WiFiClientSecure::hostNextHandshakeMs = 600000;
    const unsigned long startedAt = millis();
    const int result = handedOver->connect("api.example.test", 443);
    const unsigned long tookMs = millis() - startedAt;
    printf("  tls: a silent handshake through the adapter took %lu ms\n", tookMs);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "a handshake nobody finished opened a connection");
    TEST_ASSERT_TRUE_MESSAGE(tookMs > 20000 && tookMs <= 20000 + 10,
                             "the adapter's handshake was not bounded at 20 s");

    adapter.end();
    HTTPClient::hostReset();
    WiFiClientSecure::hostReset();
}

} // namespace

extern "C" void run_tls_churn_meter_tests(void)
{
    RUN_TEST(test_the_peak_is_the_largest_total_held_not_the_sum);
    RUN_TEST(test_each_window_starts_from_what_is_held_now);
    RUN_TEST(test_a_window_includes_what_was_already_held_when_it_opened);
    RUN_TEST(test_a_release_without_a_request_clamps_and_is_counted);
    RUN_TEST(test_a_total_that_would_wrap_saturates_and_is_counted);
    RUN_TEST(test_a_window_measured_across_the_millisecond_wraparound);
    RUN_TEST(test_the_last_window_peak_stops_when_the_window_closes);
    RUN_TEST(test_closing_a_window_that_was_never_opened_is_counted);
    RUN_TEST(test_a_second_window_opened_inside_the_first_is_counted);

    RUN_TEST(test_the_line_reports_what_the_window_measured);
    RUN_TEST(test_a_failed_handshake_is_labelled_as_one);
    RUN_TEST(test_the_widest_line_fits_the_buffer_the_client_reserves);
    RUN_TEST(test_the_line_does_not_overrun_a_short_buffer);

    RUN_TEST(test_the_pool_charges_the_meter_for_what_it_takes_from_the_heap);
    RUN_TEST(test_a_reserved_slot_costs_the_meter_nothing);
    RUN_TEST(test_a_pool_full_fallback_is_charged_to_the_meter);

    RUN_TEST(test_a_handshake_prints_one_line_with_what_it_cost);
    RUN_TEST(test_a_failed_handshake_prints_the_line_too);
    RUN_TEST(test_a_handshake_opened_by_address_is_measured_too);
    RUN_TEST(test_the_http_adapter_hands_http_client_a_metered_connection);
    RUN_TEST(test_the_http_adapter_gives_a_silent_handshake_twenty_seconds);
}
