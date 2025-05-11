#ifndef I_RULE_REPOSITORY_H
#define I_RULE_REPOSITORY_H

#include "IRepository.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include <vector>
#include <string>

class IRuleRepository : public IRepository<Rule> {
public:
    virtual ~IRuleRepository() = default;

    /**
     * @brief Get rules by their type
     * @param type Type of rules to fetch
     * @return Vector of rules matching the specified type
     */
    virtual std::vector<Rule> getRulesByType(RuleType type) = 0;

    /**
     * @brief Get rules by device UID
     * @param deviceUid UID of the device
     * @return Vector of rules associated with the specified device
     */
    virtual std::vector<Rule> getRulesByDeviceUid(const char* deviceUid) = 0;

    /**
     * @brief Get rules by sensor UID
     * @param sensorUid UID of the sensor
     * @return Vector of rules associated with the specified sensor
     */
    virtual std::vector<Rule> getRulesBySensorUid(const char* sensorUid) = 0;

    /**
     * @brief Get only active rules (where isRuleOn is true)
     * @return Vector of active rules
     */
    virtual std::vector<Rule> getActiveRules() = 0;

    /**
     * @brief Get active rules by type
     * @param type Type of rules to fetch
     * @return Vector of active rules matching the specified type
     */
    virtual std::vector<Rule> getActiveRulesByType(RuleType type) = 0;

    /**
     * @brief Get rules that need synchronization (state is not SYNCHRONIZED)
     * @return Vector of rules that need synchronization
     */
    virtual std::vector<Rule> getRulesNeedingSynchronization() = 0;

    /**
     * @brief Update rule state
     * @param uid UID of the rule to update
     * @param state New state value
     * @return true if operation was successful, false otherwise
     */
    virtual bool updateRuleState(const char* uid, RuleState state) = 0;

    /**
     * @brief Update rule mode
     * @param uid UID of the rule to update
     * @param mode New mode value
     * @param reason Optional reason for the mode change
     * @return true if operation was successful, false otherwise
     */
    virtual bool updateRuleMode(const char* uid, RuleMode mode, const char* reason = nullptr) = 0;

    /**
     * @brief Synchronize rules with cloud/server
     * @param rules Vector of rules to sync
     * @return Number of successfully synchronized rules
     */
    virtual int synchronizeRules(const std::vector<Rule>& rules) = 0;
};

#endif // I_RULE_REPOSITORY_H
