// The one line the HTTP client writes by itself, and what it is allowed to say.
//
// HttpClientAdapter::begin() prints when the connection could not be opened, and it prints to the
// port directly: not through the debug system, so no feature switch and no channel setting stands
// between that line and the serial capture. The URL it is handed is the one the transport built -
// with the account token in its query - which makes this refusal path the one place that would put
// the token back on a line after the transport itself stopped printing it.
//
// The connection is stood in for by the framework stand-in (test/Mock/native/HTTPClient.h), which
// refuses a URL whose protocol it cannot read, exactly as the core's begin() does. The class under
// test is the product's own adapter.

#include <unity.h>

#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"

#include <cstdio>
#include <string>

namespace {

// No protocol, so the framework refuses to open it - the shape of a server URL that arrived from
// storage truncated or empty. The query is the one a request of this module really carries.
const char *const kUrlTheClientCannotOpen =
    "gd.test/api/v1/HardWare/rulelist?token=TOK123&grow_director_uid=UID-1";

const char *const kUrlTheClientCanOpen =
    "https://gd.test/api/v1/HardWare/rulelist?token=TOK123&grow_director_uid=UID-1";

void forgetWhatWasPrinted()
{
    Serial.printfCalls = 0;
    Serial.printCalls = 0;
    Serial.printlnCalls = 0;
    Serial.lastText.clear();
}

} // namespace

/**
 * A connection that could not be opened is said, with the endpoint that could not be reached -
 * and with nothing of the query behind it.
 */
void test_a_refused_connection_names_the_endpoint_without_its_query(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    TEST_ASSERT_FALSE(adapter.begin(kUrlTheClientCannotOpen));

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, Serial.printfCalls, "a refused connection said nothing");
    TEST_ASSERT_EQUAL_STRING("HTTP: Failed to begin connection to gd.test/api/v1/HardWare/rulelist",
                             Serial.lastText.c_str());
}

/**
 * Said separately, because this is the claim: whatever that line grows into, the account's token
 * is not in it. The line reaches the port with no debug feature in the way.
 */
void test_the_refused_connection_line_does_not_repeat_the_account_token(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    adapter.begin(kUrlTheClientCannotOpen);

    TEST_ASSERT_TRUE_MESSAGE(Serial.lastText.find("token=") == std::string::npos,
                             Serial.lastText.c_str());
}

/** And a connection that opened says nothing at all: the line is about the failure. */
void test_a_connection_that_opened_is_not_reported(void)
{
    HTTPClient::hostReset();
    HTTPClient::hostRespondWith(200, "");
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    TEST_ASSERT_TRUE(adapter.begin(kUrlTheClientCanOpen));

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, Serial.printfCalls,
                                  "an opened connection wrote a line of its own");
}

extern "C" void run_http_client_adapter_log_tests(void)
{
    printf("\n----- Running HttpClientAdapter log Tests -----\n");

    RUN_TEST(test_a_refused_connection_names_the_endpoint_without_its_query);
    RUN_TEST(test_the_refused_connection_line_does_not_repeat_the_account_token);
    RUN_TEST(test_a_connection_that_opened_is_not_reported);

    printf("\n----- HttpClientAdapter log Tests Complete -----\n");
}
