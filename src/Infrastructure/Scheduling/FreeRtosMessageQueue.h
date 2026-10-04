#pragma once

#include "Core/Domain/Interfaces/Scheduling/IMessageQueue.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <type_traits>

/**
 * @brief Device implementation of IMessageQueue: a thin wrapper over a FreeRTOS queue.
 *
 * Both operations use a zero tick timeout, so neither producer nor consumer can be blocked by
 * the other - which is the whole point of moving work off the control loop.
 *
 * A FreeRTOS queue copies raw bytes, so only trivially copyable payloads may be stored in it.
 * Anything larger or with a destructor (a DTO holding std::string, for instance) is queued by
 * pointer: IMessageQueue<DiagnosticDto*>, with ownership passing to the consumer.
 */
template <typename T>
class FreeRtosMessageQueue : public IMessageQueue<T> {
    static_assert(std::is_trivially_copyable<T>::value,
                  "A FreeRTOS queue copies raw bytes: queue a pointer for non-trivial payloads");

public:
    explicit FreeRtosMessageQueue(size_t capacity)
        : _capacity(capacity), _overflowCount(0)
    {
        _handle = xQueueCreate(static_cast<UBaseType_t>(capacity), sizeof(T));
    }

    ~FreeRtosMessageQueue() override
    {
        if (_handle != nullptr)
        {
            vQueueDelete(_handle);
            _handle = nullptr;
        }
    }

    FreeRtosMessageQueue(const FreeRtosMessageQueue&) = delete;
    FreeRtosMessageQueue& operator=(const FreeRtosMessageQueue&) = delete;

    bool push(const T& item) override
    {
        if (_handle == nullptr || xQueueSend(_handle, &item, 0) != pdTRUE)
        {
            _overflowCount++;
            return false;
        }
        return true;
    }

    bool pop(T& out) override
    {
        if (_handle == nullptr)
        {
            return false;
        }
        return xQueueReceive(_handle, &out, 0) == pdTRUE;
    }

    size_t size() const override
    {
        if (_handle == nullptr)
        {
            return 0;
        }
        return static_cast<size_t>(uxQueueMessagesWaiting(_handle));
    }

    size_t capacity() const override { return _capacity; }

    uint32_t overflowCount() const override { return _overflowCount; }

private:
    QueueHandle_t _handle = nullptr;
    size_t _capacity;
    uint32_t _overflowCount;
};
