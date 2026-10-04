#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "mbedtls/platform.h"
#include "Core/Domain/Diagnostics/AllocationFailureCounter.h"
#include "Core/Domain/Diagnostics/TlsChurnMeter.h"
#include "Infrastructure/Adapters/Http/TlsBufferProfile.h"

// The calloc/free pair mbedtls calls for everything it allocates, with two reserved slots for the
// two per-connection record buffers - the only allocations big enough to matter. Everything else
// mbedtls asks for goes to the heap unchanged.
//
// The geometry comes from the profile: which sizes the slots are and which range of requests is
// worth serving from them. TlsBufferProfile.h explains the profiles and checks them against the
// mbedtls configuration of the build.
template <class Profile>
class TlsMemoryPoolT
{
public:
    static void install()
    {
        _slots[0].ptr = _firstBuf;
        _slots[0].size = Profile::kFirstSlotBytes;
        _slots[0].minSize = Profile::kFirstSlotMinBytes;
        _slots[0].used = false;
        _slots[1].ptr = _secondBuf;
        _slots[1].size = Profile::kSecondSlotBytes;
        _slots[1].minSize = Profile::kSecondSlotMinBytes;
        _slots[1].used = false;
        mbedtls_platform_set_calloc_free(poolCalloc, poolFree);
        Serial.printf("TLSPOOL: installed, slots %u + %u = %u bytes reserved\n",
                      (unsigned)Profile::kFirstSlotBytes,
                      (unsigned)Profile::kSecondSlotBytes,
                      (unsigned)(Profile::kFirstSlotBytes + Profile::kSecondSlotBytes));
    }

    // How many pooled-size requests found no free slot and went to the heap instead. The
    // fallback defeats the pool's purpose (a heap allocation of TLS size is exactly what the
    // pool exists to prevent), so it is counted even where nothing prints.
    static uint32_t heapFallbacks()
    {
        return _heapFallbacks;
    }

private:
    static const int kSlotCount = 2;

    struct Slot
    {
        uint8_t *ptr;
        size_t size;
        // Smallest request this slot answers. Requests below it are not what the slot was
        // reserved for and go to the heap without being counted as a refusal.
        size_t minSize;
        bool used;
    };

    // Aligned explicitly rather than by luck of the layout: a byte array has no alignment
    // requirement of its own, and what the pool returns has to be at least as aligned as what
    // calloc would have returned.
    alignas(kTlsSlotAlignmentBytes) static uint8_t _firstBuf[Profile::kFirstSlotBytes];
    alignas(kTlsSlotAlignmentBytes) static uint8_t _secondBuf[Profile::kSecondSlotBytes];
    static Slot _slots[kSlotCount];
    static portMUX_TYPE _mux;
    static uint32_t _heapFallbacks;

    static void *poolCalloc(size_t n, size_t size)
    {
        size_t total = n * size;
        if (size != 0 && total / size != n)
        {
            return nullptr;
        }

        // Smallest slot whose window covers the request, so that a profile whose slots differ in
        // size keeps the large one for the buffer that needs it whatever order mbedtls asks in.
        int chosen = -1;
        bool pooledSize = false;
        // The mask feeds only debug prints, but it is computed in every build so that debug and
        // release take the identical locked path.
        uint8_t usedMask = 0;

        portENTER_CRITICAL(&_mux);
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (total < _slots[i].minSize || total > _slots[i].size)
            {
                continue;
            }
            pooledSize = true;
            if (!_slots[i].used && (chosen < 0 || _slots[i].size < _slots[chosen].size))
            {
                chosen = i;
            }
        }
        if (chosen >= 0)
        {
            _slots[chosen].used = true;
        }
        else if (pooledSize)
        {
            ++_heapFallbacks;
        }
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (_slots[i].used)
            {
                usedMask |= (1 << i);
            }
        }
        portEXIT_CRITICAL(&_mux);
        (void)usedMask;

        if (chosen >= 0)
        {
            memset(_slots[chosen].ptr, 0, total);
            // Per-allocation prints run inside every TLS handshake, so they exist only in debug
            // builds; a release build accounts for the fallback via heapFallbacks().
#ifdef DEBUG
            Serial.printf("TLSPOOL: calloc %u -> slot%d used=0x%02X\n",
                          (unsigned)total, chosen, usedMask);
#endif
            return _slots[chosen].ptr;
        }

        void *p = heapCalloc(n, size);
#ifdef DEBUG
        if (pooledSize)
        {
            Serial.printf("TLSPOOL: calloc %u -> HEAP FALLBACK (pool full) ptr=%p used=0x%02X\n",
                          (unsigned)total, p, usedMask);
        }
#endif
        return p;
    }

    // Every request the pool sends to the heap goes through here, so a refusal is counted once,
    // in one place. mbedtls is told about it by the null pointer and nothing else: without the
    // counter a TLS handshake that died for lack of memory looks exactly like one that never
    // happened.
    //
    // This is also the whole of what mbedtls costs the heap - the two reserved slots are static
    // and cost the same whether TLS runs or not - so it is where the churn meter is fed. The size
    // comes from the allocator rather than from the request, because mbedtls releases by pointer
    // alone: the release path can only ask the allocator, and the two ends have to add and
    // subtract the same number or the total drifts.
    static void *heapCalloc(size_t n, size_t size)
    {
        void *p = calloc(n, size);
        if (p == nullptr)
        {
            AllocationFailureCounter::recordAtCallSite();
            return p;
        }
        TlsChurnMeter::instance().recordAllocated(heap_caps_get_allocated_size(p));
        return p;
    }

    static void poolFree(void *p)
    {
        if (p == nullptr)
        {
            return;
        }

        uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        for (int idx = 0; idx < kSlotCount; ++idx)
        {
            uintptr_t base = reinterpret_cast<uintptr_t>(_slots[idx].ptr);
            if (addr < base || addr >= base + _slots[idx].size)
            {
                continue;
            }

            uint8_t usedMask = 0;

            portENTER_CRITICAL(&_mux);
            _slots[idx].used = false;
            for (int i = 0; i < kSlotCount; ++i)
            {
                if (_slots[i].used)
                {
                    usedMask |= (1 << i);
                }
            }
            portEXIT_CRITICAL(&_mux);
            (void)usedMask;

#ifdef DEBUG
            Serial.printf("TLSPOOL: free slot%d used=0x%02X\n", idx, usedMask);
#endif
            return;
        }

        TlsChurnMeter::instance().recordFreed(heap_caps_get_allocated_size(p));
        free(p);
    }
};

template <class Profile>
alignas(kTlsSlotAlignmentBytes) uint8_t TlsMemoryPoolT<Profile>::_firstBuf[Profile::kFirstSlotBytes] = {};

template <class Profile>
alignas(kTlsSlotAlignmentBytes) uint8_t TlsMemoryPoolT<Profile>::_secondBuf[Profile::kSecondSlotBytes] = {};

template <class Profile>
typename TlsMemoryPoolT<Profile>::Slot TlsMemoryPoolT<Profile>::_slots[TlsMemoryPoolT<Profile>::kSlotCount] = {};

template <class Profile>
portMUX_TYPE TlsMemoryPoolT<Profile>::_mux = portMUX_INITIALIZER_UNLOCKED;

template <class Profile>
uint32_t TlsMemoryPoolT<Profile>::_heapFallbacks = 0;

// What main.cpp installs and what the counter report reads: the pool of the profile this build
// was compiled for.
typedef TlsMemoryPoolT<ActiveTlsBufferProfile> TlsMemoryPool;
