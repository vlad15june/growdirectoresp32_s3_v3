#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ArduinoJson.h>

class JsonMemoryPool
{
public:
    static JsonMemoryPool &instance()
    {
        static JsonMemoryPool inst;
        return inst;
    }

    // How many document requests a slot served.
    uint32_t pooledServed() const { return _pooledServed; }

    // How many requests were smaller than the smallest slot and went straight to the heap. These
    // are the documents the pool is not shaped for; a rising count means a new call site asks for
    // a capacity no tier covers.
    uint32_t heapBypasses() const { return _heapBypasses; }

    // How many requests a slot could have served but none was free. The fallback defeats the
    // pool's purpose - a heap block the size of a document is exactly what the pool exists to
    // keep out of the heap - so it is counted, and reported as one line where it happens.
    uint32_t heapFallbacks() const { return _heapFallbacks; }

    // The most slots ever held at the same time since the last reset: what the geometry has to
    // cover for the pool to answer every request from a slot.
    int peakSlotsInUse() const { return _peakSlotsInUse; }

    void resetPeakSlotsInUse()
    {
        portENTER_CRITICAL(&_mux);
        _peakSlotsInUse = countUsedLocked();
        portEXIT_CRITICAL(&_mux);
    }

    /** @brief Size of the buffer formatHeapEvent() needs, terminator included. */
    static const size_t kEventLineBytes = 96;

    /**
     * @brief Write the one line a request that missed the pool is reported as.
     *
     * The counter is named rather than described: it is the name the class gives the number, so
     * a line and a reading of that counter cannot drift apart. `n` is that counter's value with
     * this event included, which makes a gap in a log a lost line rather than a quiet run, and
     * `slots` says how full the pool was at the moment of the miss - the difference between a
     * tier that is the wrong shape and a tier that was simply all taken.
     *
     * @return the number of characters the line needs, terminator excluded - as snprintf does,
     *         so a caller can tell a truncated line from a complete one.
     */
    static size_t formatHeapEvent(const char *counter, uint32_t requestedBytes, uint32_t count,
                                  int slotsInUse, bool served, char *out, size_t outBytes)
    {
        const int slotCount = kSlotCount;
        const int needed = snprintf(out, outBytes,
                                    "JSON_POOL: counter=%s size=%lu n=%lu slots=%d/%d heap=%s",
                                    counter, (unsigned long)requestedBytes, (unsigned long)count,
                                    slotsInUse, slotCount, served ? "ok" : "null");
        return needed < 0 ? 0 : (size_t)needed;
    }

    void *allocate(size_t size)
    {
        if (size < kMinPooled)
        {
            void *p = malloc(size);
            portENTER_CRITICAL(&_mux);
            const uint32_t bypasses = ++_heapBypasses;
            const int inUseAtBypass = countUsedLocked();
            portEXIT_CRITICAL(&_mux);
            reportHeapEvent("heapBypasses", size, bypasses, inUseAtBypass, p != nullptr);
#ifdef DEBUG
            Serial.printf("JSONPOOL: alloc %u -> heap(small) ptr=%p\n", (unsigned)size, p);
#endif
            return p;
        }

        int chosen = -1;
        // The mask feeds only debug prints, but it is computed in every build so that debug and
        // release take the identical locked path.
        uint16_t usedMask = 0;
        uint32_t fallbacks = 0;

        portENTER_CRITICAL(&_mux);
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (!_slots[i].used && _slots[i].size >= size)
            {
                if (chosen < 0 || _slots[i].size < _slots[chosen].size)
                {
                    chosen = i;
                }
            }
        }
        if (chosen >= 0)
        {
            _slots[chosen].used = true;
            ++_pooledServed;
        }
        else
        {
            fallbacks = ++_heapFallbacks;
        }
        int inUse = countUsedLocked();
        if (inUse > _peakSlotsInUse)
        {
            _peakSlotsInUse = inUse;
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
            // Per-allocation prints run on every conversion, including the ones the control loop
            // makes, so they exist only in debug builds; a release build accounts for the traffic
            // through the counters above.
#ifdef DEBUG
            Serial.printf("JSONPOOL: alloc %u -> slot%d(%u) used=0x%04X\n",
                          (unsigned)size, chosen, (unsigned)_slots[chosen].size, usedMask);
#endif
            return _slots[chosen].ptr;
        }

        void *p = malloc(size);
        // inUse is the occupancy the request found: the slot it wanted was taken, so nothing was
        // marked used on its behalf.
        reportHeapEvent("heapFallbacks", size, fallbacks, inUse, p != nullptr);
#ifdef DEBUG
        Serial.printf("JSONPOOL: alloc %u -> HEAP FALLBACK (pool full) ptr=%p used=0x%04X\n",
                      (unsigned)size, p, usedMask);
#endif
        return p;
    }

    void deallocate(void *p)
    {
        if (p == nullptr)
        {
            return;
        }

        int idx = slotIndexOf(p);
        if (idx < 0)
        {
#ifdef DEBUG
            Serial.printf("JSONPOOL: free heap ptr=%p\n", p);
#endif
            free(p);
            return;
        }

        uint16_t usedMask = 0;
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
        Serial.printf("JSONPOOL: free slot%d(%u) used=0x%04X\n",
                      idx, (unsigned)_slots[idx].size, usedMask);
#endif
    }

    void *reallocate(void *p, size_t size)
    {
        if (p == nullptr)
        {
            return allocate(size);
        }

        int idx = slotIndexOf(p);
        if (idx < 0)
        {
            if (size < kMinPooled)
            {
                return realloc(p, size);
            }
            void *q = allocate(size);
            if (q != nullptr)
            {
                memcpy(q, p, size);
                free(p);
            }
            return q;
        }

        if (size <= _slots[idx].size)
        {
            return p;
        }

        void *q = allocate(size);
        if (q != nullptr)
        {
            memcpy(q, p, _slots[idx].size);
            deallocate(p);
        }
        return q;
    }

private:
    // The smallest capacity the pool answers. It is the smallest slot: below it there is nothing
    // to hand out, so the request goes to the heap and is counted as a bypass.
    static const size_t kMinPooled = 256;
    static const int kSlotCount = 10;

    struct Slot
    {
        uint8_t *ptr;
        size_t size;
        bool used;
    };

    // One tier per capacity the converters ask for, and as many slots per tier as documents of
    // that size are alive at once (the deepest chain nests an array document, a per-element
    // document and the document the element is re-parsed into, while another task holds its own).
    // Small documents belong here as much as large ones: they are short-lived blocks of varying
    // size, and leaving them on the heap is what puts holes between the large allocations the
    // pool already keeps out of it.
    uint8_t _buf16384[16384];
    uint8_t _buf8192a[8192];
    uint8_t _buf4096a[4096];
    uint8_t _buf4096b[4096];
    uint8_t _buf2048a[2048];
    uint8_t _buf2048b[2048];
    uint8_t _buf1024a[1024];
    uint8_t _buf512a[512];
    uint8_t _buf512b[512];
    uint8_t _buf256a[256];
    Slot _slots[kSlotCount];
    portMUX_TYPE _mux;
    uint32_t _pooledServed;
    uint32_t _heapBypasses;
    uint32_t _heapFallbacks;
    int _peakSlotsInUse;

    JsonMemoryPool()
        : _mux(portMUX_INITIALIZER_UNLOCKED),
          _pooledServed(0),
          _heapBypasses(0),
          _heapFallbacks(0),
          _peakSlotsInUse(0)
    {
        _slots[0] = {_buf16384, 16384, false};
        _slots[1] = {_buf8192a, 8192, false};
        _slots[2] = {_buf4096a, 4096, false};
        _slots[3] = {_buf4096b, 4096, false};
        _slots[4] = {_buf2048a, 2048, false};
        _slots[5] = {_buf2048b, 2048, false};
        _slots[6] = {_buf1024a, 1024, false};
        _slots[7] = {_buf512a, 512, false};
        _slots[8] = {_buf512b, 512, false};
        _slots[9] = {_buf256a, 256, false};
    }

    /**
     * @brief Report one request the slots did not serve.
     *
     * Rare by construction: the tiers cover every capacity the converters ask for, and every one
     * of them is answered from a slot as long as one is free - so a line here is an event and not
     * traffic. The path that works prints nothing at all; it runs under every conversion the
     * control loop makes.
     *
     * Called after the critical section, never inside it: that section runs with interrupts off,
     * and a UART write there would hold the core for the length of the line. The line is built on
     * the stack for the same reason a print of a heap event should not itself ask for heap.
     */
    static void reportHeapEvent(const char *counter, size_t requestedBytes, uint32_t count,
                                int slotsInUse, bool served)
    {
        char line[kEventLineBytes];
        formatHeapEvent(counter, (uint32_t)requestedBytes, count, slotsInUse, served, line,
                        sizeof(line));
        Serial.println(line);
    }

    // Callers hold _mux.
    int countUsedLocked() const
    {
        int inUse = 0;
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (_slots[i].used)
            {
                ++inUse;
            }
        }
        return inUse;
    }

    int slotIndexOf(void *p)
    {
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (_slots[i].ptr == p)
            {
                return i;
            }
        }
        return -1;
    }
};

struct JsonPoolAllocator
{
    void *allocate(size_t n)
    {
        return JsonMemoryPool::instance().allocate(n);
    }

    void deallocate(void *p)
    {
        JsonMemoryPool::instance().deallocate(p);
    }

    void *reallocate(void *p, size_t n)
    {
        return JsonMemoryPool::instance().reallocate(p, n);
    }
};

using PooledJsonDocument = ArduinoJson::BasicJsonDocument<JsonPoolAllocator>;
