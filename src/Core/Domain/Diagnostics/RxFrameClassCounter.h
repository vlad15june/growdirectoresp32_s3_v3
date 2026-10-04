#ifndef RX_FRAME_CLASS_COUNTER_H
#define RX_FRAME_CLASS_COUNTER_H

// Heap-forensics builds only: nothing here exists in an image built without GD_HEAP_FORENSICS.
#ifdef GD_HEAP_FORENSICS

#include <atomic>
#include <cstddef>
#include <cstdint>

/**
 * @brief Frames the radio has handed to the network stack, by the class of their destination:
 *        unicast to this station, or group - multicast and broadcast.
 *
 * A burst of buffers held by the wifi task at a heap low has more than one possible source, and
 * the ledger cannot tell them apart: traffic addressed to this station, group traffic the access
 * point releases after each DTIM beacon for as long as any station of the network sleeps, and
 * datagrams between modules. The class of the destination separates the first from the second,
 * and it is on the frame itself: the lowest bit of the first byte of an Ethernet destination
 * address is the group bit.
 *
 * Counted where the frame enters the stack - on the radio's task, while the frame still occupies
 * the driver's buffer, which is the moment the ledger sees it. Two counters that only grow: a
 * reading is the pair at a moment, and the difference between two readings is what arrived
 * between them. One writer; a reader on another task may see the pair one frame apart.
 */
class RxFrameClassCounter
{
public:
    /** One frame as the driver hands it over: an Ethernet frame, destination address first. */
    void count(const uint8_t* frame, size_t length)
    {
        if (frame == nullptr || length == 0)
        {
            return;
        }
        if ((frame[0] & 0x01) != 0)
        {
            _group.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            _unicast.fetch_add(1, std::memory_order_relaxed);
        }
    }

    uint32_t unicast() const { return _unicast.load(std::memory_order_relaxed); }
    uint32_t group() const { return _group.load(std::memory_order_relaxed); }

private:
    std::atomic<uint32_t> _unicast{0};
    std::atomic<uint32_t> _group{0};
};

#endif // GD_HEAP_FORENSICS

#endif // RX_FRAME_CLASS_COUNTER_H
