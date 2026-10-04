#pragma once

// Host stand-in for the ESP32 core's WiFi client headers, in the shape HttpClientAdapter names
// them: a connection object it hands to HTTPClient and a pointer type it passes on to callers.
// Nothing here talks to a socket - what a host test replaces is the connection, and the transport
// above it is the product's own.

#include <Arduino.h>
#include <IPAddress.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>

class WiFiClient
{
public:
    virtual ~WiFiClient() = default;

    // Both are virtual on the device too - the core declares them on Client - which is what lets a
    // subclass of WiFiClientSecure see the connections opened through a WiFiClient pointer, and
    // what makes the overloads that add a timeout arrive here as well.
    virtual int connect(const char *host, uint16_t port)
    {
        (void)host;
        (void)port;
        return 0;
    }

    virtual int connect(IPAddress ip, uint16_t port)
    {
        (void)ip;
        (void)port;
        return 0;
    }

    virtual int available() { return 0; }
    virtual size_t readBytes(uint8_t *, size_t) { return 0; }
    virtual bool connected() { return false; }
    virtual void stop() {}
};

class WiFiClientSecure : public WiFiClient
{
public:
    /** @brief Certificate checking off, which is what the adapter asks for on the device. */
    void setInsecure() { insecureCalls++; }

    int insecureCalls = 0;

    /**
     * @brief The handshake's bound, in seconds, kept in milliseconds as the core keeps it
     *        (WiFiClientSecure::setHandshakeTimeout()); 120 000 ms until it is set (the constructors).
     */
    void setHandshakeTimeout(unsigned long seconds) { handshakeTimeoutMs = seconds * 1000; }

    unsigned long handshakeTimeoutMs = 120000;

    // --- what a test sets up ------------------------------------------------------------------
    //
    // Through statics: the classes under test own their connection as a member and hand out no
    // handle to it, the same shape the HTTPClient stand-in next door deals with.

    /** @brief What the next handshake answers: 1 for a connection, 0 for a failure. */
    static inline int hostNextConnectResult = 1;

    /**
     * @brief How long the next handshake spends before answering. Past the client's bound the
     *        handshake answers nothing: the connect gives up at the first look past the bound, with
     *        -1 (start_ssl_client() in the core's ssl_client.cpp).
     */
    static inline unsigned long hostNextHandshakeMs = 0;

    /**
     * @brief What the TLS stack does with memory while the handshake runs.
     *
     * The device's answer is mbedtls asking the installed calloc/free pair for its record buffers
     * and its handshake scratch; a test supplies that sequence here and drives the very same pair.
     */
    static inline void (*hostDuringHandshake)() = nullptr;

    static inline int hostConnectCalls = 0;

    /** @brief Which family opened the last connection, so a test can tell the two apart. */
    static inline bool hostLastConnectByName = false;
    static inline IPAddress hostLastAddress;

    /**
     * @brief The TLS client's code for the next connect that fails, as start_ssl_client() answers it:
     *        -1 for the socket, the TCP connect or the handshake's timeout, an mbedtls code otherwise.
     */
    static inline int hostNextTlsError = 0;

    /**
     * @brief The next connect fails before the TLS client runs - the name lookup - and, as the core's
     *        does, leaves the last error of the previous connection where it was, and errno too.
     */
    static inline bool hostNextFailsBeforeTls = false;

    /**
     * @brief What errno the next connect leaves when it fails with -1: on the device, whatever the
     *        stack's last failing call set on this task. EINPROGRESS, from the non-blocking connect,
     *        on a connection whose socket opened and whose TCP connect then failed; ENFILE or
     *        ENOBUFS when the stack could not create the socket; ENOMEM when it had no memory for
     *        the connect or the select.
     */
    static inline int hostNextStackErrno = EINPROGRESS;

    /**
     * @brief The next connect fails with -1 before a socket is asked for - the core's answer when no
     *        certificate, key or insecure mode is set (start_ssl_client()) - and leaves errno as it
     *        was.
     */
    static inline bool hostNextFailsBeforeTheSocket = false;

    static void hostReset()
    {
        hostNextConnectResult = 1;
        hostNextHandshakeMs = 0;
        hostDuringHandshake = nullptr;
        hostConnectCalls = 0;
        hostLastConnectByName = false;
        hostLastAddress = IPAddress();
        hostNextTlsError = 0;
        hostNextFailsBeforeTls = false;
        hostNextStackErrno = EINPROGRESS;
        hostNextFailsBeforeTheSocket = false;
    }

    // --- the connection itself ----------------------------------------------------------------

    int connect(const char *host, uint16_t port) override
    {
        (void)host;
        (void)port;
        hostLastConnectByName = true;
        return performHandshake();
    }

    int connect(IPAddress ip, uint16_t port) override
    {
        (void)port;
        hostLastConnectByName = false;
        hostLastAddress = ip;
        return performHandshake();
    }

protected:
    // The core keeps the TLS client's last code here (WiFiClientSecure.h): set from
    // start_ssl_client() on every connect that reaches it, untouched by one that does not.
    int _lastError = 0;

private:
    int performHandshake()
    {
        hostConnectCalls++;
        if (hostNextFailsBeforeTls)
        {
            return 0;
        }
        if (hostNextFailsBeforeTheSocket)
        {
            _lastError = -1;
            return 0;
        }
        if (hostDuringHandshake != nullptr)
        {
            hostDuringHandshake();
        }
        if (hostNextHandshakeMs > handshakeTimeoutMs)
        {
            hostUptimeAdvance(handshakeTimeoutMs + 1);
            _lastError = -1;
            errno = EINPROGRESS;
            return 0;
        }
        hostUptimeAdvance(hostNextHandshakeMs);
        _lastError = (hostNextConnectResult == 1) ? 0 : hostNextTlsError;
        // A socket that opened has been through the non-blocking connect, which leaves EINPROGRESS;
        // a -1 is whatever the failing call left.
        errno = (hostNextConnectResult != 1 && hostNextTlsError == -1) ? hostNextStackErrno : EINPROGRESS;
        return hostNextConnectResult;
    }
};
