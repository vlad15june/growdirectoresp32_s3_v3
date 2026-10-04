#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Enums/StorageWriteErrorEnum.h"

/**
 * @brief One finished catalog write, as the control loop collects it.
 *
 * Everything the caller needs to react is here, because the caller must not read it back from the
 * DAO: the DAO's fields are written on the task that did the write, and the loop reading them
 * afterwards would be reading another task's memory without a happens-before.
 */
struct CatalogWriteCompletion
{
    uint32_t ticket = 0;
    /** True: the sensor types submitted with this ticket are on flash. */
    bool written = false;
    /** Why nothing was stored, when `written` is false. */
    StorageWriteErrorEnum error = STORAGE_WRITE_ERROR_NONE;
    /** Bytes handed to storage, 0 when the write never got that far. */
    size_t bytes = 0;
    /** How many sensor types the write carried. */
    size_t typeCount = 0;
};

/**
 * @brief The task that writes the sensor type catalog to flash, seen from the control loop.
 *
 * Storing the catalog is a stream of ~30 file operations - one chunk per sensor type, then the
 * sequence of renames that publishes the temporary file - and each of them costs whatever SPIFFS
 * decides to spend, up to tens of milliseconds when the file system collects garbage. Run inline,
 * that whole stream is one control-loop step. The loop submits the catalog here and collects the
 * outcome later; neither call waits.
 *
 * What does *not* move is the decision: what to store is computed on the control loop, from
 * repositories only the loop touches, and handed over as a finished snapshot.
 */
class ISensorTypeCatalogWriter
{
public:
    /** @brief Ticket value returned when a write could not be queued. Real tickets start at 1. */
    static const uint32_t kRejected = 0;

    /**
     * @brief Cap on writes accepted but not yet collected.
     *
     * Two, and only as a backstop: the one submitter hands over the next catalog no earlier than
     * it collects the outcome of the previous one, so the second slot exists to make the bound a
     * property of this class rather than a property of how it happens to be called. Waiting writes
     * are not merged - a catalog superseded before it reached flash would be written for nothing,
     * and flash writes are the resource this class exists to be careful with.
     */
    static const size_t MAX_PENDING_WRITES = 2;

    virtual ~ISensorTypeCatalogWriter() = default;

    /**
     * @brief Queue a complete catalog to be stored, and return without waiting.
     * @param catalog Taken by value so the caller can hand its snapshot over instead of copying it
     * @return a ticket, or kRejected when the queue is full or no task could be created
     */
    virtual uint32_t submit(std::vector<SensorType> catalog) = 0;

    /**
     * @brief Take every write that has finished since the last call.
     * @return true if there was anything to take
     */
    virtual bool drainResults(std::vector<CatalogWriteCompletion>& out) = 0;

    /**
     * @brief Take back the catalog a finished write carried.
     *
     * The submitter handed its snapshot over and has nothing left; this hands the same types back
     * once they are on flash, so that the copy which was written can become the copy that is
     * cached instead of being dropped and read out of the file again. Kept for one ticket only and
     * dropped when the next write starts, so nothing accumulates here if nobody asks.
     *
     * @param ticket The write to collect the catalog of; anything else answers false
     * @return true if the catalog was handed over, leaving `out` holding it
     */
    virtual bool takeStoredCatalog(uint32_t ticket, std::vector<SensorType>& out)
    {
        (void)ticket;
        (void)out;
        return false;
    }
};
