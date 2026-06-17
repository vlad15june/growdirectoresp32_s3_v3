#pragma once

#include <string>
#include "Core/Domain/Data/Entities/Rule.h"

class IRuleStateNotifier {
public:
    virtual ~IRuleStateNotifier() = default;

    virtual void notifySensorUnreachable(const Rule& rule, const std::string& sensorUid) = 0;
    virtual void notifyRuleRecovered(const Rule& rule) = 0;
    virtual void notifyProtectiveShutdown(const Rule& rule) = 0;
};
