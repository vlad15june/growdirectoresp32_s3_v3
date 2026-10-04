#pragma once

/**
 * @brief Stack traces that survive being taken on a fiber stack.
 *
 * The platform backtrace() cannot be used here. It unwinds with the frame descriptions the
 * compiler emits, and a fiber's outermost frame is built by makecontext, which leaves none. Two
 * things follow, both measured on aarch64 Linux with glibc 2.35.
 *
 * It wants kilobytes of stack of its own, and a fiber stack is a heap block. The tasks whose
 * allocations one would want to trace - sensor-acq, outbox-net, cmd-net, the catalog writer - run
 * on DEFAULT_STACK_BYTES, which is 8 KB; the roomy stacks in this harness belong to its own
 * stand-ins, not to the firmware. On 8 KB a chain of ten 256-byte frames is already too deep: the
 * same chain reaches twenty-four frames when nothing traces it and dies at twenty-six, and dies at
 * ten with backtrace() in the leaf. What it dies of is whatever the overrun lands in - "double
 * free or corruption", "free(): invalid pointer", SIGSEGV, SIGBUS - because past the end of the
 * stack is the allocator's bookkeeping. So there is no safe stack size to name: what decides it is
 * how much of the stack the call chain has already spent.
 *
 * And it walks one frame too far, past the fiber entry into makecontext's starter in libc. It does
 * not wander into the stack of whoever resumed the fiber - that was checked frame by frame - so
 * the trace is not wild, only wrong at its end: eight frames where the walk below gives seven for
 * the same chain. On macOS it gives up at the fiber entry and returns nothing at all.
 *
 * So the walk is done here instead, over the frame-pointer chain, which the native test build
 * keeps (-O0). Every pointer is checked against the bounds of the stack the caller is running on
 * before it is dereferenced, and the walk stops at the frame the fiber was entered on. Those
 * bounds exist because the harness allocates fiber stacks itself; HostFiber hands them over. It
 * also costs no stack worth speaking of: on the same 8 KB fiber it still reads correctly at the
 * depth where the program itself runs out of stack.
 */

namespace hostsim {

/**
 * @brief Record the return addresses of the frames above the caller.
 *
 * frames[0] is where captureBacktrace() returns to, frames[1] its caller, and so on outwards.
 * The walk ends at the fiber entry frame, at the bottom of the thread stack, at the first
 * pointer that does not belong to the running stack, or at maxFrames - whichever comes first.
 *
 * @return How many entries were written.
 */
int captureBacktrace(void** frames, int maxFrames);

/** @brief Write a captured trace to stderr, one frame per line, with names where available. */
void printBacktrace(void* const* frames, int count);

}  // namespace hostsim
