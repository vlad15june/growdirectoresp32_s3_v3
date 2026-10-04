#pragma once

#include "IRepository.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Data/Entities/Sensor/SensorTypeCatalog.h"
#include "Core/Domain/Enums/StorageWriteErrorEnum.h"
#include <cstddef>
#include <vector>
#include <string>

class ISensorTypeRepository : public IRepository<SensorType> {
public:
    virtual ~ISensorTypeRepository() = default;

    /**
     * @brief The catalog the module holds, shared rather than copied
     *
     * What every reader that walks the whole catalog asks for. getAll() answers the same question
     * with a list of its own, which costs one more whole catalog for as long as the reader holds
     * it; this answers with a handle to the one the repository already has.
     *
     * Never null: a module that holds no catalog answers with an empty list. The list is immutable
     * and is replaced, never edited, so the holder reads the whole of the catalog it took even if
     * the repository takes a new one in the meantime.
     */
    virtual SensorTypeCatalog catalogSnapshot() = 0;

    virtual std::vector<SensorType> getSensorTypesByProtocol(SensorProtocol protocol) = 0;
    virtual std::vector<SensorType> getAnalogSensorTypes() = 0;
    virtual std::vector<SensorType> getI2CSensorTypes() = 0;
    virtual std::vector<SensorType> getModbusSensorTypes() = 0;
    virtual std::vector<SensorType> getCalibrableSensorTypes() = 0;
    virtual std::vector<SensorType> getCanScan() = 0;

    /**
     * @brief A type of the held catalog, by name, or nullptr
     *
     * const, because the catalog is shared with whoever holds a snapshot of it: a type is changed
     * by replacing the catalog, not by writing through a pointer into it.
     */
    virtual const SensorType* getSensorTypeByName(const std::string& name) = 0;

    /**
     * @brief Why the last saveAll() stored nothing (GDV1-3827)
     *
     * saveAll() returns a count, which cannot tell "the catalog was empty" from "the write was
     * refused". The reason belongs in the log of whoever asked for the write.
     */
    virtual StorageWriteErrorEnum getLastSaveError() const = 0;

    /**
     * @brief How many bytes the last saveAll() handed to storage
     */
    virtual size_t getLastSaveBytes() const = 0;

    /**
     * @brief Drop the cached copy, because storage was written by someone else
     *
     * The catalog is stored on its own task, so the write that makes the cached copy obsolete does
     * not go through this object. The task that owns the cache says so here, and the next read
     * comes from storage. A repository that keeps no cache has nothing to do.
     */
    virtual void invalidateCache() {}

    /**
     * @brief Another task is writing storage: until this is cleared, answer from what is held
     *
     * The catalog is stored on its own task, and a read of storage meanwhile reads a file that is
     * being replaced - and, between the renames that publish it, finds no file and takes the
     * temporary one for an interrupted replacement to recover, renaming it from under the task
     * that is publishing it. Raised by whoever hands the write over, from the moment it is
     * accepted until its outcome has been collected. A module that held no catalog when the write
     * began reads no catalog until the write lands, which is what storage would have answered it
     * anyway. A repository that keeps no cache has nothing to answer from, and ignores it.
     */
    virtual void setStorageWriteInFlight(bool inFlight) { (void)inFlight; }

    /**
     * @brief Hold this catalog as the cached one, because it is what storage now holds
     *
     * Called by whoever has just seen the write land, with the very types that were written. The
     * alternative is to drop the cache and let the next reader load the file - which is the module
     * reading back, in one string plus one parse, a catalog it is still holding, in the same second
     * it spent building it.
     *
     * Only ever with a catalog that reached storage: what is cached has to be what a reboot would
     * read, so a snapshot adopted after a refused write would be a cache no reboot could reproduce.
     * A repository that keeps no cache drops it instead, and its next reader loads the file.
     */
    virtual void adoptStoredCatalog(std::vector<SensorType> catalog)
    {
        (void)catalog;
        invalidateCache();
    }
};
