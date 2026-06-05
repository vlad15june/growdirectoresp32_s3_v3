#ifndef MOCK_SPIFFS_RULE_DAO_H
#define MOCK_SPIFFS_RULE_DAO_H

#include <string>
#include <vector>
#include <algorithm>
#include "Infrastructure/Adapters/SPIFFS/ISpiffsRuleDao.h"
#include "Core/Domain/Data/Entities/Rule.h"

class MockSpiffsRuleDao : public ISpiffsRuleDao {
private:
    std::vector<Rule> storage;
    bool shouldFail = false;

public:
    void setShouldFail(bool fail) { shouldFail = fail; }
    void clearStorage() { storage.clear(); }
    const std::vector<Rule>& getStorage() const { return storage; }

    bool save(const Rule& entity, const std::string& uid) override {
        if (shouldFail) return false;
        auto it = std::find_if(storage.begin(), storage.end(),
            [&uid](const Rule& r) { return r.uid == uid; });
        if (it != storage.end()) *it = entity; else storage.push_back(entity);
        return true;
    }

    bool getById(const std::string& uid, Rule& entity) override {
        auto it = std::find_if(storage.begin(), storage.end(),
            [&uid](const Rule& r) { return r.uid == uid; });
        if (it == storage.end()) return false;
        entity = *it;
        return true;
    }

    bool remove(const std::string& uid) override {
        auto it = std::find_if(storage.begin(), storage.end(),
            [&uid](const Rule& r) { return r.uid == uid; });
        if (it == storage.end()) return false;
        storage.erase(it);
        return true;
    }

    bool saveAll(const std::vector<Rule>& entities) override {
        if (shouldFail) return false;
        storage = entities;
        return true;
    }

    bool getAll(std::vector<Rule>& entities) override {
        entities = storage;
        return true;
    }

    bool clear() override {
        storage.clear();
        return true;
    }

    bool saveRule(const Rule& rule) override { return save(rule, rule.uid); }
    bool getRule(const std::string& uid, Rule& rule) override { return getById(uid, rule); }
    bool deleteRule(const std::string& uid) override { return remove(uid); }
    bool getRules(std::vector<Rule>& rules) override { return getAll(rules); }
    bool saveRules(const std::vector<Rule>& rules) override { return saveAll(rules); }
    bool getRulesByDeviceUid(const std::string&, std::vector<Rule>&) override { return true; }
    bool getRulesByType(RuleType, std::vector<Rule>&) override { return true; }
};

#endif // MOCK_SPIFFS_RULE_DAO_H
