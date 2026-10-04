// Replacement global allocation operators for the native test binary - see
// Sim/HostAllocationCounter.h for the contract. Counting is the only added behaviour: storage
// still comes from malloc/free, failures still throw std::bad_alloc, so the binary allocates
// exactly as it would without the meter.

#include "Sim/HostAllocationCounter.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#include <unistd.h>

// Not the platform backtrace(): every task in this harness runs on a fiber, and the platform
// unwinder cannot walk a makecontext stack - see Sim/HostBacktrace.h.
#include "Sim/HostBacktrace.h"

namespace {
std::atomic<unsigned long long> g_newCalls{0};

// Lifetime totals, never reset: the difference between them is how many counted allocations are
// still held. reset() deliberately does not touch these - a caller measuring the traffic of one
// step must still be able to ask how the live balance moved across a whole run.
std::atomic<long long> g_totalNew{0};
std::atomic<long long> g_totalDelete{0};
std::atomic<long long> g_bytesNew{0};
std::atomic<long long> g_bytesDelete{0};
// High-water mark of the live balance, sampled where the balance can only have grown - an
// allocation. A release cannot raise a maximum, so the free path stays as it was.
std::atomic<long long> g_peakBytes{0};
std::atomic<bool> g_trace{false};
std::atomic<unsigned long long> g_tracedCalls{0};

// A red run of an allocation test can hit hundreds of allocations; a full backtrace for each
// buries the failing assertion under megabytes of log. The first traces locate the offender,
// the closing summary tells how many more there were.
const unsigned long long kMaxTracedBacktraces = 16;

// --- what each live block was asked for -----------------------------------------------------
//
// liveBytes() answers in requested bytes, so the size has to survive from the allocation to the
// matching release. Two ways not taken: a header in front of the block would move the address
// and its alignment, and asking the allocator on the way out (malloc_usable_size) is the very
// thing that made this meter platform-dependent. What is left is a table beside the heap.
//
// It runs on malloc/free, which these operators do not count, so the meter never reports its own
// bookkeeping. Open addressing with linear probing; a released slot keeps a tombstone so probe
// chains stay intact, and the table is rebuilt when it fills, which also sweeps the tombstones.
// Single-threaded by the same assumption the rest of this file makes - the simulation runs its
// tasks on cooperative fibers, on one thread.
struct SizeSlot {
    void* ptr;
    std::size_t size;
    // Which census bucket paid for this block, and which census it was: kNoOwner while no census
    // is running, so releasing such a block moves nothing. The generation tells a block tagged by
    // an earlier census from one this census handed out - the second run of the bench would
    // otherwise subtract the first run's bytes from its own freshly zeroed buckets.
    unsigned char owner;
    unsigned char generation;
};

const unsigned char kNoOwner = 0xFF;

void* const kTombstone = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));

SizeSlot* g_sizes = nullptr;
std::size_t g_sizesCapacity = 0;  // always a power of two
std::size_t g_sizesTaken = 0;     // live entries plus tombstones

std::size_t slotFor(void* p, std::size_t capacity) {
    std::uintptr_t h = reinterpret_cast<std::uintptr_t>(p);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 29;
    return static_cast<std::size_t>(h) & (capacity - 1);
}

void placeSize(SizeSlot* table, std::size_t capacity, void* p, std::size_t size,
               unsigned char owner, unsigned char generation) {
    std::size_t slot = slotFor(p, capacity);
    while (table[slot].ptr != nullptr && table[slot].ptr != kTombstone) {
        slot = (slot + 1) & (capacity - 1);
    }
    table[slot].ptr = p;
    table[slot].size = size;
    table[slot].owner = owner;
    table[slot].generation = generation;
}

// Grow (or, once the tombstones outnumber the live entries, merely rebuild) so that the table is
// never more than half full: linear probing degrades badly past that.
void growSizes() {
    std::size_t live = 0;
    for (std::size_t i = 0; i < g_sizesCapacity; i++) {
        if (g_sizes[i].ptr != nullptr && g_sizes[i].ptr != kTombstone) live++;
    }
    std::size_t capacity = 1024;
    while (capacity < live * 4) capacity *= 2;

    SizeSlot* table = static_cast<SizeSlot*>(std::calloc(capacity, sizeof(SizeSlot)));
    if (table == nullptr) return;  // out of memory here means the meter stops learning, not a crash
    for (std::size_t i = 0; i < g_sizesCapacity; i++) {
        if (g_sizes[i].ptr != nullptr && g_sizes[i].ptr != kTombstone) {
            placeSize(table, capacity, g_sizes[i].ptr, g_sizes[i].size, g_sizes[i].owner,
                      g_sizes[i].generation);
        }
    }
    std::free(g_sizes);
    g_sizes = table;
    g_sizesCapacity = capacity;
    g_sizesTaken = live;
}

void rememberSize(void* p, std::size_t size, unsigned char owner, unsigned char generation) {
    if (g_sizes == nullptr || (g_sizesTaken + 1) * 2 >= g_sizesCapacity) {
        growSizes();
        if (g_sizes == nullptr) return;
    }
    placeSize(g_sizes, g_sizesCapacity, p, size, owner, generation);
    g_sizesTaken++;
}

// How many bytes this block was asked for and who paid for them, and forget it. Returns false for
// a pointer this meter never handed out - a block allocated before these operators were linked in
// can still arrive here, and it must not subtract bytes that were never added.
bool forgetSize(void* p, std::size_t& size, unsigned char& owner, unsigned char& generation) {
    if (g_sizes == nullptr) return false;
    std::size_t slot = slotFor(p, g_sizesCapacity);
    for (std::size_t probed = 0; probed < g_sizesCapacity; probed++) {
        void* held = g_sizes[slot].ptr;
        if (held == nullptr) return false;
        if (held == p) {
            size = g_sizes[slot].size;
            owner = g_sizes[slot].owner;
            generation = g_sizes[slot].generation;
            g_sizes[slot].ptr = kTombstone;
            return true;
        }
        slot = (slot + 1) & (g_sizesCapacity - 1);
    }
    return false;
}

// --- who asked for it ------------------------------------------------------------------------
//
// One bucket per owner, and a stack of tags whose top is the owner of the next allocation. Both
// live beside the heap on plain globals for the same reason the size table does: bookkeeping that
// allocated would be measuring itself.

const int kOwnerCount = (int)HostAllocationCensus::kOwnerCount;
const unsigned int kMaxTagDepth = sizeof(HostAllocationCensus::Context::owners);

struct OwnerBucket {
    long long liveBytes;
    long long liveBlocks;
    long long peakBytes;
    unsigned long long peakAtMs;
    const char* peakLabel;
};

OwnerBucket g_buckets[kOwnerCount];
long long g_moduleLiveBytes = 0;
long long g_modulePeakBytes = 0;
unsigned long long g_modulePeakAtMs = 0;
const char* g_modulePeakLabel = "";

unsigned char g_tagStack[kMaxTagDepth] = {(unsigned char)HostAllocationCensus::kOther};
unsigned int g_tagDepth = 1;

bool g_censusRunning = false;
unsigned char g_generation = 0;
unsigned long long g_unpairedReleases = 0;
unsigned long long g_overflowedPushes = 0;
unsigned long long g_momentMs = 0;
const char* g_momentLabel = "";

bool isModuleBucket(int owner) {
    return owner != (int)HostAllocationCensus::kBench &&
           owner != (int)HostAllocationCensus::kProbe;
}

// Charge a fresh block to whoever is on top of the tag stack. Returns the tag written into the
// block's slot, or kNoOwner when no census is running.
unsigned char censusCharge(std::size_t size) {
    if (!g_censusRunning) return kNoOwner;

    const unsigned int top = (g_tagDepth < kMaxTagDepth) ? g_tagDepth : kMaxTagDepth;
    const unsigned char owner = g_tagStack[top - 1];
    OwnerBucket& bucket = g_buckets[owner];
    bucket.liveBytes += (long long)size;
    bucket.liveBlocks++;
    if (bucket.liveBytes > bucket.peakBytes) {
        bucket.peakBytes = bucket.liveBytes;
        bucket.peakAtMs = g_momentMs;
        bucket.peakLabel = g_momentLabel;
    }
    if (isModuleBucket(owner)) {
        g_moduleLiveBytes += (long long)size;
        if (g_moduleLiveBytes > g_modulePeakBytes) {
            g_modulePeakBytes = g_moduleLiveBytes;
            g_modulePeakAtMs = g_momentMs;
            g_modulePeakLabel = g_momentLabel;
        }
    }
    return owner;
}

// Give the bytes back to the owner that paid for them - and only if that owner paid them during
// this census. A block from an earlier one is counted apart instead.
void censusRelease(unsigned char owner, unsigned char generation, std::size_t size) {
    if (owner == kNoOwner) return;
    if (!g_censusRunning || generation != g_generation) {
        g_unpairedReleases++;
        return;
    }
    OwnerBucket& bucket = g_buckets[owner];
    bucket.liveBytes -= (long long)size;
    bucket.liveBlocks--;
    if (isModuleBucket(owner)) {
        g_moduleLiveBytes -= (long long)size;
    }
}

void* countedAlloc(std::size_t size) {
    g_newCalls.fetch_add(1, std::memory_order_relaxed);
    g_totalNew.fetch_add(1, std::memory_order_relaxed);
    if (g_trace.load(std::memory_order_relaxed)) {
        unsigned long long traced = g_tracedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (traced <= kMaxTracedBacktraces) {
            void* frames[32];
            const int n = hostsim::captureBacktrace(frames, 32);
            write(2, "=== alloc backtrace ===\n", 24);
            hostsim::printBacktrace(frames, n);
        }
    }
    void* p = std::malloc(size ? size : 1);
    if (p != nullptr) {
        const long long held = g_bytesNew.fetch_add((long long)size, std::memory_order_relaxed) +
                               (long long)size - g_bytesDelete.load(std::memory_order_relaxed);
        if (held > g_peakBytes.load(std::memory_order_relaxed)) {
            g_peakBytes.store(held, std::memory_order_relaxed);
        }
        rememberSize(p, size, censusCharge(size), g_generation);
    }
    return p;
}

// Paired with countedAlloc(): a null pointer is not a release of anything, and deleting a null
// is legal and common, so it must not push the balance negative.
void countedFree(void* p) {
    if (p != nullptr) {
        g_totalDelete.fetch_add(1, std::memory_order_relaxed);
        std::size_t size = 0;
        unsigned char owner = kNoOwner;
        unsigned char generation = 0;
        if (forgetSize(p, size, owner, generation)) {
            g_bytesDelete.fetch_add((long long)size, std::memory_order_relaxed);
            censusRelease(owner, generation, size);
        }
    }
    std::free(p);
}
}  // namespace

namespace HostAllocationCounter {

void reset() { g_newCalls.store(0, std::memory_order_relaxed); }

void setTrace(bool on) {
    if (on) {
        g_tracedCalls.store(0, std::memory_order_relaxed);
        g_trace.store(true, std::memory_order_relaxed);
        return;
    }
    g_trace.store(false, std::memory_order_relaxed);
    unsigned long long traced = g_tracedCalls.load(std::memory_order_relaxed);
    if (traced > kMaxTracedBacktraces) {
        // The summary itself is not counted: whatever fprintf allocates goes through malloc,
        // and this counter only replaces operator new. Same stream as the backtraces.
        fprintf(stderr, "=== %llu more allocations not traced (limit %llu backtraces) ===\n",
                traced - kMaxTracedBacktraces, kMaxTracedBacktraces);
    }
}

unsigned long long newCount() { return g_newCalls.load(std::memory_order_relaxed); }

long long liveCount() {
    return g_totalNew.load(std::memory_order_relaxed) -
           g_totalDelete.load(std::memory_order_relaxed);
}

long long liveBytes() {
    return g_bytesNew.load(std::memory_order_relaxed) -
           g_bytesDelete.load(std::memory_order_relaxed);
}

void resetPeak() { g_peakBytes.store(liveBytes(), std::memory_order_relaxed); }

long long peakBytes() { return g_peakBytes.load(std::memory_order_relaxed); }

}  // namespace HostAllocationCounter

namespace HostAllocationCensus {

const char* ownerName(Owner owner) {
    switch (owner) {
        case kOther: return "other";
        case kSensors: return "sensors/modbus";
        case kDevices: return "devices/relays";
        case kRules: return "rules";
        case kCatalog: return "sensor-type catalog";
        case kOutbox: return "outbox";
        case kCommands: return "commands";
        case kDiagnostics: return "diagnostics";
        case kStructure: return "module structure";
        case kOta: return "ota/recovery";
        case kMqtt: return "mqtt";
        case kJson: return "json";
        case kRest: return "rest transport";
        case kWebServers: return "web servers";
        case kTls: return "tls (off pool)";
        case kBench: return "bench harness";
        case kProbe: return "probe";
        default: return "?";
    }
}

bool isModuleOwner(Owner owner) { return isModuleBucket((int)owner); }

void begin() {
    for (int i = 0; i < kOwnerCount; i++) {
        g_buckets[i].liveBytes = 0;
        g_buckets[i].liveBlocks = 0;
        g_buckets[i].peakBytes = 0;
        g_buckets[i].peakAtMs = 0;
        g_buckets[i].peakLabel = "";
    }
    g_moduleLiveBytes = 0;
    g_modulePeakBytes = 0;
    g_modulePeakAtMs = 0;
    g_modulePeakLabel = "";
    g_unpairedReleases = 0;
    g_overflowedPushes = 0;
    g_momentMs = 0;
    g_momentLabel = "";
    g_tagDepth = 1;
    g_tagStack[0] = (unsigned char)kOther;
    g_generation++;
    g_censusRunning = true;
}

void end() { g_censusRunning = false; }

bool running() { return g_censusRunning; }

void pushOwner(Owner owner) {
    // The depth counts every push, including the ones with no room left to record them, so that
    // pushes and pops stay paired whatever happens. What a push past the end costs is precision -
    // allocations keep charging the deepest tag that did fit - and the count says so.
    if (g_tagDepth < kMaxTagDepth) {
        g_tagStack[g_tagDepth] = (unsigned char)owner;
    } else {
        g_overflowedPushes++;
    }
    g_tagDepth++;
}

void popOwner() {
    if (g_tagDepth > 1) g_tagDepth--;
}

Owner retagScope(Owner owner) {
    const unsigned int top = (g_tagDepth < kMaxTagDepth) ? g_tagDepth : kMaxTagDepth;
    const Owner previous = (Owner)g_tagStack[top - 1];
    g_tagStack[top - 1] = (unsigned char)owner;
    return previous;
}

void setMoment(unsigned long long ms, const char* label) {
    g_momentMs = ms;
    g_momentLabel = (label != nullptr) ? label : "";
}

long long liveBytes(Owner owner) { return g_buckets[(int)owner].liveBytes; }
long long liveBlocks(Owner owner) { return g_buckets[(int)owner].liveBlocks; }
long long peakBytes(Owner owner) { return g_buckets[(int)owner].peakBytes; }
unsigned long long peakAtMs(Owner owner) { return g_buckets[(int)owner].peakAtMs; }
const char* peakLabel(Owner owner) { return g_buckets[(int)owner].peakLabel; }

void rearmPeaks() {
    for (int i = 0; i < kOwnerCount; i++) {
        g_buckets[i].peakBytes = g_buckets[i].liveBytes;
        g_buckets[i].peakAtMs = g_momentMs;
        g_buckets[i].peakLabel = g_momentLabel;
    }
    g_modulePeakBytes = g_moduleLiveBytes;
    g_modulePeakAtMs = g_momentMs;
    g_modulePeakLabel = g_momentLabel;
}

long long moduleLiveBytes() { return g_moduleLiveBytes; }
long long modulePeakBytes() { return g_modulePeakBytes; }
unsigned long long modulePeakAtMs() { return g_modulePeakAtMs; }
const char* modulePeakLabel() { return g_modulePeakLabel; }

unsigned long long unpairedReleases() { return g_unpairedReleases; }
unsigned long long overflowedPushes() { return g_overflowedPushes; }

void initContext(Context& context, Owner base) {
    context.depth = 1;
    context.owners[0] = (unsigned char)base;
    for (unsigned int i = 1; i < kMaxTagDepth; i++) context.owners[i] = (unsigned char)kOther;
}

void swapContext(Context& context) {
    unsigned char parked[kMaxTagDepth];
    const unsigned int parkedDepth = context.depth;
    for (unsigned int i = 0; i < kMaxTagDepth; i++) parked[i] = context.owners[i];

    context.depth = (unsigned short)g_tagDepth;
    for (unsigned int i = 0; i < kMaxTagDepth; i++) context.owners[i] = g_tagStack[i];

    g_tagDepth = (parkedDepth == 0) ? 1 : parkedDepth;
    for (unsigned int i = 0; i < kMaxTagDepth; i++) g_tagStack[i] = parked[i];
}

}  // namespace HostAllocationCensus

void* operator new(std::size_t size) {
    void* p = countedAlloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size) {
    void* p = countedAlloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return countedAlloc(size); }

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return countedAlloc(size); }

void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { countedFree(p); }

#if defined(__cpp_aligned_new)
void* operator new(std::size_t size, std::align_val_t align) {
    g_newCalls.fetch_add(1, std::memory_order_relaxed);
    g_totalNew.fetch_add(1, std::memory_order_relaxed);
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(align), size ? size : 1) != 0) {
        throw std::bad_alloc();
    }
    const long long held = g_bytesNew.fetch_add((long long)size, std::memory_order_relaxed) +
                           (long long)size - g_bytesDelete.load(std::memory_order_relaxed);
    if (held > g_peakBytes.load(std::memory_order_relaxed)) {
        g_peakBytes.store(held, std::memory_order_relaxed);
    }
    rememberSize(p, size, censusCharge(size), g_generation);
    return p;
}

void* operator new[](std::size_t size, std::align_val_t align) {
    return operator new(size, align);
}

void operator delete(void* p, std::align_val_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }
#endif
