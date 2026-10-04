#pragma once

// Host stand-in for the ESP32 core's HTTPClient, written to drive the production code above it the
// way the real one does.
//
// It exists for one class: HttpClientAdapter, and inside it SinkStream, which only the device
// builds because the adapter names <HTTPClient.h>. Everything else in the HTTP path is executed on
// the host already (RestAPIService against Sim/SimHttpClient.h); this is the last piece, and the
// contract it has to meet is not a formality:
//
//   * writeToStream() hands the body over in blocks of at most HTTP_TCP_BUFFER_SIZE, so a caller
//     that only works when the body arrives whole must fail here;
//   * a short write is not an error yet. The core logs it, clears the stream's write error, waits
//     a millisecond and writes the remainder; only a second short write returns
//     HTTPC_ERROR_STREAM_WRITE. A sink that refuses once and then accepts would therefore be fed
//     the same bytes twice - which is why SinkStream, once refused, refuses for good;
//   * writeToStream() on a connection that is not open returns HTTPC_ERROR_NOT_CONNECTED and
//     touches the stream not at all;
//   * getString() answers the same body, though not through writeToStream() as the core's does -
//     nothing here tests getString(), and reproducing its StreamString would be reproducing the
//     allocation this work exists to remove.
//
// Those four are transcribed from HTTPClient::writeToStream / writeToStreamDataBlock in the pinned
// framework (packages/framework-arduinoespressif32, libraries/HTTPClient). This is a stand-in for
// the far end of the connection, not a second implementation of anything the tests measure.

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTPC_ERROR_SEND_HEADER_FAILED (-2)
#define HTTPC_ERROR_SEND_PAYLOAD_FAILED (-3)
#define HTTPC_ERROR_NOT_CONNECTED (-4)
#define HTTPC_ERROR_CONNECTION_LOST (-5)
#define HTTPC_ERROR_NO_STREAM (-6)
#define HTTPC_ERROR_STREAM_WRITE (-11)

class HTTPClient
{
public:
    /** The block size the core reads a socket in, and hands to the stream. */
    static const size_t HTTP_TCP_BUFFER_SIZE = 1460;

    // --- what a test sets up -----------------------------------------------------------------
    //
    // Through statics, and deliberately: the class under test owns its HTTPClient as a member and
    // hands out no handle to it, which is exactly the shape that puts it out of reach of a host test.
    // A stand-in that needed a new accessor in production code to be reachable would have changed
    // the thing it is standing in for.

    /** @brief The answer every connection opened from now on gets. */
    static inline int hostNextStatus = -1;
    static inline std::string hostNextBody;
    /** @brief The client that opened the last connection, for a test that wants to look at it. */
    static inline HTTPClient *hostLast = nullptr;

    static void hostRespondWith(int statusCode, const std::string &body)
    {
        hostNextStatus = statusCode;
        hostNextBody = body;
    }

    static void hostReset()
    {
        hostNextStatus = -1;
        hostNextBody.clear();
        hostLast = nullptr;
    }

    /** @brief Answer as a server that closed the connection before the body was read. */
    void hostDropConnection() { _connected = false; }

    /** @brief Blocks the body was handed over in, in order. */
    const std::vector<size_t> &hostWrittenBlocks() const { return _blocks; }

    const std::string &hostLastUrl() const { return _url; }

    // --- the subset of HTTPClient the adapter uses -------------------------------------------

    void setReuse(bool reuse) { _reuse = reuse; }
    void setTimeout(uint16_t timeout) { _timeout = timeout; }
    void setConnectTimeout(int32_t timeout) { _connectTimeout = timeout; }

    bool begin(const String &url)
    {
        // The core reads the protocol before it opens anything and refuses what it cannot read: a
        // colon is required, and only http and https pass (HTTPClient::begin / beginInternal in
        // the pinned framework). A refused URL opens no connection and answers false.
        const std::string text(url.c_str());
        const size_t colon = text.find(':');
        const std::string protocol = colon == std::string::npos ? std::string() : text.substr(0, colon);
        if (protocol != "http" && protocol != "https")
        {
            return false;
        }

        _url = url.c_str();
        _statusCode = hostNextStatus;
        _body = hostNextBody;
        _connected = true;
        _blocks.clear();
        hostLast = this;
        return true;
    }

    bool begin(WiFiClientSecure &client, const String &url)
    {
        _secure = &client;
        return begin(url);
    }

    void addHeader(const String &name, const String &value)
    {
        _headers.push_back(std::string(name.c_str()) + ": " + value.c_str());
    }

    int GET() { return _statusCode; }

    int POST(const String &payload)
    {
        _lastPayload = payload.c_str();
        return _statusCode;
    }

    String getString()
    {
        // The core builds one String the size of the whole body here (StreamString with a
        // reserve), which is exactly the allocation the streaming reader exists to avoid.
        return String(_body);
    }

    int getSize() const { return static_cast<int>(_body.size()); }

    bool connected() const { return _connected; }

    WiFiClient *getStreamPtr() { return _secure; }

    void end()
    {
        _connected = false;
        _endCalls++;
    }

    int hostEndCalls() const { return _endCalls; }

    /**
     * @brief Hand the body to a stream in blocks, the way the core does.
     *
     * Short writes are retried once - see the file comment - and the transfer ends in
     * HTTPC_ERROR_STREAM_WRITE if the stream refuses the remainder too.
     */
    int writeToStream(Stream *stream)
    {
        if (stream == nullptr)
        {
            return HTTPC_ERROR_NO_STREAM;
        }
        if (!_connected)
        {
            return HTTPC_ERROR_NOT_CONNECTED;
        }

        int bytesWritten = 0;
        size_t offset = 0;
        while (offset < _body.size())
        {
            size_t block = _body.size() - offset;
            if (block > HTTP_TCP_BUFFER_SIZE)
            {
                block = HTTP_TCP_BUFFER_SIZE;
            }
            _blocks.push_back(block);

            const uint8_t *from = reinterpret_cast<const uint8_t *>(_body.data()) + offset;
            size_t written = stream->write(from, block);
            bytesWritten += static_cast<int>(written);

            if (written != block)
            {
                if (stream->getWriteError())
                {
                    stream->clearWriteError();
                }
                delay(1);

                const size_t left = block - written;
                const size_t again = stream->write(from + written, left);
                bytesWritten += static_cast<int>(again);
                if (again != left)
                {
                    _connected = false;
                    return HTTPC_ERROR_STREAM_WRITE;
                }
            }

            offset += block;
        }

        // The core disconnects here rather than in end(); a caller may still call end() after.
        _connected = false;
        return bytesWritten;
    }

private:
    std::string _url;
    std::string _body;
    std::string _lastPayload;
    std::vector<std::string> _headers;
    std::vector<size_t> _blocks;
    WiFiClientSecure *_secure = nullptr;
    int _statusCode = -1;
    bool _connected = false;
    bool _reuse = false;
    uint16_t _timeout = 0;
    int32_t _connectTimeout = 0;
    int _endCalls = 0;
};
