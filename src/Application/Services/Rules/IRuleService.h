#ifndef IRULE_SERVICE_H
#define IRULE_SERVICE_H

#include "Core/Domain/Data/Entities/Rule.h"
#include <vector>

/**
 * Interface for rule service that manages rule operations
 */
class IRuleService {
public:
    virtual ~IRuleService() = default;
    
    /**
     * Initialize the rule service
     * @return true if initialization successful, false otherwise
     */
    virtual bool init() = 0;
    
    /**
     * Main loop method to process rules
     * Should be called regularly to evaluate and sync rules
     */
    virtual void loop() = 0;
    
    /**
     * Force synchronization of rules with server
     * @return true if sync initiated successfully, false otherwise
     */
    virtual bool forceSyncRules() = 0;
};

#endif // IRULE_SERVICE_H
