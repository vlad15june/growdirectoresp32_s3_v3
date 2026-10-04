#pragma once

#include "Core/Domain/Interfaces/Scheduling/IMessageQueue.h"

#include <cstdint>
#include <deque>

namespace hostsim {

/**
 * @brief Host implementation of IMessageQueue.
 *
 * Same contract as the FreeRTOS wrapper - non-blocking on both ends, refusals counted - but
 * without the trivially-copyable restriction, so a test can queue a DTO directly instead of
 * queueing a pointer to one.
 */
template <typename T>
class HostMessageQueue : public IMessageQueue<T> {
public:
    explicit HostMessageQueue(size_t capacity)
        : _capacity(capacity)
    {
    }

    bool push(const T& item) override
    {
        if (_items.size() >= _capacity)
        {
            _overflowCount++;
            return false;
        }
        _items.push_back(item);
        return true;
    }

    bool pop(T& out) override
    {
        if (_items.empty())
        {
            return false;
        }
        out = _items.front();
        _items.pop_front();
        return true;
    }

    size_t size() const override { return _items.size(); }
    size_t capacity() const override { return _capacity; }
    uint32_t overflowCount() const override { return _overflowCount; }

private:
    std::deque<T> _items;
    size_t _capacity;
    uint32_t _overflowCount = 0;
};

} // namespace hostsim
