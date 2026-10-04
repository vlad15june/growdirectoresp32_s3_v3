#include "Core/Domain/Diagnostics/TlsChurnMeter.h"

#include <cstdio>

namespace
{
// The one meter the allocator shim and the TLS client share. A namespace-scope object rather than
// a function-local static: the latter costs a guard variable and a check on every call, and this
// one is first reached long after static construction, when the TLS pool is installed.
TlsChurnMeter theMeter;

const uint32_t kMaxBytes = 0xFFFFFFFFu;
} // namespace

TlsChurnMeter::TlsChurnMeter()
    : _liveBytes(0),
      _peakBytes(0),
      _episodePeakBytes(0),
      _unpairedEvents(0),
      _handshakes(0),
      _episodeOpen(false),
      _episodeStartMs(0)
{
}

void TlsChurnMeter::recordAllocated(size_t bytes)
{
    if (bytes == 0)
    {
        return;
    }

    // size_t is wider than the total on the host; on the device it is the same width. Either way
    // the request cannot add more than the total can hold.
    const uint32_t add = bytes > (size_t)kMaxBytes ? kMaxBytes : (uint32_t)bytes;

    uint32_t expected = _liveBytes.load(std::memory_order_relaxed);
    uint32_t updated = 0;
    bool saturated = false;
    do
    {
        saturated = add > kMaxBytes - expected;
        updated = saturated ? kMaxBytes : expected + add;
    } while (!_liveBytes.compare_exchange_weak(expected, updated, std::memory_order_relaxed));

    if (saturated)
    {
        _unpairedEvents.fetch_add(1, std::memory_order_relaxed);
    }
    notePeak(updated);
}

void TlsChurnMeter::recordFreed(size_t bytes)
{
    if (bytes == 0)
    {
        return;
    }

    const uint32_t subtract = bytes > (size_t)kMaxBytes ? kMaxBytes : (uint32_t)bytes;

    uint32_t expected = _liveBytes.load(std::memory_order_relaxed);
    uint32_t updated = 0;
    bool clamped = false;
    do
    {
        clamped = subtract > expected;
        updated = clamped ? 0 : expected - subtract;
    } while (!_liveBytes.compare_exchange_weak(expected, updated, std::memory_order_relaxed));

    if (clamped)
    {
        _unpairedEvents.fetch_add(1, std::memory_order_relaxed);
    }
}

void TlsChurnMeter::beginHandshake(uint32_t nowMs)
{
    if (_episodeOpen.exchange(true, std::memory_order_relaxed))
    {
        // A window is already open. Keeping it is the safer of the two mistakes: the inner
        // handshake is then reported with the wider window, where starting a new one would
        // report the outer handshake with a window that misses its own beginning.
        _unpairedEvents.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    _episodeStartMs = nowMs;
    _episodePeakBytes.store(_liveBytes.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
}

TlsChurnMeter::Episode TlsChurnMeter::endHandshake(uint32_t nowMs, bool completed)
{
    // Closed before anything is read, so that what the TLS stack does from here on is outside the
    // window for the line and for a later reader of lastEpisodePeakBytes() alike.
    if (!_episodeOpen.exchange(false, std::memory_order_relaxed))
    {
        // Nothing to close. The duration below is then measured from whenever the last window
        // opened, which is not this handshake, so the line is marked the way a clamp marks one.
        _unpairedEvents.fetch_add(1, std::memory_order_relaxed);
    }

    Episode episode;
    episode.index = _handshakes.fetch_add(1, std::memory_order_relaxed) + 1;
    // Unsigned throughout, so the millisecond counter wrapping inside the window costs nothing.
    episode.durationMs = nowMs - _episodeStartMs;
    episode.peakBytes = _episodePeakBytes.load(std::memory_order_relaxed);
    episode.liveBytesAfter = _liveBytes.load(std::memory_order_relaxed);
    episode.globalPeakBytes = _peakBytes.load(std::memory_order_relaxed);
    episode.unpairedEvents = _unpairedEvents.load(std::memory_order_relaxed);
    episode.completed = completed;

    return episode;
}

uint32_t TlsChurnMeter::liveBytes() const
{
    return _liveBytes.load(std::memory_order_relaxed);
}

uint32_t TlsChurnMeter::peakBytes() const
{
    return _peakBytes.load(std::memory_order_relaxed);
}

uint32_t TlsChurnMeter::lastEpisodePeakBytes() const
{
    return _episodePeakBytes.load(std::memory_order_relaxed);
}

uint32_t TlsChurnMeter::handshakeCount() const
{
    return _handshakes.load(std::memory_order_relaxed);
}

uint32_t TlsChurnMeter::unpairedEvents() const
{
    return _unpairedEvents.load(std::memory_order_relaxed);
}

void TlsChurnMeter::notePeak(uint32_t live)
{
    uint32_t peak = _peakBytes.load(std::memory_order_relaxed);
    while (live > peak && !_peakBytes.compare_exchange_weak(peak, live, std::memory_order_relaxed))
    {
    }

    // The window's peak moves only while a window is open. Between windows the value is the answer
    // the last one gave, and it has to stay that: otherwise a reader gets whatever the TLS stack
    // has held since, which is not a handshake's cost and is not what the name promises.
    if (!_episodeOpen.load(std::memory_order_relaxed))
    {
        return;
    }

    uint32_t episodePeak = _episodePeakBytes.load(std::memory_order_relaxed);
    while (live > episodePeak &&
           !_episodePeakBytes.compare_exchange_weak(episodePeak, live, std::memory_order_relaxed))
    {
    }
}

size_t TlsChurnMeter::formatEpisode(const Episode &episode, char *out, size_t outBytes)
{
    // handshake= says what the attempt was worth: a connection, or an attempt that spent the
    // memory and got nothing. It does not distinguish a full handshake from an abbreviated one,
    // because the TLS client of this framework neither saves nor restores a session - every
    // connection it opens is a full handshake, and the value would be a constant.
    const int needed = snprintf(out, outBytes,
                                "TLS_CHURN: handshake=%s n=%lu dur_ms=%lu peak=%lu max=%lu "
                                "live_after=%lu unpaired=%lu",
                                episode.completed ? "full" : "failed",
                                (unsigned long)episode.index,
                                (unsigned long)episode.durationMs,
                                (unsigned long)episode.peakBytes,
                                (unsigned long)episode.globalPeakBytes,
                                (unsigned long)episode.liveBytesAfter,
                                (unsigned long)episode.unpairedEvents);
    return needed < 0 ? 0 : (size_t)needed;
}

TlsChurnMeter &TlsChurnMeter::instance()
{
    return theMeter;
}
