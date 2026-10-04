#ifndef BACKGROUND_ACT_BOARD_H
#define BACKGROUND_ACT_BOARD_H

#include <atomic>
#include <cstdint>

#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

/**
 * @brief What each background task is doing, one word per task, for whoever has to name the work
 *        a heap low fell on.
 *
 * The heap watermark trace says when the floor broke and how deep. The work that broke it runs on
 * another task - a request on the network task, a pass of the discovery on the acquisition task, a
 * catalog on its way to flash - and often says nothing on the log at the time, so the line about
 * the floor stands alone. This board is what lets that line name its neighbours.
 *
 * ### What a task pays
 *
 * One atomic store when an act begins and one when it ends, and nothing in between: no lock, no
 * allocation, no text. The word carries the act's code, whether it is over, and the low 24 bits of
 * millis() at that moment - all of it in one store, so a reader never sees half of an update.
 *
 * Each slot has one writer, the task it is named after. A word is replaced, never accumulated: what
 * the board holds is the last boundary that task crossed.
 *
 * ### What a reader gets
 *
 * capture() copies the words and reads the clock after them - in that order, so that no stamp in
 * the copy is later than the time the copy is dated with. read() then answers, for one slot, one
 * of three things: an act is running, and for how long; an act is over but ended inside the
 * reader's window, and how long ago; or nothing the window can hold against that task. The window
 * is the reader's own - for the watermark trace, the span since its previous reading, which is
 * exactly the span a new low can have happened in. An act that ended before it cannot have made a
 * low that was not there when it ended, and is not named.
 *
 * ### The 24 bits
 *
 * Ages are differences modulo 2^24 ms, about 4 h 40 min. A running act is always younger than
 * that. A finished word that is never replaced - a task that stays idle for days - reads as fresh
 * again for one window's length once per wrap: the odds that a given reading lands there are the
 * window over the wrap, a few in ten million for a loop step of milliseconds. Accepted, because
 * the alternative is a second word and with it a second store the reader can see between.
 */
class BackgroundActBoard
{
public:
    /** The tasks that keep a word here. */
    enum Slot
    {
        NETWORK = 0,          // outbox-net
        ACQUISITION = 1,      // sensor-acq
        COMMAND_NETWORK = 2,  // cmd-net
        CATALOG_STORAGE = 3,  // catalog-io
        SLOT_COUNT = 4
    };

    /** The code of a slot no act has been written to. Act codes are 1..MAX_ACT. */
    static const uint8_t NO_ACT = 0;
    static const uint8_t MAX_ACT = 0x7F;

    /** The words as they were at one moment, and the window they are to be read against. */
    struct Snapshot
    {
        uint32_t words[SLOT_COUNT];
        uint32_t takenAtMs;
        uint32_t windowMs;
    };

    /** What one slot of a snapshot says. */
    struct Reading
    {
        /** NO_ACT when there is nothing to name: never ran, or ended before the window. */
        uint8_t act;
        /** True: the act was over at the snapshot, and ended inside the window. */
        bool finished;
        /** Running: for how long. Finished: how long before the snapshot it ended. */
        uint32_t ms;
    };

    explicit BackgroundActBoard(IArduinoAdapter* clock) : _clock(clock)
    {
        for (int slot = 0; slot < SLOT_COUNT; slot++)
        {
            _words[slot].store(0, std::memory_order_relaxed);
        }
    }

    BackgroundActBoard(const BackgroundActBoard&) = delete;
    BackgroundActBoard& operator=(const BackgroundActBoard&) = delete;

    /** @brief The slot's task begins an act. Called by that task only. */
    void actStarted(Slot slot, uint8_t act) { write(slot, act, 0u); }

    /** @brief The slot's task is done with the act. Called by that task only. */
    void actFinished(Slot slot, uint8_t act) { write(slot, act, FINISHED_BIT); }

    /**
     * @brief Copy the board, to be read against the window that began at windowStartedAtMs.
     *
     * Allocation-free and lock-free: four loads and one reading of the clock, which is what makes
     * it callable at the moment the heap is at its floor.
     */
    void capture(uint32_t windowStartedAtMs, Snapshot& out) const
    {
        for (int slot = 0; slot < SLOT_COUNT; slot++)
        {
            out.words[slot] = _words[slot].load(std::memory_order_relaxed);
        }
        out.takenAtMs = nowMs();
        out.windowMs = out.takenAtMs - windowStartedAtMs;
    }

    static Reading read(const Snapshot& snapshot, Slot slot)
    {
        Reading reading;
        reading.act = NO_ACT;
        reading.finished = false;
        reading.ms = 0;

        const uint32_t word = snapshot.words[slot];
        const uint8_t act = (uint8_t)((word >> ACT_SHIFT) & MAX_ACT);
        if (act == NO_ACT)
        {
            return reading;
        }

        // Modulo the stamp's width, so an act that straddles the wrap still has its true age.
        const uint32_t ageMs = (snapshot.takenAtMs - (word & STAMP_MASK)) & STAMP_MASK;
        const bool finished = (word & FINISHED_BIT) != 0;
        if (finished && ageMs > snapshot.windowMs)
        {
            return reading;
        }

        reading.act = act;
        reading.finished = finished;
        reading.ms = ageMs;
        return reading;
    }

private:
    static const uint32_t FINISHED_BIT = 0x80000000u;
    static const uint32_t ACT_SHIFT = 24;
    static const uint32_t STAMP_MASK = 0x00FFFFFFu;

    uint32_t nowMs() const { return (_clock != nullptr) ? (uint32_t)_clock->millis() : 0u; }

    void write(Slot slot, uint8_t act, uint32_t finishedBit)
    {
        const uint32_t word = finishedBit | ((uint32_t)(act & MAX_ACT) << ACT_SHIFT) |
                              (nowMs() & STAMP_MASK);
        _words[slot].store(word, std::memory_order_relaxed);
    }

    IArduinoAdapter* _clock;
    std::atomic<uint32_t> _words[SLOT_COUNT];
};

#endif // BACKGROUND_ACT_BOARD_H
