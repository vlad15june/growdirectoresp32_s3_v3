#ifndef HEAP_TASK_LEDGER_H
#define HEAP_TASK_LEDGER_H

// Heap-forensics builds only: nothing here exists in an image built without GD_HEAP_FORENSICS.
#ifdef GD_HEAP_FORENSICS

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

/**
 * @brief Who holds the heap, task by task, exactly as the platform hands it over.
 *
 * With per-task heap tracking compiled in, the allocator stamps every block with the task that
 * asked for it, and one walk over the heap adds the blocks up per task. That answers the question
 * the heap trace cannot: a low that falls while every participant the firmware knows of is idle
 * was made by a task the firmware did not write - the WiFi driver, lwIP, the TCP glue - and the
 * walk names it.
 *
 * The ledger is the raw material and nothing else: owners as task handles with their totals, and
 * the tasks alive at that moment with their names. It is filled by the platform adapter in two
 * calls - the walk over the heap and the list of tasks, kept apart so that each can be timed -
 * and each of them is one platform call and one copy loop; everything that needs thought -
 * putting names to handles, merging, ordering, cutting to a line - is HeapHolders below, which
 * the host tests execute. Both arrays are fixed: a reading taken at a heap low asks for no memory.
 */
struct HeapTaskLedger
{
    /** Distinct owners the platform is given room for; owners past it are not collected. */
    static const size_t MAX_OWNERS = 32;
    /** Tasks the platform is given room for; with more alive it lists none (tasksListed). */
    static const size_t MAX_TASKS = 32;
    /** A task name with its terminator: configMAX_TASK_NAME_LEN of the device build. */
    static const size_t NAME_BYTES = 16;

    struct Owner
    {
        /** The owning task's handle as a number; 0 is memory taken before any task existed. */
        uintptr_t task;
        uint32_t bytes;
        uint32_t blocks;
    };

    struct LiveTask
    {
        uintptr_t task;
        char name[NAME_BYTES];
    };

    Owner owners[MAX_OWNERS];
    size_t ownerCount;
    LiveTask tasks[MAX_TASKS];
    size_t taskCount;
    /** False when the platform could not list the tasks; owners are then named by handle. */
    bool tasksListed;
};

/**
 * @brief The ledger cut down to one log line: the largest holders by name, and the rest as a sum.
 *
 * A handle is given the name of the live task it belongs to. Handle 0 is "pre-sched": memory
 * taken before the scheduler started, which is where the static object graph lives. A handle no
 * live task answers to belongs to a task that has been deleted while its memory stayed - the
 * startup task is one - and all of those are one holder, "gone". (A deleted task's control block
 * can be reused for a new task at the same address; the new task then inherits the old one's
 * blocks in this account. The platform cannot tell them apart, and neither can this.)
 *
 * Largest first, ties in the order the platform reported them; TOP holders are named and the
 * others are summed, so the line has a fixed upper length whatever the number of tasks.
 */
struct HeapHolders
{
    static const size_t TOP = 10;

    /**
     * Room for the widest line, 570 characters: a 35-character opening, TOP holders of
     * " <15>=<10>/<10>", and " other=<10>/<10> all=<10>/<10> owners=<2>+ walk=<10>us
     * names=<10>us skipped=<10> rx_uc=<10> rx_mc=<10>". format() never writes past what it is
     * given; this is what to give it for nothing to be cut.
     */
    static const size_t LINE_BYTES = 576;

    struct Holder
    {
        char name[HeapTaskLedger::NAME_BYTES];
        uint32_t bytes;
        uint32_t blocks;
    };

    Holder top[TOP];
    size_t topCount;
    uint32_t otherBytes;
    uint32_t otherBlocks;
    uint32_t allBytes;
    uint32_t allBlocks;
    /** Owners the platform reported; at MAX_OWNERS some may have been left out. */
    uint32_t ownerCount;
    bool ownersAtCapacity;
    /** How long the walk over the heap took; the heap is locked for all of it. */
    uint32_t walkMicros;
    /** How long listing the tasks took; the kernel is locked for all of it. */
    uint32_t namesMicros;
    /**
     * Episodes that went without an account since the last one, by the ration of the trace; the
     * trace fills it in, and it is on the line only when it is not zero.
     */
    uint32_t skippedEpisodes;
    /**
     * Frames the radio has handed to the network stack since boot, unicast and group, as they
     * stood at the reading: the difference between the two lines of an episode is what arrived
     * in between, and its class. On the line only when the platform counts them.
     */
    bool rxFramesKnown;
    uint32_t rxUnicastFrames;
    uint32_t rxGroupFrames;

    static void reduce(const HeapTaskLedger& ledger, uint32_t walkMicros, uint32_t namesMicros,
                       HeapHolders& out)
    {
        out.topCount = 0;
        out.otherBytes = 0;
        out.otherBlocks = 0;
        out.allBytes = 0;
        out.allBlocks = 0;
        out.ownerCount = (uint32_t)ledger.ownerCount;
        out.ownersAtCapacity = ledger.ownerCount >= HeapTaskLedger::MAX_OWNERS;
        out.walkMicros = walkMicros;
        out.namesMicros = namesMicros;
        out.skippedEpisodes = 0;
        out.rxFramesKnown = false;
        out.rxUnicastFrames = 0;
        out.rxGroupFrames = 0;

        // A count past the capacity is read up to the capacity and not past the array.
        size_t ownerCount = ledger.ownerCount;
        if (ownerCount > HeapTaskLedger::MAX_OWNERS)
        {
            ownerCount = HeapTaskLedger::MAX_OWNERS;
        }

        // The deleted tasks first, as one holder, so that it takes its place among the others by
        // its total and not by the order its parts were met in.
        uint32_t goneBytes = 0;
        uint32_t goneBlocks = 0;
        for (size_t i = 0; i < ownerCount; i++)
        {
            const HeapTaskLedger::Owner& owner = ledger.owners[i];
            out.allBytes += owner.bytes;
            out.allBlocks += owner.blocks;
            if (isGone(ledger, owner.task))
            {
                goneBytes += owner.bytes;
                goneBlocks += owner.blocks;
            }
        }

        for (size_t i = 0; i < ownerCount; i++)
        {
            const HeapTaskLedger::Owner& owner = ledger.owners[i];
            if (isGone(ledger, owner.task))
            {
                continue;
            }

            char name[HeapTaskLedger::NAME_BYTES];
            nameOf(ledger, owner.task, name);
            out.place(name, owner.bytes, owner.blocks);
        }

        if (goneBlocks > 0)
        {
            out.place("gone", goneBytes, goneBlocks);
        }
    }

    /**
     * @brief " name=bytes/blocks ..." for every named holder, then the sums.
     * @return Characters written, not counting the terminator; never more than size - 1
     */
    static size_t format(const HeapHolders& holders, char* out, size_t size)
    {
        if (out == nullptr || size == 0)
        {
            return 0;
        }
        out[0] = '\0';

        size_t written = 0;
        for (size_t i = 0; i < holders.topCount; i++)
        {
            written += append(out + written, size - written, " %s=%lu/%lu", holders.top[i].name,
                              (unsigned long)holders.top[i].bytes,
                              (unsigned long)holders.top[i].blocks);
        }

        written += append(out + written, size - written, " other=%lu/%lu all=%lu/%lu",
                          (unsigned long)holders.otherBytes, (unsigned long)holders.otherBlocks,
                          (unsigned long)holders.allBytes, (unsigned long)holders.allBlocks);
        written += append(out + written, size - written, " owners=%lu%s walk=%luus names=%luus",
                          (unsigned long)holders.ownerCount, holders.ownersAtCapacity ? "+" : "",
                          (unsigned long)holders.walkMicros, (unsigned long)holders.namesMicros);
        if (holders.skippedEpisodes > 0)
        {
            written += append(out + written, size - written, " skipped=%lu",
                              (unsigned long)holders.skippedEpisodes);
        }
        if (holders.rxFramesKnown)
        {
            written += append(out + written, size - written, " rx_uc=%lu rx_mc=%lu",
                              (unsigned long)holders.rxUnicastFrames,
                              (unsigned long)holders.rxGroupFrames);
        }
        return written;
    }

private:
    /** snprintf that reports what it wrote rather than what it would have liked to write. */
    static size_t append(char* out, size_t size, const char* format, ...)
    {
        if (size <= 1)
        {
            return 0;
        }

        va_list args;
        va_start(args, format);
        const int wanted = vsnprintf(out, size, format, args);
        va_end(args);

        if (wanted < 0)
        {
            out[0] = '\0';
            return 0;
        }
        return (size_t)wanted < size ? (size_t)wanted : size - 1;
    }

    static const HeapTaskLedger::LiveTask* liveTask(const HeapTaskLedger& ledger, uintptr_t task)
    {
        size_t taskCount = ledger.taskCount;
        if (taskCount > HeapTaskLedger::MAX_TASKS)
        {
            taskCount = HeapTaskLedger::MAX_TASKS;
        }
        for (size_t i = 0; i < taskCount; i++)
        {
            if (ledger.tasks[i].task == task)
            {
                return &ledger.tasks[i];
            }
        }
        return nullptr;
    }

    /** A handle with no live task behind it - which can only be said when the tasks were listed. */
    static bool isGone(const HeapTaskLedger& ledger, uintptr_t task)
    {
        return task != 0 && ledger.tasksListed && liveTask(ledger, task) == nullptr;
    }

    static void nameOf(const HeapTaskLedger& ledger, uintptr_t task, char* out)
    {
        if (task == 0)
        {
            snprintf(out, HeapTaskLedger::NAME_BYTES, "pre-sched");
            return;
        }

        const HeapTaskLedger::LiveTask* live = ledger.tasksListed ? liveTask(ledger, task) : nullptr;
        if (live == nullptr)
        {
            snprintf(out, HeapTaskLedger::NAME_BYTES, "@%08lx", (unsigned long)task);
            return;
        }

        // A name is one token of the line: whatever would split it, or end it, is replaced.
        size_t length = 0;
        while (length < HeapTaskLedger::NAME_BYTES - 1 && live->name[length] != '\0')
        {
            const unsigned char c = (unsigned char)live->name[length];
            out[length] = (c <= ' ' || c == '=' || c == '/' || c >= 0x7f) ? '_' : (char)c;
            length++;
        }
        if (length == 0)
        {
            out[length++] = '?';
        }
        out[length] = '\0';
    }

    /** Insert among the named holders, largest first; whoever falls off the end joins the sum. */
    void place(const char* name, uint32_t bytes, uint32_t blocks)
    {
        size_t position = topCount;
        while (position > 0 && top[position - 1].bytes < bytes)
        {
            position--;
        }

        if (position >= TOP)
        {
            otherBytes += bytes;
            otherBlocks += blocks;
            return;
        }

        if (topCount == TOP)
        {
            otherBytes += top[TOP - 1].bytes;
            otherBlocks += top[TOP - 1].blocks;
        }
        else
        {
            topCount++;
        }

        for (size_t i = topCount - 1; i > position; i--)
        {
            top[i] = top[i - 1];
        }

        snprintf(top[position].name, sizeof(top[position].name), "%s", name);
        top[position].bytes = bytes;
        top[position].blocks = blocks;
    }
};

#endif // GD_HEAP_FORENSICS

#endif // HEAP_TASK_LEDGER_H
