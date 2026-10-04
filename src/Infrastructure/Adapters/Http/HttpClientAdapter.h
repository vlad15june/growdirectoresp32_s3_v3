#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <utility>
#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Infrastructure/Adapters/Http/LoggableUrl.h"
#include "Infrastructure/Adapters/Http/MeteredTlsClient.h"
#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Core/Domain/Interfaces/ILogger.h"

/**
 * @brief Implementation of IHttpClientAdapter using ESP32's HTTPClient
 */
class HttpClientAdapter : public IHttpClientAdapter {
private:
    /**
     * @brief The Stream HTTPClient writes the body into, forwarding every piece to a sink.
     *
     * Holds nothing: what the pieces are worth is the sink's business. The read half of Stream is
     * never used - writeToStream() only writes - so it answers "nothing to read".
     */
    class SinkStream : public Stream {
    public:
        explicit SinkStream(IHttpBodySink& sink) : _sink(sink), _stopped(false) {}

        size_t write(uint8_t byte) override {
            const char c = static_cast<char>(byte);
            return forward(&c, 1);
        }

        size_t write(const uint8_t* buffer, size_t size) override {
            return forward(reinterpret_cast<const char*>(buffer), size);
        }

        int available() override { return 0; }
        int read() override { return -1; }
        int peek() override { return -1; }
        void flush() override {}

        /** @brief True when the sink asked for the rest of the body not to be read. */
        bool stopped() const { return _stopped; }

    private:
        // A sink that refuses answers "nothing written", which HTTPClient reports as a stream
        // write error and stops the transfer - the outcome asked for.
        size_t forward(const char* data, size_t size) {
            if (_stopped) {
                return 0;
            }
            if (!_sink.write(data, size)) {
                _stopped = true;
                return 0;
            }
            return size;
        }

        IHttpBodySink& _sink;
        bool _stopped;
    };

    /**
     * @brief The length above which an answer is large, and why the line is drawn here.
     *
     * HTTPClient reads the socket in blocks of 1460 bytes, so an answer of a block or two is what
     * ordinary traffic looks like - a status envelope, an acknowledgement, a module record - and
     * reporting those would be a line per request rather than an event. Above that the answer is
     * a list of something, and it is worth a line: the module runs with about 63 KB of free heap,
     * so a block of this size is already a few percent of it, and it is taken while the
     * framework's own copy of the same answer is still alive.
     *
     * The same length decides what is given back when the request ends: worth a line to read
     * about and worth handing to the heap are the same size of answer, and one number keeps the
     * log and the behaviour describing each other.
     */
    static const unsigned int kLargeBodyBytes = 2048;

    /** @brief Size of the buffer the line is built in, terminator included. */
    static const size_t kBodyLineBytes = 64;

    HTTPClient _client;
    // The connection HTTPClient opens for an https URL, and the one place a handshake happens on
    // this path: the metered flavour is what makes each one show up in the log.
    MeteredTlsClient _secureClient;
    bool _isConnected;
    String _responseBuffer; // Internal buffer to store response
    // Bodies reported since boot, so that a gap in a log reads as a lost line and not as a quiet
    // period.
    uint32_t _largeBodiesRetained;
    // Where each request's ending - with the server's status or without - is recorded; optional.
    ModuleStatusBoard *_statusBoard;

    int noteOutcome(int responseCode) {
        if (_statusBoard != nullptr) {
            _statusBoard->noteRequestOutcome((uint32_t)millis(), responseCode);
        }
        return responseCode;
    }

    /**
     * @brief Hand a large answer's block back to the heap now that the request is over.
     *
     * String gives its block back in one place only - its destructor. Assignment does not:
     * copy() asks reserve(), which returns at once while the buffer is big enough, and move()
     * copies into the buffer the destination already has, so `= ""`, `= String()`, clear() and
     * reserve(0) all leave the block exactly where it was. Without this the buffer would be the
     * high-water mark of every answer the module has ever read: one list of rules from a large
     * account, and its kilobytes are held until the module reboots. Moving the buffer into a
     * local hands the block to an object that dies at the closing brace, and leaves the member
     * with no buffer at all, so the next answer starts from its own size again.
     *
     * Ordinary answers keep their buffer on purpose. They are the same handful of sizes request
     * after request, they fit the buffer that is already there, and giving it back would turn
     * every one of the module's requests into an allocation and a free for nothing.
     */
    void releaseLargeResponseBuffer() {
        if (_responseBuffer.length() <= kLargeBodyBytes) {
            return;
        }
        const String released(std::move(_responseBuffer));
    }

public:
    /** Seconds a TLS handshake may last before the connect gives up on it. */
    static const unsigned long kHandshakeTimeoutS = 20;

    /**
     * @brief Construct a new Http Client Adapter
     *
     * The handshake is bounded at 20 s instead of WiFiClientSecure's 120 s: a full handshake takes
     * about a second, and a server that does not finish one in twenty is not going to - while every
     * other request of the network task waits behind it. The bound is set once and stays across
     * connections (stop_ssl_socket() keeps it).
     */
    HttpClientAdapter()
        : _isConnected(false),
          _largeBodiesRetained(0),
          _statusBoard(nullptr) {
        _secureClient.setInsecure();
        _secureClient.setHandshakeTimeout(kHandshakeTimeoutS);
        _client.setReuse(true);
    }
    
    /**
     * @brief Record how every request and every TLS connect of this client ends, for the link's
     *        supervision: whether the server answered with a status, and whether a handshake failed
     *        for memory.
     */
    void setStatusBoard(ModuleStatusBoard *statusBoard) {
        _statusBoard = statusBoard;
        _secureClient.setStatusBoard(statusBoard);
    }

    /**
     * @brief Destroy the Http Client Adapter
     */
    ~HttpClientAdapter() {
        if (_isConnected) {
            _client.end();
        }
    }
    
    /**
     * @brief Begin the HTTP connection
     * 
     * @param url Target URL for the HTTP request
     * @return true if successful
     */
    bool begin(const char* url) override {
        if (_isConnected) {
            _client.end();
            _isConnected = false;
        }

        String urlStr(url);
        bool success = urlStr.startsWith("https")
            ? _client.begin(_secureClient, urlStr)
            : _client.begin(urlStr);
        _isConnected = success;

        if (!success) {
            // This line goes to the port whatever the log settings are, and the URL it is given is
            // the one the transport built: the endpoint that could not be opened is what the line
            // is for, the query behind it is the account's token (see LoggableUrl.h).
            Serial.printf("HTTP: Failed to begin connection to %.*s",
                          HttpLog::loggablePathLength(url), url);
        }

        return success;
    }
    
    /**
     * @brief Set a request header
     * 
     * @param headerName Name of the header
     * @param headerValue Value of the header
     */
    void addHeader(const char* headerName, const char* headerValue) override {
        // We need to convert to String for underlying HTTPClient
        _client.addHeader(String(headerName), String(headerValue));
    }
    
    /**
     * @brief Set request timeout
     * 
     * @param timeout Timeout in milliseconds
     */
    void setTimeout(uint32_t timeout) override {
        _client.setTimeout(timeout);
    }

    /**
     * @brief Set the budget for opening the connection (TCP connect plus TLS handshake)
     *
     * @param timeout Timeout in milliseconds
     */
    void setConnectTimeout(uint32_t timeout) override {
        _client.setConnectTimeout((int32_t)timeout);
    }
    
    /**
     * @brief Make a GET request
     * 
     * @return HTTP response code
     */
    int GET() override {
        int responseCode = _client.GET();
        return noteOutcome(responseCode);
    }
    
    /**
     * @brief Make a POST request with payload
     * 
     * @param payload Request body
     * @return HTTP response code
     */
    int POST(const char* payload) override {
        // We need to convert to String for underlying HTTPClient
        int responseCode = _client.POST(String(payload));
        return noteOutcome(responseCode);
    }
    
    /**
     * @brief Get the response as a string
     *
     * The answer is kept in a member, so the pointer stays good until the next call replaces the
     * answer or end() closes the request - whichever comes first. A body large enough to matter to
     * the heap says so on the way in: nothing else counts, prints or samples it, so a heap that
     * dropped by kilobytes between two samples cannot otherwise be told from one that held a large
     * answer.
     *
     * @return Response as a C-string
     */
    const char* getString() override {
        _responseBuffer = _client.getString();

        const unsigned int retained = _responseBuffer.length();
        if (retained > kLargeBodyBytes) {
            _largeBodiesRetained++;
            char line[kBodyLineBytes];
            snprintf(line, sizeof(line), "HTTP_BODY: retained=%lu n=%lu",
                     (unsigned long)retained, (unsigned long)_largeBodiesRetained);
            Serial.println(line);
        }

        return _responseBuffer.c_str();
    }
    
    /**
     * @brief Hand the response body to a sink in the pieces it arrives in
     *
     * Driven by HTTPClient::writeToStream(), which is what getString() itself calls: the
     * content-length and chunked-transfer handling stays the framework's, and reading the socket
     * directly here would be a second copy of it that mistakes chunk headers for body. What
     * changes is where the pieces go - into the sink, ~1.4 KB at a time, instead of into one
     * String the size of the whole answer.
     */
    bool readBodyInto(IHttpBodySink& sink) override {
        SinkStream stream(sink);
        const int written = _client.writeToStream(&stream);
        return written >= 0 && !stream.stopped();
    }

    /**
     * @brief End the HTTP connection and free resources
     *
     * Including the body read from it: the caller has its own copy by now, and the heap gets the
     * block back before the caller starts parsing that copy - the moment the module holds the
     * most.
     */
    void end() override {
        _client.end();
        _isConnected = false;
        releaseLargeResponseBuffer();
    }
    
    /**
     * @brief Get the content length of the response
     * 
     * @return Content length in bytes, -1 if unknown
     */
    int getSize() override {
        return _client.getSize();
    }
    
    /**
     * @brief Get a pointer to the WiFi client stream for reading data
     * 
     * @return Pointer to WiFiClient for streaming operations
     */
    WiFiClient* getStreamPtr() override {
        return _client.getStreamPtr();
    }
    
    /**
     * @brief Check if the connection is still active
     * 
     * @return true if connected
     */
    bool connected() override {
        return _client.connected();
    }
    
    /**
     * @brief Read data from the stream
     * 
     * @param buffer Buffer to store the data
     * @param length Maximum number of bytes to read
     * @return Number of bytes actually read
     */
    size_t readBytes(uint8_t* buffer, size_t length) override {
        WiFiClient* stream = _client.getStreamPtr();
        if (stream != nullptr) {
            return stream->readBytes(buffer, length);
        }
        return 0;
    }
    
    /**
     * @brief Get the number of bytes available for reading
     * 
     * @return Number of available bytes
     */
    int available() override {
        WiFiClient* stream = _client.getStreamPtr();
        if (stream != nullptr) {
            return stream->available();
        }
        return 0;
    }
};
