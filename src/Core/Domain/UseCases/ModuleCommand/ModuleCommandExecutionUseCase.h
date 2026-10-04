#pragma once

#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/IClearModuleDataService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IWifiManager.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/UseCases/ICheckModuleDeletionUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IFirmwareUpdateRecoveryUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ISensorStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IModbusAddressAllocator.h"
#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRawBusAccess.h"
#include "Core/Domain/Interfaces/Udp/IRemoteModuleCommandSender.h"
#include "Core/Utils/HexCodec.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include "Core/Domain/Enums/ModuleCommandEnum.h"
#include "Core/Domain/Enums/ModuleCommandStatus.h"
#include "Core/Domain/Enums/SensorStatusEnum.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h" //TODO: move to Core/Domain/Interfaces/Adapters/Time/INTPTimeAdapter.h
#include "constants.h"
#include <cstdarg>
#include <string>

/**
 * @brief Implementation of module command execution use case
 */
class ModuleCommandExecutionUseCase : public IModuleCommandExecutionUseCase
{
private:
    ILogger *logger;
    IDebugSystem *debugSystem;
    INTPTimeAdapter *timeAdapter;
    IFirmwareRemoteUpdate *firmwareUpdater;
    IArduinoAdapter *arduinoAdapter;
    ISensorRepository *sensorRepository;
    ISensorTypeRepository *sensorTypeRepository;
    ISensorReader *sensorReader;
    IModuleConfig *moduleConfig;
    ISettings *settings;
    IHybridAPIService *hybridApiService;
    IClearModuleDataService *clearModuleDataService;
    ModuleStructure *moduleStructure;
    ISensorValueModel *sensorValueModel;
    IWifiManager *wifiManager;
    ILedUseCases *ledUseCases;
    IRawBusAccess *rawBus;
    ICheckModuleDeletionUseCase *checkModuleDeletionUseCase;
    IRemoteModuleCommandSender *remoteModuleCommandSender = nullptr;
    IFirmwareUpdateRecoveryUseCase *firmwareUpdateRecoveryUseCase = nullptr;
    ISensorStructureUseCase *sensorStructureUseCase = nullptr;
    IModbusAddressAllocator *modbusAddressAllocator = nullptr;
    // Stores the catalog a ScanSensors command fetched; without it the command stores it itself.
    ISyncSensorTypesUseCase *catalogSync = nullptr;
    // Raised while runComplexInline() composes a command on its caller's stack.
    bool composingInline = false;
    static const unsigned long commandMaxAgeSeconds = 300;
    static const unsigned long rawReadTimeoutMs = 1000;

public:
    ModuleCommandExecutionUseCase(ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr, INTPTimeAdapter *timeAdapter = nullptr, IFirmwareRemoteUpdate *firmwareUpdater = nullptr, IArduinoAdapter *arduinoAdapter = nullptr, ISensorRepository *sensorRepository = nullptr, ISensorTypeRepository *sensorTypeRepository = nullptr, ISensorReader *sensorReader = nullptr, IModuleConfig *moduleConfig = nullptr, ISettings *settings = nullptr, IHybridAPIService *hybridApiService = nullptr, IClearModuleDataService *clearModuleDataService = nullptr, ModuleStructure *moduleStructure = nullptr, ISensorValueModel *sensorValueModel = nullptr, IWifiManager *wifiManager = nullptr, ILedUseCases *ledUseCases = nullptr, IRawBusAccess *rawBus = nullptr, ICheckModuleDeletionUseCase *checkModuleDeletionUseCase = nullptr, IRemoteModuleCommandSender *remoteModuleCommandSender = nullptr, IFirmwareUpdateRecoveryUseCase *firmwareUpdateRecoveryUseCase = nullptr, IModbusAddressAllocator *modbusAddressAllocator = nullptr)
        : logger(logger), debugSystem(debugSystem), timeAdapter(timeAdapter), firmwareUpdater(firmwareUpdater), arduinoAdapter(arduinoAdapter), sensorRepository(sensorRepository), sensorTypeRepository(sensorTypeRepository), sensorReader(sensorReader), moduleConfig(moduleConfig), settings(settings), hybridApiService(hybridApiService), clearModuleDataService(clearModuleDataService), moduleStructure(moduleStructure), sensorValueModel(sensorValueModel), wifiManager(wifiManager), ledUseCases(ledUseCases), rawBus(rawBus), checkModuleDeletionUseCase(checkModuleDeletionUseCase), remoteModuleCommandSender(remoteModuleCommandSender), firmwareUpdateRecoveryUseCase(firmwareUpdateRecoveryUseCase), modbusAddressAllocator(modbusAddressAllocator) {}
    
    virtual ~ModuleCommandExecutionUseCase() = default;

    void setRemoteModuleCommandSender(IRemoteModuleCommandSender* sender) { remoteModuleCommandSender = sender; }

    void setSensorStructureUseCase(ISensorStructureUseCase* useCase) { sensorStructureUseCase = useCase; }

    /**
     * @brief The use case a ScanSensors command hands the catalog it fetched to.
     *
     * The synchronization merges it and writes it through the storage task, as it writes the
     * catalogs it fetches itself; the command goes on to its scan once the catalog has landed.
     */
    void setSyncSensorTypesUseCase(ISyncSensorTypesUseCase* useCase) { catalogSync = useCase; }

    /**
     * @brief Execute module commands from JSON options
     * @param command Command name (e.g., "BlinkingStart")
     * @param options JSON string containing array of module commands
     * @param commandUid Command unique identifier
     * @param createdAt Timestamp when command was created
     * @return true if all commands were executed successfully, false otherwise
     */
    bool execute(const std::string &command, const std::string &options, const std::string &commandUid, const std::string &createdAt) override
    {
        ModuleCommandEnum commandEnum = getModuleCommandEnum(command);
        
        switch (commandEnum) {
            case ModuleCommandEnum::BLINKING_START:
                return handleBlinkingStart("", options);
            case ModuleCommandEnum::BLINKING_STOP:
                return handleBlinkingStop("", options);
            case ModuleCommandEnum::REBOOT:
            {
                ModuleCommandDto moduleCommand;
                moduleCommand.command = command;
                moduleCommand.options = options;
                moduleCommand.uid = commandUid;
                moduleCommand.createdAt = createdAt;
                return handleReboot(moduleCommand);
            }
            default:
                return false;
        }
    }

    /**
     * @brief Execute multiple module commands
     * @param moduleCommands Vector of module commands to execute
     * @return Vector of execution results
     */
    std::vector<ModuleCommandExecutionResult> executeCommands(const std::vector<ModuleCommandDto>& moduleCommands) override
    {
        //TODO: bad arch!!! do not use ModuleCommandDto! Create separate Entity for Domain layer

        std::vector<ModuleCommandExecutionResult> results;
        
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Processing %d module commands", moduleCommands.size());

        // Execute each command one by one
        for (const auto& moduleCommand : moduleCommands)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Processing command - command: %s, commandUid: %s",
                moduleCommand.command.c_str(), moduleCommand.uid.c_str());

            ModuleCommandStatus status = ModuleCommandStatus::ERROR;
            std::string errorMessage = "Unknown command";
            ModuleCommandResult cmdResult;
            bool isExpired = false;

            // Check if command is expired
            if (!moduleCommand.createdAt.empty())
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Checking command time - createdAt: %s", moduleCommand.createdAt.c_str());
                isExpired = !checkCommandTime(moduleCommand.createdAt);
            }

            if (isExpired)
            {
                status = ModuleCommandStatus::ERROR;
                errorMessage = "Command is expired";
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "MODULE_COMMAND: command expired uid=%s createdAt=%s",
                    moduleCommand.uid.c_str(), moduleCommand.createdAt.c_str());
            }
            else
            {
                // Process specific commands
                ModuleCommandEnum commandEnum = getModuleCommandEnum(moduleCommand.command);
                
                switch (commandEnum) {
                    case ModuleCommandEnum::ADD_WIFI:
                    {
                        // Inline, like BlinkingStart: AddWifi waits for nothing - no bus, no
                        // network. It writes EEPROM, SPIFFS and the WiFiMulti list, all of which
                        // the control loop also writes, so executing it anywhere else would
                        // create a second writer for no wait saved.
                        auto result = handleAddWifi(moduleCommand);
                        status = result.first;
                        errorMessage = result.second;
                        break;
                    }
                    case ModuleCommandEnum::UPDATE_FIRMWARE:
                    case ModuleCommandEnum::PING_PIN:
                    case ModuleCommandEnum::SET_SENSOR_MODEL:
                    case ModuleCommandEnum::SCAN_SENSORS:
                    case ModuleCommandEnum::DELETE_MODULE:
                    {
                        // The complex commands run through the same prepare/execute/finish steps
                        // the asynchronous path uses, composed inline: one copy of the logic.
                        // CommandService routes these commands to the acquisition and network
                        // tasks and does not reach this case on the device; the inline shape
                        // serves a caller that needs the answer on its own call stack.
                        ModuleCommandExecutionResult r = runComplexInline(moduleCommand);
                        status = getModuleCommandStatus(r.status);
                        errorMessage = r.errorMessage;
                        cmdResult = r.result;
                        break;
                    }
                    case ModuleCommandEnum::BLINKING_START:
                    {
                        bool success = handleBlinkingStart("", moduleCommand.options);
                        status = success ? ModuleCommandStatus::DONE : ModuleCommandStatus::ERROR;
                        errorMessage = success ? "" : "BlinkingStart command failed";
                        break;
                    }
                    case ModuleCommandEnum::BLINKING_STOP:
                    {
                        bool success = handleBlinkingStop("", moduleCommand.options);
                        status = success ? ModuleCommandStatus::DONE : ModuleCommandStatus::ERROR;
                        errorMessage = success ? "" : "BlinkingStop command failed";
                        break;
                    }
                    case ModuleCommandEnum::REBOOT:
                    {
                        bool success = handleReboot(moduleCommand);
                        status = success ? ModuleCommandStatus::DONE : ModuleCommandStatus::ERROR;
                        errorMessage = success ? "" : "Reboot command failed";
                        break;
                    }
                    case ModuleCommandEnum::DELETE_SENSORS:
                    {
                        auto result = handleDeleteSensors(moduleCommand);
                        status = result.status;
                        errorMessage = result.errorMessage;
                        cmdResult = result.result;
                        break;
                    }
                    case ModuleCommandEnum::UNKNOWN:
                    default:
                        // Keep default error message
                        break;
                }
            }

            std::string statusStr = getModuleCommandStatusName(status);
            
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Command execution result - command: %s, status: %s, error: %s",
                moduleCommand.command.c_str(), statusStr.c_str(), errorMessage.c_str());

            results.emplace_back(moduleCommand.uid, statusStr, errorMessage, cmdResult);
        }

        return results;
    }

    RawBusCommandResult executeRaw(const std::string &command, const std::string &options) override
    {
        ModuleCommandEnum commandEnum = getModuleCommandEnum(command);

        switch (commandEnum) {
            case ModuleCommandEnum::RAW_MODBUS_WRITE:
                return handleModbusWrite(options);
            case ModuleCommandEnum::RAW_MODBUS_READ:
                return handleModbusRead(options);
            case ModuleCommandEnum::RAW_SERIAL_WRITE:
                return handleSerialWrite(options);
            case ModuleCommandEnum::REBOOT_REMOTE_MODULE:
                return handleRebootRemoteModule(options);
            default:
                return RawBusCommandResult(ModuleCommandStatus::ERROR, "Unknown raw command", "", false, 0);
        }
    }

    // --- The complex commands, split by the task that runs each part ---------------------------
    //
    // The statuses and error messages these produce are exactly the ones an inline
    // handler produces: the backend sees the same reports whichever task earns them.

    bool prepareComplexCommand(const ModuleCommandDto &moduleCommand,
                               ComplexCommandWork &work,
                               ModuleCommandExecutionResult &immediate) override
    {
        work = ComplexCommandWork();
        work.command = moduleCommand;

        // The same age gate executeCommands() applies, kept here because the asynchronous path
        // enters through this method: a command must not become executable by taking the other
        // road in.
        if (!moduleCommand.createdAt.empty() && !checkCommandTime(moduleCommand.createdAt))
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "MODULE_COMMAND: command expired uid=%s createdAt=%s",
                moduleCommand.uid.c_str(), moduleCommand.createdAt.c_str());
            immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR, "Command is expired");
            return false;
        }

        switch (getModuleCommandEnum(moduleCommand.command)) {
            case ModuleCommandEnum::UPDATE_FIRMWARE:
            {
                if (moduleCommand.options.empty())
                {
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR, "Missing options");
                    return false;
                }
                std::string url = extractJsonValue(moduleCommand.options, "url");
                std::string version = extractJsonValue(moduleCommand.options, "version");
                if (url.empty() || version.empty())
                {
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Missing url or version in options");
                    return false;
                }
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Firmware update - URL: %s, Version: %s, UID: %s",
                    url.c_str(), version.c_str(), moduleCommand.uid.c_str());

                // The same question the boot check asks, on the door a signalled command comes
                // through: while the running image is on probation the download would write the
                // slot the bootloader falls back to, and this boot has not yet earned the right
                // to spend it. The update waits here exactly as it waits at boot - in the file,
                // started by the control loop once the image is confirmed - and the server is
                // told what it is told on every other path that takes an update on: in progress.
                if (firmwareUpdateRecoveryUseCase &&
                    firmwareUpdateRecoveryUseCase->isRunningImageOnProbation())
                {
                    if (firmwareUpdateRecoveryUseCase->holdUntilImageConfirmed(moduleCommand))
                    {
                        immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::IN_PROGRESS);
                        return false;
                    }

                    // One update is held per boot and this is not it. Refused rather than
                    // downloaded: the slot is the same slot. Unlike the boot check, this path
                    // owes the server an outcome for every command of the list it answers, so
                    // the refusal is named instead of left silent.
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Another firmware update is waiting for the running "
                                           "image to be confirmed");
                    return false;
                }

                // A link the module already reports as down does not get a download attempt: the
                // update is kept for the next boot and the module restarts into it. Decided here,
                // in the step that runs on the control loop, because keeping it writes the
                // settings, the pending-update file and the requested module mode - the state
                // this step owns. The download task is never handed work that cannot start.
                if (firmwareUpdateRecoveryUseCase &&
                    firmwareUpdateRecoveryUseCase->isHttpConnectionDown() &&
                    firmwareUpdateRecoveryUseCase->keepForNextBoot(moduleCommand))
                {
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::IN_PROGRESS);
                    return false;
                }

                work.onBus = false;
                return true;
            }
            case ModuleCommandEnum::DELETE_MODULE:
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Processing DeleteModule command - uid: %s",
                    moduleCommand.uid.c_str());
                if (!checkModuleDeletionUseCase)
                {
                    logError("ModuleCommandUseCase: CheckModuleDeletionUseCase not available for module deletion verification");
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Deletion check use case not available");
                    return false;
                }
                work.onBus = false;
                return true;
            }
            case ModuleCommandEnum::PING_PIN:
            {
                if (moduleCommand.options.empty())
                {
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR, "Missing options");
                    return false;
                }
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: PingPin options: %s", moduleCommand.options.c_str());

                int pin = convertPinFromJson(extractJsonIntValue(moduleCommand.options, "pin"));
                if (pin < 0 || pin >= moduleConfig->getSensorPinCount())
                {
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Missing or invalid pin in options");
                    return false;
                }
                if (!sensorTypeRepository || !sensorReader)
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Sensor repository or reader not available for PingPin");
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Sensor service not available");
                    return false;
                }

                // Snapshotted here, on the control loop: the repository must not be read from the
                // acquisition task. The whole catalog travels, and the scan tries the types it
                // marks as scannable - counted here, because a pin ping with nothing to try is
                // answered now rather than by an empty pass on the line.
                SensorTypeCatalog catalog = sensorTypeRepository->catalogSnapshot();
                int scannableCount = 0;
                for (const auto &sensorType : *catalog)
                {
                    if (sensorType.canScan)
                    {
                        scannableCount++;
                    }
                }
                if (scannableCount == 0)
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: No scannable sensor types available for scanning");
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "No sensor types available");
                    return false;
                }

                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: PingPin - got %d scannable sensor type(s)",
                    scannableCount);

                work.onBus = true;
                work.pin = pin;
                work.sensorTypes = catalog;
                return true;
            }
            case ModuleCommandEnum::SET_SENSOR_MODEL:
            {
                if (moduleCommand.options.empty())
                {
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR, "Missing options");
                    return false;
                }
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: SetSensorModel options: %s", moduleCommand.options.c_str());

                int pin = convertPinFromJson(extractJsonIntValue(moduleCommand.options, "pin"));
                std::string modelUid = extractJsonValue(moduleCommand.options, "uid");

                if ((pin < 0 || pin >= moduleConfig->getSensorPinCount()) || modelUid.empty())
                {
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Missing pin or uid in options");
                    return false;
                }
                if (!sensorTypeRepository || !sensorReader || !sensorRepository || !moduleConfig ||
                    !modbusAddressAllocator)
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Sensor services not available for SetSensorModel");
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Sensor service not available");
                    return false;
                }

                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: SetSensorModel command - Pin: %d, ModelUID: %s",
                    pin, modelUid.c_str());

                // One type, looked up by the uid the command names: the whole catalog is walked
                // to find it, and only the one found is held.
                SensorTypeCatalog catalog = sensorTypeRepository->catalogSnapshot();
                const SensorType* targetSensorType = nullptr;
                for (const auto &sensorType : *catalog)
                {
                    if (sensorType.uid == modelUid)
                    {
                        targetSensorType = &sensorType;
                        break;
                    }
                }
                if (!targetSensorType)
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Sensor type with UID %s not found", modelUid.c_str());
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Sensor type not found");
                    return false;
                }

                std::string moduleUid = moduleConfig->getModuleUid();
                char pinStr[3];
                sprintf(pinStr, "%02d", pin);
                std::string generatedSensorUid = moduleUid + "." + pinStr;

                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Generated sensor UID: %s", generatedSensorUid.c_str());

                SensorEntity newSensorEntity;
                newSensorEntity.uid = generatedSensorUid;
                newSensorEntity.pin = pin;
                newSensorEntity.sensorType = targetSensorType->name;
                newSensorEntity.model = targetSensorType->name;
                newSensorEntity.protocol = targetSensorType->getProtocolString();
                newSensorEntity.sensorTypeUid = targetSensorType->uid;
                newSensorEntity.modbusDeviceAddress = targetSensorType->modbusDeviceAddress;
                newSensorEntity.baudRate = targetSensorType->baudRate;
                newSensorEntity.modbusPingAddress = targetSensorType->modbusPingAddress;
                newSensorEntity.registerToStoreModbusDeviceAddress = targetSensorType->registerToStoreModbusDeviceAddress;
                newSensorEntity.writeRegistersCommand = targetSensorType->writeRegistersCommand;
                newSensorEntity.applyAddressAfterPowerCycle = targetSensorType->applyAddressAfterPowerCycle;
                newSensorEntity.i2cAddress = targetSensorType->i2cAddress;
                newSensorEntity.chipType = targetSensorType->chipType;
                newSensorEntity.modbusRegisterConfigs = targetSensorType->modbusRegisterConfig;
                newSensorEntity.i2cRegisterConfigs = targetSensorType->i2cRegisterConfig;
                newSensorEntity.isActive = true;
                newSensorEntity.isError = false;
                newSensorEntity.status = SensorStatusEnum::SENSOR_STATUS_ADDED;

                work.onBus = true;
                work.pin = pin;
                work.sensorToVerify = newSensorEntity;
                // Read here, on the loop that owns the repository; the resolution on the bus task
                // works from this snapshot. The entity being configured is left out of it, so a
                // sensor that is being given a new model does not count as its own competitor.
                work.occupiedModbusAddresses =
                    modbusAddressAllocator->occupiedAddresses(generatedSensorUid);
                return true;
            }
            case ModuleCommandEnum::SCAN_SENSORS:
            {
                if (moduleCommand.options.empty())
                {
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR, "Missing options");
                    return false;
                }
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: ScanSensors options: %s", moduleCommand.options.c_str());

                int pin = convertPinFromJson(extractJsonIntValue(moduleCommand.options, "pin"));
                if (pin < 0 || pin >= moduleConfig->getSensorPinCount())
                {
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Missing or invalid pin in options");
                    return false;
                }
                if (!sensorTypeRepository || !sensorReader || !moduleConfig || !modbusAddressAllocator)
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Sensor services not available for ScanSensors");
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "Sensor service not available");
                    return false;
                }

                SensorTypeCatalog catalog = sensorTypeRepository->catalogSnapshot();
                if (catalog->empty())
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: No sensor types in repository, fetching from server");
                    if (hybridApiService)
                    {
                        // The catalog fetch is network work; the scan that follows is bus work.
                        // The chain is expressed by finishComplexCommand() handing back nextWork.
                        work.onBus = false;
                        work.fetchCatalog = true;
                        work.pin = pin;
                        return true;
                    }
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: No sensor types available for scanning after sync");
                    immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                                           "No sensor types available");
                    return false;
                }

                work.onBus = true;
                work.pin = pin;
                work.sensorTypes = catalog;
                work.moduleUid = moduleConfig->getModuleUid();
                work.occupiedModbusAddresses = occupancyForScan(work.moduleUid, pin);
                return true;
            }
            default:
                immediate = makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR, "Unknown command");
                return false;
        }
    }

    ComplexCommandOutcome executeBusWork(const ComplexCommandWork &work) override
    {
        ComplexCommandOutcome outcome;

        switch (getModuleCommandEnum(work.command.command)) {
            case ModuleCommandEnum::PING_PIN:
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: PingPin command - scanning pin %d", work.pin);

                for (const auto &sensorType : work.candidates())
                {
                    // A pin ping tries what the catalog says can be scanned for; the rest of it
                    // has no scan to answer.
                    if (!sensorType.canScan)
                    {
                        continue;
                    }

                    SensorEntity tempSensor;
                    tempSensor.pin = work.pin;
                    tempSensor.sensorType = sensorType.name;
                    tempSensor.model = sensorType.name;
                    tempSensor.protocol = sensorType.getProtocolString();
                    tempSensor.modbusDeviceAddress = sensorType.modbusDeviceAddress;
                    tempSensor.baudRate = sensorType.baudRate;
                    tempSensor.modbusPingAddress = sensorType.modbusPingAddress;
                    tempSensor.registerToStoreModbusDeviceAddress = sensorType.registerToStoreModbusDeviceAddress;
                    tempSensor.writeRegistersCommand = sensorType.writeRegistersCommand;
                    tempSensor.applyAddressAfterPowerCycle = sensorType.applyAddressAfterPowerCycle;
                    tempSensor.i2cAddress = sensorType.i2cAddress;
                    tempSensor.chipType = sensorType.chipType;
                    tempSensor.modbusRegisterConfigs = sensorType.modbusRegisterConfig;
                    tempSensor.i2cRegisterConfigs = sensorType.i2cRegisterConfig;

                    if (sensorReader->scanSensor(tempSensor))
                    {
                        log(DebugFeatures::MODULE_COMMAND_DEBUG,
                            "ModuleCommandUseCase: Found %s sensor on pin %d",
                            sensorType.name.c_str(), work.pin);
                        outcome.status = ModuleCommandStatus::DONE;
                        outcome.detected = true;
                        return outcome;
                    }

                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Sensor not found at default address, scanning range 0x%02X-0x%02X",
                        MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX);

                    for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
                    {
                        tempSensor.modbusDeviceAddress = address;
                        if (sensorReader->scanSensor(tempSensor))
                        {
                            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                                "ModuleCommandUseCase: Found %s sensor on pin %d at address 0x%02X",
                                sensorType.name.c_str(), work.pin, address);
                            outcome.status = ModuleCommandStatus::DONE;
                            outcome.detected = true;
                            return outcome;
                        }
                    }
                }

                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: No sensor detected on pin %d", work.pin);
                outcome.status = ModuleCommandStatus::ERROR;
                outcome.errorMessage = "No sensor detected on pin";
                return outcome;
            }
            case ModuleCommandEnum::SET_SENSOR_MODEL:
            {
                SensorEntity sensor = work.sensorToVerify;

                auto resolution = resolveModbusAddress(sensor, work.occupiedModbusAddresses);
                bool sensorDetected = resolution.first;

                outcome.detected = sensorDetected;
                outcome.sensor = sensor;
                outcome.status = sensorDetected ? ModuleCommandStatus::DONE : ModuleCommandStatus::ERROR;
                if (!sensorDetected)
                {
                    // A named message means the device answered but could not be given an address
                    // of its own; an empty one means nothing answered anywhere, which is the case
                    // this command reported before the allocator existed.
                    outcome.errorMessage = resolution.second.empty()
                        ? std::string("Sensor not detected on specified pin")
                        : resolution.second;
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Sensor %s not saved on pin %d - %s",
                        sensor.sensorType.c_str(), work.pin, outcome.errorMessage.c_str());
                }
                return outcome;
            }
            case ModuleCommandEnum::SCAN_SENSORS:
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: ScanSensors command - scanning pin %d", work.pin);

                for (const auto &sensorType : work.candidates())
                {
                    SensorEntity tempSensor;
                    tempSensor.pin = work.pin;
                    tempSensor.sensorType = sensorType.name;
                    tempSensor.model = sensorType.name;
                    tempSensor.protocol = sensorType.getProtocolString();
                    tempSensor.sensorTypeUid = sensorType.uid;
                    tempSensor.modbusDeviceAddress = sensorType.modbusDeviceAddress;
                    tempSensor.baudRate = sensorType.baudRate;
                    tempSensor.modbusPingAddress = sensorType.modbusPingAddress;
                    tempSensor.registerToStoreModbusDeviceAddress = sensorType.registerToStoreModbusDeviceAddress;
                    tempSensor.writeRegistersCommand = sensorType.writeRegistersCommand;
                    tempSensor.applyAddressAfterPowerCycle = sensorType.applyAddressAfterPowerCycle;
                    tempSensor.i2cAddress = sensorType.i2cAddress;
                    tempSensor.chipType = sensorType.chipType;
                    tempSensor.modbusRegisterConfigs = sensorType.modbusRegisterConfig;
                    tempSensor.i2cRegisterConfigs = sensorType.i2cRegisterConfig;

                    char pinStr[3];
                    sprintf(pinStr, "%02d", work.pin);
                    tempSensor.uid = work.moduleUid + "." + pinStr;

                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Trying sensor type %s on pin %d",
                        sensorType.name.c_str(), work.pin);

                    auto resolution = resolveModbusAddress(tempSensor, work.occupiedModbusAddresses);

                    if (!resolution.first)
                    {
                        if (!resolution.second.empty())
                        {
                            // The device is there and cannot be given an address of its own:
                            // saying so beats trying the next model on a line with a collision.
                            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                                "ModuleCommandUseCase: Sensor type %s on pin %d not saved - %s",
                                sensorType.name.c_str(), work.pin, resolution.second.c_str());
                            outcome.status = ModuleCommandStatus::ERROR;
                            outcome.errorMessage = resolution.second;
                            return outcome;
                        }

                        continue;
                    }

                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Found %s sensor on pin %d at address 0x%02X",
                        sensorType.name.c_str(), work.pin, tempSensor.modbusDeviceAddress);
                    outcome.status = ModuleCommandStatus::DONE;
                    outcome.detected = true;
                    outcome.sensor = tempSensor;
                    return outcome;
                }

                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: No sensor detected on pin %d", work.pin);
                outcome.status = ModuleCommandStatus::DONE;
                outcome.detected = false;
                return outcome;
            }
            default:
                outcome.status = ModuleCommandStatus::ERROR;
                outcome.errorMessage = "Not a bus command";
                return outcome;
        }
    }

    ComplexCommandOutcome executeNetworkWork(const ComplexCommandWork &work) override
    {
        ComplexCommandOutcome outcome;

        if (work.fetchCatalog)
        {
            outcome.fetchedTypes = fetchSensorTypesForScan();
            outcome.status = ModuleCommandStatus::DONE;
            if (outcome.fetchedTypes.empty())
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Server returned no sensor types");
            }
            return outcome;
        }

        switch (getModuleCommandEnum(work.command.command)) {
            case ModuleCommandEnum::UPDATE_FIRMWARE:
            {
                if (!firmwareUpdater)
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Firmware updater not available");
                    outcome.status = ModuleCommandStatus::ERROR;
                    outcome.errorMessage = "Firmware updater not available";
                    return outcome;
                }

                std::string url = extractJsonValue(work.command.options, "url");
                // Optional field: an empty value means the command carried no checksum, and the
                // image is installed the way it was before the field existed.
                std::string md5 = extractJsonValue(work.command.options, "md5");
                outcome.networkOk = firmwareUpdater->updateFirmwareFromURL(url.c_str(), md5.c_str());
                outcome.firmwareUpdateStatus = firmwareUpdater->getLastUpdateStatus();
                outcome.firmwareHttpCode = firmwareUpdater->getLastHttpCode();

                if (outcome.networkOk)
                {
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Firmware update completed successfully");
                    outcome.status = ModuleCommandStatus::DONE;
                    return outcome;
                }

                // The updater knows why it stopped (HTTP code, short write, no space); without it
                // every cause of a failed OTA reaches the backend as the same opaque sentence.
                const char* reason = firmwareUpdater->getLastErrorMessage();
                std::string errorMessage = "Failed to start firmware update";
                if (reason != nullptr && reason[0] != '\0')
                {
                    errorMessage += ": ";
                    errorMessage += reason;
                }
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Firmware update failed to start (%s)",
                    (reason != nullptr && reason[0] != '\0') ? reason : "no reason reported");

                outcome.status = ModuleCommandStatus::ERROR;
                outcome.errorMessage = errorMessage;
                return outcome;
            }
            case ModuleCommandEnum::DELETE_MODULE:
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "DELETE_MODULE: Verifying module deletion status with server");
                outcome.deletionStatus = checkModuleDeletionUseCase->evaluate();
                outcome.status = ModuleCommandStatus::DONE;
                return outcome;
            }
            default:
                outcome.status = ModuleCommandStatus::ERROR;
                outcome.errorMessage = "Not a network command";
                return outcome;
        }
    }

    bool finishComplexCommand(const ComplexCommandWork &work,
                              const ComplexCommandOutcome &outcome,
                              ModuleCommandExecutionResult &result,
                              ComplexCommandWork &nextWork) override
    {
        if (work.fetchCatalog && (catalogSync == nullptr || composingInline))
        {
            // Stored on this task: there is no synchronization to hand the catalog to, or nothing
            // would collect a write handed to the storage task while runComplexInline() holds the
            // caller's stack.
            if (!outcome.fetchedTypes.empty())
            {
                sensorTypeRepository->saveAll(outcome.fetchedTypes);
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Fetched and saved %d sensor types from server",
                    static_cast<int>(outcome.fetchedTypes.size()));
            }

            SensorTypeCatalog catalog = sensorTypeRepository->catalogSnapshot();
            if (catalog->empty())
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: No sensor types available for scanning after sync");
                result = makeResult(work.command.uid, ModuleCommandStatus::ERROR,
                                    "No sensor types available");
                return true;
            }

            scanFromCatalog(work, catalog, nextWork);
            return false;
        }

        if (work.fetchCatalog || work.awaitCatalogStore)
        {
            return finishCatalogStage(work, outcome, result, nextWork);
        }

        switch (getModuleCommandEnum(work.command.command)) {
            case ModuleCommandEnum::PING_PIN:
            {
                result = makeResult(work.command.uid, outcome.status, outcome.errorMessage);
                return true;
            }
            case ModuleCommandEnum::UPDATE_FIRMWARE:
            {
                // The persisted update status is an EEPROM write, so it happens here, on the
                // control loop, like every other settings write - the download task only records
                // the value, and this step is the single writer.
                if (outcome.firmwareUpdateStatus != 0 && settings)
                {
                    settings->setUpdateStatus(outcome.firmwareUpdateStatus);
                }

                if (outcome.networkOk)
                {
                    settings->setNoScanSensors(1);
                    settings->setBootType(BOOT_TYPE_FIRMWARE_UPDATE);
                    moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);
                    result = makeResult(work.command.uid, ModuleCommandStatus::DONE);
                    return true;
                }

                // A download that never opened a connection is worth one restart: the network
                // stack comes back with the module, and the update is carried out on the next
                // boot. A refusal that came from the server is not - it would refuse again.
                if (firmwareUpdateRecoveryUseCase &&
                    outcome.firmwareHttpCode == FIRMWARE_UPDATE_HTTP_NOT_CONNECTED &&
                    firmwareUpdateRecoveryUseCase->keepForNextBoot(work.command))
                {
                    result = makeResult(work.command.uid, ModuleCommandStatus::IN_PROGRESS);
                    return true;
                }

                result = makeResult(work.command.uid, ModuleCommandStatus::ERROR, outcome.errorMessage);
                return true;
            }
            case ModuleCommandEnum::DELETE_MODULE:
            {
                if (outcome.status != ModuleCommandStatus::DONE)
                {
                    result = makeResult(work.command.uid, outcome.status, outcome.errorMessage);
                    return true;
                }
                result = applyDeletionVerdict(work.command, outcome.deletionStatus);
                return true;
            }
            case ModuleCommandEnum::SET_SENSOR_MODEL:
            {
                result = applySetSensorModel(work, outcome);
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: SET_SENSOR_MODEL executed - status: %s, error: %s",
                    result.status.c_str(), result.errorMessage.c_str());
                return true;
            }
            case ModuleCommandEnum::SCAN_SENSORS:
            {
                result = applyScanSensors(work, outcome);
                return true;
            }
            default:
                result = makeResult(work.command.uid, ModuleCommandStatus::ERROR, "Unknown command");
                return true;
        }
    }

    bool catalogStorePending() override
    {
        return catalogSync != nullptr && catalogSync->isWritePending();
    }

private:
    /**
     * @brief The ScanSensors stage between its catalog fetch and its scan, through the
     *        synchronization. Control loop.
     *
     * The fetched catalog is handed to the catalog synchronization, which writes it on the storage
     * task like every catalog it fetches itself, and the scan runs from the catalog the module
     * holds once the write has landed - nothing here waits for flash.
     *
     * A catalog already on its way to flash - the one handed over here, or one the synchronization
     * handed over before this command got here - is waited for, not written alongside: the file has
     * one writer. The repository is not read meanwhile either, because a module without a catalog
     * has no cache and every read of it goes to the file being replaced. A write that was already
     * in flight carries the server's catalog, as the one fetched here does, so once it has landed
     * the command scans from it and stores nothing of its own.
     */
    bool finishCatalogStage(const ComplexCommandWork &work,
                            const ComplexCommandOutcome &outcome,
                            ModuleCommandExecutionResult &result,
                            ComplexCommandWork &nextWork)
    {
        if (catalogSync->isWritePending())
        {
            return waitForCatalogStore(work, nextWork);
        }

        SensorTypeCatalog catalog = sensorTypeRepository->catalogSnapshot();
        if (catalog->empty() && work.fetchCatalog && !outcome.fetchedTypes.empty())
        {
            const ISyncSensorTypesUseCase::StoreResult stored =
                catalogSync->storeFetchedCatalog(outcome.fetchedTypes);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: %d fetched sensor types handed to the catalog sync - %s",
                static_cast<int>(outcome.fetchedTypes.size()),
                stored == ISyncSensorTypesUseCase::StoreResult::Pending ? "on their way to flash"
                : stored == ISyncSensorTypesUseCase::StoreResult::Stored ? "stored"
                : "not stored");

            if (stored == ISyncSensorTypesUseCase::StoreResult::Pending)
            {
                return waitForCatalogStore(work, nextWork);
            }
            if (stored == ISyncSensorTypesUseCase::StoreResult::Stored)
            {
                catalog = sensorTypeRepository->catalogSnapshot();
            }
        }

        if (catalog->empty())
        {
            // A server that had no catalog to give is told apart from a catalog that did not make
            // it to flash - refused before the write, or by the file system during it.
            const bool storeFailed = work.awaitCatalogStore || !outcome.fetchedTypes.empty();
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: No sensor types available for scanning after sync (%s)",
                storeFailed ? "the catalog was not stored" : "the server sent none");
            result = makeResult(work.command.uid, ModuleCommandStatus::ERROR,
                                storeFailed ? "Sensor types could not be stored"
                                            : "No sensor types available");
            return true;
        }

        scanFromCatalog(work, catalog, nextWork);
        return false;
    }

    /** @brief Hold the scan until the catalog on its way to flash has landed. */
    bool waitForCatalogStore(const ComplexCommandWork &work, ComplexCommandWork &nextWork)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: a sensor type catalog is on its way to flash, the scan on pin %d waits for it",
            work.pin);
        nextWork = work;
        nextWork.fetchCatalog = false;
        nextWork.awaitCatalogStore = true;
        nextWork.onBus = false;
        return false;
    }

    /** @brief The scan stage of a ScanSensors, over the catalog the module now holds. */
    void scanFromCatalog(const ComplexCommandWork &work, const SensorTypeCatalog &catalog,
                         ComplexCommandWork &nextWork)
    {
        nextWork = work;
        nextWork.fetchCatalog = false;
        nextWork.awaitCatalogStore = false;
        nextWork.onBus = true;
        nextWork.sensorTypes = catalog;
        nextWork.moduleUid = moduleConfig->getModuleUid();
        nextWork.occupiedModbusAddresses = occupancyForScan(nextWork.moduleUid, work.pin);
    }

    /**
     * @brief Helper structure for commands that return results with sensor data
     */
    struct CommandExecutionResult {
        ModuleCommandStatus status;
        std::string errorMessage;
        ModuleCommandResult result;
        
        CommandExecutionResult(ModuleCommandStatus status, const std::string& errorMessage)
            : status(status), errorMessage(errorMessage) {}
            
        CommandExecutionResult(ModuleCommandStatus status, const std::string& errorMessage, const ModuleCommandResult& result)
            : status(status), errorMessage(errorMessage), result(result) {}
    };

    /**
     * @brief Attempts ScanSensors makes when it has to fetch the sensor type catalog itself.
     *
     * Three, and the retries live at this call site rather than in the transport: the outbox
     * has a retry budget of its own, and retries inside the transport would multiply with it -
     * one outbox attempt would grow to 90 s while holding the API lock. ScanSensors is the one
     * catalog consumer the outbox does not drive, so it brings its own attempts.
     */
    static const uint8_t SCAN_SENSOR_TYPES_MAX_ATTEMPTS = 3;

    /**
     * @brief GET the sensor type catalog for a scan, up to SCAN_SENSOR_TYPES_MAX_ATTEMPTS times.
     *
     * Retried on `httpFailed` and on nothing else: an empty catalog from a server that answered is
     * a real answer, and asking three times for it would cost two extra requests every time a
     * module with no local catalog is scanned against an account that has no sensor models.
     */
    std::vector<SensorType> fetchSensorTypesForScan()
    {
        std::vector<SensorType> serverTypes;

        for (uint8_t attempt = 1; attempt <= SCAN_SENSOR_TYPES_MAX_ATTEMPTS; ++attempt)
        {
            // The calling task vouches for itself between its attempts: one attempt is a
            // bounded legal pause (the API lock plus one call), three back to back are not.
            if (attempt > 1 && arduinoAdapter)
            {
                arduinoAdapter->feedTaskWatchdog();
            }

            bool httpFailed = false;
            serverTypes = hybridApiService->getSensorTypesList(&httpFailed);

            if (!httpFailed)
            {
                return serverTypes;
            }

            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: sensor type fetch failed, attempt %d of %d",
                (int)attempt, (int)SCAN_SENSOR_TYPES_MAX_ATTEMPTS);
        }

        return serverTypes;
    }

    /**
     * @brief Add a sensor to the module structure through the builder the setup flow uses
     * @param sensorEntity Sensor the command has just saved
     */
    void addSensorToStructure(const SensorEntity& sensorEntity)
    {
        if (!moduleStructure || moduleStructure->nodes.empty())
        {
            return;
        }

        if (!sensorStructureUseCase)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: sensor structure use case is not wired - sensor %s is not added to the structure",
                sensorEntity.uid.c_str());
            return;
        }

        sensorStructureUseCase->addSensor(moduleStructure->nodes[0], sensorEntity);
    }

    /**
     * @brief Handle AddWifi command
     * @param moduleCommand Module command DTO with options
     * @return Pair of status and error message
     *
     * Deliberately not one of the split complex commands: there is no network wait in here to
     * move anywhere. What it does is write - EEPROM through the settings, the networks file in
     * SPIFFS, the WiFiMulti access-point list - and every one of those is state the control loop
     * also writes, so this runs on the control loop like the other state writes.
     */
    std::pair<ModuleCommandStatus, std::string> handleAddWifi(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }

        std::string ssid = extractJsonValue(moduleCommand.options, "ssid");
        std::string password = extractJsonValue(moduleCommand.options, "password");

        if (ssid.empty() || password.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing ssid or password in options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Adding WiFi - SSID: %s", ssid.c_str());

        if (!wifiManager)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: WiFi manager not available");
            return {ModuleCommandStatus::ERROR, "WiFi manager not available"};
        }

        bool result = wifiManager->addWifiNetwork(ssid.c_str(), password.c_str());

        if (result)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: WiFi network added successfully - SSID: %s", ssid.c_str());
            return {ModuleCommandStatus::DONE, ""};
        }
        else
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Failed to add WiFi network - SSID: %s", ssid.c_str());
            return {ModuleCommandStatus::ERROR, "Failed to add WiFi network"};
        }
    }

    static ModuleCommandExecutionResult makeResult(const std::string& uid,
                                                   ModuleCommandStatus status,
                                                   const std::string& errorMessage = "",
                                                   const ModuleCommandResult& commandResult = ModuleCommandResult())
    {
        return ModuleCommandExecutionResult(uid, getModuleCommandStatusName(status), errorMessage, commandResult);
    }

    /**
     * @brief The synchronous composition of prepare -> execute -> finish, for a caller that
     *        needs the answer on this call stack: executeCommands() reaches it for the complex
     *        commands. On the device CommandService routes those commands to their tasks instead.
     */
    ModuleCommandExecutionResult runComplexInline(const ModuleCommandDto& moduleCommand)
    {
        // A stage that waits for the storage task would wait here for ever: its outcome is
        // collected by a loop iteration, and this call holds the caller's stack until it returns.
        struct InlineScope
        {
            bool &flag;
            const bool previous;
            explicit InlineScope(bool &f) : flag(f), previous(f) { flag = true; }
            ~InlineScope() { flag = previous; }
        } inlineScope(composingInline);

        ComplexCommandWork work;
        ModuleCommandExecutionResult immediate("", "");
        if (!prepareComplexCommand(moduleCommand, work, immediate))
        {
            return immediate;
        }

        while (true)
        {
            ComplexCommandOutcome outcome = work.onBus ? executeBusWork(work) : executeNetworkWork(work);
            ModuleCommandExecutionResult result("", "");
            ComplexCommandWork nextWork;
            if (finishComplexCommand(work, outcome, result, nextWork))
            {
                return result;
            }
            work = nextWork;
        }
    }

    /** @brief The loop-owned half of SetSensorModel: repository and structure writes. */
    ModuleCommandExecutionResult applySetSensorModel(const ComplexCommandWork& work,
                                                     const ComplexCommandOutcome& outcome)
    {
        if (!outcome.detected)
        {
            // The bus step names why when the device answered but could not be given a free
            // address; the plain "not detected" stays the answer when nothing answered at all.
            return makeResult(work.command.uid, ModuleCommandStatus::ERROR,
                              outcome.errorMessage.empty()
                                  ? std::string("Sensor not detected on specified pin")
                                  : outcome.errorMessage);
        }

        const SensorEntity& newSensorEntity = outcome.sensor;
        const int pin = work.pin;

        // The new row is stored before anything of the port's current sensor is let go of, so a
        // write the flash refuses costs nothing: the port keeps the sensor it has - its row, its
        // records in the structure and its readings. The row carries the port's uid, so storing it
        // is what replaces the row the port had.
        if (!sensorRepository->save(newSensorEntity))
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Failed to save sensor to repository");
            return makeResult(work.command.uid, ModuleCommandStatus::ERROR, "Failed to save sensor");
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Sensor saved, now checking for other sensors on pin %d", pin);

        auto allSensors = sensorRepository->getAll();
        std::vector<SensorEntity> sensorsToDelete;
        for (const auto &existingSensor : allSensors)
        {
            if (existingSensor.pin == pin && existingSensor.uid != newSensorEntity.uid)
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Found existing sensor on pin %d with UID: %s, marking for deletion",
                    pin, existingSensor.uid.c_str());
                sensorsToDelete.push_back(existingSensor);
            }
        }

        for (const auto &sensorToDelete : sensorsToDelete)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Deleting old sensor with UID: %s from pin %d",
                sensorToDelete.uid.c_str(), pin);

            if (!sensorRepository->remove(sensorToDelete.uid.c_str()))
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Warning - Failed to delete old sensor UID: %s",
                    sensorToDelete.uid.c_str());
            }

            // Clear stale cached channel values so they don't appear in future update packets
            if (sensorValueModel)
            {
                sensorValueModel->removeSensor(sensorToDelete.uid);
            }
        }

        // Also clear any stale channel values for the new sensor's base UID (same key in sensorValueModel)
        if (sensorValueModel)
        {
            sensorValueModel->removeSensor(newSensorEntity.uid);
        }

        if (moduleStructure)
        {
            moduleStructure->removeSensorsByPin(pin);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Cleared moduleStructure on pin %d", pin);
            addSensorToStructure(newSensorEntity);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Added new sensor to moduleStructure on pin %d", pin);
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Successfully saved %s sensor on pin %d with UID: %s",
            newSensorEntity.sensorType.c_str(), pin, newSensorEntity.uid.c_str());

        return makeResult(work.command.uid, ModuleCommandStatus::DONE);
    }

    /** @brief The loop-owned half of ScanSensors: repository and structure writes. */
    ModuleCommandExecutionResult applyScanSensors(const ComplexCommandWork& work,
                                                  const ComplexCommandOutcome& outcome)
    {
        ModuleCommandResult result;

        if (!outcome.detected)
        {
            // A pin with nothing on it is still a successful scan with an empty list; a device
            // that could not be moved off an address another entity holds is not, and nothing of
            // it is saved.
            if (outcome.status == ModuleCommandStatus::ERROR)
            {
                return makeResult(work.command.uid, ModuleCommandStatus::ERROR, outcome.errorMessage);
            }
            return makeResult(work.command.uid, ModuleCommandStatus::DONE, "", result);
        }

        SensorEntity detectedSensor = outcome.sensor;
        detectedSensor.status = SensorStatusEnum::SENSOR_STATUS_NEW;
        detectedSensor.isActive = true;
        detectedSensor.isError = false;

        if (sensorRepository)
        {
            if (sensorRepository->createOrUpdateSensor(detectedSensor))
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Successfully saved detected sensor %s to repository with NEW status",
                    detectedSensor.uid.c_str());

                // The uid is the port's: on an occupied port the row just replaced another
                // sensor's, and what the value model holds under the uid was read from that one.
                if (sensorValueModel)
                {
                    sensorValueModel->removeSensor(detectedSensor.uid);
                }

                if (moduleStructure)
                {
                    // The port's records go with the row they describe: a channel of the replaced
                    // sensor that the new model does not have, or the record of an empty port,
                    // would otherwise stay in the structure beside the new sensor's records.
                    moduleStructure->removeSensorsByPin(work.pin);
                    addSensorToStructure(detectedSensor);
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Added detected sensor to moduleStructure on pin %d", work.pin);
                }
            }
            else
            {
                // The port keeps the row it had, and the structure and the readings go on
                // describing that one; reporting the sensor found would tell the server the port
                // holds a sensor the module does not have. Answered as SetSensorModel answers the
                // same refusal - and on an error no structure follows the result.
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Failed to save detected sensor %s to repository",
                    detectedSensor.uid.c_str());
                return makeResult(work.command.uid, ModuleCommandStatus::ERROR, "Failed to save sensor");
            }
        }

        SensorResultDto sensorResult(
            detectedSensor.uid,
            detectedSensor.sensorType,
            detectedSensor.pin,
            1, // group
            detectedSensor.sensorTypeUid
        );
        result.sensors.push_back(sensorResult);

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: ScanSensors completed - found sensor %s",
            detectedSensor.sensorType.c_str());

        return makeResult(work.command.uid, ModuleCommandStatus::DONE, "", result);
    }

    /** @brief The loop-owned half of DeleteModule: the factory reset the verdict allows. */
    ModuleCommandExecutionResult applyDeletionVerdict(const ModuleCommandDto& moduleCommand,
                                                      ModuleDeletionStatusEnum deletionStatus)
    {
        if (deletionStatus == ModuleDeletionStatusEnum::UNKNOWN)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "DELETE_MODULE: verdict UNKNOWN - returning InProgress (retry)");
            return makeResult(moduleCommand.uid, ModuleCommandStatus::IN_PROGRESS,
                              "Deletion not confirmed yet, will retry");
        }

        if (deletionStatus == ModuleDeletionStatusEnum::NOT_DELETED)
        {
            logError("ModuleCommandUseCase: DELETE_MODULE verdict NOT_DELETED - module is not marked as deleted");
            return makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                              "Module not marked as deleted");
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "DELETE_MODULE: verdict DELETED - proceeding with factory reset");

        if (!clearModuleDataService)
        {
            logError("ModuleCommandUseCase: ClearModuleDataService not available");
            return makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR,
                              "Clear data service not available");
        }

        bool clearResult = clearModuleDataService->clearAllData();
        if (clearResult)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Factory reset completed successfully - all module data cleared");
            if (moduleConfig)
            {
                moduleConfig->setRequestedModuleMode(ModuleModeEnum::CONFIGURED_NEED_RESTART);
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Requested mode change to CONFIGURED_NEED_RESTART - device will restart");
            }
            return makeResult(moduleCommand.uid, ModuleCommandStatus::DONE);
        }

        logError("ModuleCommandUseCase: Factory reset failed - could not clear all data");
        return makeResult(moduleCommand.uid, ModuleCommandStatus::ERROR, "Failed to clear module data");
    }

    /**
     * @brief Handle BlinkingStart command
     * @param uid Module unique identifier
     * @param commandOptions JSON object with command-specific options
     * @return true if command was executed successfully, false otherwise
     */
    bool handleBlinkingStart(const std::string &uid, const std::string &commandOptions)
    {
        if (!ledUseCases)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: BlinkingStart - ledUseCases not available");
            return false;
        }

        ledUseCases->setBlinking(true);
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: BlinkingStart - identification mode enabled");
        return true;
    }

    /**
     * @brief Handle BlinkingStop command
     * @param uid Module unique identifier
     * @param commandOptions JSON object with command-specific options
     * @return true if command was executed successfully, false otherwise
     */
    bool handleBlinkingStop(const std::string &uid, const std::string &commandOptions)
    {
        if (!ledUseCases)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: BlinkingStop - ledUseCases not available");
            return false;
        }

        ledUseCases->setBlinking(false);
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: BlinkingStop - identification mode disabled");
        return true;
    }

    /**
     * @brief Occupancy of the ModBus range for a scan of one pin, read on the control loop
     *
     * The sensor a scan of this pin would save carries the uid of the pin, so its own row is left
     * out: rescanning a pin must not let the sensor already on it block itself.
     *
     * @param moduleUid Uid of this module, the prefix of every sensor uid it owns
     * @param pin Pin being scanned
     */
    ModbusAddressOccupancy occupancyForScan(const std::string &moduleUid, int pin)
    {
        char pinStr[3];
        sprintf(pinStr, "%02d", pin);
        return modbusAddressAllocator->occupiedAddresses(moduleUid + "." + pinStr);
    }

    /**
     * @brief Write one structured record of the address resolution to the log stream of the module
     * @param action Action of the record; the severity lives in the name of the action (*_fail)
     * @param details Body of the record, valid JSON
     */
    void logAddressEvent(const char *action, const std::string &details)
    {
        if (logger)
        {
            logger->logJson("SENSOR_ADDRESS", action, details.c_str());
        }
    }

    /**
     * @brief ModBus address as the "0x72" text of the SETUP log, for a field of a JSON record
     * @param address ModBus address
     */
    std::string addressToText(int address)
    {
        char buffer[8];
        snprintf(buffer, sizeof(buffer), "0x%02X", address);
        return std::string(buffer);
    }

    /**
     * @brief Occupancy as a JSON array of "0x72:uid" pairs, for the details of a JSON record
     * @param occupied Occupancy built by the allocator
     */
    std::string occupancyToText(const ModbusAddressOccupancy &occupied)
    {
        std::string pairs = "[";
        for (const auto &entry : occupied)
        {
            for (const auto &uid : entry.second)
            {
                if (pairs.size() > 1)
                {
                    pairs += ",";
                }
                pairs += "\"" + addressToText(entry.first) + ":" + uid + "\"";
            }
        }
        return pairs + "]";
    }

    /**
     * @brief Probe one address and take it when it answers and no other entity of the module holds it
     * @param entity Entity being configured; on success its modbusDeviceAddress stays on this address
     * @param address Address to probe
     * @param occupied Occupancy of the module built by the allocator
     * @param probeStep Step of the resolution this address comes from, for the log record
     * @return true when the address answered and was taken
     */
    bool probeFreeAddress(SensorEntity &entity, int address, const ModbusAddressOccupancy &occupied, const char *probeStep)
    {
        if (modbusAddressAllocator->isAddressTaken(occupied, address))
        {
            logAddressEvent("address_skipped_occupied",
                            "{\"sensorUid\":\"" + entity.uid + "\",\"address\":\"" + addressToText(address) +
                                "\",\"heldBy\":\"" + modbusAddressAllocator->addressHolder(occupied, address) + "\"}");
            return false;
        }

        entity.modbusDeviceAddress = address;
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Scanning ModBus address 0x%02X for sensor %s",
            address, entity.sensorType.c_str());

        if (!sensorReader->scanSensor(entity))
        {
            return false;
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Found %s sensor on pin %d at address 0x%02X",
            entity.sensorType.c_str(), entity.pin, address);
        logAddressEvent("address_accepted",
                        "{\"sensorUid\":\"" + entity.uid + "\",\"address\":\"" + addressToText(address) +
                            "\",\"probeStep\":\"" + probeStep + "\"}");
        return true;
    }

    /**
     * @brief Move a device that answers only where another entity of the module already sits
     * @param entity Entity being configured; on success its modbusDeviceAddress is the new address
     * @param occupied Occupancy of the module built by the allocator
     * @param preferredAddress Address of the entity's own port, preferred target of the move
     * @return true and an empty message when the device now answers at a free address, false and an
     *         empty message when it answers nowhere, false and a message when the move failed
     */
    std::pair<bool, std::string> moveDeviceOffOccupiedAddress(SensorEntity &entity, const ModbusAddressOccupancy &occupied, int preferredAddress)
    {
        int collisionAddress = -1;
        for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
        {
            if (!modbusAddressAllocator->isAddressTaken(occupied, address))
            {
                continue;
            }

            entity.modbusDeviceAddress = address;
            if (sensorReader->scanSensor(entity))
            {
                collisionAddress = address;
                break;
            }
        }

        if (collisionAddress == -1)
        {
            return {false, ""};
        }

        std::string heldBy = modbusAddressAllocator->addressHolder(occupied, collisionAddress);
        std::string collisionText = addressToText(collisionAddress);
        logAddressEvent("collision_detected",
                        "{\"sensorUid\":\"" + entity.uid + "\",\"address\":\"" + collisionText +
                            "\",\"heldBy\":\"" + heldBy + "\",\"pin\":" + std::to_string(entity.pin) + "}");

        int targetAddress = modbusAddressAllocator->nextFreeAddress(occupied, preferredAddress);
        if (targetAddress == -1)
        {
            logAddressEvent("reassign_fail",
                            "{\"sensorUid\":\"" + entity.uid + "\",\"fromAddress\":\"" + collisionText +
                                "\",\"toAddress\":\"\",\"reason\":\"no_free_address\"}");
            return {false, "No free ModBus address on module"};
        }

        std::string targetText = addressToText(targetAddress);
        logAddressEvent("reassign_attempt",
                        "{\"sensorUid\":\"" + entity.uid + "\",\"fromAddress\":\"" + collisionText +
                            "\",\"toAddress\":\"" + targetText + "\",\"preferred\":\"" + addressToText(preferredAddress) + "\"}");

        if (!sensorReader->writeSensorAddress(entity, targetAddress))
        {
            logAddressEvent("reassign_fail",
                            "{\"sensorUid\":\"" + entity.uid + "\",\"fromAddress\":\"" + collisionText +
                                "\",\"toAddress\":\"" + targetText + "\",\"reason\":\"write_rejected\"}");
            return {false, "Failed to move sensor to a free ModBus address"};
        }

        entity.modbusDeviceAddress = targetAddress;
        if (!sensorReader->scanSensor(entity))
        {
            logAddressEvent("reassign_fail",
                            "{\"sensorUid\":\"" + entity.uid + "\",\"fromAddress\":\"" + collisionText +
                                "\",\"toAddress\":\"" + targetText + "\",\"reason\":\"verify_failed\"}");
            return {false, "Sensor does not answer at the new ModBus address"};
        }

        logAddressEvent("reassign_done",
                        "{\"sensorUid\":\"" + entity.uid + "\",\"fromAddress\":\"" + collisionText +
                            "\",\"toAddress\":\"" + targetText + "\"}");
        return {true, ""};
    }

    /**
     * @brief Bring a ModBus entity to an address of the module nobody else holds
     *
     * Runs on the bus task, so the occupancy arrives as an argument: it was read from the sensor
     * repository by the prepare step, on the control loop that owns the repositories. Everything
     * this resolution asks of the allocator afterwards is arithmetic over that snapshot.
     *
     * @param entity Entity being configured; it enters with the default address of its type and
     *        leaves with the address it may keep
     * @param occupied Addresses other entities of the module hold, snapshotted on the control loop
     * @return true and an empty message when the entity may be saved, false and an empty message
     *         when nothing answered at all, false and a message when the device could not be moved
     */
    std::pair<bool, std::string> resolveModbusAddress(SensorEntity &entity,
                                                      const ModbusAddressOccupancy &occupied)
    {
        int defaultAddress = entity.modbusDeviceAddress;
        int preferredAddress = MODBUS_ADDRESS_MIN + entity.pin;

        logAddressEvent("resolve_start",
                        "{\"sensorUid\":\"" + entity.uid + "\",\"pin\":" + std::to_string(entity.pin) +
                            ",\"typeUid\":\"" + entity.sensorTypeUid + "\",\"defaultAddress\":\"" + addressToText(defaultAddress) +
                            "\",\"preferredAddress\":\"" + addressToText(preferredAddress) +
                            "\",\"occupied\":" + occupancyToText(occupied) + "}");

        if (probeFreeAddress(entity, defaultAddress, occupied, "default"))
        {
            takeTheDeviceOffTheFactoryAddress(entity, occupied, preferredAddress);
            return {true, ""};
        }

        if (!entity.isModbusSensor())
        {
            return {false, ""};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Sensor not found at default address, scanning range 0x%02X-0x%02X",
            MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX);

        if (probeFreeAddress(entity, preferredAddress, occupied, "per_port"))
        {
            return {true, ""};
        }

        for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
        {
            if (address == preferredAddress || address == defaultAddress)
            {
                continue;
            }

            if (probeFreeAddress(entity, address, occupied, "sweep"))
            {
                return {true, ""};
            }
        }

        return moveDeviceOffOccupiedAddress(entity, occupied, preferredAddress);
    }

    /**
     * @brief Move a device that answered at its model's own address onto an address of the module
     *
     * The address a device leaves the factory with belongs to its model and not to this row: every
     * device of that model answers at it, so a second one plugged into the port sharing this one's
     * UART answers every question addressed to it, and nothing on the line can tell the two apart.
     * The module's own range gives one address per port, and a row holding one of those is a row
     * nobody else answers for. So the factory address is a state a row passes through, not one it
     * keeps: the module writes its own address into the device while the person is still watching
     * the app, instead of leaving it to the address phase of the next restart.
     *
     * The same writeSensorAddress() sequence that phase uses, and the same allocator: one isolation
     * of the port, one write, and the reader's own verification that the device answers where it
     * was sent. An attempt, not a condition - a device whose address no write of the module's takes
     * stays where it is and is saved on the factory address, exactly as it was before.
     *
     * @param entity Entity answering at the factory address; on success it carries the new one
     * @param occupied Addresses other entities of the module hold
     * @param preferredAddress Address of the entity's own port, preferred target of the move
     */
    void takeTheDeviceOffTheFactoryAddress(SensorEntity &entity,
                                           const ModbusAddressOccupancy &occupied,
                                           int preferredAddress)
    {
        if (!entity.isModbusSensor())
        {
            return;
        }

        const int factoryAddress = entity.modbusDeviceAddress;
        if (factoryAddress == preferredAddress)
        {
            // The model's own address is this port's address: there is nothing to move it to that
            // would be more its own than where it already is.
            return;
        }

        const int targetAddress = modbusAddressAllocator->nextFreeAddress(occupied, preferredAddress);
        if (targetAddress == -1 || targetAddress == factoryAddress)
        {
            logAddressEvent("reassign_fail",
                            "{\"sensorUid\":\"" + entity.uid + "\",\"fromAddress\":\"" +
                                addressToText(factoryAddress) +
                                "\",\"toAddress\":\"\",\"reason\":\"no_free_address\"}");
            return;
        }

        const std::string factoryText = addressToText(factoryAddress);
        const std::string targetText = addressToText(targetAddress);
        logAddressEvent("reassign_attempt",
                        "{\"sensorUid\":\"" + entity.uid + "\",\"fromAddress\":\"" + factoryText +
                            "\",\"toAddress\":\"" + targetText + "\",\"preferred\":\"" +
                            addressToText(preferredAddress) + "\"}");

        if (!sensorReader->writeSensorAddress(entity, targetAddress))
        {
            // The device stays on the address it answered at, and so does the row: a model the
            // module cannot address is still a sensor, and the person's sensor is saved either way.
            logAddressEvent("reassign_fail",
                            "{\"sensorUid\":\"" + entity.uid + "\",\"fromAddress\":\"" + factoryText +
                                "\",\"toAddress\":\"" + targetText + "\",\"reason\":\"write_rejected\"}");
            entity.modbusDeviceAddress = factoryAddress;
            return;
        }

        entity.modbusDeviceAddress = targetAddress;
        logAddressEvent("reassign_done",
                        "{\"sensorUid\":\"" + entity.uid + "\",\"fromAddress\":\"" + factoryText +
                            "\",\"toAddress\":\"" + targetText + "\"}");
    }

    /**
     * @brief Handle DeleteSensors command - delete sensors from a specific pin
     * @param moduleCommand Module command DTO with options
     * @return CommandExecutionResult with deleted sensors info
     */
    CommandExecutionResult handleDeleteSensors(const ModuleCommandDto& moduleCommand)
    {
        if (moduleCommand.options.empty())
        {
            return {ModuleCommandStatus::ERROR, "Missing options"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: DeleteSensors options: %s", moduleCommand.options.c_str());

        int pin = convertPinFromJson(extractJsonIntValue(moduleCommand.options, "pin"));
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: DeleteSensors - parsed pin: %d, sensorPinCount: %d",
            pin, moduleConfig ? moduleConfig->getSensorPinCount() : -1);
        if (pin < 0 || pin >= moduleConfig->getSensorPinCount())
        {
            logError("ModuleCommandUseCase: DeleteSensors - invalid pin %d (valid range 0..%d), options: %s",
                     pin, moduleConfig->getSensorPinCount() - 1, moduleCommand.options.c_str());
            return {ModuleCommandStatus::ERROR, "Missing or invalid pin in options"};
        }

        if (!sensorRepository || !moduleConfig)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor repository or module config not available for DeleteSensors");
            return {ModuleCommandStatus::ERROR, "Sensor service not available"};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: DeleteSensors command - deleting sensors from pin %d", pin);

        // Get all sensors and find those on the specified pin
        std::vector<SensorEntity> allSensors = sensorRepository->getAll();
        std::vector<SensorEntity> sensorsToDelete;
        
        for (const auto& sensor : allSensors)
        {
            if (sensor.pin == pin)
            {
                sensorsToDelete.push_back(sensor);
            }
        }

        // Prepare result with sensor information before deletion
        ModuleCommandResult result;
        for (const auto& sensor : sensorsToDelete)
        {
            // Extract group from sensor UID (format: MAC.group.pin)
            int group = 0;
            size_t firstDot = sensor.uid.find('.');
            if (firstDot != std::string::npos)
            {
                size_t secondDot = sensor.uid.find('.', firstDot + 1);
                if (secondDot != std::string::npos)
                {
                    std::string groupStr = sensor.uid.substr(firstDot + 1, secondDot - firstDot - 1);
                    try {
                        group = std::stoi(groupStr);
                    } catch (...) {
                        group = 0;
                    }
                }
            }
            
            SensorResultDto sensorResult(
                sensor.uid,
                sensor.sensorType,
                sensor.pin,
                group,
                sensor.sensorTypeUid
            );
            result.sensors.push_back(sensorResult);
        }

        // If no sensors found on this pin, return success with empty result
        if (sensorsToDelete.empty())
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: No sensors found on pin %d", pin);
            return {ModuleCommandStatus::DONE, "", result};
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Found %d sensor(s) on pin %d to delete", 
            sensorsToDelete.size(), pin);

        // Delete each sensor
        bool allDeleted = true;
        for (const auto& sensor : sensorsToDelete)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Deleting sensor %s from pin %d", 
                sensor.uid.c_str(), pin);
                
            if (!sensorRepository->remove(sensor.uid.c_str()))
            {
                logError("ModuleCommandUseCase: Failed to delete sensor %s", sensor.uid.c_str());
                allDeleted = false;
                continue;
            }

            // The readings go with the row: the uid is the port's, and a sensor added on this
            // port later would otherwise start out with them.
            if (sensorValueModel)
            {
                sensorValueModel->removeSensor(sensor.uid);
            }
        }

        if (!allDeleted)
        {
            return {ModuleCommandStatus::ERROR, "Failed to delete some sensors"};
        }

        // Update module structure - remove sensors from this pin
        if (moduleStructure)
        {
            moduleStructure->removeSensorsByPin(pin);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Removed sensors from moduleStructure on pin %d", pin);
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Successfully deleted %d sensor(s) from pin %d", 
            sensorsToDelete.size(), pin);

        return {ModuleCommandStatus::DONE, "", result};
    }

    /**
     * @brief Handle Reboot command
     * @param moduleCommand Module command DTO with options (may contain noScanSensors flag)
     * @return true if command was executed successfully, false otherwise
     */
    bool handleReboot(const ModuleCommandDto& moduleCommand)
    {
        if (!arduinoAdapter)
        {
            logError("ModuleCommandUseCase: Arduino adapter not available for reboot");
            return false;
        }
        
        bool noScan = false;
        if (settings)
        {
            if (!moduleCommand.options.empty())
            {
                std::string noScanValue = extractJsonValue(moduleCommand.options, "noScanSensors");
                if (!noScanValue.empty())
                {
                    noScan = (noScanValue == "true" || noScanValue == "1");
                }
            }
            settings->setNoScanSensors(noScan ? 1 : 0);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Reboot - Set noScanSensors flag to %d (default 0 if absent)",
                noScan ? 1 : 0);
        }
        
        // If noScan is false, clear sensor and sensor type data
        if (!noScan)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Clearing sensor data (noScan=false)");
            
            if (sensorRepository) //TODO: need add new flag clearSensor
            {
                bool sensorsCleared = false;
                // bool sensorsCleared = sensorRepository->clear();
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Sensor data clear %s", sensorsCleared ? "successful" : "failed");
            }
            else
            {
                log(DebugFeatures::MODULE_COMMAND_DEBUG,
                    "ModuleCommandUseCase: Sensor repository not available for clearing");
            }
            
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Sensor type data kept, sync rewrites the catalog after reboot");
        }
        
        if (settings)
        {
            settings->setBootType(BOOT_TYPE_USER);
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Reboot - Set boot type to BOOT_TYPE_USER");
        }

        // The board has no battery-backed RTC: the next boot starts its clock from the time in
        // EEPROM until a server answers, and this restart is one the module can see coming.
        if (timeAdapter)
        {
            timeAdapter->saveTimeToStorage();
        }

        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Executing reboot command");

        arduinoAdapter->restart();
        return true;
    }

    /**
     * @brief Get module by UID
     * @param uid Module unique identifier
     * @return Pointer to module or nullptr if not found
     */
    void* getModuleByUid(const std::string &uid)
    {
        // TODO: Implement module lookup by UID
        return nullptr;
    }

    bool checkCommandTime(const std::string& createdDate)
    {
        if (createdDate.empty())
        {
            logError("ModuleCommandUseCase: Empty createdDate provided");
            return false;
        }

        if (!timeAdapter)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: No time adapter available for validation");
            return true; // Allow command if no time validation possible
        }

        unsigned long currentEpoch = timeAdapter->getEpochTime();
        if (currentEpoch == 0)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG,
                "ModuleCommandUseCase: Could not get current time from adapter");
            return true; // Allow command if time cannot be retrieved
        }

        return timeAdapter->isCommandTimeValid(createdDate.c_str(), commandMaxAgeSeconds);
    }

    /**
     * @brief Convert internal pin numbering to external JSON format
     * Mapping: internal -> JSON (0 -> 4, 1 -> 3, 2 -> 2, 3 -> 1)
     */
    int convertPinToJson(int internalPin)
    {
        switch (internalPin) {
            case 0: return 4;
            case 1: return 3;
            case 2: return 2;
            case 3: return 1;
            default: return internalPin;
        }
    }

    /**
     * @brief Convert external JSON pin numbering to internal format
     * Mapping: JSON -> internal (4 -> 0, 3 -> 1, 2 -> 2, 1 -> 3)
     */
    int convertPinFromJson(int jsonPin)
    {
        switch (jsonPin) {
            case 4: return 0;
            case 3: return 1;
            case 2: return 2;
            case 1: return 3;
            default: return jsonPin;
        }
    }

    std::string extractJsonValue(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return "";
        size_t valueStart = json.find("\"", colonPos);
        if (valueStart == std::string::npos) return "";
        size_t valueEnd = json.find("\"", valueStart + 1);
        if (valueEnd == std::string::npos) return "";
        return json.substr(valueStart + 1, valueEnd - valueStart - 1);
    }

    int extractJsonIntValue(const std::string& json, const std::string& key)
    {
        log(DebugFeatures::MODULE_COMMAND_DEBUG,
            "ModuleCommandUseCase: Extracting int value - JSON: %s, Key: %s", json.c_str(), key.c_str());

        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return 0;
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return 0;
        size_t valueStart = colonPos + 1;
        while (valueStart < json.size() && std::isspace(json[valueStart])) valueStart++;
        
        // Check if value is a string (starts with quote)
        if (valueStart < json.size() && json[valueStart] == '"')
        {
            // Extract string value
            size_t stringStart = valueStart + 1;
            size_t stringEnd = json.find("\"", stringStart);
            if (stringEnd != std::string::npos)
            {
                std::string stringValue = json.substr(stringStart, stringEnd - stringStart);
                try
                {
                    int result = std::stoi(stringValue);
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Extracted int from string - value: %d", result);
                    return result;
                }
                catch (const std::exception& e)
                {
                    logError("ModuleCommandUseCase: Failed to parse integer from string value '%s' for key '%s'", 
                             stringValue.c_str(), key.c_str());
                }
            }
        }
        else
        {
            // Extract numeric value
            size_t valueEnd = valueStart;
            while (valueEnd < json.size() && (std::isdigit(json[valueEnd]) || json[valueEnd] == '-')) valueEnd++;
            if (valueEnd > valueStart)
            {
                try
                {
                    int result = std::stoi(json.substr(valueStart, valueEnd - valueStart));
                    log(DebugFeatures::MODULE_COMMAND_DEBUG,
                        "ModuleCommandUseCase: Extracted int from number - value: %d", result);
                    return result;
                }
                catch (const std::exception& e)
                {
                    logError("ModuleCommandUseCase: Failed to parse integer value for key '%s'", key.c_str());
                }
            }
        }
        return 0;
    }

    RawBusCommandResult handleRebootRemoteModule(const std::string &options)
    {
        std::string targetUid = extractJsonValue(options, "targetUid");
        log(DebugFeatures::MODULE_COMMAND_DEBUG, "REBOOT_REMOTE: send targetUid=%s", targetUid.c_str());

        if (targetUid.empty())
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG, "REBOOT_REMOTE: reject empty targetUid");
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Empty targetUid", "", false, 0);
        }

        if (remoteModuleCommandSender == nullptr)
        {
            log(DebugFeatures::MODULE_COMMAND_DEBUG, "REBOOT_REMOTE: sender not available");
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Sender not available", "", false, 0);
        }

        bool ok = remoteModuleCommandSender->sendReboot(targetUid);
        log(DebugFeatures::MODULE_COMMAND_DEBUG, "REBOOT_REMOTE: result targetUid=%s ok=%d", targetUid.c_str(), ok ? 1 : 0);

        if (ok)
        {
            return RawBusCommandResult(ModuleCommandStatus::DONE, "", "", false, 0);
        }
        return RawBusCommandResult(ModuleCommandStatus::ERROR, "UDP send failed", "", false, 0);
    }

    RawBusCommandResult handleModbusWrite(const std::string &options)
    {
        if (!rawBus || !moduleConfig)
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Raw bus not available", "", false, 0);
        }

        int channel = extractJsonIntValue(options, "channel");
        if (channel < 0 || channel >= moduleConfig->getSensorPinCount())
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Invalid channel", "", false, channel);
        }

        int address = extractJsonIntValue(options, "address");
        int function = extractJsonIntValue(options, "function");
        int reg = extractJsonIntValue(options, "register");

        std::vector<uint8_t> data;
        if (!Utils::HexCodec::hexToBytes(extractJsonValue(options, "data"), data))
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Invalid hex data", "", false, channel);
        }
        if (data.size() > 252)
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Data too large", "", false, channel);
        }

        std::vector<uint8_t> pdu;
        pdu.push_back((uint8_t)address);
        pdu.push_back((uint8_t)function);
        pdu.push_back((uint8_t)((reg >> 8) & 0xFF));
        pdu.push_back((uint8_t)(reg & 0xFF));
        for (size_t i = 0; i < data.size(); i++)
        {
            pdu.push_back(data[i]);
        }

        std::vector<uint8_t> out;
        bool ok = rawBus->modbusRequest(channel, pdu.data(), pdu.size(), false, rawReadTimeoutMs, out);
        if (!ok)
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Modbus write failed", "", false, channel);
        }
        return RawBusCommandResult(ModuleCommandStatus::DONE, "", "", false, channel);
    }

    RawBusCommandResult handleModbusRead(const std::string &options)
    {
        if (!rawBus || !moduleConfig)
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Raw bus not available", "", true, 0);
        }

        int channel = extractJsonIntValue(options, "channel");
        if (channel < 0 || channel >= moduleConfig->getSensorPinCount())
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Invalid channel", "", true, channel);
        }

        int address = extractJsonIntValue(options, "address");
        int function = extractJsonIntValue(options, "function");
        int reg = extractJsonIntValue(options, "register");
        int count = extractJsonIntValue(options, "count");
        if (count < 1 || count > 125)
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Invalid count", "", true, channel);
        }

        std::vector<uint8_t> pdu;
        pdu.push_back((uint8_t)address);
        pdu.push_back((uint8_t)function);
        pdu.push_back((uint8_t)((reg >> 8) & 0xFF));
        pdu.push_back((uint8_t)(reg & 0xFF));
        pdu.push_back((uint8_t)((count >> 8) & 0xFF));
        pdu.push_back((uint8_t)(count & 0xFF));

        std::vector<uint8_t> out;
        bool ok = rawBus->modbusRequest(channel, pdu.data(), pdu.size(), true, rawReadTimeoutMs, out);
        if (!ok || out.empty())
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Read timeout", "", true, channel);
        }

        std::string hex = Utils::HexCodec::bytesToHex(out.data(), out.size());
        return RawBusCommandResult(ModuleCommandStatus::DONE, "", hex, true, channel);
    }

    RawBusCommandResult handleSerialWrite(const std::string &options)
    {
        if (!rawBus || !moduleConfig)
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Raw bus not available", "", false, 0);
        }

        int channel = extractJsonIntValue(options, "channel");
        if (channel < 0 || channel >= moduleConfig->getSensorPinCount())
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Invalid channel", "", false, channel);
        }

        std::vector<uint8_t> bytes;
        if (!Utils::HexCodec::hexToBytes(extractJsonValue(options, "bytes"), bytes) || bytes.empty())
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Invalid hex bytes", "", false, channel);
        }
        if (bytes.size() > 256)
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Bytes too large", "", false, channel);
        }

        bool expectResponse = extractJsonBool(options, "expectResponse");

        std::vector<uint8_t> out;
        bool ok = rawBus->serialWrite(channel, bytes.data(), bytes.size(), expectResponse, rawReadTimeoutMs, out);

        if (!expectResponse)
        {
            if (!ok)
            {
                return RawBusCommandResult(ModuleCommandStatus::ERROR, "Serial write failed", "", false, channel);
            }
            return RawBusCommandResult(ModuleCommandStatus::DONE, "", "", false, channel);
        }

        if (!ok || out.empty())
        {
            return RawBusCommandResult(ModuleCommandStatus::ERROR, "Read timeout", "", true, channel);
        }

        std::string hex = Utils::HexCodec::bytesToHex(out.data(), out.size());
        return RawBusCommandResult(ModuleCommandStatus::DONE, "", hex, true, channel);
    }

    bool extractJsonBool(const std::string &json, const std::string &key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return false;
        size_t colonPos = json.find(":", keyPos);
        if (colonPos == std::string::npos) return false;
        size_t valueStart = colonPos + 1;
        while (valueStart < json.size() && std::isspace((unsigned char)json[valueStart])) valueStart++;
        if (json.compare(valueStart, 4, "true") == 0) return true;
        if (valueStart < json.size() && json[valueStart] == '1') return true;
        if (valueStart < json.size() && json[valueStart] == '"')
        {
            std::string quoted = extractJsonValue(json, key);
            return quoted == "true" || quoted == "1";
        }
        return false;
    }

    void log(const char *feature, const char *format, ...)
    {
        if (debugSystem && debugSystem->isFeatureEnabled(feature) && logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* debugFeature = debugSystem->getFeature(feature);
            if (debugFeature)
            {
                logger->println(feature, buffer, debugFeature->getLogChannel());
            }
            else
            {
                logger->println(feature, buffer);
            }
        }
    }

    void logError(const char *format, ...)
    {
        if (logger)
        {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println("ERROR", buffer);
        }
    }
};
