#ifndef ISYNC_SENSOR_TYPES_USE_CASE_H
#define ISYNC_SENSOR_TYPES_USE_CASE_H

#include "Application/Interfaces/IOutbox.h"
#include "Application/Interfaces/ISensorTypeCatalogWriter.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include <vector>

/**
 * Interface for synchronizing sensor types with the server
 */
class ISyncSensorTypesUseCase {
public:
    virtual ~ISyncSensorTypesUseCase() = default;

    /**
     * Told, on the control loop, how a request submitted to the outbox ended. For catalog sync this
     * is where the fetched sensor types arrive: the GET is asked for in forceSync() and answered
     * here, so nothing waits for the server inside the loop iteration that asked.
     *
     * Default: nothing, for implementations that submit nothing.
     */
    virtual void onOutboxCompletion(const OutboxCompletion& completion) { (void)completion; }

    /**
     * Told, on the control loop, how a catalog handed to the storage task ended. Storing the
     * catalog is a stream of file operations, so it happens on that task; what the stored catalog
     * is a precondition for - the sensors of a changed type, the reported sync status - happens
     * here, once it is stored.
     *
     * Default: nothing, for implementations that hand over nothing.
     */
    virtual void onCatalogWriteCompletion(const CatalogWriteCompletion& completion) { (void)completion; }

    /** What became of a catalog handed to storeFetchedCatalog(). */
    enum class StoreResult {
        Stored,    // the module holds it: stored here, or nothing in it differed from what it held
        Pending,   // on its way to storage; isWritePending() answers true until it has landed
        NotStored  // nothing was stored and nothing is on its way
    };

    /**
     * Store a catalog somebody else fetched from the server, exactly as one this use case fetched:
     * merged with what the module holds and written through the storage task. Control loop only.
     *
     * The caller is a ScanSensors command that found the module without a catalog and fetched one
     * for its scan. It hands the catalog here instead of writing it, so that every catalog the
     * server sends reaches the file through one submitter and one storage task, and a write of it
     * never runs alongside another.
     *
     * Default: nothing is stored, for implementations that store nothing.
     */
    virtual StoreResult storeFetchedCatalog(std::vector<SensorType> sensorTypes) {
        (void)sensorTypes;
        return StoreResult::NotStored;
    }

    /**
     * True while a catalog is on its way to storage: from its hand-over to the storage task until
     * onCatalogWriteCompletion() has applied its outcome. Control loop only.
     *
     * Default: never, for implementations that hand nothing over.
     */
    virtual bool isWritePending() const { return false; }

    /**
     * True from the moment a synchronization is asked for until it has finished - fetched and
     * stored, or given up on: an ask not yet handed over, a fetch on its way, or a catalog on its
     * way to storage. Control loop only.
     *
     * Default: never, for implementations that synchronize nothing.
     */
    virtual bool isSyncInFlight() const { return false; }

    /**
     * Initialize the use case
     * @return true if initialization successful, false otherwise
     */
    virtual bool init() = 0;
    
    /**
     * Main loop method to handle sensor types synchronization
     * Should be called regularly to sync sensor types with server
     */
    virtual void loop() = 0;
    
    /**
     * Force immediate synchronization of sensor types
     * @return true if sync initiated successfully, false otherwise
     */
    virtual bool forceSync() = 0;

    /**
     * Fetch the catalog on the calling task and apply it before returning. Blocks.
     *
     * The distinction from forceSync() is who waits, and it exists for exactly one caller.
     * forceSync() hands the fetch to the outbox and returns within microseconds, because its
     * caller is the control loop; the answer arrives later, in onOutboxCompletion(). That is
     * useless to a caller that reads the sensor type repository on its very next line -
     * ModuleSetupService::fillSensorModuleStructure() does, and builds a structure of "none"
     * placeholders when the repository is still empty.
     *
     * Setup runs before the control loop exists (main.cpp: setupModule() at :320, the loop service
     * at :336), so there is no loop to drain the completion and nothing for a blocking call to
     * hold up. This is the one place where waiting is the correct thing to do.
     *
     * @return true if the catalog was fetched and applied, false if the request never reached the
     *         server - which is not the same as the server answering that there are no models
     */
    virtual bool syncNow() = 0;
    
    /**
     * Get the last sync status
     * @return true if last sync was successful, false otherwise
     */
    virtual bool getLastSyncStatus() const = 0;
    
    /**
     * Get time of last successful sync
     * @return timestamp of last sync, 0 if never synced
     */
    virtual time_t getLastSyncTime() const = 0;
};

#endif // ISYNC_SENSOR_TYPES_USE_CASE_H