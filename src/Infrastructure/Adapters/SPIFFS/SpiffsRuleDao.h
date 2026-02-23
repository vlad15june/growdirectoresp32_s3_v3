#pragma once

#include "SpiffsDao.h"
#include "ISpiffsRuleDao.h"
#include "Core/Domain/Data/Entities/Rule.h"

/**
 * @brief Implementation of Rule-specific DAO using SPIFFS storage
 */
class SpiffsRuleDao : public SpiffsDao<Rule>, public ISpiffsRuleDao {
protected:
    // Implementation of abstract methods from SpiffsDao
    std::string getFileName() const override {
        return "rules.json";
    }

    std::string getBasePath() const override {
        return "/data";
    }

    // JSON conversion methods
    std::string entityToJson(const Rule& rule) override {
        return jsonConverter->ruleToJson(rule);
    }

    bool jsonToEntity(const std::string& jsonStr, Rule& rule) override {
        return jsonConverter->ruleFromJson(jsonStr, rule);
    }

    std::string entitiesToJson(const std::vector<Rule>& rules) override {
        return jsonConverter->rulesToJson(rules);
    }

    bool jsonToEntities(const std::string& jsonStr, std::vector<Rule>& rules) override {
        return jsonConverter->rulesFromJson(jsonStr, rules);
    }

    bool filterRules(const std::function<bool(const Rule&)>& predicate, std::vector<Rule>& rules) {
        std::vector<Rule> allRules;
        if (!SpiffsDao<Rule>::getAll(allRules)) {
            return false;
        }

        rules.clear();
        for (const auto& rule : allRules) {
            if (predicate(rule)) {
                rules.push_back(rule);
            }
        }

        return true;
    }

public:
    /**
     * @brief Constructor for SpiffsRuleDao
     * @param spiffsAdapter SPIFFS adapter for file operations
     * @param jsonConverter JSON converter for serialization/deserialization
     * @param debugSystem Debug system for logging (optional)
     * @param logger Logger for output (optional)
     */
    SpiffsRuleDao(ISpiffsAdapter* spiffsAdapter, IJsonConverter* jsonConverter, IDebugSystem* debugSystem = nullptr, ILogger* logger = nullptr)
        : SpiffsDao<Rule>(spiffsAdapter, jsonConverter, debugSystem, logger) {
        initialize(); // Initialize after object creation
    }

    // Implementation of ISpiffsDao methods - needed to resolve ambiguity
    bool save(const Rule& entity, const std::string& uid) override {
        return SpiffsDao<Rule>::save(entity, uid);
    }

    bool getById(const std::string& uid, Rule& entity) override {
        return SpiffsDao<Rule>::getById(uid, entity);
    }

    bool remove(const std::string& uid) override {
        return SpiffsDao<Rule>::remove(uid);
    }

    bool saveAll(const std::vector<Rule>& entities) override {
        return SpiffsDao<Rule>::saveAll(entities);
    }

    bool getAll(std::vector<Rule>& entities) override {
        return SpiffsDao<Rule>::getAll(entities);
    }

    bool clear() override {
        return SpiffsDao<Rule>::clear();
    }

    // Implementation of ISpiffsRuleDao methods
    bool saveRule(const Rule& rule) override {
        if (rule.uid.empty()) return false;
        return SpiffsDao<Rule>::save(rule, rule.uid);
    }

    bool getRule(const std::string& uid, Rule& rule) override {
        return SpiffsDao<Rule>::getById(uid, rule);
    }

    bool deleteRule(const std::string& uid) override {
        return SpiffsDao<Rule>::remove(uid);
    }

    bool getRules(std::vector<Rule>& rules) override {
        return SpiffsDao<Rule>::getAll(rules);
    }

    bool saveRules(const std::vector<Rule>& rules) override {
        return SpiffsDao<Rule>::saveAll(rules);
    }

    bool getRulesByDeviceUid(const std::string& deviceUid, std::vector<Rule>& rules) override {    
        return filterRules([&deviceUid](const Rule& rule) { return rule.deviceUid == deviceUid; }, rules);
    }

    bool getRulesByType(RuleType type, std::vector<Rule>& rules) override {    
        return filterRules([&type](const Rule& rule) { return rule.ruleType == type; }, rules);
    }
};