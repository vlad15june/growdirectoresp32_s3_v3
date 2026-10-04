#ifndef TLS_CHURN_METER_H
#define TLS_CHURN_METER_H

#include <atomic>
#include <cstddef>
#include <cstdint>

/**
 * @brief What a TLS handshake costs, measured where the TLS stack asks for the memory.
 *
 * A handshake is the largest transient the module makes: the certificate chain, the key exchange
 * and the scratch around them are allocated, used and released within a second or two, and
 * nothing that samples the heap on a schedule is likely to see the bottom of it. What is left
 * afterwards - the session, the transform, the peer certificate - stays allocated for as long as
 * the connection lives, and is released in one go when the peer closes it. Both numbers matter
 * and neither is visible from outside the allocator.
 *
 * So the accounting sits in the allocator the TLS stack was given: every request it serves from
 * the heap is added, every release is subtracted, and three totals come out of it - what is held
 * right now, the worst total since boot, and the worst total inside the current window. A window
 * is opened when a handshake starts and closed when it ends, which turns the third number into
 * "what this handshake cost".
 *
 * The class holds no platform header on purpose: what it counts is handed to it. The allocator
 * shim and the TLS client that opens and closes the windows are the platform parts, and they are
 * thin enough to read in one screen.
 *
 * Bytes are counted as 32-bit totals. Live bytes are bounded by the heap - hundreds of kilobytes
 * on this chip - so the width is ample, and the arithmetic saturates rather than wraps: a total
 * that wrapped would report a peak of nearly four gigabytes or of a few hundred bytes, and both
 * read as a plausible number. The one case that really happens is a release with no matching
 * request behind it - the TLS stack allocates through the C library until the shim is installed,
 * and frees those blocks through the shim afterwards. Such an event is clamped and counted, so a
 * line whose numbers cannot be trusted says so.
 *
 * Every total is atomic: allocations arrive on whichever task is doing TLS.
 */
class TlsChurnMeter
{
public:
    /** @brief One handshake, as the meter saw it. */
    struct Episode
    {
        /** 1-based ordinal since boot, so a gap in a log is a lost line and not a quiet run. */
        uint32_t index;
        /** Wall time from the start of the connect attempt to its outcome. */
        uint32_t durationMs;
        /** Largest total held at any moment inside the window. */
        uint32_t peakBytes;
        /** Still held when the window closed - the resident cost of the connection. */
        uint32_t liveBytesAfter;
        /** Largest total held at any moment since boot. */
        uint32_t globalPeakBytes;
        /** Releases without a matching request, plus saturations; non-zero invalidates the rest. */
        uint32_t unpairedEvents;
        /** Whether the attempt produced a usable connection. */
        bool completed;
    };

    /** @brief Size of the buffer formatEpisode() needs, terminator included. */
    static const size_t kLineBytes = 144;

    TlsChurnMeter();

    /** @brief Report memory the TLS stack was given. */
    void recordAllocated(size_t bytes);

    /** @brief Report memory the TLS stack gave back. */
    void recordFreed(size_t bytes);

    /**
     * @brief Open a measurement window.
     *
     * The window's peak starts at what is held right now, so a handshake carried out while a
     * previous connection is still up is charged with what that connection holds - which is what
     * the heap sees. A window opened while another is still open keeps the outer window and is
     * counted as an unpaired event: the numbers of both would otherwise be a mixture.
     */
    void beginHandshake(uint32_t nowMs);

    /**
     * @brief Close the window and answer what it cost.
     *
     * The window is closed before the answer is taken, so everything the TLS stack does afterwards
     * is outside it. Closing a window that was never opened is counted as an unpaired event: the
     * duration would be measured from whenever the previous one started.
     */
    Episode endHandshake(uint32_t nowMs, bool completed);

    /** @brief Bytes the TLS stack holds right now. */
    uint32_t liveBytes() const;

    /** @brief Worst total since boot. */
    uint32_t peakBytes() const;

    /**
     * @brief Worst total inside the window that closed last, and it stops there.
     *
     * While a window is open this is that window's running worst; once it closes the value stays
     * put until the next window opens, so a reader asking between handshakes gets the last
     * handshake's cost and not what the TLS stack has held since.
     */
    uint32_t lastEpisodePeakBytes() const;

    /** @brief Handshakes measured since boot. */
    uint32_t handshakeCount() const;

    /** @brief Releases without a matching request, plus saturations. */
    uint32_t unpairedEvents() const;

    /**
     * @brief Write the one line an episode is reported as.
     *
     * @return the number of characters the line needs, terminator excluded - as snprintf does,
     *         so a caller can tell a truncated line from a complete one.
     */
    static size_t formatEpisode(const Episode &episode, char *out, size_t outBytes);

    /** @brief The meter the TLS allocator shim and the TLS client share. */
    static TlsChurnMeter &instance();

private:
    void notePeak(uint32_t live);

    std::atomic<uint32_t> _liveBytes;
    std::atomic<uint32_t> _peakBytes;
    std::atomic<uint32_t> _episodePeakBytes;
    std::atomic<uint32_t> _unpairedEvents;
    std::atomic<uint32_t> _handshakes;
    std::atomic<bool> _episodeOpen;
    uint32_t _episodeStartMs;
};

#endif // TLS_CHURN_METER_H
