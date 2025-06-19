#ifndef IEVALUATE_RULES_USE_CASE_H
#define IEVALUATE_RULES_USE_CASE_H

#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Data/Models/RuleEvaluationResult.h"
#include <vector>

/**
 * Interface for evaluating rules and executing their actions
 */
class IEvaluateRulesUseCase {
public:
    virtual ~IEvaluateRulesUseCase() = default;
    
    /**
     * Initialize the use case
     * @return true if initialization successful, false otherwise
     */
    virtual bool init() = 0;
    
    /**
     * Main loop method to evaluate rules
     * Should be called regularly to check rule conditions and execute actions
     */
    virtual void loop() = 0;
    
    /**
     * Evaluate a specific rule
     * @param rule The rule to evaluate
     * @return RuleEvaluationResult containing comprehensive evaluation data
     */
    virtual RuleEvaluationResult evaluateRule(const Rule& rule) = 0;
    
    /**
     * Execute actions for a rule
     * @param rule The rule whose actions should be executed
     * @return true if actions executed successfully, false otherwise
     */
    virtual bool executeRuleActions(const Rule& rule) = 0;
    
    /**
     * Check if a rule is currently active
     * @param ruleUid The UID of the rule to check
     * @return true if rule is active, false otherwise
     */
    virtual bool isRuleActive(const std::string& ruleUid) const = 0;
    
    /**
     * Get the last evaluation result for a rule
     * @param ruleUid The UID of the rule
     * @return RuleEvaluationResult of the last evaluation
     */
    virtual RuleEvaluationResult getLastEvaluationResult(const std::string& ruleUid) const = 0;
};

#endif // IEVALUATE_RULES_USE_CASE_H
