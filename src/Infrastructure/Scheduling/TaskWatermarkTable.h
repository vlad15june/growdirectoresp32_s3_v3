#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

/**
 * @brief The tasks a scheduler created, by name, so that their stack watermarks can be asked for.
 *
 * The decisions of FreeRtosTaskScheduler's bookkeeping, kept free of FreeRTOS so that a host test
 * executes them; the scheduler supplies the task handles, reads the watermark through `Reader` and
 * holds its lock around every call.
 *
 * A task is entered when it starts running and leaves when its body has returned, before the task
 * deletes itself. The order is what makes a handle in this table safe to read: FreeRTOS frees a
 * deleted task's control block and stack (the idle task does), so a handle is only ever read here
 * between the moment its task entered and the moment it left - and a task that has left is never
 * read again, however often its name is asked for.
 *
 * What a task that left measured is kept under its name. A task created, ended and created again
 * - one per catalog write - answers with the least it ever had over all its lifetimes, and its
 * next lifetime takes the same entry rather than a new one, so a task that comes and goes does
 * not fill the table. Fixed capacity: a task past it still runs, it is just not measurable, and
 * that is counted.
 */
class TaskWatermarkTable
{
public:
    static const size_t CAPACITY = 8;

    /** Reads a live task's watermark, in bytes, from its handle. */
    typedef uint32_t (*Reader)(void* handle);

    /**
     * @brief A task has started running under this name.
     * @param name Stored by pointer: callers pass their static TASK_NAME constants
     * @return false when the table had no room and the task is not measurable
     */
    bool started(const char* name, void* handle)
    {
        if (name == nullptr || handle == nullptr)
        {
            return false;
        }

        // An earlier lifetime of the same task first, so its measurement carries on.
        Entry* entry = nullptr;
        for (size_t i = 0; i < CAPACITY && entry == nullptr; i++)
        {
            if (_entries[i].name != nullptr && _entries[i].handle == nullptr &&
                strcmp(_entries[i].name, name) == 0)
            {
                entry = &_entries[i];
            }
        }
        for (size_t i = 0; i < CAPACITY && entry == nullptr; i++)
        {
            if (_entries[i].name == nullptr)
            {
                entry = &_entries[i];
                entry->name = name;
            }
        }

        if (entry == nullptr)
        {
            _untrackedCount++;
            return false;
        }
        entry->handle = handle;
        return true;
    }

    /**
     * @brief The task is about to delete itself; this is the least stack it ever had free.
     *
     * After this the handle is not read again. A task that never entered (the table was full)
     * leaves nothing to update.
     */
    void ended(void* handle, uint32_t watermarkBytes)
    {
        if (handle == nullptr)
        {
            return;
        }
        for (size_t i = 0; i < CAPACITY; i++)
        {
            if (_entries[i].handle == handle)
            {
                _entries[i].handle = nullptr;
                remember(_entries[i], watermarkBytes);
                return;
            }
        }
    }

    /**
     * @brief The least free stack a task of this name has had, over every lifetime of it.
     *
     * A live task is read through `read`; an ended one answers with what it measured as it left.
     * @return false when no task of this name ever entered the table
     */
    bool lowest(const char* name, Reader read, uint32_t& outBytes) const
    {
        if (name == nullptr)
        {
            return false;
        }

        bool known = false;
        uint32_t lowestBytes = 0;
        for (size_t i = 0; i < CAPACITY; i++)
        {
            const Entry& entry = _entries[i];
            if (entry.name == nullptr || strcmp(entry.name, name) != 0)
            {
                continue;
            }
            if (entry.measured)
            {
                take(entry.lowestBytes, known, lowestBytes);
            }
            if (entry.handle != nullptr && read != nullptr)
            {
                take(read(entry.handle), known, lowestBytes);
            }
        }

        if (known)
        {
            outBytes = lowestBytes;
        }
        return known;
    }

    /** @brief Tasks that started while the table was full - running, but not measurable. */
    uint32_t untrackedCount() const { return _untrackedCount; }

private:
    struct Entry
    {
        const char* name = nullptr;
        /** Non-null while the task lives; the only time it may be read. */
        void* handle = nullptr;
        /** Whether a lifetime of this task has ended and left its measurement here. */
        bool measured = false;
        uint32_t lowestBytes = 0;
    };

    static void remember(Entry& entry, uint32_t watermarkBytes)
    {
        if (!entry.measured || watermarkBytes < entry.lowestBytes)
        {
            entry.lowestBytes = watermarkBytes;
        }
        entry.measured = true;
    }

    static void take(uint32_t candidate, bool& known, uint32_t& lowestBytes)
    {
        if (!known || candidate < lowestBytes)
        {
            lowestBytes = candidate;
        }
        known = true;
    }

    Entry _entries[CAPACITY];
    uint32_t _untrackedCount = 0;
};
