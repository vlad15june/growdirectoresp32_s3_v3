#ifndef HOST_ALLOCATION_COUNTER_H
#define HOST_ALLOCATION_COUNTER_H

// Heap traffic meter for the native test binary.
//
// The replacement global operator new/new[] (defined in sim_host_alloc_counter.cpp) count every
// C++ heap allocation made by the whole test executable - std::string, std::vector, std::map
// nodes, everything that goes through the default allocator. Raw malloc() calls are NOT counted;
// none of the paths measured with this meter allocate that way.
//
// Usage in a test: bring the scenario to its steady state first (warm-up ticks), then reset(),
// run the measured step, and read newCount(). The meter is process-global, so the measured step
// must not overlap other threads' allocations; the simulation tests are single-threaded unless
// they start host fibers explicitly.

namespace HostAllocationCounter {

// Zero the counter. Only resets the number, nothing about the heap itself.
void reset();

// Number of global operator new/new[] calls since the last reset().
unsigned long long newCount();

// How many counted allocations are still held: every operator new ever made by this binary minus
// every operator delete. Not affected by reset(), because it answers a different question -
// newCount() is traffic over one measured step, liveCount() is what the process is holding right
// now. A run whose traffic per hour is flat and whose live balance climbs is leaking; without
// this number the two are indistinguishable.
//
// Signed on purpose: memory allocated before these operators were linked in can still be freed
// through them, so the balance may legitimately dip below zero. What is meaningful is how it
// moves between two points of the same run, never its absolute value.
long long liveCount();

// How many bytes of counted allocations are still held, counted as the program asked for them.
// The companion of liveCount() and the one that notices a leak of few blocks and many bytes: a
// container that doubles its way up to megabytes is one live allocation, so a count alone would
// call it healthy. Same signed-and-relative caveat as liveCount().
//
// Requested bytes, not the block size the allocator reports, and that is the whole point: on
// glibc the reported block size is not a function of the request. A request that needs a 48-byte
// chunk is served out of a free 64-byte one whenever splitting it would leave a remainder below
// the smallest chunk the allocator can make, and malloc_usable_size() then answers 56 instead of
// 40 for the very same std::string. Which of the two happens depends on the shape of the heap,
// which depends on everything the process did earlier - so a total built from it measures the
// allocator's history as much as the program's memory, and two runs of the same code disagree.
// macOS answers from the size class of the request and does not have this property, which is why
// a difference here shows up as a Linux-only failure.
//
// What the program asked for does not depend on the heap, which is what makes an equality
// between two points of one run mean something. It is not a number to compare across standard
// libraries: libstdc++ asks for capacity+1 where libc++ rounds a string's capacity up to a
// multiple of sixteen, so the same containers report different totals under each - the same
// endurance run reports about 1.43 MB held under GCC and about 1.32 MB under clang. Nothing
// here compares the two; every assertion compares a run with itself.
long long liveBytes();

// Start a new high-water measurement: the peak is set to the live balance of this moment.
void resetPeak();

// The largest liveBytes() ever observed since resetPeak(), sampled at every allocation.
//
// What liveBytes() cannot answer: a step that allocates 40 KB, frees it and returns leaves the
// balance where it found it, and the board it runs on still had to have those 40 KB free at once.
// The number is only meaningful against another number of the same run - the live balance before
// the step, or the one after it - because its absolute value carries everything the process was
// already holding. Peak minus the balance at the end of a step is what that step needed on top of
// what it produced.
long long peakBytes();

// Print a backtrace of counted allocations to stderr while enabled - the first few in full
// (see kMaxTracedBacktraces in the implementation), then one closing summary line with the
// number of the rest, emitted when tracing is switched off. Meant to be switched on around
// the measured step: a passing zero-allocation test prints nothing, and a regression shows
// where the new allocations came from instead of just a number. Traces are taken with
// Sim/HostBacktrace.h, which can walk a fiber stack - the platform unwinder cannot, and every
// task in the simulation runs on one.
void setTrace(bool on);

}  // namespace HostAllocationCounter

// Attribution of the same allocations to the subsystem that asked for them.
//
// The counter above answers "how much"; this answers "who". Both read the same events - every
// counted allocation is charged to exactly one owner and every matching release gives those bytes
// back to the same owner - so the buckets are a partition of the meter's totals, not a second
// measurement beside them.
//
// Who is decided by a tag that the harness pushes around a call, not by anything in src/. Two
// kinds of tag exist and they nest:
//
//   - a base tag per simulated task, set by HostTaskScheduler when it resumes that task's fiber.
//     Everything the outbox network task allocates is the outbox's, whoever called it;
//   - a scope around a call into a subsystem, entered by a forwarding relay the harness injects
//     where the composition root injects the real object. The innermost tag wins, so a converter
//     called from the catalog sync charges the converter, not the catalog.
//
// The tag stack belongs to the task that is running, not to the process: the tasks here are
// cooperative fibers and one of them can be suspended in the middle of a scope, so the scheduler
// swaps the whole stack when it switches. Without that a scope entered on the control loop would
// keep charging while the outbox task ran.
//
// What is NOT attributed: everything allocated before begin() and after end(). Such a block
// carries no owner, and releasing it moves no bucket - so the buckets never go negative from
// blocks the census did not see. A block tagged by an earlier census and released during a later
// one is counted in unpairedReleases() instead of being subtracted from a bucket that never
// gained it.
namespace HostAllocationCensus {

/**
 * @brief The subsystems this harness can tell apart.
 *
 * kOther is not a subsystem: it is what a module allocation gets when no tag covers it, and its
 * share is the measure of how complete the census is. kBench is the harness itself - the sample
 * vectors, the simulated line, the fixture - and is excluded from the module totals for the same
 * reason a scale is tared. kProbe belongs to the census's own tests.
 *
 * kWebServers and kTls exist and stay at zero here on purpose: neither is in the host graph (both
 * name device-only headers), and a bucket that is visibly empty says that better than a missing
 * row does.
 */
enum Owner {
    kOther = 0,
    kSensors,
    kDevices,
    kRules,
    kCatalog,
    kOutbox,
    kCommands,
    kDiagnostics,
    kStructure,
    kOta,
    kMqtt,
    kJson,
    kRest,
    kWebServers,
    kTls,
    kBench,
    kProbe,
    kOwnerCount
};

/** @brief Short name of an owner, as it appears in the census table. */
const char* ownerName(Owner owner);

/** @brief Is this owner part of the module, as opposed to the harness around it? */
bool isModuleOwner(Owner owner);

/**
 * @brief Start attributing, from a clean set of buckets.
 *
 * Bumps a generation, so blocks tagged by a previous census are recognised as foreign when they
 * are released rather than subtracted from buckets that never gained them.
 */
void begin();

/** @brief Stop attributing. Buckets keep their values so they can still be read. */
void end();

bool running();

void pushOwner(Owner owner);
void popOwner();

/** @brief Push an owner for the extent of a scope. */
class Scope {
public:
    explicit Scope(Owner owner) { pushOwner(owner); }
    ~Scope() { popOwner(); }

private:
    Scope(const Scope&);
    Scope& operator=(const Scope&);
};

/**
 * @brief Replace the owner of the innermost open scope, and answer the one it replaced.
 *
 * Unlike a push, this does not deepen the stack: it renames the frame that is already there.
 * That is what makes PhaseMarker possible, and it is the only way to tag a member initializer
 * list, where there is nowhere to put a scope.
 */
Owner retagScope(Owner owner);

/**
 * @brief Charge the members declared after it, up to the next marker, to one owner.
 *
 * A constructor's member initializer list is the one place a scope cannot reach: the members run
 * before the body and nothing of ours sits between them. Declaring a marker among them works
 * because the order is fixed by the language - members are constructed in declaration order and
 * destroyed in reverse - so a marker that renames the current frame on construction and puts the
 * previous name back on destruction brackets exactly the members that follow it, on both ends.
 *
 * This matters more than it looks. A graph built inside a single bench scope charges every
 * buffer the module reserves up front - and this firmware reserves its bounded queues to their
 * cap in constructors - to the harness, which is how a census can quietly answer a third less
 * than the module holds.
 */
class PhaseMarker {
public:
    explicit PhaseMarker(Owner owner) : _previous(retagScope(owner)) {}
    ~PhaseMarker() { retagScope(_previous); }

private:
    Owner _previous;

    PhaseMarker(const PhaseMarker&);
    PhaseMarker& operator=(const PhaseMarker&);
};

/**
 * @brief What the run is doing right now, stamped onto every peak recorded from here on.
 *
 * A peak without a moment is a number nobody can act on. The label is stored by pointer, so it
 * has to outlive the run - a string literal, which is what the bench passes.
 */
void setMoment(unsigned long long ms, const char* label);

long long liveBytes(Owner owner);
long long liveBlocks(Owner owner);

/** @brief The largest liveBytes() this owner ever held since begin(), and where it happened. */
long long peakBytes(Owner owner);
unsigned long long peakAtMs(Owner owner);
const char* peakLabel(Owner owner);

/**
 * @brief Start every peak again from the level being held right now.
 *
 * What a run-long peak measures on a module that is still filling its caches is mostly that
 * filling. Rearming at the top of a measured window turns the peak into the question that matters
 * for a heap: how much did this owner need at once, on top of what it was already holding, inside
 * this window. The moment and the label are restamped from the current ones.
 */
void rearmPeaks();

/** @brief The sum over the module owners - the harness's own bytes are not in it. */
long long moduleLiveBytes();
long long modulePeakBytes();
unsigned long long modulePeakAtMs();
const char* modulePeakLabel();

/**
 * @brief Releases of blocks that carried a tag from an earlier census.
 *
 * Not an error and not lost bytes: it is how much of what the previous run held was still being
 * given back during this one. A non-zero number here means the two runs overlap in the heap, and
 * the buckets of this one are about this one only.
 */
unsigned long long unpairedReleases();

/** @brief Pushes that exceeded the depth the stack has room for. Must stay at zero. */
unsigned long long overflowedPushes();

/**
 * @brief The tag stack of one simulated task, parked while the task is not running.
 *
 * Plain storage with no allocation of its own: the scheduler keeps one per task and swaps it in
 * around the fiber resume.
 */
struct Context {
    unsigned short depth;
    unsigned char owners[12];
};

void initContext(Context& context, Owner base);

/** @brief Exchange the live tag stack with the one parked in `context`. */
void swapContext(Context& context);

}  // namespace HostAllocationCensus

#endif  // HOST_ALLOCATION_COUNTER_H
