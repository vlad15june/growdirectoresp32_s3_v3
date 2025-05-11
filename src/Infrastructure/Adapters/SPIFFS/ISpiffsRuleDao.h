#pragma once

#include "ISpiffsDao.h"
#include "Core/Domain/Data/Entities/Rule.h"

/**
 * @brief Rule-specific DAO interface extending base DAO functionality
 */
class ISpiffsRuleDao : public ISpiffsDao<Rule> {
public:
    virtual ~ISpiffsRuleDao() = default;

    /**
     * @brief Save a rule using its UID as identifier
     * @param rule The rule to save
     * @return true if the operation was successful, false otherwise
     */
    virtual bool saveRule(const Rule& rule) = 0;

    /**
     * @brief Get a rule by its UID
     * @param uid The UID of the rule to retrieve
     * @param rule Reference to a rule object to populate
     * @return true if the rule was found, false otherwise
     */
    virtual bool getRule(const std::string& uid, Rule& rule) = 0;

    /**
     * @brief Delete a rule by its UID
     * @param uid The UID of the rule to delete
     * @return true if the rule was deleted, false otherwise
     */
    virtual bool deleteRule(const std::string& uid) = 0;

    /**
     * @brief Get all rules
     * @param rules Reference to a vector to populate with rules
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getRules(std::vector<Rule>& rules) = 0;

    /**
     * @brief Save multiple rules
     * @param rules Vector of rules to save
     * @return true if the operation was successful, false otherwise
     */
    virtual bool saveRules(const std::vector<Rule>& rules) = 0;

    /**
     * @brief Get rules for a specific device
     * @param deviceUid The device UID to filter by
     * @param rules Reference to a vector to populate with rules
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getRulesByDeviceUid(const std::string& deviceUid, std::vector<Rule>& rules) = 0;

    /**
     * @brief Get rules by rule type
     * @param type The rule type to filter by
     * @param rules Reference to a vector to populate with rules
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getRulesByType(RuleType type, std::vector<Rule>& rules) = 0;
};
