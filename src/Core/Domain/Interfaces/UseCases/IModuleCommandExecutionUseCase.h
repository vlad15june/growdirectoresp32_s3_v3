#pragma once

#include <string>
#include <vector>
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Core/Domain/Data/Entities/Sensor/SensorTypeCatalog.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"
#include "Core/Domain/Enums/ModuleDeletionStatusEnum.h"
#include "Core/Domain/Interfaces/UseCases/IModbusAddressAllocator.h"

/**
 * @brief Result of module command execution
 */
struct ModuleCommandExecutionResult {
    std::string uid;
    std::string status;
    std::string errorMessage;
    ModuleCommandResult result;
    
    ModuleCommandExecutionResult(const std::string& uid, const std::string& status, const std::string& errorMessage = "")
        : uid(uid), status(status), errorMessage(errorMessage) {}
        
    ModuleCommandExecutionResult(const std::string& uid, const std::string& status, const std::string& errorMessage, const ModuleCommandResult& result)
        : uid(uid), status(status), errorMessage(errorMessage), result(result) {}
};

/**
 * @brief Result of a raw bus command execution
 */
struct RawBusCommandResult {
    ModuleCommandStatus status;
    std::string errorMessage;
    std::string dataHex;
    bool hasData;
    int channel;

    RawBusCommandResult()
        : status(ModuleCommandStatus::ERROR), hasData(false), channel(0) {}

    RawBusCommandResult(ModuleCommandStatus status, const std::string& errorMessage, const std::string& dataHex, bool hasData, int channel)
        : status(status), errorMessage(errorMessage), dataHex(dataHex), hasData(hasData), channel(channel) {}
};

/**
 * @brief One complex module command, split into the parts that different tasks run.
 *
 * A complex command mixes three kinds of work: reading and writing loop-owned state (the
 * repositories, the module structure), waiting on the RS-485 line, and waiting on the network.
 * Run as one handler they would all be one task's time; the split keeps every kind on the task
 * that owns it:
 *
 *   - `prepareComplexCommand()` runs on the control loop. It validates the command and snapshots
 *     everything the slow part will need - the sensor-type candidates, the entity to verify, the
 *     module uid - because the repositories must not be read from another task.
 *   - `executeBusWork()` runs on the acquisition task, the owner of the RS-485 line. Bus
 *     transactions only; it touches no repository.
 *   - `executeNetworkWork()` runs on the command network task. HTTP, WiFi and the firmware
 *     download only.
 *   - `finishComplexCommand()` runs on the control loop again: it applies the outcome to the
 *     loop-owned state and produces the result the backend sees.
 *
 * The synchronous path still exists - executeCommands() composes exactly these four steps inline -
 * so there is one copy of the logic, not a fast one and a tested one. On the device CommandService
 * runs the complex commands through the steps above and calls executeCommands() for the local ones.
 */
struct ComplexCommandWork {
    ModuleCommandDto command;
    /** True: the slow part belongs to the acquisition task. False: to the network task. */
    bool onBus = false;
    /** ScanSensors with an empty local catalog: fetch the catalog from the server first. */
    bool fetchCatalog = false;
    /**
     * ScanSensors after its catalog fetch, waiting for a catalog on its way to flash. No task
     * executes this stage: the caller holds it until catalogStorePending() answers false, then
     * hands it back to finishComplexCommand(), which goes on to the scan.
     */
    bool awaitCatalogStore = false;
    int pin = -1;
    /**
     * The candidates a scan tries, snapshotted from the repository on the control loop.
     *
     * A handle to the catalog the repository holds, not a list of its own: the work travels
     * through a queue to another task, and a scan on a forty-type account would otherwise carry
     * two more whole catalogs - one taken from the repository, one copied into the queue.
     */
    SensorTypeCatalog sensorTypes;
    /** SetSensorModel: the entity to verify on the line, prebuilt on the control loop. */
    SensorEntity sensorToVerify;
    /** ScanSensors: the uid prefix for a detected sensor. */
    std::string moduleUid;
    /**
     * ModBus addresses of the module other entities already hold, snapshotted from the sensor
     * repository on the control loop. The address resolution runs on the bus task and asks this
     * snapshot rather than the repository, for the same reason the sensor-type candidates above
     * are snapshotted: the repositories belong to the loop.
     */
    ModbusAddressOccupancy occupiedModbusAddresses;

    /** @brief The candidates to try, or an empty list when the work carries no catalog. */
    const std::vector<SensorType>& candidates() const {
        static const std::vector<SensorType> none;
        return sensorTypes ? *sensorTypes : none;
    }
};

/**
 * @brief What the slow part of a complex command came back with.
 */
struct ComplexCommandOutcome {
    ModuleCommandStatus status = ModuleCommandStatus::ERROR;
    std::string errorMessage;
    /** Scans: a sensor answered, and `sensor` is the entity it answered as. */
    bool detected = false;
    SensorEntity sensor;
    /** Catalog fetch: what the server handed back. */
    std::vector<SensorType> fetchedTypes;
    /** UpdateFirmware: what the updater answered. */
    bool networkOk = false;
    /**
     * UpdateFirmware: the FIRMWARE_UPDATE_STATE_* value the session ended with, 0 when none was
     * recorded. Carried here because the updater does not persist it - the settings sector is
     * written on the control loop, in the finish step.
     */
    uint8_t firmwareUpdateStatus = 0;
    /**
     * UpdateFirmware: the HTTP code the download attempt got back, 0 when no request was made.
     * Carried here for the same reason as the status above: the decision it feeds - keep the
     * update for the next boot, which writes the settings and the pending-update file - belongs
     * to the finish step, on the control loop.
     */
    int firmwareHttpCode = 0;
    /** DeleteModule: the server's verdict. */
    ModuleDeletionStatusEnum deletionStatus = ModuleDeletionStatusEnum::UNKNOWN;
};

/**
 * @brief Interface for module command execution use case
 */
class IModuleCommandExecutionUseCase
{
public:
    virtual ~IModuleCommandExecutionUseCase() = default;

    /**
     * @brief Execute a single module command
     * @param command Command name (e.g., "BlinkingStart")
     * @param options JSON string containing command options
     * @param commandUid Command unique identifier
     * @param createdAt Timestamp when command was created
     * @return true if command was executed successfully, false otherwise
     */
    virtual bool execute(const std::string &command, const std::string &options, const std::string &commandUid, const std::string &createdAt) = 0;

    /**
     * @brief Execute multiple module commands
     * @param moduleCommands Vector of module commands to execute
     * @return Vector of execution results
     */
    virtual std::vector<ModuleCommandExecutionResult> executeCommands(const std::vector<ModuleCommandDto>& moduleCommands) = 0;

    /**
     * @brief Execute a raw bus command (ModbusWrite/ModbusRead/SerialWrite) inline
     * @param command Command name
     * @param options JSON string containing command parameters
     * @return RawBusCommandResult with status and optional returned data
     */
    virtual RawBusCommandResult executeRaw(const std::string &command, const std::string &options) = 0;

    // --- The complex commands, split by the task that runs each part ---------------------------

    /**
     * @brief Validate a complex command and snapshot what its slow part needs. Control loop only.
     * @param command The command as the server sent it
     * @param work Filled with the work to hand to the bus or network task
     * @param immediate Filled with the final result when there is nothing slow to do
     * @return true when `work` must be executed; false when `immediate` already answers
     */
    virtual bool prepareComplexCommand(const ModuleCommandDto &command,
                                       ComplexCommandWork &work,
                                       ModuleCommandExecutionResult &immediate) = 0;

    /** @brief The bus transactions of a complex command. Acquisition task only. */
    virtual ComplexCommandOutcome executeBusWork(const ComplexCommandWork &work) = 0;

    /** @brief The network waits of a complex command. Command network task only. */
    virtual ComplexCommandOutcome executeNetworkWork(const ComplexCommandWork &work) = 0;

    /**
     * @brief Apply an outcome to loop-owned state and produce the backend-visible result.
     *        Control loop only.
     * @param result Filled with the final result when the command is complete
     * @param nextWork Filled with the next slow part when it is not (ScanSensors after its
     *        catalog fetch)
     * @return true when `result` is final; false when `nextWork` must be executed next
     */
    virtual bool finishComplexCommand(const ComplexCommandWork &work,
                                      const ComplexCommandOutcome &outcome,
                                      ModuleCommandExecutionResult &result,
                                      ComplexCommandWork &nextWork) = 0;

    /**
     * @brief Whether a sensor type catalog is on its way to flash. Control loop only.
     *
     * A stage marked `awaitCatalogStore` is held until this answers false and is then handed back
     * to finishComplexCommand() with an empty outcome. Default: never, for implementations that
     * produce no such stage.
     */
    virtual bool catalogStorePending() { return false; }
};
