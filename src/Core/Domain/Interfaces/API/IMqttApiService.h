#pragma once

#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/UpdateDto.h"

class IMqttApiService {
public:
    virtual ~IMqttApiService() = default;

    virtual bool isModuleOnline() = 0;

    virtual bool sendToServerModuleStructure(const ModuleStructure& moduleStructure) = 0;

    virtual bool sendDiagnosticData(const DiagnosticDto& diagnosticData) = 0;

    /**
     * @brief Sends module update data through MQTT
     * @param updateData The update data to send
     * @return true if the data was sent successfully, false otherwise
     */
    virtual bool sendModuleUpdateData(const UpdateDto& updateData) = 0;
};
