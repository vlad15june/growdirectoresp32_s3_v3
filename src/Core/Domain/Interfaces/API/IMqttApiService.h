#pragma once

#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"

class IMqttApiService {
public:
    virtual ~IMqttApiService() = default;

    virtual bool isModuleOnline() = 0;

    virtual void sendToServerModuleStructure(const ModuleStructure& moduleStructure) = 0;

    virtual bool sendDiagnosticData(const DiagnosticDto& diagnosticData) = 0;
};
