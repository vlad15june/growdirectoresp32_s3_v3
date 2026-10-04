#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFiClientSecure.h>

#include <cerrno>

#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Core/Domain/Diagnostics/TlsChurnMeter.h"
#include "mbedtls/platform.h"

#ifndef GD_TLS_MBEDTLS_HOST_STANDIN
#include "mbedtls/asn1.h"
#include "mbedtls/bignum.h"
#include "mbedtls/cipher.h"
#include "mbedtls/dhm.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/pem.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"

// The status board names the allocation failures by number, being a domain class with no mbedtls of
// its own; the TLS stack this build links is where the numbers are checked.
static_assert(ModuleStatusBoard::kSslAllocFailed == MBEDTLS_ERR_SSL_ALLOC_FAILED, "mbedtls ssl alloc code");
static_assert(ModuleStatusBoard::kEcpAllocFailed == MBEDTLS_ERR_ECP_ALLOC_FAILED, "mbedtls ecp alloc code");
static_assert(ModuleStatusBoard::kPkAllocFailed == MBEDTLS_ERR_PK_ALLOC_FAILED, "mbedtls pk alloc code");
static_assert(ModuleStatusBoard::kX509AllocFailed == MBEDTLS_ERR_X509_ALLOC_FAILED, "mbedtls x509 alloc code");
static_assert(ModuleStatusBoard::kMdAllocFailed == MBEDTLS_ERR_MD_ALLOC_FAILED, "mbedtls md alloc code");
static_assert(ModuleStatusBoard::kCipherAllocFailed == MBEDTLS_ERR_CIPHER_ALLOC_FAILED, "mbedtls cipher alloc code");
static_assert(ModuleStatusBoard::kDhmAllocFailed == MBEDTLS_ERR_DHM_ALLOC_FAILED, "mbedtls dhm alloc code");
static_assert(ModuleStatusBoard::kPemAllocFailed == MBEDTLS_ERR_PEM_ALLOC_FAILED, "mbedtls pem alloc code");
static_assert(ModuleStatusBoard::kMpiAllocFailed == MBEDTLS_ERR_MPI_ALLOC_FAILED, "mbedtls mpi alloc code");
static_assert(ModuleStatusBoard::kAsn1AllocFailed == MBEDTLS_ERR_ASN1_ALLOC_FAILED, "mbedtls asn1 alloc code");
#endif

/**
 * @brief The TLS client, with a measurement window around every handshake.
 *
 * Opening a connection is where the TLS stack spends its memory, and connect() is the only entry
 * point that does it: everything above - a request on a kept-alive connection, a body read - runs
 * inside buffers that already exist. So the window is exactly this call, and one line is printed
 * per handshake rather than per request.
 *
 * The core offers connect() in nine overloads, of which two are virtual: one taking a host name
 * and one taking an address. Both are overridden here, and that is what the accounting is
 * anchored to - each overload that adds a timeout stores it and calls one of these two on `this`,
 * so the call arrives through the vtable and lands inside a window. What the base does from
 * there - the name lookup, then the five- and six-argument overloads that carry out the
 * handshake - is not virtual and is reached by a qualified call, so a window is opened once per
 * connection and never re-entered.
 *
 * What is not measured: the overloads handed a certificate pair or a pre-shared key explicitly.
 * They are not virtual and they reach the handshake without passing through either of the two
 * above, so a caller holding a WiFiClientSecure& could open a connection this never sees.
 * Nothing in this firmware does - HTTPClient is the only caller and it connects by host name -
 * but such a call would be measured as nothing at all rather than refused.
 *
 * A window also contains the name lookup and the TCP connect that precede the handshake. The
 * duration includes them; the byte totals do not, since neither allocates through the TLS
 * allocator.
 *
 * The same two calls tell the module's status board how the connect ended for memory: a handshake
 * that completed, one that the TLS stack failed for want of an allocation, or anything else. The TLS
 * stack's code is the client's last error, which the core sets only once the TLS client has run - a
 * name it could not look up returns before that and leaves the previous connection's code behind -
 * so it is cleared before every connect.
 *
 * And whether the module's own network stack refused the connection its socket. The core answers
 * -1 for that and for a TCP connect that failed alike (start_ssl_client() in the core's ssl_client.cpp); what tells
 * them apart is errno, which the stack sets on the calling task and only on a call that fails:
 * ENFILE or ENOBUFS from a socket it could not create, ENOBUFS or ENOMEM from a connect it had no
 * port or memory for, ENOMEM from a select it had no memory for - and EINPROGRESS, left by the
 * non-blocking connect, on every connection whose socket did open. errno is cleared before the
 * connect, so a code an earlier call left behind is never read as this connect's, and it is read
 * first thing once connect() returns. What the core runs between the failing call and that return -
 * its error log and stop(), which closes no socket that is not open - sets no errno.
 */
class MeteredTlsClient : public WiFiClientSecure
{
public:
    // The overloads that take certificates or a pre-shared key stay reachable; declaring connect()
    // here would otherwise hide every one of them.
    using WiFiClientSecure::connect;

    int connect(const char *host, uint16_t port) override
    {
        openWindow();
        _lastError = 0;
        errno = 0;
        return closeWindow(noteOutcome(WiFiClientSecure::connect(host, port)));
    }

    int connect(IPAddress ip, uint16_t port) override
    {
        openWindow();
        _lastError = 0;
        errno = 0;
        return closeWindow(noteOutcome(WiFiClientSecure::connect(ip, port)));
    }

    /** @brief Where the outcome of every connect is recorded; optional. */
    void setStatusBoard(ModuleStatusBoard *statusBoard) { _statusBoard = statusBoard; }

private:
    ModuleStatusBoard *_statusBoard = nullptr;

    int noteOutcome(int result)
    {
        const int stackError = errno;
        if (_statusBoard != nullptr)
        {
            const uint32_t nowMs = (uint32_t)millis();
            _statusBoard->noteTlsConnect(nowMs, ModuleStatusBoard::classifyTlsConnect(result, _lastError));
            _statusBoard->noteSocketOpen(nowMs,
                                         ModuleStatusBoard::classifySocketOpen(result, _lastError, stackError));
        }
        return result;
    }

    static void openWindow()
    {
        TlsChurnMeter::instance().beginHandshake(millis());
    }

    /**
     * @brief Close the window, report what it measured, and hand the connect's answer back.
     *
     * Takes the result as an argument so that the handshake is over before this is entered: the
     * line buffer then belongs to a frame that no longer sits underneath the TLS stack.
     */
    static int closeWindow(int result)
    {
        const TlsChurnMeter::Episode episode =
            TlsChurnMeter::instance().endHandshake(millis(), result == 1);

        char line[TlsChurnMeter::kLineBytes];
        TlsChurnMeter::formatEpisode(episode, line, sizeof(line));
        Serial.println(line);

        return result;
    }
};
