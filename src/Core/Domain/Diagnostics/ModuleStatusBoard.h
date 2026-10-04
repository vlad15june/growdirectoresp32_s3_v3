#ifndef MODULE_STATUS_BOARD_H
#define MODULE_STATUS_BOARD_H

#include <atomic>
#include <cerrno>
#include <cstdint>

/**
 * @brief What the module's own parts report about themselves, for the link's supervision to read.
 *
 * Each status is written where it happens - the TLS client when a handshake ends, the HTTP client
 * when a request gets its answer or does not, the outbox when its network task is or is not
 * created, the radio when its driver does or does not start - and read on the supervision's tick.
 * Nothing here asks anybody anything: a status is a record of what an act already found.
 *
 * Four of them are faults of the module itself, which a restart of the module would clear and a
 * restart of the radio would not: TLS without memory for a handshake, a network task the heap
 * refused, a radio driver that would not start, sockets the module's own network stack refused one
 * connect after another. The fifth - whether the server answered a request with a status at all - is
 * not a fault; it tells a server that refuses from a path that loses the request.
 *
 * Every field is atomic: the writers run on the network task, the command task and the radio's
 * restart, the reader on the control loop. A moment of 0 means "never"; a moment that happens to be
 * 0 is stored as 1.
 */
class ModuleStatusBoard
{
public:
    /** @brief How a TLS connect ended, as far as memory is concerned. */
    enum class TlsConnect : uint8_t
    {
        /** A connection: whatever memory the handshake needed, it got. */
        COMPLETED,
        /** The TLS stack reported that an allocation failed. */
        OUT_OF_MEMORY,
        /** Anything else - no name, no TCP, a timeout, a protocol error. Says nothing about memory. */
        OTHER_FAILURE
    };

    // mbedtls 2.28 error codes that mean "an allocation failed" (the ssl, ecp,
    // pk, x509, md, cipher, dhm and pem ones - the high-level ones; the bignum
    // and asn1 ones - the low-level ones). A high-level code is a multiple of 0x80 in 0x1000..0x7F80,
    // a low-level one is below 0x80, and a reported error is the sum of at most one of each - so both
    // parts are looked at. The device build checks every value against the mbedtls headers.
    static const int kSslAllocFailed = -0x7F00;
    static const int kEcpAllocFailed = -0x4D80;
    static const int kPkAllocFailed = -0x3F80;
    static const int kX509AllocFailed = -0x2880;
    static const int kMdAllocFailed = -0x5180;
    static const int kCipherAllocFailed = -0x6180;
    static const int kDhmAllocFailed = -0x3400;
    static const int kPemAllocFailed = -0x1180;
    static const int kMpiAllocFailed = -0x0010;
    static const int kAsn1AllocFailed = -0x006A;

    /**
     * @brief What a TLS client's connect() result and its last error say about memory.
     *
     * @param connectResult what connect() returned: 1 for a connection
     * @param tlsError      the TLS client's last error, cleared to 0 before the connect - the core
     *                      leaves it untouched when the connect fails before the TLS client runs
     */
    static TlsConnect classifyTlsConnect(int connectResult, int tlsError)
    {
        if (connectResult == 1)
        {
            return TlsConnect::COMPLETED;
        }
        if (tlsError >= 0)
        {
            return TlsConnect::OTHER_FAILURE;
        }

        const int magnitude = -tlsError;
        const int high = -(magnitude & 0xFF80);
        const int low = -(magnitude & 0x007F);
        if (high == kSslAllocFailed || high == kEcpAllocFailed || high == kPkAllocFailed ||
            high == kX509AllocFailed || high == kMdAllocFailed || high == kCipherAllocFailed ||
            high == kDhmAllocFailed || high == kPemAllocFailed || low == kMpiAllocFailed ||
            low == kAsn1AllocFailed)
        {
            return TlsConnect::OUT_OF_MEMORY;
        }
        return TlsConnect::OTHER_FAILURE;
    }

    /** @brief How a connect fared at getting its socket from the module's own network stack. */
    enum class SocketOpen : uint8_t
    {
        /** The socket opened; whatever failed, if anything, came after it - the TCP connect, TLS. */
        OPENED,
        /** The stack refused it: no free socket, or no memory for the socket, its connect or its wait. */
        REFUSED,
        /** The connect ended before a socket was asked for - the name lookup, the client's setup. */
        NOT_ASKED
    };

    /**
     * @brief What a TLS client's connect() result, its last error and the task's errno right after
     *        it say about the socket.
     *
     * The core answers -1 alike for a socket it could not get and for a TCP connect that failed;
     * errno tells them apart. The stack sets it only on a call that fails, on the calling task:
     * ENFILE when every socket is in use and ENOBUFS when it has no memory for a new one, ENOBUFS or
     * ENOMEM when the connect has no local port or no memory for its first segment, ENOMEM when the
     * wait for the connect has no memory - and EINPROGRESS, from the non-blocking connect, on every
     * socket that did open. A -1 with errno still 0 asked for no socket at all.
     *
     * @param connectResult what connect() returned: 1 for a connection
     * @param tlsError      the TLS client's last error, cleared to 0 before the connect
     * @param stackErrno    errno read first thing after the connect, cleared to 0 before it
     */
    static SocketOpen classifySocketOpen(int connectResult, int tlsError, int stackErrno)
    {
        if (connectResult == 1)
        {
            return SocketOpen::OPENED;
        }
        if (tlsError == 0 || (tlsError == -1 && stackErrno == 0))
        {
            return SocketOpen::NOT_ASKED;
        }
        if (tlsError == -1 && (stackErrno == ENFILE || stackErrno == ENOBUFS || stackErrno == ENOMEM))
        {
            return SocketOpen::REFUSED;
        }
        return SocketOpen::OPENED;
    }

    ModuleStatusBoard()
        : _tlsOutOfMemoryAtMs(0),
          _networkTaskRefused(false),
          _radioNotStarted(false),
          _serverAnsweredAtMs(0),
          _requestNotReachedAtMs(0),
          _socketsRefusedInARow(0),
          _socketRefusedAtMs(0)
    {
    }

    // --- writers -----------------------------------------------------------------------------------

    /**
     * @brief A TLS connect ended. A completed handshake clears the memory status: whatever was short
     *        before, the next handshake found what it needed. Any other failure leaves it as it is.
     */
    void noteTlsConnect(uint32_t nowMs, TlsConnect outcome)
    {
        if (outcome == TlsConnect::COMPLETED)
        {
            _tlsOutOfMemoryAtMs.store(0);
        }
        else if (outcome == TlsConnect::OUT_OF_MEMORY)
        {
            _tlsOutOfMemoryAtMs.store(moment(nowMs));
        }
    }

    /** @brief The outbox tried to create its network task. */
    void noteNetworkTask(bool created) { _networkTaskRefused.store(!created); }

    /** @brief The radio's restart asked the driver to start the station. */
    void noteRadioStart(bool started) { _radioNotStarted.store(!started); }

    /**
     * @brief A connect asked the stack for its socket. A refusal adds one to the refusals in a row;
     *        a socket that opened starts them again; a connect that asked for none changes nothing.
     */
    void noteSocketOpen(uint32_t nowMs, SocketOpen outcome)
    {
        if (outcome == SocketOpen::REFUSED)
        {
            _socketsRefusedInARow.fetch_add(1);
            _socketRefusedAtMs.store(moment(nowMs));
        }
        else if (outcome == SocketOpen::OPENED)
        {
            _socketsRefusedInARow.store(0);
        }
    }

    /**
     * @brief A request ended: with a status from the server (any status, success or not), or
     *        without one - a negative code from the HTTP client.
     */
    void noteRequestOutcome(uint32_t nowMs, int httpCode)
    {
        if (httpCode > 0)
        {
            _serverAnsweredAtMs.store(moment(nowMs));
        }
        else
        {
            _requestNotReachedAtMs.store(moment(nowMs));
        }
    }

    // --- readers -----------------------------------------------------------------------------------

    /**
     * @brief Whether a handshake failed for memory within the last windowMs, with no handshake
     *        completed since.
     */
    bool tlsOutOfMemoryWithin(uint32_t nowMs, uint32_t windowMs) const
    {
        const uint32_t at = _tlsOutOfMemoryAtMs.load();
        return at != 0 && nowMs - at < windowMs;
    }

    bool networkTaskRefused() const { return _networkTaskRefused.load(); }
    bool radioNotStarted() const { return _radioNotStarted.load(); }

    /**
     * @brief Whether at least inARow sockets were refused one after another, with none opened since,
     *        and the last of them within the last windowMs.
     */
    bool socketsRefusedWithin(uint32_t nowMs, uint32_t windowMs, uint32_t inARow) const
    {
        const uint32_t at = _socketRefusedAtMs.load();
        return _socketsRefusedInARow.load() >= inARow && at != 0 && nowMs - at < windowMs;
    }

    /** @brief Whether the server answered a request with a status at sinceMs or later. */
    bool serverAnsweredSince(uint32_t nowMs, uint32_t sinceMs) const
    {
        return reachedSince(_serverAnsweredAtMs.load(), nowMs, sinceMs);
    }

    /** @brief Whether a request ended without the server's answer at sinceMs or later. */
    bool requestNotReachedSince(uint32_t nowMs, uint32_t sinceMs) const
    {
        return reachedSince(_requestNotReachedAtMs.load(), nowMs, sinceMs);
    }

private:
    static uint32_t moment(uint32_t nowMs) { return nowMs == 0 ? 1u : nowMs; }

    static bool reachedSince(uint32_t at, uint32_t nowMs, uint32_t sinceMs)
    {
        return at != 0 && nowMs - at <= nowMs - sinceMs;
    }

    std::atomic<uint32_t> _tlsOutOfMemoryAtMs;
    std::atomic<bool> _networkTaskRefused;
    std::atomic<bool> _radioNotStarted;
    std::atomic<uint32_t> _serverAnsweredAtMs;
    std::atomic<uint32_t> _requestNotReachedAtMs;
    std::atomic<uint32_t> _socketsRefusedInARow;
    std::atomic<uint32_t> _socketRefusedAtMs;
};

#endif // MODULE_STATUS_BOARD_H
