// The module's parts that report their own state to the link's supervision: the TLS client, which
// says when a handshake failed for want of memory and when the module's own stack refused it a
// socket, and the HTTP client, which says whether the server answered a request with a status at all.
//
// The classes under test are the product's MeteredTlsClient and HttpClientAdapter. Replaced is the
// framework under them (test/Mock/native/WiFiClientSecure.h, HTTPClient.h), and it fails where the
// core fails: a connect that ends before the TLS client runs - a name the lookup did not find - leaves
// the client's last error at what the previous connection left there, exactly as the core's connect()
// returns before start_ssl_client() sets it.

#include <unity.h>

#include <cerrno>

#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"

namespace {

const uint32_t FRESH_MS = 90000;

void resetTheFramework()
{
    WiFiClientSecure::hostReset();
    HTTPClient::hostReset();
    hostUptimeMs = 100000;
}

} // namespace

/** A handshake the TLS stack failed for memory is reported; a completed one clears it. */
void test_a_handshake_that_failed_for_memory_is_reported_and_a_completed_one_clears_it(void)
{
    resetTheFramework();
    ModuleStatusBoard board;
    MeteredTlsClient client;
    client.setStatusBoard(&board);

    WiFiClientSecure::hostNextConnectResult = 0;
    WiFiClientSecure::hostNextTlsError = ModuleStatusBoard::kSslAllocFailed;
    TEST_ASSERT_EQUAL_INT(0, client.connect("gd.test", 443));
    TEST_ASSERT_TRUE_MESSAGE(board.tlsOutOfMemoryWithin(millis(), FRESH_MS),
                             "a handshake failed for memory was not reported");

    hostUptimeAdvance(1000);
    WiFiClientSecure::hostNextConnectResult = 1;
    TEST_ASSERT_EQUAL_INT(1, client.connect(IPAddress(10, 0, 0, 1), 443));
    TEST_ASSERT_FALSE_MESSAGE(board.tlsOutOfMemoryWithin(millis(), FRESH_MS),
                              "a completed handshake did not clear the report");
}

/** A handshake that failed for another reason says nothing about memory. */
void test_a_handshake_that_failed_otherwise_reports_no_lack_of_memory(void)
{
    resetTheFramework();
    ModuleStatusBoard board;
    MeteredTlsClient client;
    client.setStatusBoard(&board);

    WiFiClientSecure::hostNextConnectResult = 0;
    WiFiClientSecure::hostNextTlsError = -0x7280; // MBEDTLS_ERR_SSL_CONN_EOF, as seen on a device
    client.connect("gd.test", 443);
    WiFiClientSecure::hostNextTlsError = -1;       // the TCP connect, or the handshake's timeout
    client.connect("gd.test", 443);
    TEST_ASSERT_FALSE(board.tlsOutOfMemoryWithin(millis(), FRESH_MS));
}

/**
 * A name the lookup did not find ends the connect before the TLS client runs, and the core then
 * leaves the previous connection's code in place. Read as it stands, a lack of memory long gone would
 * be reported afresh on every outage of the name server.
 *
 * Mutation: do not clear the client's last error before the connect.
 */
void test_a_name_not_found_is_not_taken_for_the_last_connections_lack_of_memory(void)
{
    resetTheFramework();
    ModuleStatusBoard board;
    MeteredTlsClient client;
    client.setStatusBoard(&board);

    WiFiClientSecure::hostNextConnectResult = 0;
    WiFiClientSecure::hostNextTlsError = ModuleStatusBoard::kSslAllocFailed;
    client.connect("gd.test", 443);
    TEST_ASSERT_TRUE(board.tlsOutOfMemoryWithin(millis(), FRESH_MS));

    // Long after, the name server is down: the connect fails before TLS.
    hostUptimeAdvance(10 * FRESH_MS);
    TEST_ASSERT_FALSE(board.tlsOutOfMemoryWithin(millis(), FRESH_MS));
    WiFiClientSecure::hostNextFailsBeforeTls = true;
    TEST_ASSERT_EQUAL_INT(0, client.connect("gd.test", 443));
    TEST_ASSERT_FALSE_MESSAGE(board.tlsOutOfMemoryWithin(millis(), FRESH_MS),
                              "a failed name lookup was reported as a handshake short of memory");
}

/**
 * The core answers -1 both for a socket the module's own stack refused and for a TCP connect that
 * failed; errno is what tells them apart. Refusals - no free socket, no buffers, no memory - are
 * counted in a row, and a connect whose socket opened, failed afterwards or not, starts the count again.
 */
void test_sockets_the_stack_refused_are_counted_in_a_row_until_one_opens(void)
{
    resetTheFramework();
    ModuleStatusBoard board;
    MeteredTlsClient client;
    client.setStatusBoard(&board);

    WiFiClientSecure::hostNextConnectResult = 0;
    WiFiClientSecure::hostNextTlsError = -1;
    WiFiClientSecure::hostNextStackErrno = ENFILE;
    client.connect("gd.test", 443);
    WiFiClientSecure::hostNextStackErrno = ENOBUFS;
    client.connect("gd.test", 443);
    TEST_ASSERT_FALSE_MESSAGE(board.socketsRefusedWithin(millis(), FRESH_MS, 3), "two refusals counted as three");
    WiFiClientSecure::hostNextStackErrno = ENOMEM;
    client.connect(IPAddress(10, 0, 0, 1), 443);
    TEST_ASSERT_TRUE_MESSAGE(board.socketsRefusedWithin(millis(), FRESH_MS, 3),
                             "three sockets refused in a row were not reported");

    // The socket opens; the TCP connect behind it fails - the network's, not the module's.
    WiFiClientSecure::hostNextStackErrno = EINPROGRESS;
    client.connect("gd.test", 443);
    TEST_ASSERT_FALSE_MESSAGE(board.socketsRefusedWithin(millis(), FRESH_MS, 1),
                              "a socket that opened did not start the count again");
}

/** A server that does not answer fails every TCP connect: not one of them is a refused socket. */
void test_tcp_connects_that_failed_are_not_refused_sockets(void)
{
    resetTheFramework();
    ModuleStatusBoard board;
    MeteredTlsClient client;
    client.setStatusBoard(&board);

    WiFiClientSecure::hostNextConnectResult = 0;
    WiFiClientSecure::hostNextTlsError = -1;
    for (int i = 0; i < 6; i++)
    {
        client.connect("gd.test", 443);
    }
    TEST_ASSERT_FALSE(board.socketsRefusedWithin(millis(), FRESH_MS, 1));
}

/**
 * Two connects that never asked for a socket - a name the lookup did not find, and the core's -1 for
 * a client with no certificate set - neither count nor start the count again, whatever errno an
 * earlier refusal left on the task.
 *
 * Mutations: do not clear errno before either connect; take a -1 that asked for no socket for one that
 * opened.
 */
void test_a_connect_that_never_asked_for_a_socket_neither_counts_nor_clears_the_refusals(void)
{
    resetTheFramework();
    ModuleStatusBoard board;
    MeteredTlsClient client;
    client.setStatusBoard(&board);

    WiFiClientSecure::hostNextConnectResult = 0;
    WiFiClientSecure::hostNextTlsError = -1;
    WiFiClientSecure::hostNextStackErrno = ENFILE;
    client.connect("gd.test", 443);
    client.connect("gd.test", 443);

    // What the refusal left on the task, as the next connect finds it - by name, and by address.
    errno = ENFILE;
    WiFiClientSecure::hostNextFailsBeforeTheSocket = true;
    client.connect("gd.test", 443);
    TEST_ASSERT_FALSE_MESSAGE(board.socketsRefusedWithin(millis(), FRESH_MS, 3),
                              "a connect by name that asked for no socket was counted as refused");
    errno = ENFILE;
    client.connect(IPAddress(10, 0, 0, 1), 443);
    TEST_ASSERT_FALSE_MESSAGE(board.socketsRefusedWithin(millis(), FRESH_MS, 3),
                              "a connect by address that asked for no socket was counted as refused");
    WiFiClientSecure::hostNextFailsBeforeTheSocket = false;

    errno = ENFILE;
    WiFiClientSecure::hostNextFailsBeforeTls = true;
    client.connect("gd.test", 443);
    TEST_ASSERT_FALSE(board.socketsRefusedWithin(millis(), FRESH_MS, 3));
    WiFiClientSecure::hostNextFailsBeforeTls = false;

    client.connect("gd.test", 443);
    TEST_ASSERT_TRUE_MESSAGE(board.socketsRefusedWithin(millis(), FRESH_MS, 3),
                             "a connect that asked for no socket started the count again");
}

/** Any status from the server is an answer; a negative code from the client is a request not answered. */
void test_every_request_tells_whether_the_server_answered(void)
{
    resetTheFramework();
    ModuleStatusBoard board;
    HttpClientAdapter adapter;
    adapter.setStatusBoard(&board);
    const uint32_t start = (uint32_t)millis();

    HTTPClient::hostRespondWith(-1, "");
    TEST_ASSERT_TRUE(adapter.begin("https://gd.test/api/v1/HardWare/update?token=T"));
    TEST_ASSERT_EQUAL_INT(-1, adapter.POST("{}"));
    adapter.end();
    TEST_ASSERT_TRUE(board.requestNotReachedSince(millis(), start));
    TEST_ASSERT_FALSE(board.serverAnsweredSince(millis(), start));

    hostUptimeAdvance(5000);
    HTTPClient::hostRespondWith(503, "");
    TEST_ASSERT_TRUE(adapter.begin("https://gd.test/api/v1/HardWare/rulelist?token=T"));
    TEST_ASSERT_EQUAL_INT(503, adapter.GET());
    adapter.end();
    TEST_ASSERT_TRUE_MESSAGE(board.serverAnsweredSince(millis(), start), "a status from the server was not an answer");
}

extern "C" void run_link_status_reporters_tests(void)
{
    RUN_TEST(test_a_handshake_that_failed_for_memory_is_reported_and_a_completed_one_clears_it);
    RUN_TEST(test_a_handshake_that_failed_otherwise_reports_no_lack_of_memory);
    RUN_TEST(test_a_name_not_found_is_not_taken_for_the_last_connections_lack_of_memory);
    RUN_TEST(test_sockets_the_stack_refused_are_counted_in_a_row_until_one_opens);
    RUN_TEST(test_tcp_connects_that_failed_are_not_refused_sockets);
    RUN_TEST(test_a_connect_that_never_asked_for_a_socket_neither_counts_nor_clears_the_refusals);
    RUN_TEST(test_every_request_tells_whether_the_server_answered);
}
