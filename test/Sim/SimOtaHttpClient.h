#pragma once

// A stand-in for the socket of an OTA download, so that the production download session
// (FirmwareRemoteUpdate) can run on the host against every shape of network the field produces:
// a healthy fast stream, a slow but alive one, a stream that freezes with the socket open, and
// a connection that dies outright.
//
// Deliberately a data model, not an algorithm: the stream is described by "how many bytes have
// arrived by time t" - a rate applied to the virtual clock, cut off at the point where the
// stream freezes. The class decides nothing about chunking, budgets or retries; whatever the
// session under test does with a dry socket is the session's behaviour, and that is the thing
// the tests are proving.

#include "Infrastructure/Adapters/Http/IHttpClientAdapter.h"
#include "Sim/VirtualClock.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hostsim {

class SimOtaHttpClient : public IHttpClientAdapter {
public:
    /** @brief One download the server will serve, consumed by GET() in queue order. */
    struct Plan {
        /** What GET() answers. */
        int httpCode = 200;
        /** What getSize() reports after this GET. */
        int contentLength = 0;
        /** How many bytes actually arrive before the stream goes silent forever. */
        size_t deliverableBytes = 0;
        /** How fast the deliverable bytes arrive. */
        uint32_t bytesPerSecond = 0;
        /** Whether the socket stays open once the stream has gone silent. */
        bool connectionStaysOpen = true;
    };

    explicit SimOtaHttpClient(VirtualClock& clock) : _clock(clock) {}

    /** @brief A download that delivers the whole image at the given rate. */
    void queueHealthyDownload(int contentLength, uint32_t bytesPerSecond)
    {
        Plan plan;
        plan.contentLength = contentLength;
        plan.deliverableBytes = (size_t)contentLength;
        plan.bytesPerSecond = bytesPerSecond;
        _plans.push_back(plan);
    }

    /** @brief A download that freezes after freezeAtBytes with the socket still open. */
    void queueFrozenDownload(int contentLength, uint32_t bytesPerSecond, size_t freezeAtBytes)
    {
        Plan plan;
        plan.contentLength = contentLength;
        plan.deliverableBytes = freezeAtBytes;
        plan.bytesPerSecond = bytesPerSecond;
        plan.connectionStaysOpen = true;
        _plans.push_back(plan);
    }

    /** @brief A download whose connection drops after dropAtBytes. */
    void queueDroppedDownload(int contentLength, uint32_t bytesPerSecond, size_t dropAtBytes)
    {
        Plan plan;
        plan.contentLength = contentLength;
        plan.deliverableBytes = dropAtBytes;
        plan.bytesPerSecond = bytesPerSecond;
        plan.connectionStaysOpen = false;
        _plans.push_back(plan);
    }

    void queuePlan(const Plan& plan) { _plans.push_back(plan); }

    // --- What happened -------------------------------------------------------------------------

    std::vector<std::string> requestedUrls;
    int beginCount = 0;
    int endCount = 0;
    uint32_t lastTimeoutMs = 0;
    uint32_t lastConnectTimeoutMs = 0;

    /** @brief Bytes the session under test actually consumed from the active stream. */
    size_t consumedBytes() const { return _consumed; }

    // --- IHttpClientAdapter --------------------------------------------------------------------

    bool begin(const char* url) override
    {
        beginCount++;
        requestedUrls.push_back((url != nullptr) ? url : "");
        return true;
    }

    void addHeader(const char*, const char*) override {}

    void setTimeout(uint32_t timeout) override { lastTimeoutMs = timeout; }

    void setConnectTimeout(uint32_t timeout) override { lastConnectTimeoutMs = timeout; }

    int GET() override
    {
        if (_nextPlan < _plans.size())
        {
            _active = _plans[_nextPlan];
            _nextPlan++;
        }
        else
        {
            _active = Plan(); // no plan queued: the server did not answer
            _active.httpCode = -1;
        }
        _streamStartMs = _clock.nowMs();
        _consumed = 0;
        _streamOpen = true;
        return _active.httpCode;
    }

    int POST(const char*) override { return -1; }

    const char* getString() override { return ""; }

    // The firmware image is read through readBytes()/available() below, not as a body; nothing in
    // the update path asks for a sink, and answering one here would be a second delivery path
    // beside the one under test.
    bool readBodyInto(IHttpBodySink&) override { return false; }

    int getSize() override { return _active.contentLength; }

    WiFiClient* getStreamPtr() override { return nullptr; }

    bool connected() override
    {
        if (!_streamOpen)
        {
            return false;
        }
        if (_consumed < _active.deliverableBytes)
        {
            return true;
        }
        return _active.connectionStaysOpen;
    }

    size_t readBytes(uint8_t* buffer, size_t length) override
    {
        size_t ready = (size_t)available();
        size_t n = (length < ready) ? length : ready;
        for (size_t i = 0; i < n; i++)
        {
            // Arbitrary but deterministic payload; nothing on the host inspects image bytes.
            buffer[i] = (uint8_t)((_consumed + i) & 0xFF);
        }
        _consumed += n;
        return n;
    }

    int available() override
    {
        if (!_streamOpen || _active.bytesPerSecond == 0)
        {
            return 0;
        }
        const uint64_t elapsedMs = _clock.nowMs() - _streamStartMs;
        uint64_t arrived = (uint64_t)_active.bytesPerSecond * elapsedMs / 1000ULL;
        if (arrived > _active.deliverableBytes)
        {
            arrived = _active.deliverableBytes;
        }
        return (int)(arrived - _consumed);
    }

    void end() override
    {
        endCount++;
        _streamOpen = false;
    }

private:
    VirtualClock& _clock;
    std::vector<Plan> _plans;
    size_t _nextPlan = 0;
    Plan _active;
    uint64_t _streamStartMs = 0;
    size_t _consumed = 0;
    bool _streamOpen = false;
};

} // namespace hostsim
