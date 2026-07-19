#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "mbedtls/platform.h"

class TlsMemoryPool
{
public:
    static void install()
    {
        for (int i = 0; i < kSlotCount; ++i)
        {
            _slots[i].ptr = _buf[i];
            _slots[i].used = false;
        }
        mbedtls_platform_set_calloc_free(poolCalloc, poolFree);
        Serial.printf("TLSPOOL: installed, %d slots x %u bytes = %u bytes reserved\n",
                      kSlotCount, (unsigned)kSlotSize, (unsigned)(kSlotCount * kSlotSize));
    }

private:
    static const size_t kSlotSize = 18432;
    static const size_t kMinPooled = 12288;
    static const int kSlotCount = 3;

    struct Slot
    {
        uint8_t *ptr;
        bool used;
    };

    static inline uint8_t _buf[kSlotCount][kSlotSize] = {};
    static inline Slot _slots[kSlotCount] = {};
    static inline portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

    static void *poolCalloc(size_t n, size_t size)
    {
        size_t total = n * size;
        if (size != 0 && total / size != n)
        {
            return nullptr;
        }

        if (total >= kMinPooled && total <= kSlotSize)
        {
            int chosen = -1;
            uint8_t usedMask = 0;

            portENTER_CRITICAL(&_mux);
            for (int i = 0; i < kSlotCount; ++i)
            {
                if (!_slots[i].used)
                {
                    chosen = i;
                    break;
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
                memset(_slots[chosen].ptr, 0, total);
                Serial.printf("TLSPOOL: calloc %u -> slot%d used=0x%02X\n",
                              (unsigned)total, chosen, usedMask);
                return _slots[chosen].ptr;
            }

            void *p = calloc(n, size);
            Serial.printf("TLSPOOL: calloc %u -> HEAP FALLBACK (pool full) ptr=%p used=0x%02X\n",
                          (unsigned)total, p, usedMask);
            return p;
        }

        void *p = calloc(n, size);
        if (total >= kMinPooled)
        {
            Serial.printf("TLSPOOL: calloc %u -> heap (over slot) ptr=%p\n", (unsigned)total, p);
        }
        return p;
    }

    static void poolFree(void *p)
    {
        if (p == nullptr)
        {
            return;
        }

        uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        uintptr_t base = reinterpret_cast<uintptr_t>(_buf);
        if (addr >= base && addr < base + sizeof(_buf))
        {
            int idx = static_cast<int>((addr - base) / kSlotSize);
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

            Serial.printf("TLSPOOL: free slot%d used=0x%02X\n", idx, usedMask);
            return;
        }

        free(p);
    }
};
