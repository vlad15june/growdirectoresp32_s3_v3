#pragma once

// The TLS record buffers mbedtls allocates once per connection - one for incoming records, one
// for outgoing - are the largest single allocations in the firmware, and their size is fixed by
// the mbedtls configuration of the build. TlsMemoryPool reserves a slot for each, so the pool's
// geometry and the mbedtls configuration are two halves of one decision and are kept together
// here.
//
// GD_TLS_BUFFER_PROFILE selects which half-pair a build uses:
//
//   0 (default) - the sizes the fleet runs today: symmetric 16 KB records, two equal slots with
//                 room to spare. Nothing about it changes unless the flag is passed.
//   1           - incoming records untouched at the 16 KB the TLS standard allows a peer to
//                 send; outgoing records cut to 4 KB. What the module sends is its own to
//                 fragment, so this costs nothing on the wire.
//   2           - both directions cut. The incoming limit is no longer what the standard lets a
//                 peer use, so a server that sends a larger record - a long certificate chain,
//                 a big response body - fails the connection. Measured on test hardware only.
//
// The mbedtls half lives in sdkconfig.defaults.tls-* and only reaches the compiler through the
// ESP-IDF build (env:esp32-s3-devkitm-1-src and the profile environments derived from it). The
// two halves are checked against each other below: a build whose mbedtls options did not arrive
// stops at the static_assert instead of quietly pushing every handshake onto the heap.

#include <cstddef>

#include "mbedtls/platform.h"

#ifndef GD_TLS_BUFFER_PROFILE
#define GD_TLS_BUFFER_PROFILE 0
#endif

// Per-record overhead mbedtls adds to the content length when it sizes a record buffer: the
// 13-byte header it stores (5 on the wire plus the implicit sequence number) and the worst-case
// expansion of the ciphersuites this configuration compiles in - IV, MAC and CBC padding.
// MBEDTLS_SSL_IN_BUFFER_LEN is this plus the content length; the static_asserts at the bottom of
// this file are what keeps the number honest.
const size_t kTlsRecordOverheadBytes = 333;

// The pool stands in for calloc, and calloc never hands back memory aligned less strictly than a
// word - on this platform heap_caps_calloc gives at least 16, which is what the AES peripheral's
// EDMA path expects of a buffer it may be pointed at. A slot whose size is the exact buffer size
// would leave the next slot starting wherever the previous one ended, so slot sizes are rounded
// up to this and the buffers are declared with the matching alignment. The round-up doubles as a
// few bytes of space between two buffers that would otherwise butt together.
const size_t kTlsSlotAlignmentBytes = 16;

constexpr size_t alignedTlsSlotBytes(size_t bytes)
{
    return (bytes + kTlsSlotAlignmentBytes - 1) / kTlsSlotAlignmentBytes * kTlsSlotAlignmentBytes;
}

// Each slot carries the window of request sizes it answers, not just its size. mbedtls allocates
// far more than the two record buffers - the handshake state alone is 2280 bytes and a peer
// certificate is one allocation of whatever the server sends - and a slot that accepts those
// would report a fallback for every one of them once both record buffers are in place. The
// window is what keeps the fallback counter meaning "a TLS record buffer went to the heap".

// The sizes the fleet runs today: two equal slots larger than the buffers, answering everything
// from 12 KB up. This profile describes the pool as it is, it does not re-derive it.
struct TlsBufferProfileStock
{
    static const size_t kInBufferBytes = 16384 + kTlsRecordOverheadBytes;
    static const size_t kOutBufferBytes = 16384 + kTlsRecordOverheadBytes;

    static const size_t kFirstSlotBytes = 18432;
    static const size_t kFirstSlotMinBytes = 12288;
    static const size_t kSecondSlotBytes = 18432;
    static const size_t kSecondSlotMinBytes = 12288;
};

// Outgoing records cut to 4 KB - the ESP-IDF default for an asymmetric configuration. Each slot
// exists for exactly one buffer: its size is that buffer rounded up to the slot alignment, and
// its window runs from the buffer size to that rounded size - a range a few bytes wide, and the
// only request in this configuration that falls in it is that buffer.
struct TlsBufferProfileTrimmedOut
{
    static const size_t kInBufferBytes = 16384 + kTlsRecordOverheadBytes;
    static const size_t kOutBufferBytes = 4096 + kTlsRecordOverheadBytes;

    static const size_t kFirstSlotBytes = alignedTlsSlotBytes(kInBufferBytes);
    static const size_t kFirstSlotMinBytes = kInBufferBytes;
    static const size_t kSecondSlotBytes = alignedTlsSlotBytes(kOutBufferBytes);
    static const size_t kSecondSlotMinBytes = kOutBufferBytes;
};

// Both directions cut. Gated on hardware testing: see the note on profile 2 above.
struct TlsBufferProfileTrimmedBoth
{
    static const size_t kInBufferBytes = 8192 + kTlsRecordOverheadBytes;
    static const size_t kOutBufferBytes = 2048 + kTlsRecordOverheadBytes;

    static const size_t kFirstSlotBytes = alignedTlsSlotBytes(kInBufferBytes);
    static const size_t kFirstSlotMinBytes = kInBufferBytes;
    static const size_t kSecondSlotBytes = alignedTlsSlotBytes(kOutBufferBytes);
    static const size_t kSecondSlotMinBytes = kOutBufferBytes;
};

#if GD_TLS_BUFFER_PROFILE == 0
typedef TlsBufferProfileStock ActiveTlsBufferProfile;
#elif GD_TLS_BUFFER_PROFILE == 1
typedef TlsBufferProfileTrimmedOut ActiveTlsBufferProfile;
#elif GD_TLS_BUFFER_PROFILE == 2
typedef TlsBufferProfileTrimmedBoth ActiveTlsBufferProfile;
#else
#error "GD_TLS_BUFFER_PROFILE must be 0, 1 or 2"
#endif

// On the device, mbedtls itself says how large the two buffers are. GD_TLS_MBEDTLS_HOST_STANDIN
// is defined only by the host stand-in for mbedtls/platform.h, so this check runs in every build
// that links the real thing - and a profile that disagrees with the sdkconfig the build actually
// resolved fails to compile.
#ifndef GD_TLS_MBEDTLS_HOST_STANDIN
#include "mbedtls/ssl_internal.h"

static_assert(ActiveTlsBufferProfile::kInBufferBytes == MBEDTLS_SSL_IN_BUFFER_LEN,
              "GD_TLS_BUFFER_PROFILE does not match the mbedtls incoming record buffer this "
              "build resolved: check that the matching sdkconfig.defaults.tls-* overlay reached "
              "the generated sdkconfig");
static_assert(ActiveTlsBufferProfile::kOutBufferBytes == MBEDTLS_SSL_OUT_BUFFER_LEN,
              "GD_TLS_BUFFER_PROFILE does not match the mbedtls outgoing record buffer this "
              "build resolved: check that the matching sdkconfig.defaults.tls-* overlay reached "
              "the generated sdkconfig");
#endif

// True for every profile, host build included: each slot has to be able to hold - and to accept -
// the record buffer it exists for, or every handshake goes to the heap.
static_assert(ActiveTlsBufferProfile::kFirstSlotBytes >= ActiveTlsBufferProfile::kInBufferBytes &&
                  ActiveTlsBufferProfile::kFirstSlotMinBytes <= ActiveTlsBufferProfile::kInBufferBytes,
              "the first slot does not answer the incoming record buffer");
static_assert(ActiveTlsBufferProfile::kSecondSlotBytes >= ActiveTlsBufferProfile::kOutBufferBytes &&
                  ActiveTlsBufferProfile::kSecondSlotMinBytes <= ActiveTlsBufferProfile::kOutBufferBytes,
              "the second slot does not answer the outgoing record buffer");

// A slot size that is not a whole number of alignment units puts the slot after it - and every
// pointer the pool then hands to mbedtls - on an arbitrary byte boundary.
static_assert(ActiveTlsBufferProfile::kFirstSlotBytes % kTlsSlotAlignmentBytes == 0,
              "the first slot size is not a multiple of the slot alignment");
static_assert(ActiveTlsBufferProfile::kSecondSlotBytes % kTlsSlotAlignmentBytes == 0,
              "the second slot size is not a multiple of the slot alignment");
