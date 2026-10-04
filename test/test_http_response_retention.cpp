// The answer the module goes on holding after it has been read.
//
// HttpClientAdapter::getString() copies the whole response into a String member and hands out a
// pointer into it. That copy is one heap block the size of the answer, it is taken while the
// framework's own copy of the same answer is still alive, and it stays until the next call or
// until the adapter is destroyed - two adapters live for the lifetime of the module. Nothing
// counts it, nothing prints it, and no column of the diagnostic line shows it, so a heap that
// dropped by kilobytes between two samples cannot be told apart from one that held a large body.
//
// So a body above the threshold is reported when it is retained, with the length that is being
// held. Below the threshold nothing is printed: an answer of a block or two is what ordinary
// traffic looks like, and a line per request would drown the events this exists to show.
//
// The other half of the same story is what happens to that copy afterwards. A String hands its
// block back in one place only - its destructor - so a body that has been read is held until the
// adapter dies, and the buffer becomes the high-water mark of every answer the module has ever
// read. Above the threshold the block is given back when the request ends; below it the buffer
// stays, because an ordinary answer reuses it and costs the heap nothing.
//
// The stand-in is the framework (test/Mock/native/HTTPClient.h) and the class under test is the
// product's own adapter - the arrangement test_http_body_sink.cpp uses for the other half of the
// same class.

#include <unity.h>

#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"
#include "Sim/HostAllocationCounter.h"

#include <cstring>
#include <string>

namespace {

/** A body of exactly the length asked for. */
std::string bodyOf(size_t length)
{
    return std::string(length, 'x');
}

void forgetWhatWasPrinted()
{
    Serial.printlnCalls = 0;
    Serial.printfCalls = 0;
    Serial.printCalls = 0;
    Serial.lastText.clear();
}

/** @brief Read one answer of the given length through a fresh adapter. */
const char* readBodyOfLength(HttpClientAdapter& adapter, size_t length)
{
    HTTPClient::hostRespondWith(200, bodyOf(length));
    TEST_ASSERT_TRUE(adapter.begin("https://api.example.test/v1/rules"));
    TEST_ASSERT_EQUAL_INT(200, adapter.GET());
    return adapter.getString();
}

// The episode being looked for: a body of thousands of bytes, held after the call that read it.
void test_a_large_retained_body_is_reported_with_the_length_that_is_held(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    const char* body = readBodyOfLength(adapter, 8192);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, Serial.printlnCalls,
                                  "an 8 KB body was retained without a line");
    TEST_ASSERT_EQUAL_STRING("HTTP_BODY: retained=8192 n=1", Serial.lastText.c_str());
    // The number in the line is the number of bytes the adapter is still holding, not the number
    // the caller asked for or the one the header announced.
    TEST_ASSERT_EQUAL_UINT32(8192, (uint32_t)strlen(body));

    adapter.end();
    HTTPClient::hostReset();
}

// Ordinary traffic - a status envelope, an acknowledgement, a module record - is what the module
// spends its requests on, and a line for each of those is a log nobody reads.
void test_an_ordinary_answer_is_not_reported(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    readBodyOfLength(adapter, 12);
    readBodyOfLength(adapter, 1460);
    // The threshold itself: a body of exactly two blocks and a bit is still an ordinary answer.
    readBodyOfLength(adapter, 2048);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, Serial.printlnCalls, "an ordinary answer produced a line");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, Serial.printfCalls, "an ordinary answer produced a line");

    adapter.end();
    HTTPClient::hostReset();
}

// Where the threshold is, stated as a boundary rather than as a magnitude.
void test_the_first_byte_above_the_threshold_is_reported(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    readBodyOfLength(adapter, 2049);

    TEST_ASSERT_EQUAL_INT(1, Serial.printlnCalls);
    TEST_ASSERT_EQUAL_STRING("HTTP_BODY: retained=2049 n=1", Serial.lastText.c_str());

    adapter.end();
    HTTPClient::hostReset();
}

// Every retention, not only the first: a body of this size read once an hour and a body of this
// size read every minute are different problems, and the count is what tells them apart when a
// line has been lost.
void test_every_large_body_is_reported_and_counted(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    readBodyOfLength(adapter, 4096);
    TEST_ASSERT_EQUAL_STRING("HTTP_BODY: retained=4096 n=1", Serial.lastText.c_str());

    readBodyOfLength(adapter, 6000);
    TEST_ASSERT_EQUAL_STRING("HTTP_BODY: retained=6000 n=2", Serial.lastText.c_str());

    TEST_ASSERT_EQUAL_INT(2, Serial.printlnCalls);

    adapter.end();
    HTTPClient::hostReset();
}

// --- what is still held once the request is over ----------------------------------------------

/**
 * @brief Bytes the request gives back to the heap when it ends.
 *
 * end() itself allocates nothing on either side of the stand-in, so whatever the balance drops by
 * across this call is what the adapter let go of. Measured as a difference across one call, not as
 * an absolute: the stand-in keeps its own copy of the body for the life of the adapter, and an
 * absolute figure would be mostly that.
 */
long long bytesGivenBackByEnd(HttpClientAdapter& adapter)
{
    const long long before = HostAllocationCounter::liveBytes();
    adapter.end();
    return before - HostAllocationCounter::liveBytes();
}

// The episode this exists for: one large answer, and the kilobytes it took stay taken. The module
// runs on about 63 KB of free heap, so an 8 KB answer read once is an eighth of it, held by an
// adapter that lives as long as the module does.
void test_a_large_answer_is_given_back_when_the_request_ends(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    readBodyOfLength(adapter, 8192);
    // The line proves the adapter took the body; the balance proves what happens to it next.
    TEST_ASSERT_EQUAL_STRING("HTTP_BODY: retained=8192 n=1", Serial.lastText.c_str());

    const long long givenBack = bytesGivenBackByEnd(adapter);

    TEST_ASSERT_TRUE_MESSAGE(givenBack >= 8192,
                             "the block an 8 KB answer took is still held after the request ended");

    HTTPClient::hostReset();
}

// The boundary, from the side that has to give the block back.
void test_the_first_byte_above_the_threshold_is_given_back(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    readBodyOfLength(adapter, 2049);

    TEST_ASSERT_TRUE_MESSAGE(bytesGivenBackByEnd(adapter) >= 2049,
                             "an answer above the threshold was not given back");

    HTTPClient::hostReset();
}

// The optimization that must survive: an ordinary answer keeps its buffer, so the next answer of
// that size is served out of it and the heap is not asked for anything. Giving every buffer back
// would turn each of the module's requests into an allocation and a free.
void test_an_ordinary_answer_keeps_its_buffer_for_the_next_request(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    readBodyOfLength(adapter, 1000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bytesGivenBackByEnd(adapter),
                                  "an ordinary answer had its buffer taken away");

    // The threshold itself is an ordinary answer, the same way it is for the line.
    readBodyOfLength(adapter, 2048);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bytesGivenBackByEnd(adapter),
                                  "an answer of exactly the threshold had its buffer taken away");

    HTTPClient::hostReset();
}

// Two ordinary requests in a row cost the same: nothing is added to the path an ordinary answer
// takes. What the buffer itself does between them - reused, not reallocated - is a property of the
// core's String and is shown against the core's own sources, not here.
void test_two_ordinary_answers_in_a_row_cost_the_same(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    // Steady state first: the stand-in's own buffers grow on the first requests and then stop.
    readBodyOfLength(adapter, 1000);
    adapter.end();
    readBodyOfLength(adapter, 1000);
    adapter.end();

    HostAllocationCounter::reset();
    readBodyOfLength(adapter, 1000);
    adapter.end();
    const unsigned long long third = HostAllocationCounter::newCount();

    HostAllocationCounter::reset();
    readBodyOfLength(adapter, 1000);
    adapter.end();
    const unsigned long long fourth = HostAllocationCounter::newCount();

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(third, fourth,
                                     "an ordinary request stopped costing what the one before it did");

    HTTPClient::hostReset();
}

// The whole sequence the module lives through: a large answer, the request that read it ending,
// and then ordinary traffic again - which must not inherit the large block.
void test_the_answer_after_a_large_one_starts_from_a_buffer_of_its_own(void)
{
    HTTPClient::hostReset();
    HttpClientAdapter adapter;
    forgetWhatWasPrinted();

    readBodyOfLength(adapter, 8192);
    TEST_ASSERT_TRUE_MESSAGE(bytesGivenBackByEnd(adapter) >= 8192,
                             "the large answer's block was not given back");

    readBodyOfLength(adapter, 1000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bytesGivenBackByEnd(adapter),
                                  "the answer after a large one did not keep its own buffer");

    HTTPClient::hostReset();
}

} // namespace

extern "C" void run_http_response_retention_tests(void)
{
    RUN_TEST(test_a_large_retained_body_is_reported_with_the_length_that_is_held);
    RUN_TEST(test_an_ordinary_answer_is_not_reported);
    RUN_TEST(test_the_first_byte_above_the_threshold_is_reported);
    RUN_TEST(test_every_large_body_is_reported_and_counted);
    RUN_TEST(test_a_large_answer_is_given_back_when_the_request_ends);
    RUN_TEST(test_the_first_byte_above_the_threshold_is_given_back);
    RUN_TEST(test_an_ordinary_answer_keeps_its_buffer_for_the_next_request);
    RUN_TEST(test_two_ordinary_answers_in_a_row_cost_the_same);
    RUN_TEST(test_the_answer_after_a_large_one_starts_from_a_buffer_of_its_own);
}
