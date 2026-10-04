#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Facade over the RTOS queue API.
 *
 * Hands work from one task to another. Both operations are non-blocking so that a producer
 * running inside the control loop can never be stalled by a slow consumer: push() reports
 * failure instead of waiting, and every refused item is counted by overflowCount(), so an
 * overflowing queue is a visible number rather than a silent loss.
 */
template <typename T>
class IMessageQueue {
public:
    virtual ~IMessageQueue() = default;

    /**
     * @brief Put an item into the queue without blocking.
     * @return true if the item was queued, false if the queue was full (overflow counted)
     */
    virtual bool push(const T& item) = 0;

    /**
     * @brief Take the oldest item out of the queue without blocking.
     * @param out Receives the item when the call returns true
     * @return true if an item was taken, false if the queue was empty
     */
    virtual bool pop(T& out) = 0;

    /** @brief Number of items currently queued. */
    virtual size_t size() const = 0;

    /** @brief Maximum number of items the queue can hold. */
    virtual size_t capacity() const = 0;

    /** @brief How many items push() has refused because the queue was full. */
    virtual uint32_t overflowCount() const = 0;
};
