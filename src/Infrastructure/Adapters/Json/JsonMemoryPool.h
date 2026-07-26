#pragma once

#include <Arduino.h>
#include <cstdint>
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

    void *allocate(size_t size)
    {
        if (size < kMinPooled)
        {
            void *p = malloc(size);
            Serial.printf("JSONPOOL: alloc %u -> heap(small) ptr=%p\n", (unsigned)size, p);
            return p;
        }

        int chosen = -1;
        uint8_t usedMask = 0;

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
        }
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (_slots[i].used)
            {
                usedMask |= (1 << i);
            }
        }
        portEXIT_CRITICAL(&_mux);

        if (chosen >= 0)
        {
            Serial.printf("JSONPOOL: alloc %u -> slot%d(%u) used=0x%02X\n",
                          (unsigned)size, chosen, (unsigned)_slots[chosen].size, usedMask);
            return _slots[chosen].ptr;
        }

        void *p = malloc(size);
        Serial.printf("JSONPOOL: alloc %u -> HEAP FALLBACK (pool full) ptr=%p used=0x%02X\n",
                      (unsigned)size, p, usedMask);
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
            Serial.printf("JSONPOOL: free heap ptr=%p\n", p);
            free(p);
            return;
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

        Serial.printf("JSONPOOL: free slot%d(%u) used=0x%02X\n",
                      idx, (unsigned)_slots[idx].size, usedMask);
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
    static const size_t kMinPooled = 4096;
    static const int kSlotCount = 4;

    struct Slot
    {
        uint8_t *ptr;
        size_t size;
        bool used;
    };

    uint8_t _buf16384[16384];
    uint8_t _buf8192a[8192];
    uint8_t _buf4096a[4096];
    uint8_t _buf4096b[4096];
    Slot _slots[kSlotCount];
    portMUX_TYPE _mux;

    JsonMemoryPool() : _mux(portMUX_INITIALIZER_UNLOCKED)
    {
        _slots[0] = {_buf16384, 16384, false};
        _slots[1] = {_buf8192a, 8192, false};
        _slots[2] = {_buf4096a, 4096, false};
        _slots[3] = {_buf4096b, 4096, false};
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
