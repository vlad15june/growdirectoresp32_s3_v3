#pragma once

// FreeRTOS queue stand-in for the native (host) test environment.
//
// A faithful model of the primitive, not of any caller: a queue is created with a length and an
// item size, xQueueSend COPIES item-size bytes into it and refuses - pdFALSE, nothing written -
// once the length is reached, xQueueReceive copies the oldest item out and returns pdFALSE when
// there is none. That is what makes a test of MqttManager's inbound queue mean something: the
// bound and the refusal are the queue's, and the production code meets them here exactly as it
// meets them on the device.
//
// Not modelled: blocking. The device's ticks-to-wait argument is ignored because a host test runs
// on one thread - every call the firmware makes passes 0 anyway ("do not block the network
// callback"), so nothing that runs here would have blocked on the board either.
//
// Reachable only through the include path of env:native (see platformio.ini).

#include "FreeRTOS.h"

#include <cstring>
#include <deque>
#include <vector>

struct HostQueueControlBlock
{
    size_t length = 0;
    size_t itemSize = 0;
    std::deque<std::vector<uint8_t>> items;
};

typedef HostQueueControlBlock *QueueHandle_t;

inline QueueHandle_t xQueueCreate(size_t length, size_t itemSize)
{
    HostQueueControlBlock *queue = new HostQueueControlBlock();
    queue->length = length;
    queue->itemSize = itemSize;
    return queue;
}

inline void vQueueDelete(QueueHandle_t queue)
{
    delete queue;
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t /*ticksToWait*/)
{
    if (queue == nullptr || item == nullptr || queue->items.size() >= queue->length)
    {
        return pdFALSE;
    }

    std::vector<uint8_t> copy(queue->itemSize);
    std::memcpy(copy.data(), item, queue->itemSize);
    queue->items.push_back(copy);
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t /*ticksToWait*/)
{
    if (queue == nullptr || item == nullptr || queue->items.empty())
    {
        return pdFALSE;
    }

    std::memcpy(item, queue->items.front().data(), queue->itemSize);
    queue->items.pop_front();
    return pdTRUE;
}

inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue)
{
    return queue == nullptr ? 0 : static_cast<UBaseType_t>(queue->items.size());
}
