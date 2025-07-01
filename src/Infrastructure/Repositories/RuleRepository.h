#pragma once

#include "Repository.h"
#include "Core/Domain/Interfaces/Repositories/IRuleRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsRuleDao.h"
#include <string>
#include <vector>
#include <unordered_map>

/**
 * @brief Implementation of rule repository using SPIFFS storage
 */
class RuleRepository : public Repository<Rule>, public IRuleRepository {

private:
    ISpiffsRuleDao* ruleDao;

public:
    ~RuleRepository() noexcept override = default;

    /**
     * @brief Constructor for RuleRepository
     * @param dao Data access object for Rule entities
     */
    RuleRepository(ISpiffsRuleDao* dao) 
        : Repository<Rule>(dao), ruleDao(dao) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
    }

    // IRepository<Rule> implementations 
    Utils::Optional<Rule> get(const char* uid) override {
        return Repository<Rule>::get(uid);
    }

    std::vector<Rule> getAll() override {
        return Repository<Rule>::getAll();
    }

    bool save(const Rule& entity) override {
        return Repository<Rule>::save(entity);
    }

    int saveAll(const std::vector<Rule>& entities) override {
        return Repository<Rule>::saveAll(entities);
    }

    bool remove(const char* uid) override {
        return Repository<Rule>::remove(uid);
    }

    int removeAll() override {
        return Repository<Rule>::removeAll();
    }

    int count() override {
        return Repository<Rule>::count();
    }

    bool exists(const char* uid) override {
        return Repository<Rule>::exists(uid);
    }

    /**
     * @brief Get rules for a specific device
     * @param deviceUid Device unique identifier
     * @return Vector of rules associated with the device
     */
    std::vector<Rule> getRulesByDeviceUid(const char* deviceUid) override {
        if (!deviceUid) return {};

        std::vector<Rule> rules;
        ruleDao->getRulesByDeviceUid(deviceUid, rules);
        return rules;
    }

    /**
     * @brief Get rules of a specific type
     * @param type Rule type to filter by
     * @return Vector of rules of the specified type
     */
    std::vector<Rule> getRulesByType(RuleType type) override {
        std::vector<Rule> rules;
        ruleDao->getRulesByType(type, rules);
        return rules;
    }

    /**
     * @brief Get rules by sensor UID
     * @param sensorUid Sensor unique identifier
     * @return Vector of rules associated with the sensor
     */
    std::vector<Rule> getRulesBySensorUid(const char* sensorUid) override {
        if (!sensorUid) return {};

        // Get all rules and filter by sensor UID
        std::vector<Rule> allRules = Repository<Rule>::getAll();
        std::vector<Rule> sensorRules;

        for (const auto& rule : allRules) {
            if (rule.sensorUid == sensorUid) {
                sensorRules.push_back(rule);
            }
        }

        return sensorRules;
    }

    /**
     * @brief Get active rules by type
     * @param type Rule type to filter by
     * @return Vector of active rules of the specified type
     */
    std::vector<Rule> getActiveRulesByType(RuleType type) override {
        std::vector<Rule> activeRules = getActiveRules();
        std::vector<Rule> typedRules;

        for (const auto& rule : activeRules) {
            if (rule.ruleType == type) {
                typedRules.push_back(rule);
            }
        }

        return typedRules;
    }

    /**
     * @brief Get rules that need synchronization
     * @return Vector of rules that are not synchronized
     */
    std::vector<Rule> getRulesNeedingSynchronization() override {
        // Get all rules first
        std::vector<Rule> allRules = Repository<Rule>::getAll();
        std::vector<Rule> unsyncedRules;
        
        // Filter rules that need synchronization
        for (const auto& rule : allRules) {
            if (rule.state != RuleState::SYNCHRONIZED) {
                unsyncedRules.push_back(rule);
            }
        }
        
        return unsyncedRules;
    }

    /**
     * @brief Update the state of a rule
     * @param uid Rule unique identifier
     * @param state New state for the rule
     * @return true if successful, false otherwise
     */
    bool updateRuleState(const char* uid, RuleState state) override {
        if (!uid) return false;

        Rule rule;
        if (!ruleDao->getRule(uid, rule)) {
            return false;
        }

        rule.state = state;        
        return Repository<Rule>::save(rule);
    }

    /**
     * @brief Update the mode of a rule
     * @param uid Rule unique identifier
     * @param mode New mode for the rule
     * @param reason Reason for the mode change (optional)
     * @return true if successful, false otherwise
     */
    bool updateRuleMode(const char* uid, RuleMode mode, const char* reason = nullptr) override {
        if (!uid) return false;

        Rule rule;
        if (!ruleDao->getRule(uid, rule)) {
            return false;
        }

        rule.mode = mode;
        
        if (reason) {
            rule.modeReason = reason;
        } else {
            rule.modeReason = "";
        }
        
        return Repository<Rule>::save(rule);
    }

    /**
     * @brief Synchronize rules with cloud/server
     * @param rules Vector of rules to sync
     * @return Number of successfully synchronized rules
     */
    int synchronizeRules(const std::vector<Rule>& rules) override {
        if (rules.empty()) return 0;

        // Get existing rules
        std::vector<Rule> existingRules = Repository<Rule>::getAll();
        
        // Create a map of existing rules by UID for quick lookup
        std::unordered_map<std::string, Rule*> existingRuleMap;
        for (auto& rule : existingRules) {
            existingRuleMap[rule.uid] = &rule;
        }
        
        int syncCount = 0;
        std::vector<Rule> updatedRules;
        
        // Process incoming rules
        for (const auto& incomingRule : rules) {
            // Skip rules without UIDs
            if (incomingRule.uid.empty()) {
                continue;
            }
            
            auto it = existingRuleMap.find(incomingRule.uid);
            if (it != existingRuleMap.end()) {
                // Rule exists - check if we need to update it
                Rule* existingRule = it->second;
                
                // Only update if version is newer or state needs synchronization
                if (incomingRule.version > existingRule->version || 
                    existingRule->state == RuleState::UNSYNCHRONIZED) {
                    
                    // Preserve local mode if needed
                    Rule updatedRule = incomingRule;
                    if (existingRule->state != RuleState::UNSYNCHRONIZED) {
                        updatedRule.mode = existingRule->mode;
                        updatedRule.modeReason = existingRule->modeReason;
                    }
                    
                    updatedRule.state = RuleState::SYNCHRONIZED;
                    updatedRules.push_back(updatedRule);
                    existingRuleMap.erase(it); // Remove from map to mark as processed
                    syncCount++;
                } else {
                    // Keep existing rule in the final set
                    updatedRules.push_back(*existingRule);
                    existingRuleMap.erase(it); // Remove from map to mark as processed
                }
            } else {
                // New rule
                Rule newRule = incomingRule;
                newRule.state = RuleState::SYNCHRONIZED;
                updatedRules.push_back(newRule);
                syncCount++;
            }
        }
        
        // Add any remaining rules that weren't in the incoming set
        for (auto& pair : existingRuleMap) {
            updatedRules.push_back(*pair.second);
        }
        
        // Save all rules
        if (Repository<Rule>::saveAll(updatedRules) == static_cast<int>(updatedRules.size())) {
            return syncCount;
        }
        
        return 0;
    }

    /**
     * @brief Get all active rules that are in ON mode
     * @return Vector of active rules
     */
    std::vector<Rule> getActiveRules() override {
        // Get all rules first
        std::vector<Rule> allRules = Repository<Rule>::getAll();
        std::vector<Rule> activeRules;
        
        // Current time information - will be needed for time-based filters
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo); //TODO: use _ntpTimeAdapter if available
        
        // Filter rules that are active
        for (const auto& rule : allRules) {
            bool isRuleOn = false;
            
            // Check if rule is deleted or disabled
            if (rule.state == RuleState::DELETING || rule.protectiveShutdown) {
                continue;
            }
            
            if (rule.mode == RuleMode::ON) {                
                // Check day of week
                if (!rule.daysOfWeek.empty()) {
                    int today = timeinfo.tm_wday; // 0 is Sunday
                    if (std::find(rule.daysOfWeek.begin(), rule.daysOfWeek.end(), today) == rule.daysOfWeek.end()) {
                        continue; // Not a valid day of week
                    }
                }
                
                // Check day of month
                if (!rule.daysOfMonth.empty()) {
                    int dayOfMonth = timeinfo.tm_mday;
                    if (std::find(rule.daysOfMonth.begin(), rule.daysOfMonth.end(), dayOfMonth) == rule.daysOfMonth.end()) {
                        continue; // Not a valid day of month
                    }
                }
                
                // For schedule rules, check if there's an active schedule right now
                if (rule.isScheduleRule()) {
                    bool isScheduleActive = false;
                    int currentHour = timeinfo.tm_hour;
                    int currentMinute = timeinfo.tm_min;
                    int currentSecond = timeinfo.tm_sec;
                    uint32_t totalSecondsNow = currentHour * 3600 + currentMinute * 60 + currentSecond;
                    
                    for (const auto& scheduleItem : rule.schedule) {
                        // Here we assume startAt is a time_t value or can be converted to hours/minutes
                        // In a real implementation, you'd need to parse the time string properly
                        int startHour = (scheduleItem.startAt / 3600) % 24; // Extract hour
                        int startMinute = (scheduleItem.startAt / 60) % 60; // Extract minute
                        
                        uint32_t totalSecondsStart = startHour * 3600 + startMinute * 60;
                        uint32_t totalSecondsEnd = totalSecondsStart + scheduleItem.duration;
                        
                        if (totalSecondsStart <= totalSecondsNow && totalSecondsNow < totalSecondsEnd) {
                            isScheduleActive = true;
                            break;
                        }
                    }
                    
                    if (!isScheduleActive) {
                        continue; // No active schedule at this time
                    }
                }
                
                isRuleOn = true;
            }
            
            // If the rule is active, add it to our result
            if (isRuleOn) {
                activeRules.push_back(rule);
            }
        }
        
        return activeRules;
    }
};
