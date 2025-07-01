#pragma once

#include <string>
#include <vector>

class RuleStateDto {
public:
    std::string uid;
    std::string state = "Synchronized";
    std::string mode = "Off";
    std::string modeReason;
    std::string errorType;
    
    RuleStateDto() = default;
    RuleStateDto(const std::string& ruleUid, const std::string& ruleState = "Synchronized", 
                 const std::string& ruleMode = "Off", const std::string& reason = "", 
                 const std::string& error = "")
        : uid(ruleUid), state(ruleState), mode(ruleMode), modeReason(reason), errorType(error) {}
};

/**
 * @brief Data Transfer Object for rule state update response
 */
class RuleStateUpdateDto {
public:
    std::string timestamp;
    std::string status = "Success";
    std::string errorType;
    std::string accountUid;
    std::vector<RuleStateDto> rules;
    
    RuleStateUpdateDto() = default;
    
    /**
     * @brief Add a rule state to the update
     */
    void addRuleState(const RuleStateDto& ruleState) {
        rules.push_back(ruleState);
    }
    
    /**
     * @brief Add a rule state with parameters
     */
    void addRuleState(const std::string& uid, const std::string& state = "Synchronized", 
                      const std::string& mode = "Off", const std::string& modeReason = "",
                      const std::string& errorType = "") {
        rules.emplace_back(uid, state, mode, modeReason, errorType);
    }
    
    /**
     * @brief Set error status
     */
    void setError(const std::string& error) {
        status = "Error";
        errorType = error;
    }
    
    /**
     * @brief Check if update has any rules
     */
    bool hasRules() const {
        return !rules.empty();
    }
    
    /**
     * @brief Get rule count
     */
    size_t getRuleCount() const {
        return rules.size();
    }
};
