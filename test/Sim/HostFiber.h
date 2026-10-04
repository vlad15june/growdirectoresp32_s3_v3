#pragma once

#include <cstddef>

namespace hostsim {

/**
 * @brief A suspendable call stack, used by HostTaskScheduler to stand in for an RTOS task.
 *
 * A host task has to be able to stop in the middle of a call - inside a network send, say -
 * and let another task run. Run-to-completion callbacks cannot express that, so each task
 * gets its own stack and the scheduler switches between them.
 *
 * The switching is done with ucontext. That is deliberately not visible here: on macOS
 * <ucontext.h> refuses to compile unless _XOPEN_SOURCE is defined before it, and defining a
 * libc feature macro halfway through a translation unit that has already included system
 * headers is a good way to produce inconsistent declarations. So the macro and the header
 * live alone in HostFiber.cpp, and this header exposes an opaque type.
 *
 * Single-threaded by construction: exactly one fiber runs at any moment and the scheduler
 * decides which. No threads, no locks, no ordering that changes between runs.
 */
class Fiber {
public:
    using Entry = void (*)(void* arg);

    Fiber(Entry entry, void* arg, size_t stackBytes);
    ~Fiber();

    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;

    /** @brief Switch into the fiber; returns when the fiber suspends or finishes. */
    void resume();

    /** @brief Switch out of the fiber that is running now, back to whoever resumed it. */
    static void suspendCurrent();

    /** @brief True once the entry function has returned. */
    bool finished() const;

    /**
     * @brief Where the running fiber's stack is, for anything that has to walk it.
     *
     * A fiber stack has no frame description under its entry point - makecontext builds that
     * frame - so a walker needs both the bounds to check against and the frame to stop at.
     * Both are known here because the stack is allocated here.
     *
     * @param low          Lowest address of the stack.
     * @param high         One past its highest address.
     * @param entryFrame   Frame pointer of the fiber's entry function: the bottom of the chain.
     * @return False when no fiber is running, in which case nothing is written.
     */
    static bool currentStack(const char** low, const char** high, const void** entryFrame);

    // Declared here and defined only in HostFiber.cpp: callers see an opaque name, and the
    // implementation file can name the type from its file-local helpers.
    struct Impl;

private:
    Impl* _impl;
};

} // namespace hostsim
