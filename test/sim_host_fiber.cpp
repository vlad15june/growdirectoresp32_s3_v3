// Implementation of Sim/HostFiber.h.
//
// It sits in the test root rather than next to its header because PlatformIO only compiles
// sources found directly in the test directory - a .cpp under test/Sim/ is silently not built
// and shows up as an undefined symbol at link time.
//
// _XOPEN_SOURCE must be defined before any system header is pulled in, which is why this
// translation unit includes nothing else first and why the ucontext machinery is confined to
// it. See HostFiber.h.
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <ucontext.h>

#include "Sim/HostFiber.h"

#include <cstdlib>
#include <new>

// makecontext/swapcontext are marked deprecated on macOS. They are still the only way to get
// a second stack without threads, and the alternative - std::thread with a handoff mutex -
// would put real concurrency under a test suite that has to be reproducible run after run.
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace hostsim {

// The fiber's control block and its stack are raw allocations, invisible to the allocation meter
// (Sim/HostAllocationCounter.h counts operator new only). Their sizes are decisions of the test
// bench - a host stack is sized by host frames, see HostTaskScheduler::kMinimumFiberStackBytes -
// so a meter that saw them would report, as the module's, memory the board does not have; and a
// task created inside a measured window would bury every figure under a quarter of a megabyte.
struct Fiber::Impl {
    ucontext_t context;
    ucontext_t resumerContext;
    char* stack = nullptr;
    size_t stackBytes = 0;
    Entry entry = nullptr;
    void* arg = nullptr;
    bool started = false;
    bool finished = false;

    // Frame pointer of fiberTrampoline(), taken once the fiber is running on its own stack.
    // Below it the chain is whatever makecontext built, which no walker can read, so this is
    // where a stack walk has to stop. See Sim/HostBacktrace.h.
    const void* entryFrame = nullptr;
};

namespace {

// makecontext can only pass ints to the entry point, so the fiber being started is handed
// over through this pointer instead. Safe because a fiber is only ever started from
// Fiber::resume(), which sets it immediately before switching in, and only one fiber runs at
// a time.
Fiber::Impl* g_starting = nullptr;

// The fiber that is running right now, so that suspendCurrent() knows where to switch back to.
Fiber::Impl* g_current = nullptr;

void fiberTrampoline()
{
    Fiber::Impl* self = g_starting;
    g_starting = nullptr;

    if (self != nullptr)
    {
        self->entryFrame = __builtin_frame_address(0);

        if (self->entry != nullptr)
        {
            self->entry(self->arg);
        }
        self->finished = true;
    }
    // Returning from here follows uc_link back to the resumer.
}

} // namespace

Fiber::Fiber(Entry entry, void* arg, size_t stackBytes)
    : _impl(nullptr)
{
    void* block = std::malloc(sizeof(Impl));
    // Zeroed, as the stack always was: nothing reads it before writing it, and a run that did
    // would read the same bytes every time.
    char* stack = static_cast<char*>(std::calloc(stackBytes, 1));
    if (block == nullptr || stack == nullptr)
    {
        std::abort();
    }
    _impl = new (block) Impl();
    _impl->entry = entry;
    _impl->arg = arg;
    _impl->stack = stack;
    _impl->stackBytes = stackBytes;
}

Fiber::~Fiber()
{
    std::free(_impl->stack);
    _impl->~Impl();
    std::free(_impl);
}

bool Fiber::finished() const
{
    return _impl->finished;
}

bool Fiber::currentStack(const char** low, const char** high, const void** entryFrame)
{
    Impl* self = g_current;
    if (self == nullptr || self->entryFrame == nullptr)
    {
        return false;
    }
    *low = self->stack;
    *high = self->stack + self->stackBytes;
    *entryFrame = self->entryFrame;
    return true;
}

void Fiber::resume()
{
    if (_impl->finished)
    {
        return;
    }

    Impl* previous = g_current;
    g_current = _impl;

    if (!_impl->started)
    {
        _impl->started = true;

        getcontext(&_impl->context);
        _impl->context.uc_stack.ss_sp = _impl->stack;
        _impl->context.uc_stack.ss_size = _impl->stackBytes;
        _impl->context.uc_link = &_impl->resumerContext;
        makecontext(&_impl->context, fiberTrampoline, 0);

        g_starting = _impl;
    }

    swapcontext(&_impl->resumerContext, &_impl->context);

    g_current = previous;
}

void Fiber::suspendCurrent()
{
    Impl* self = g_current;
    if (self == nullptr)
    {
        // Called outside any fiber: nothing to suspend.
        return;
    }
    swapcontext(&self->context, &self->resumerContext);
}

} // namespace hostsim
