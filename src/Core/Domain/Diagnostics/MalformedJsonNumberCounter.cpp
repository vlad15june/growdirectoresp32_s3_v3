#include "Core/Domain/Diagnostics/MalformedJsonNumberCounter.h"

// Constant-initialised, so the counter is usable before any static constructor has run: the stored
// registry is read on the way up.
std::atomic<uint32_t> MalformedJsonNumberCounter::_count(0);

void MalformedJsonNumberCounter::record()
{
    _count.fetch_add(1, std::memory_order_relaxed);
}

uint32_t MalformedJsonNumberCounter::count()
{
    return _count.load(std::memory_order_relaxed);
}
