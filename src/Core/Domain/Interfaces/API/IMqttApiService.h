#pragma once

#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"

class IMqttApiService {
public:
    virtual ~IMqttApiService() = default;

    virtual bool isModuleOnline() = 0;

    virtual void sendToServerModuleStructure(const ModuleStructure& moduleStructure) = 0;

    virtual bool sendDiagnosticData(const DiagnosticDto& diagnosticData) = 0;

    /**
     * @brief Sends module update data through MQTT
     * @param updateData The update data to send
     * @return true if the data was sent successfully, false otherwise
     */
    virtual bool sendModuleUpdateData(const UpdateDto& updateData) = 0;
};
