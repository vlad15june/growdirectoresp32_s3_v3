// The same TLS traffic, run against all three buffer profiles.
//
// Cutting the mbedtls record buffers is only worth anything if the pool still answers every
// request the buffers make; a profile whose slots no longer match the configuration turns each
// handshake into a pair of heap allocations of exactly the size the pool exists to keep out of
// the heap - and does it silently, because mbedtls is perfectly happy with heap memory.
//
// What is simulated here is the sequence of mbedtls_calloc/mbedtls_free calls a session makes,
// not the TLS protocol: the record buffers come from mbedtls_ssl_setup(), which allocates
// MBEDTLS_SSL_IN_BUFFER_LEN and then MBEDTLS_SSL_OUT_BUFFER_LEN and frees both in
// mbedtls_ssl_free(); the sizes in between are the other things mbedtls allocates during a
// handshake (handshake state 2280 B, transform 212 B, session 128 B, the peer certificate
// structure 344 B and one allocation per certificate in the chain). The pool is driven through
// the two function pointers install() hands to mbedtls, so the entry points are the device's.
//
// The buffer sizes themselves are the profile's, and on a device build they are checked against
// mbedtls's own MBEDTLS_SSL_IN_BUFFER_LEN / MBEDTLS_SSL_OUT_BUFFER_LEN (TlsBufferProfile.h) -
// which is what keeps this file from proving something about numbers the board does not use.

#include <unity.h>
#include <cstring>

#include "Infrastructure/Adapters/Http/TlsMemoryPool.h"

namespace
{

// Everything else mbedtls allocates while a connection is up, in bytes. The first four are
// fixed-size structures; the last three stand for a certificate chain and a long one - sizes the
// module does not control, because the server chooses them.
const size_t kNonRecordAllocations[] = {2280, 212, 128, 344, 1400, 2600, 5000};

template <class Profile>
class SessionDriver
{
public:
    typedef TlsMemoryPoolT<Profile> Pool;

    // mbedtls_ssl_setup(): incoming record buffer first, then outgoing.
    void open()
    {
        _inBuf = mbedtlsInstalledCalloc(1, Profile::kInBufferBytes);
        _outBuf = mbedtlsInstalledCalloc(1, Profile::kOutBufferBytes);
        TEST_ASSERT_NOT_NULL(_inBuf);
        TEST_ASSERT_NOT_NULL(_outBuf);
        TEST_ASSERT_TRUE(_inBuf != _outBuf);
    }

    // What a handshake allocates besides the record buffers, taken and released in order.
    void handshake()
    {
        for (size_t i = 0; i < sizeof(kNonRecordAllocations) / sizeof(kNonRecordAllocations[0]); ++i)
        {
            void *p = mbedtlsInstalledCalloc(1, kNonRecordAllocations[i]);
            TEST_ASSERT_NOT_NULL(p);
            mbedtlsInstalledFree(p);
        }
    }

    // mbedtls_ssl_free().
    void close()
    {
        mbedtlsInstalledFree(_outBuf);
        mbedtlsInstalledFree(_inBuf);
        _outBuf = nullptr;
        _inBuf = nullptr;
    }

    void *inBuf() const { return _inBuf; }
    void *outBuf() const { return _outBuf; }

private:
    void *_inBuf = nullptr;
    void *_outBuf = nullptr;
};

// The exchanges the firmware actually performs, in the order the outbox performs them: a
// telemetry post that opens and closes its own connection, a kept-alive connection carrying
// several requests, a handshake that dies halfway, and an OTA download.
template <class Profile>
void runTheExchanges(const char *profileName)
{
    typedef TlsMemoryPoolT<Profile> Pool;
    Pool::install();
    const uint32_t fallbacksBefore = Pool::heapFallbacks();

    SessionDriver<Profile> session;

    // Three telemetry posts, each with its own connection - the slots have to come back.
    void *firstIn = nullptr;
    for (int i = 0; i < 3; ++i)
    {
        session.open();
        if (i == 0)
        {
            firstIn = session.inBuf();
        }
        else
        {
            TEST_ASSERT_EQUAL_PTR_MESSAGE(firstIn, session.inBuf(),
                                          "a closed session did not release its slot");
        }
        session.handshake();
        session.close();
    }

    // One connection kept open across several requests, as HTTPClient::setReuse(true) does.
    session.open();
    session.handshake();
    for (int i = 0; i < 5; ++i)
    {
        void *body = mbedtlsInstalledCalloc(1, 512);
        TEST_ASSERT_NOT_NULL(body);
        mbedtlsInstalledFree(body);
    }
    session.close();

    // A handshake that fails: the buffers are allocated and released without the rest.
    session.open();
    session.close();

    // The OTA download: one connection, a certificate chain, and a long stream of reads.
    session.open();
    session.handshake();
    for (int i = 0; i < 20; ++i)
    {
        void *chunk = mbedtlsInstalledCalloc(1, 4096);
        TEST_ASSERT_NOT_NULL(chunk);
        mbedtlsInstalledFree(chunk);
    }
    session.close();

    char message[128];
    snprintf(message, sizeof(message),
             "%s: a TLS record buffer went to the heap during the exchanges", profileName);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fallbacksBefore, Pool::heapFallbacks(), message);
}

// The record buffers really come out of the slots: with both taken, a third request of record
// size has nowhere to go and is counted.
template <class Profile>
void assertRecordBuffersOccupyTheSlots(const char *profileName)
{
    typedef TlsMemoryPoolT<Profile> Pool;
    Pool::install();

    SessionDriver<Profile> session;
    session.open();

    const uint32_t fallbacksBefore = Pool::heapFallbacks();
    void *third = mbedtlsInstalledCalloc(1, Profile::kInBufferBytes);
    TEST_ASSERT_NOT_NULL(third);

    char message[128];
    snprintf(message, sizeof(message),
             "%s: the two record buffers did not take the two slots", profileName);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fallbacksBefore + 1, Pool::heapFallbacks(), message);

    mbedtlsInstalledFree(third);
    session.close();
}

void test_stock_profile_serves_every_record_buffer(void)
{
    runTheExchanges<TlsBufferProfileStock>("stock");
    assertRecordBuffersOccupyTheSlots<TlsBufferProfileStock>("stock");
}

void test_trimmed_out_profile_serves_every_record_buffer(void)
{
    runTheExchanges<TlsBufferProfileTrimmedOut>("trimmed-out");
    assertRecordBuffersOccupyTheSlots<TlsBufferProfileTrimmedOut>("trimmed-out");
}

void test_trimmed_both_profile_serves_every_record_buffer(void)
{
    runTheExchanges<TlsBufferProfileTrimmedBoth>("trimmed-both");
    assertRecordBuffersOccupyTheSlots<TlsBufferProfileTrimmedBoth>("trimmed-both");
}

// The asymmetric profiles reserve one slot per direction, and the small one must not be handed
// to the incoming buffer: mbedtls asks for the incoming buffer first, and if it took the slot
// that fits it least the outgoing buffer would find nothing.
void test_each_direction_lands_in_the_slot_reserved_for_it(void)
{
    typedef TlsMemoryPoolT<TlsBufferProfileTrimmedOut> Pool;
    Pool::install();
    const uint32_t fallbacksBefore = Pool::heapFallbacks();

    void *inBuf = mbedtlsInstalledCalloc(1, TlsBufferProfileTrimmedOut::kInBufferBytes);
    void *outBuf = mbedtlsInstalledCalloc(1, TlsBufferProfileTrimmedOut::kOutBufferBytes);
    TEST_ASSERT_NOT_NULL(inBuf);
    TEST_ASSERT_NOT_NULL(outBuf);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fallbacksBefore, Pool::heapFallbacks(),
                                     "one of the two directions was pushed to the heap");

    // Writing the whole of each buffer must not touch the other one.
    memset(inBuf, 0xA5, TlsBufferProfileTrimmedOut::kInBufferBytes);
    memset(outBuf, 0x5A, TlsBufferProfileTrimmedOut::kOutBufferBytes);
    TEST_ASSERT_EQUAL_UINT8(0xA5, static_cast<uint8_t *>(inBuf)[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA5,
                            static_cast<uint8_t *>(inBuf)[TlsBufferProfileTrimmedOut::kInBufferBytes - 1]);
    TEST_ASSERT_EQUAL_UINT8(0x5A, static_cast<uint8_t *>(outBuf)[0]);

    mbedtlsInstalledFree(outBuf);
    mbedtlsInstalledFree(inBuf);
}

// The pool replaces calloc, so what it returns has to be aligned like calloc's result. This
// test is two claims of different strength, and they are worth telling apart. The address
// checks are a guard on the host - the host compiler aligns a large array whether or not it is
// asked to, so what decides them on the device is the alignment specifier on the buffers, and
// the addresses in the built ELF are where that is observable. The slot-size checks below are a
// discriminator here and now: take the round-up out of TlsBufferProfile.h and the trimmed
// profiles go red on the host, where the static_asserts cover only whichever profile is active.
// Stock is unaffected either way - its slot sizes are literals that are already multiples of the
// alignment - and the run stops at the first failing assertion, so the red names one trimmed
// profile rather than both.
template <class Profile>
void assertBothSlotsAreAligned(const char *profileName)
{
    typedef TlsMemoryPoolT<Profile> Pool;
    Pool::install();

    SessionDriver<Profile> session;
    session.open();

    char message[128];
    snprintf(message, sizeof(message), "%s: a record buffer is not aligned", profileName);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, (uint32_t)(reinterpret_cast<uintptr_t>(session.inBuf()) % kTlsSlotAlignmentBytes),
        message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, (uint32_t)(reinterpret_cast<uintptr_t>(session.outBuf()) % kTlsSlotAlignmentBytes),
        message);

    // A slot that is a whole number of alignment units keeps the next one aligned too, and
    // leaves the round-up between the two buffers instead of butting them together.
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)(Profile::kFirstSlotBytes % kTlsSlotAlignmentBytes));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)(Profile::kSecondSlotBytes % kTlsSlotAlignmentBytes));

    session.close();
}

void test_every_profile_hands_mbedtls_aligned_buffers(void)
{
    assertBothSlotsAreAligned<TlsBufferProfileStock>("stock");
    assertBothSlotsAreAligned<TlsBufferProfileTrimmedOut>("trimmed-out");
    assertBothSlotsAreAligned<TlsBufferProfileTrimmedBoth>("trimmed-both");
}

// Everything mbedtls allocates that is not a record buffer stays out of the slots, whichever
// profile is compiled in. Without this the fallback counter would fill up with the handshake
// state and the certificates the server sends, and stop meaning anything.
void test_non_record_allocations_never_touch_the_slots(void)
{
    typedef TlsMemoryPoolT<TlsBufferProfileTrimmedBoth> Pool;
    Pool::install();

    SessionDriver<TlsBufferProfileTrimmedBoth> session;
    session.open();

    // Both slots are taken; a handshake-sized allocation must go to the heap without being
    // reported as a refused record buffer.
    const uint32_t fallbacksBefore = Pool::heapFallbacks();
    for (size_t i = 0; i < sizeof(kNonRecordAllocations) / sizeof(kNonRecordAllocations[0]); ++i)
    {
        void *p = mbedtlsInstalledCalloc(1, kNonRecordAllocations[i]);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_TRUE_MESSAGE(p != session.inBuf() && p != session.outBuf(),
                                 "a non-record allocation was served from a reserved slot");
        mbedtlsInstalledFree(p);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fallbacksBefore, Pool::heapFallbacks(),
                                     "a non-record allocation was counted as a refused buffer");

    session.close();
}

} // namespace

extern "C" void run_tls_handshake_buffer_tests(void)
{
    RUN_TEST(test_stock_profile_serves_every_record_buffer);
    RUN_TEST(test_trimmed_out_profile_serves_every_record_buffer);
    RUN_TEST(test_trimmed_both_profile_serves_every_record_buffer);
    RUN_TEST(test_each_direction_lands_in_the_slot_reserved_for_it);
    RUN_TEST(test_every_profile_hands_mbedtls_aligned_buffers);
    RUN_TEST(test_non_record_allocations_never_touch_the_slots);
}
