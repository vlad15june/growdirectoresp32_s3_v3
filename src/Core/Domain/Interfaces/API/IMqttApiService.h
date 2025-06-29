#pragma once

#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"

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

    /**
     * @brief Send device command execution result via MQTT
     * @param commandUpdate The device command update containing the execution result
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendDeviceCommandExecutionResult(const DeviceCommandUpdateDto& commandUpdate) = 0;

    /**
     * @brief Send command result via MQTT
     * @param commandResult The command result containing status and command type
     * @return true if the operation was successful, false otherwise
     */
    virtual bool sendCommandResult(const CommandResultDto& commandResult) = 0;
};
