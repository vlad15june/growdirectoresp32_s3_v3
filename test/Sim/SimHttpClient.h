#pragma once

// A stand-in for the socket, so that the production HTTP transport can run on the host.
//
// RestAPIService is handed its HttpClientAdapter, which is <HTTPClient.h> plus a TLS
// stack and has no host build. The adapter is injected, so what has to be replaced here is the
// connection - not the transport.
//
// Deliberately dumb, in the same spirit as Mock/native/HardwareSerial.h: it records what was
// asked of it and answers what the test told it to answer. It parses no URL, builds no JSON,
// knows no endpoint. Any of that would be a second copy of the thing under test, and then the
// test would be proving the copy.
//
// It also takes no time. The waits inside the transport are free delay() calls, which the host
// Arduino.h records in hostDelayTotalMs; a scenario that needs the network to *block a task*
// uses Sim/SimNetwork.h with the scheduler instead.

#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hostsim {

class SimHttpClient : public IHttpClientAdapter {
public:
    /** @brief One request the transport actually put on the wire. */
    struct Request {
        std::string method;   // "GET" or "POST"
        std::string url;      // exactly the string the transport passed to begin()
        std::string body;     // POST payload, empty for GET
        uint32_t timeoutMs = 0;
        std::vector<std::pair<std::string, std::string> > headers;
    };

    /**
     * @brief Queue one answer.
     *
     * Answers are consumed in the order they were queued. A code of 0 or less is what the device
     * sees when the server did not answer at all (HTTPClient returns a negative error code);
     * anything above 0 is an HTTP status.
     */
    void queueResponse(int code, const char* body = "")
    {
        Response response;
        response.code = code;
        response.body = (body != nullptr) ? body : "";
        _queued.push_back(response);
    }

    /** @brief What to answer once the queue has run out. Defaults to "no answer at all". */
    void alwaysRespond(int code, const char* body = "")
    {
        _fallback.code = code;
        _fallback.body = (body != nullptr) ? body : "";
    }

    /** @brief Every request the transport made, in order. */
    const std::vector<Request>& requests() const { return _requests; }

    size_t requestCount() const { return _requests.size(); }

    const Request& lastRequest() const { return _requests.back(); }

    int beginCount() const { return _beginCount; }
    int endCount() const { return _endCount; }

    void reset()
    {
        _requests.clear();
        _queued.clear();
        _consumed = 0;
        _beginCount = 0;
        _endCount = 0;
        _pendingHeaders.clear();
        _pendingUrl.clear();
        _lastQueued = -1;
        _fallback = Response();
    }

    // --- IHttpClientAdapter ---------------------------------------------------------------

    bool begin(const char* url) override
    {
        _beginCount++;
        _pendingUrl = (url != nullptr) ? url : "";
        _pendingHeaders.clear();
        return true;
    }

    void addHeader(const char* headerName, const char* headerValue) override
    {
        _pendingHeaders.push_back(std::make_pair(
            std::string((headerName != nullptr) ? headerName : ""),
            std::string((headerValue != nullptr) ? headerValue : "")));
    }

    void setTimeout(uint32_t timeout) override { _timeoutMs = timeout; }

    void setConnectTimeout(uint32_t timeout) override { _connectTimeoutMs = timeout; }

    int GET() override { return record("GET", ""); }

    int POST(const char* payload) override
    {
        return record("POST", (payload != nullptr) ? payload : "");
    }

    // Answers the body of the request just made. The real adapter empties its buffer on end(),
    // and sendModuleCommandExecutionResult() calls end() before getString(); that difference is
    // left alone here on purpose - modelling it would turn this stand-in into a claim about
    // HTTPClient's buffer lifetime, which is not what these tests are about.
    const char* getString() override { return lastBody().c_str(); }

    // The body in pieces, the way a socket delivers one. The piece size is small and adjustable on
    // purpose: a body cut at every position a chunk boundary can fall on is what the reader on the
    // other side has to survive, and a stand-in that handed the whole body over in one call would
    // never exercise that.
    bool readBodyInto(IHttpBodySink& sink) override
    {
        const std::string& body = lastBody();
        size_t offset = 0;
        while (offset < body.size())
        {
            size_t piece = _bodyPieceBytes;
            if (piece == 0 || piece > body.size() - offset)
            {
                piece = body.size() - offset;
            }
            if (!sink.write(body.data() + offset, piece))
            {
                return false;
            }
            offset += piece;
        }
        return true;
    }

    /** @brief How large the pieces readBodyInto() delivers are; 0 means the whole body at once. */
    void setBodyPieceBytes(size_t bytes) { _bodyPieceBytes = bytes; }

    int getSize() override { return static_cast<int>(lastBody().size()); }

    WiFiClient* getStreamPtr() override { return nullptr; }

    bool connected() override { return false; }

    size_t readBytes(uint8_t*, size_t) override { return 0; }

    int available() override { return 0; }

    void end() override { _endCount++; }

private:
    struct Response {
        int code = -1;
        std::string body;
    };

    int record(const char* method, const std::string& body)
    {
        Request request;
        request.method = method;
        request.url = _pendingUrl;
        request.body = body;
        request.timeoutMs = _timeoutMs;
        request.headers = _pendingHeaders;
        _requests.push_back(request);

        // The answer is pointed at, never copied. A stand-in that copied it would allocate the
        // size of the body inside the call it is standing in for, and a test measuring what
        // reading a body costs would be measuring this class.
        if (_consumed < _queued.size())
        {
            _lastQueued = (int)_consumed;
            _consumed++;
        }
        else
        {
            _lastQueued = -1;
        }
        return lastResponse().code;
    }

    const Response& lastResponse() const
    {
        return (_lastQueued >= 0) ? _queued[(size_t)_lastQueued] : _fallback;
    }

    const std::string& lastBody() const { return lastResponse().body; }

    std::vector<Request> _requests;
    std::vector<Response> _queued;
    size_t _consumed = 0;
    Response _fallback;

    std::string _pendingUrl;
    std::vector<std::pair<std::string, std::string> > _pendingHeaders;
    // Which queued answer the last request got, or -1 for the fallback.
    int _lastQueued = -1;
    uint32_t _timeoutMs = 0;
    uint32_t _connectTimeoutMs = 0;
    size_t _bodyPieceBytes = 64;
    int _beginCount = 0;
    int _endCount = 0;
};

} // namespace hostsim
