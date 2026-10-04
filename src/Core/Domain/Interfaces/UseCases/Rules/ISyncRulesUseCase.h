#ifndef ISYNC_RULES_USE_CASE_H
#define ISYNC_RULES_USE_CASE_H

#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include <vector>

/**
 * Interface for synchronizing rules with the server
 */
class ISyncRulesUseCase {
public:
    virtual ~ISyncRulesUseCase() = default;

    /**
     * Told, on the control loop, how a request submitted to the outbox ended. For rule sync this
     * is where the fetched rules arrive: the GET is asked for in loop() and answered here, so
     * nothing waits for the server inside the loop iteration that asked.
     *
     * Default: nothing, for implementations that submit nothing.
     */
    virtual void onOutboxCompletion(const OutboxCompletion& completion) { (void)completion; }
    
    /**
     * Initialize the use case
     * @return true if initialization successful, false otherwise
     */
    virtual bool init() = 0;
    
    /**
     * Main loop method to handle rule synchronization
     * Should be called regularly to sync rules with server
     */
    virtual void loop() = 0;
    
    /**
     * Force immediate synchronization of rules
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

#endif // ISYNC_RULES_USE_CASE_H
