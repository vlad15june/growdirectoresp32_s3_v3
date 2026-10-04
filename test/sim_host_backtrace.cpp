// Implementation of Sim/HostBacktrace.h.
//
// It sits in the test root rather than next to its header for the same reason sim_host_fiber.cpp
// does: PlatformIO only compiles sources found directly in the test directory.

#include "Sim/HostBacktrace.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <pthread.h>
#include <unistd.h>

#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define HOST_BACKTRACE_HAS_SYMBOLS 1
#endif

#include "Sim/HostFiber.h"

namespace hostsim {
namespace {

// A frame is two words: the caller's frame pointer, then the address to return to. This is the
// layout both targets of this harness use - x86-64 pushes rbp then the return address, AArch64
// stores the pair {x29, x30} at x29 - and it is what -O0 guarantees is present.
const int kFramePointerSlot = 0;
const int kReturnAddressSlot = 1;

/**
 * @brief Bounds of the stack the caller is running on, and the frame to stop at.
 *
 * A fiber knows both. On the thread stack there is no entry frame to stop at, so the walk relies
 * on the bounds and on the chain itself ending; the C runtime's outermost frame does end it.
 */
bool stackBounds(const char** low, const char** high, const void** entryFrame)
{
    if (Fiber::currentStack(low, high, entryFrame))
    {
        return true;
    }

    *entryFrame = nullptr;

    // Asked once and kept: on glibc pthread_getattr_np() reads /proc/self/maps for the initial
    // thread, and that allocates. A meter that allocates while reporting an allocation would be
    // measuring itself.
    static const char* cachedLow = nullptr;
    static const char* cachedHigh = nullptr;
    static bool asked = false;
    if (asked)
    {
        *low = cachedLow;
        *high = cachedHigh;
        return cachedLow != nullptr;
    }
    asked = true;

    pthread_t self = pthread_self();
#if defined(__APPLE__)
    char* base = static_cast<char*>(pthread_get_stackaddr_np(self));  // highest address
    size_t size = pthread_get_stacksize_np(self);
    if (base == nullptr || size == 0) return false;
    cachedLow = base - size;
    cachedHigh = base;
#elif defined(__GLIBC__)
    pthread_attr_t attr;
    if (pthread_getattr_np(self, &attr) != 0) return false;
    void* base = nullptr;  // lowest address
    size_t size = 0;
    const int rc = pthread_attr_getstack(&attr, &base, &size);
    pthread_attr_destroy(&attr);
    if (rc != 0 || base == nullptr || size == 0) return false;
    cachedLow = static_cast<char*>(base);
    cachedHigh = static_cast<char*>(base) + size;
#else
    return false;
#endif
    *low = cachedLow;
    *high = cachedHigh;
    return true;
}

bool readable(const void* frame, const char* low, const char* high)
{
    const char* p = static_cast<const char*>(frame);
    if (p < low || p + 2 * sizeof(void*) > high) return false;
    return (reinterpret_cast<std::uintptr_t>(p) % sizeof(void*)) == 0;
}

}  // namespace

int captureBacktrace(void** frames, int maxFrames)
{
    if (frames == nullptr || maxFrames <= 0) return 0;

    const char* low = nullptr;
    const char* high = nullptr;
    const void* entryFrame = nullptr;
    if (!stackBounds(&low, &high, &entryFrame)) return 0;

    const void* frame = __builtin_frame_address(0);
    int count = 0;
    while (count < maxFrames && readable(frame, low, high))
    {
        // The entry frame returns into makecontext's own starter, which is a different function
        // on each platform and has no frame below it that can be read. The trace ends with the
        // frame above it, so that the same call chain has the same depth everywhere.
        if (entryFrame != nullptr && frame == entryFrame) break;

        void* const* words = static_cast<void* const*>(frame);
        void* returnAddress = words[kReturnAddressSlot];
        if (returnAddress == nullptr) break;
        frames[count++] = returnAddress;

        const void* next = words[kFramePointerSlot];
        // The chain runs towards higher addresses; anything else is not a frame pointer any more.
        if (next <= frame) break;
        frame = next;
    }
    return count;
}

void printBacktrace(void* const* frames, int count)
{
    if (frames == nullptr || count <= 0) return;
#ifdef HOST_BACKTRACE_HAS_SYMBOLS
    // The _fd form, not backtrace_symbols(): it writes address and name straight to the
    // descriptor without allocating, which matters because the allocation meter calls this from
    // inside an allocation.
    backtrace_symbols_fd(const_cast<void**>(frames), count, 2);
#else
    for (int i = 0; i < count; i++)
    {
        fprintf(stderr, "  %p\n", frames[i]);
    }
    fflush(stderr);
#endif
}

}  // namespace hostsim
