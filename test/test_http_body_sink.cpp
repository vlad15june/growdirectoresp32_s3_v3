// The device's own body reader, executed rather than compiled.
//
// HttpClientAdapter is the one HTTP class the host build could not reach: it names <HTTPClient.h>,
// so every host test of the transport runs against Sim/SimHttpClient.h instead - a second
// implementation of the same contract. That is fine for the transport above it and useless for the
// adapter itself, and the adapter is where the body is now read: readBodyInto() drives
// HTTPClient::writeToStream() through a private Stream (SinkStream) that forwards every block to
// the sink.
//
// So the stand-in here is the framework, not the adapter: test/Mock/native/HTTPClient.h reproduces
// what writeToStream() does to a stream - blocks of at most 1460 bytes, one retry after a short
// write, and no stream at all when the connection is closed - and the class under test is the
// product's own. The one claim that needs it: a sink that refuses must stay refused. The core
// answers a short write by clearing the error and writing the remainder, so a stream that refused
// once and accepted on the retry would hand the sink a body with a hole in it, and nothing above
// would know.

#include <unity.h>

#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

/** @brief A sink that records what it was given, and can refuse from a chosen block onwards. */
class RecordingSink : public IHttpBodySink {
public:
    std::string received;
    size_t calls = 0;
    /** Refuse the call with this index and every call after it; SIZE_MAX accepts everything. */
    size_t refuseFrom = static_cast<size_t>(-1);

    bool write(const char* data, size_t length) override
    {
        calls++;
        if (calls > refuseFrom)
        {
            return false;
        }
        received.append(data, length);
        return true;
    }
};

/** A body long enough to arrive in more than one block: three of them plus a remainder. */
std::string longBody()
{
    std::string body = "[";
    for (int i = 0; i < 40; i++)
    {
        if (i > 0)
        {
            body += ",";
        }
        body += "{\"uid\":\"type-";
        body += std::to_string(i);
        body += "\",\"name\":\"sensor_model_";
        body += std::to_string(i);
        body += "\",\"filler\":\"";
        body.append(100, 'x');
        body += "\"}";
    }
    body += "]";
    return body;
}

} // namespace

/**
 * The whole body reaches the sink, and it reaches it in blocks: what the adapter must never do is
 * ask for the answer in one piece, which is the whole point of the method.
 */
static void test_the_adapter_hands_the_whole_body_over_in_blocks(void)
{
    const std::string body = longBody();
    HTTPClient::hostReset();
    HTTPClient::hostRespondWith(200, body);

    HttpClientAdapter adapter;
    adapter.begin("http://gd.test/api/v1/Hardware/sensor_models");
    TEST_ASSERT_EQUAL_INT(200, adapter.GET());

    RecordingSink sink;
    const bool read = adapter.readBodyInto(sink);

    TEST_ASSERT_TRUE_MESSAGE(read, "the adapter did not report the body as read");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(body.size(), sink.received.size(),
                                     "the sink was not given the whole body");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(body.c_str(), sink.received.c_str(),
                                     "the body the sink got is not the body the server sent");

    char message[192];
    snprintf(message, sizeof(message), "the body of %u bytes reached the sink in %u call(s)",
             (unsigned)body.size(), (unsigned)sink.calls);
    TEST_ASSERT_TRUE_MESSAGE(sink.calls > 1, message);

    const std::vector<size_t>& blocks = HTTPClient::hostLast->hostWrittenBlocks();
    for (size_t i = 0; i < blocks.size(); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(blocks[i] <= HTTPClient::HTTP_TCP_BUFFER_SIZE,
                                 "a block larger than the transport's own buffer was handed over");
    }
}

/**
 * A sink that refuses ends the transfer, and the adapter says so. This is what a body already known
 * to be unusable costs: the rest of it is not read.
 */
static void test_a_sink_that_refuses_ends_the_transfer(void)
{
    const std::string body = longBody();
    HTTPClient::hostReset();
    HTTPClient::hostRespondWith(200, body);

    HttpClientAdapter adapter;
    adapter.begin("http://gd.test/api/v1/Hardware/sensor_models");
    adapter.GET();

    RecordingSink sink;
    sink.refuseFrom = 1; // the first block is taken, everything after it is refused

    const bool read = adapter.readBodyInto(sink);

    TEST_ASSERT_FALSE_MESSAGE(read, "a refused body was reported as read");
    TEST_ASSERT_TRUE_MESSAGE(sink.received.size() < body.size(),
                             "the transfer went on after the sink refused");
}

/**
 * And it stays refused when the transport retries. The core answers a short write by clearing the
 * error, waiting a millisecond and writing the remainder of the same block - so a stream that
 * accepted on the retry would give the sink the same bytes twice, with a hole where the refusal
 * was.
 */
static void test_a_refusal_is_not_undone_by_the_transports_retry(void)
{
    const std::string body = longBody();
    HTTPClient::hostReset();
    HTTPClient::hostRespondWith(200, body);

    HttpClientAdapter adapter;
    adapter.begin("http://gd.test/api/v1/Hardware/sensor_models");
    adapter.GET();

    RecordingSink sink;
    sink.refuseFrom = 0; // refuse from the very first block

    const size_t delaysBefore = hostDelayTotalMs;
    const bool read = adapter.readBodyInto(sink);

    TEST_ASSERT_FALSE_MESSAGE(read, "a body the sink refused outright was reported as read");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, sink.received.size(),
                                     "bytes reached a sink that refused them");
    TEST_ASSERT_TRUE_MESSAGE(hostDelayTotalMs > delaysBefore,
                             "the transport's retry after a short write was not exercised - this "
                             "test is not proving what it says it does");
    // Once, and not again: the retry is answered by the stream itself, so a sink that has said no
    // is neither asked twice nor handed the bytes it refused.
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, sink.calls,
                                     "the sink was asked again after refusing, which is how the "
                                     "same bytes end up delivered twice");
}

/** A connection the server has already closed: nothing is read and nothing is claimed. */
static void test_a_closed_connection_reads_no_body(void)
{
    HTTPClient::hostReset();
    HTTPClient::hostRespondWith(200, longBody());

    HttpClientAdapter adapter;
    adapter.begin("http://gd.test/api/v1/Hardware/sensor_models");
    adapter.GET();
    HTTPClient::hostLast->hostDropConnection();

    RecordingSink sink;
    const bool read = adapter.readBodyInto(sink);

    TEST_ASSERT_FALSE_MESSAGE(read, "a body was reported as read off a closed connection");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, sink.calls, "the sink was called on a closed connection");
}

extern "C" int run_http_body_sink_tests(void)
{
    RUN_TEST(test_the_adapter_hands_the_whole_body_over_in_blocks);
    RUN_TEST(test_a_sink_that_refuses_ends_the_transfer);
    RUN_TEST(test_a_refusal_is_not_undone_by_the_transports_retry);
    RUN_TEST(test_a_closed_connection_reads_no_body);
    return 0;
}
