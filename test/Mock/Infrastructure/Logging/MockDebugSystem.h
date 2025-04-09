#ifndef MOCK_DEBUG_SYSTEM_H
#define MOCK_DEBUG_SYSTEM_H

#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <map>
#include <string>
#include <vector>

// Simplified MockDebugFeature
class MockDebugFeature : public IDebugFeature {
private:
    std::string name;
    std::string mqttTopic;
    bool enabled;

public:
    MockDebugFeature(const char* featureName, const char* topic, bool initialState) 
        : name(featureName ? featureName : ""), 
          mqttTopic(topic ? topic : ""),
          enabled(initialState) {}

    const char* getName() const override {
        return name.c_str();
    }

    const char* getMqttTopic() const override {
        return mqttTopic.c_str();
    }

    bool isEnabled() const override {
        return enabled;
    }

    void setEnabled(bool newState) override {
        enabled = newState;
    }
};

// Simplified MockDebugSystem
class MockDebugSystem : public IDebugSystem {
public:
    bool featureEnabled = true;
    
    IDebugFeature* registerFeature(const char* name, const char* mqttTopic = nullptr, bool initialState = false) override {
        return nullptr;
    }
    
    IDebugFeature* getFeature(const char* name) override {
        return nullptr;
    }
    
    void setDebugFeatureState(const char *feature, bool isEnabled) override {
        // Do nothing
    }
    
    bool isFeatureEnabled(const char* featureName) override {
        return featureEnabled;
    }
    
    void reset() {
        featureEnabled = true;
    }
};

#endif // MOCK_DEBUG_SYSTEM_H
