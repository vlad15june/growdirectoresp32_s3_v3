#pragma once

#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include "Application/DTOs/RawIoResultDto.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"

#include <vector>

/**
 * @brief What a command report carries from the control loop to the network task.
 *
 * CommandService produces five different reports - the generic command acknowledgement, the device
 * command status, the module command status, the raw bus IO result and the module structure after
 * a command changed it. They go through the outbox as events, and an event travels as bytes owned
 * by the outbox rather than as a reference to state the control loop still owns.
 *
 * One struct with five members rather than five structs: the outbox kind already says which member
 * is meant, and a report is allocated one at a time, only when a command has actually been dealt
 * with. The unused members are empty strings and empty vectors.
 *
 * The DTOs are the same ones IHybridAPIService takes, deliberately. The network task hands them
 * straight to the corresponding API methods, so nothing about the request that reaches the
 * backend - endpoint, JSON, topic, order of fields - depends on which task made it.
 */
struct CommandReportDto
{
    /** COMMAND_RESULT: status/command/message triple, as sendCommandResult() takes it. */
    CommandResultDto commandResult;

    /** DEVICE_COMMAND_RESULT: one device command's outcome. */
    DeviceCommandUpdateDto deviceCommandUpdate;

    /** MODULE_COMMAND_RESULT: module command outcomes, sent as one batch like before. */
    std::vector<ModuleCommandUpdateDto> moduleCommandUpdates;

    /** RAW_IO_RESULT: the bytes a raw bus read returned. */
    RawIoResultDto rawIoResult;

    /** MODULE_STRUCTURE: a copy, not a pointer - the loop keeps changing the original. */
    ModuleStructure moduleStructure;

    /**
     * Which transport the report asks for. Carried rather than defaulted because the choice is
     * part of the existing behaviour: the raw and delete acknowledgements go MQTT_PREFERRED and
     * the module structure goes BOTH.
     */
    ApiPriority priority = ApiPriority::UNDEFINED;
};
