#ifndef ISYNC_SENSOR_TYPES_USE_CASE_H
#define ISYNC_SENSOR_TYPES_USE_CASE_H

#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include <vector>

/**
 * Interface for synchronizing sensor types with the server
 */
class ISyncSensorTypesUseCase {
public:
    virtual ~ISyncSensorTypesUseCase() = default;
    
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