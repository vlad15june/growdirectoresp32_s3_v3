#pragma once

/**
 * @brief Forwarding relays that put an owner tag around a call into a subsystem.
 *
 * Each of these implements one of the interfaces the composition root injects, holds the real
 * object, and forwards every method to it unchanged - the only thing added is a
 * HostAllocationCensus::Scope, so that whatever the call allocates below it is charged to the
 * subsystem the call went into. Nothing here decides anything, and no method has a body of its
 * own: a relay that computed an answer would make the census measure the harness instead of the
 * firmware.
 *
 * They are injected where the real object would be injected, so the module runs exactly the graph
 * it runs without them, one virtual call deeper. The relay for a task the firmware starts itself
 * does not exist and cannot: HostTaskScheduler tags those by task name instead.
 *
 * Scopes nest, and the innermost wins. That is what puts the converter's own allocations under
 * "json" even when the catalog synchronization is what called it, and what keeps them out of the
 * catalog's own row.
 */

#include "Sim/HostAllocationCounter.h"

#include "Application/Interfaces/ICommandService.h"
#include "Application/Interfaces/IDiagnosticService.h"
#include "Application/Interfaces/IOutbox.h"
#include "Application/Services/Devices/IDeviceService.h"
#include "Application/Services/Rules/IRuleService.h"
#include "Application/Services/Sensors/ISensorService.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/UseCases/IConfirmRunningFirmwareUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IOnConnectivityRestoredUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IRepublishModuleStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Rules/ISyncRulesUseCase.h"
#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"

namespace hostsim {

namespace census = HostAllocationCensus;

class CensusSensorService : public ISensorService {
public:
    explicit CensusSensorService(ISensorService& real) : _real(real) {}

    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        census::Scope owner(census::kSensors);
        _real.onOutboxCompletion(completion);
    }
    bool init() override
    {
        census::Scope owner(census::kSensors);
        return _real.init();
    }
    void loop() override
    {
        census::Scope owner(census::kSensors);
        _real.loop();
    }
    bool readAllSensors() override
    {
        census::Scope owner(census::kSensors);
        return _real.readAllSensors();
    }
    bool readSensor(SensorEntity sensor) override
    {
        census::Scope owner(census::kSensors);
        return _real.readSensor(sensor);
    }

private:
    ISensorService& _real;
};

class CensusDeviceService : public IDeviceService {
public:
    explicit CensusDeviceService(IDeviceService& real) : _real(real) {}

    bool init() override
    {
        census::Scope owner(census::kDevices);
        return _real.init();
    }
    void loop() override
    {
        census::Scope owner(census::kDevices);
        _real.loop();
    }
    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        census::Scope owner(census::kDevices);
        _real.onOutboxCompletion(completion);
    }

private:
    IDeviceService& _real;
};

class CensusRuleService : public IRuleService {
public:
    explicit CensusRuleService(IRuleService& real) : _real(real) {}

    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        census::Scope owner(census::kRules);
        _real.onOutboxCompletion(completion);
    }
    bool init() override
    {
        census::Scope owner(census::kRules);
        return _real.init();
    }
    void loop() override
    {
        census::Scope owner(census::kRules);
        _real.loop();
    }
    bool forceSyncRules() override
    {
        census::Scope owner(census::kRules);
        return _real.forceSyncRules();
    }

private:
    IRuleService& _real;
};

class CensusCommandService : public ICommandService {
public:
    explicit CensusCommandService(ICommandService& real) : _real(real) {}

    bool process(const CommandDto& command) override
    {
        census::Scope owner(census::kCommands);
        return _real.process(command);
    }
    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        census::Scope owner(census::kCommands);
        _real.onOutboxCompletion(completion);
    }
    void loop() override
    {
        census::Scope owner(census::kCommands);
        _real.loop();
    }
    void requestModuleCommandFetch() override
    {
        census::Scope owner(census::kCommands);
        _real.requestModuleCommandFetch();
    }
    void resumeKeptUpdate() override
    {
        census::Scope owner(census::kCommands);
        _real.resumeKeptUpdate();
    }
    void checkModuleDeletion() override
    {
        census::Scope owner(census::kCommands);
        _real.checkModuleDeletion();
    }

private:
    ICommandService& _real;
};

class CensusDiagnosticService : public IDiagnosticService {
public:
    explicit CensusDiagnosticService(IDiagnosticService& real) : _real(real) {}

    DiagnosticDto perform() override
    {
        census::Scope owner(census::kDiagnostics);
        return _real.perform();
    }
    bool send(const DiagnosticDto& diagnosticData) override
    {
        census::Scope owner(census::kDiagnostics);
        return _real.send(diagnosticData);
    }
    bool performAndSend() override
    {
        census::Scope owner(census::kDiagnostics);
        return _real.performAndSend();
    }

private:
    IDiagnosticService& _real;
};

class CensusSyncSensorTypesUseCase : public ISyncSensorTypesUseCase {
public:
    explicit CensusSyncSensorTypesUseCase(ISyncSensorTypesUseCase& real) : _real(real) {}

    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        census::Scope owner(census::kCatalog);
        _real.onOutboxCompletion(completion);
    }
    void onCatalogWriteCompletion(const CatalogWriteCompletion& completion) override
    {
        census::Scope owner(census::kCatalog);
        _real.onCatalogWriteCompletion(completion);
    }
    bool init() override
    {
        census::Scope owner(census::kCatalog);
        return _real.init();
    }
    void loop() override
    {
        census::Scope owner(census::kCatalog);
        _real.loop();
    }
    bool forceSync() override
    {
        census::Scope owner(census::kCatalog);
        return _real.forceSync();
    }
    bool syncNow() override
    {
        census::Scope owner(census::kCatalog);
        return _real.syncNow();
    }
    bool getLastSyncStatus() const override { return _real.getLastSyncStatus(); }
    time_t getLastSyncTime() const override { return _real.getLastSyncTime(); }

private:
    ISyncSensorTypesUseCase& _real;
};

class CensusSyncRulesUseCase : public ISyncRulesUseCase {
public:
    explicit CensusSyncRulesUseCase(ISyncRulesUseCase& real) : _real(real) {}

    bool init() override
    {
        census::Scope owner(census::kRules);
        return _real.init();
    }
    void loop() override
    {
        census::Scope owner(census::kRules);
        _real.loop();
    }
    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        census::Scope owner(census::kRules);
        _real.onOutboxCompletion(completion);
    }
    bool forceSync() override
    {
        census::Scope owner(census::kRules);
        return _real.forceSync();
    }
    bool getLastSyncStatus() const override { return _real.getLastSyncStatus(); }
    time_t getLastSyncTime() const override { return _real.getLastSyncTime(); }

private:
    ISyncRulesUseCase& _real;
};

class CensusRepublishStructureUseCase : public IRepublishModuleStructureUseCase {
public:
    explicit CensusRepublishStructureUseCase(IRepublishModuleStructureUseCase& real) : _real(real)
    {
    }

    bool execute(const char* reason) override
    {
        census::Scope owner(census::kStructure);
        return _real.execute(reason);
    }
    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        census::Scope owner(census::kStructure);
        _real.onOutboxCompletion(completion);
    }

private:
    IRepublishModuleStructureUseCase& _real;
};

class CensusOnChangeLocalIpUseCase : public IOnChangeLocalIpUseCase {
public:
    explicit CensusOnChangeLocalIpUseCase(IOnChangeLocalIpUseCase& real) : _real(real) {}

    void perform() override
    {
        census::Scope owner(census::kStructure);
        _real.perform();
    }
    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        census::Scope owner(census::kStructure);
        _real.onOutboxCompletion(completion);
    }

private:
    IOnChangeLocalIpUseCase& _real;
};

class CensusOnConnectivityRestoredUseCase : public IOnConnectivityRestoredUseCase {
public:
    explicit CensusOnConnectivityRestoredUseCase(IOnConnectivityRestoredUseCase& real) : _real(real)
    {
    }

    bool init() override
    {
        census::Scope owner(census::kRules);
        return _real.init();
    }
    void perform() override
    {
        // Charged to the rules: all this use case does with the heap is force a rule sync.
        census::Scope owner(census::kRules);
        _real.perform();
    }

private:
    IOnConnectivityRestoredUseCase& _real;
};

class CensusConfirmRunningFirmwareUseCase : public IConfirmRunningFirmwareUseCase {
public:
    explicit CensusConfirmRunningFirmwareUseCase(IConfirmRunningFirmwareUseCase& real) : _real(real)
    {
    }

    void onControlLoopIteration() override
    {
        census::Scope owner(census::kOta);
        _real.onControlLoopIteration();
    }

private:
    IConfirmRunningFirmwareUseCase& _real;
};

/**
 * @brief The outbox, with every submission charged to the outbox rather than to the caller.
 *
 * Deliberate: the bytes a submission allocates are the queued event, and the queue is what holds
 * them until the network task is done. Charging them to the sensor service that asked for the
 * send would show the queue's depth spread over its customers.
 */
class CensusOutbox : public IOutbox {
public:
    explicit CensusOutbox(IOutbox& real) : _real(real) {}

    uint32_t submitSensorUpdate(const UpdateDto& update) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitSensorUpdate(update);
    }
    uint32_t submitDeviceUpdate(const UpdateDto& update) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitDeviceUpdate(update);
    }
    uint32_t submitDiagnostic(const DiagnosticDto& diagnostic) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitDiagnostic(diagnostic);
    }
    uint32_t submitRuleListRequest() override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitRuleListRequest();
    }
    uint32_t submitRuleStateBulk(const std::string& json) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitRuleStateBulk(json);
    }
    uint32_t submitRuleStateByUid(const std::string& ruleUid, const std::string& json) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitRuleStateByUid(ruleUid, json);
    }
    uint32_t submitRuleSensorOffline(const std::string& json) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitRuleSensorOffline(json);
    }
    uint32_t submitDeviceCommandFetch() override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitDeviceCommandFetch();
    }
    uint32_t submitModuleCommandFetch() override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitModuleCommandFetch();
    }
    uint32_t submitSensorTypesRequest() override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitSensorTypesRequest();
    }
    uint32_t submitWifiReconnect() override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitWifiReconnect();
    }
    uint32_t submitWifiReconnectForALostLink() override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitWifiReconnectForALostLink();
    }
    bool ensureNetworkTask() override
    {
        census::Scope owner(census::kOutbox);
        return _real.ensureNetworkTask();
    }
    uint32_t submitLinkProbe() override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitLinkProbe();
    }
    uint32_t submitWifiRoam(const uint8_t* bssid, uint8_t channel) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitWifiRoam(bssid, channel);
    }
    uint32_t submitCommandResult(const CommandResultDto& result, ApiPriority priority) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitCommandResult(result, priority);
    }
    uint32_t submitDeviceCommandResult(const DeviceCommandUpdateDto& update) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitDeviceCommandResult(update);
    }
    uint32_t submitModuleCommandResult(const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitModuleCommandResult(updates);
    }
    uint32_t submitRawIoResult(const RawIoResultDto& result) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitRawIoResult(result);
    }
    uint32_t submitModuleStructure(const ModuleStructure& structure, ApiPriority priority) override
    {
        census::Scope owner(census::kOutbox);
        return _real.submitModuleStructure(structure, priority);
    }
    bool pollCompletion(OutboxCompletion& out) override
    {
        census::Scope owner(census::kOutbox);
        return _real.pollCompletion(out);
    }
    bool takeRuleListResponse(std::vector<Rule>& out) override
    {
        census::Scope owner(census::kOutbox);
        return _real.takeRuleListResponse(out);
    }
    bool takeDeviceCommandResponse(std::vector<DeviceCommandDto>& out) override
    {
        census::Scope owner(census::kOutbox);
        return _real.takeDeviceCommandResponse(out);
    }
    bool takeModuleCommandResponse(std::vector<ModuleCommandDto>& out) override
    {
        census::Scope owner(census::kOutbox);
        return _real.takeModuleCommandResponse(out);
    }
    bool takeSensorTypesResponse(std::vector<SensorType>& out) override
    {
        census::Scope owner(census::kOutbox);
        return _real.takeSensorTypesResponse(out);
    }
    uint32_t collapsedCount() const override { return _real.collapsedCount(); }
    uint32_t refusedEventCount() const override { return _real.refusedEventCount(); }
    uint32_t undeliveredCount() const override { return _real.undeliveredCount(); }
    uint32_t lostCompletionCount() const override { return _real.lostCompletionCount(); }

private:
    IOutbox& _real;
};

/**
 * @brief The JSON converter, with parsing and serialization charged to json.
 *
 * The documents themselves do not appear in any bucket on either side: ArduinoJson takes them
 * from JsonMemoryPool, which serves malloc and static slots, and this meter counts operator new.
 * What this row measures is the rest of the work - the entity vectors a parse fills, the strings a
 * serialization builds - which is heap on the board too.
 */
class CensusJsonConverter : public IJsonConverter {
public:
    explicit CensusJsonConverter(IJsonConverter& real) : _real(real) {}

    bool moduleStructureFromJson(const std::string& jsonStr, ModuleStructure& moduleStructure) override
    {
        census::Scope owner(census::kJson);
        return _real.moduleStructureFromJson(jsonStr, moduleStructure);
    }
    std::string moduleStructureToJson(const ModuleStructure& moduleStructure) override
    {
        census::Scope owner(census::kJson);
        return _real.moduleStructureToJson(moduleStructure);
    }
    bool ruleFromJson(const std::string& jsonStr, Rule& rule) override
    {
        census::Scope owner(census::kJson);
        return _real.ruleFromJson(jsonStr, rule);
    }
    std::string ruleToJson(const Rule& rule) override
    {
        census::Scope owner(census::kJson);
        return _real.ruleToJson(rule);
    }
    JsonListParse rulesFromJson(const std::string& jsonStr, std::vector<Rule>& rules) override
    {
        census::Scope owner(census::kJson);
        return _real.rulesFromJson(jsonStr, rules);
    }
    std::string rulesToJson(const std::vector<Rule>& rules) override
    {
        census::Scope owner(census::kJson);
        return _real.rulesToJson(rules);
    }
    std::string diagnosticToJson(const DiagnosticDto& diagnosticData) override
    {
        census::Scope owner(census::kJson);
        return _real.diagnosticToJson(diagnosticData);
    }
    std::string updateToJson(const UpdateDto& updateData) override
    {
        census::Scope owner(census::kJson);
        return _real.updateToJson(updateData);
    }
    bool commandFromJson(const std::string& jsonStr, CommandDto& commandDto) override
    {
        census::Scope owner(census::kJson);
        return _real.commandFromJson(jsonStr, commandDto);
    }
    std::string commandToJson(const CommandDto& commandDto) override
    {
        census::Scope owner(census::kJson);
        return _real.commandToJson(commandDto);
    }
    std::string deviceCommandUpdateToJson(const DeviceCommandUpdateDto& commandUpdateDto) override
    {
        census::Scope owner(census::kJson);
        return _real.deviceCommandUpdateToJson(commandUpdateDto);
    }
    bool deviceCommandFromJson(const std::string& jsonStr, DeviceCommandDto& commandDto) override
    {
        census::Scope owner(census::kJson);
        return _real.deviceCommandFromJson(jsonStr, commandDto);
    }
    std::string deviceCommandToJson(const DeviceCommandDto& commandDto) override
    {
        census::Scope owner(census::kJson);
        return _real.deviceCommandToJson(commandDto);
    }
    bool deviceCommandsFromJson(const std::string& jsonStr,
                                std::vector<DeviceCommandDto>& commands) override
    {
        census::Scope owner(census::kJson);
        return _real.deviceCommandsFromJson(jsonStr, commands);
    }
    std::string moduleCommandUpdateToJson(const ModuleCommandUpdateDto& commandUpdateDto) override
    {
        census::Scope owner(census::kJson);
        return _real.moduleCommandUpdateToJson(commandUpdateDto);
    }
    std::string moduleCommandUpdatesToJson(
        const std::vector<ModuleCommandUpdateDto>& commandUpdates) override
    {
        census::Scope owner(census::kJson);
        return _real.moduleCommandUpdatesToJson(commandUpdates);
    }
    bool moduleCommandFromJson(const std::string& jsonStr, ModuleCommandDto& commandDto) override
    {
        census::Scope owner(census::kJson);
        return _real.moduleCommandFromJson(jsonStr, commandDto);
    }
    std::string moduleCommandToJson(const ModuleCommandDto& commandDto) override
    {
        census::Scope owner(census::kJson);
        return _real.moduleCommandToJson(commandDto);
    }
    bool moduleCommandsFromJson(const std::string& jsonStr,
                                std::vector<ModuleCommandDto>& commands) override
    {
        census::Scope owner(census::kJson);
        return _real.moduleCommandsFromJson(jsonStr, commands);
    }
    std::string ruleStateToJson(const RuleStateDto& ruleState) override
    {
        census::Scope owner(census::kJson);
        return _real.ruleStateToJson(ruleState);
    }
    bool ruleStateFromJson(const std::string& jsonStr, RuleStateDto& ruleState) override
    {
        census::Scope owner(census::kJson);
        return _real.ruleStateFromJson(jsonStr, ruleState);
    }
    std::string ruleStateUpdateToJson(const RuleStateUpdateDto& ruleStateUpdate) override
    {
        census::Scope owner(census::kJson);
        return _real.ruleStateUpdateToJson(ruleStateUpdate);
    }
    bool ruleStateUpdateFromJson(const std::string& jsonStr,
                                 RuleStateUpdateDto& ruleStateUpdate) override
    {
        census::Scope owner(census::kJson);
        return _real.ruleStateUpdateFromJson(jsonStr, ruleStateUpdate);
    }
    std::string ruleStateUpdateSingleToJson(const RuleStateDto& ruleState,
                                            const std::string& timestamp) override
    {
        census::Scope owner(census::kJson);
        return _real.ruleStateUpdateSingleToJson(ruleState, timestamp);
    }
    std::string ruleSensorOfflineEventToJson(const RuleSensorOfflineEventDto& event) override
    {
        census::Scope owner(census::kJson);
        return _real.ruleSensorOfflineEventToJson(event);
    }
    std::string commandResultToJson(const CommandResultDto& commandResultDto) override
    {
        census::Scope owner(census::kJson);
        return _real.commandResultToJson(commandResultDto);
    }
    bool commandResultFromJson(const std::string& jsonStr,
                               CommandResultDto& commandResultDto) override
    {
        census::Scope owner(census::kJson);
        return _real.commandResultFromJson(jsonStr, commandResultDto);
    }
    std::string rawIoResultToJson(const RawIoResultDto& rawIoResult) override
    {
        census::Scope owner(census::kJson);
        return _real.rawIoResultToJson(rawIoResult);
    }
    bool sensorFromJson(const std::string& jsonStr, SensorEntity& sensor) override
    {
        census::Scope owner(census::kJson);
        return _real.sensorFromJson(jsonStr, sensor);
    }
    std::string sensorToJson(const SensorEntity& sensor) override
    {
        census::Scope owner(census::kJson);
        return _real.sensorToJson(sensor);
    }
    bool sensorsFromJson(const std::string& jsonStr, std::vector<SensorEntity>& sensors) override
    {
        census::Scope owner(census::kJson);
        return _real.sensorsFromJson(jsonStr, sensors);
    }
    bool sensorsFromJson(const std::string& jsonStr, std::vector<SensorEntity>& sensors,
                         size_t& skipped, size_t& refused) override
    {
        // Forwarded rather than left to the interface default: the default would reach the real
        // converter through the two-argument call above, which cannot answer with the counts, and
        // a caller behind this relay would be told nothing was skipped or refused.
        census::Scope owner(census::kJson);
        return _real.sensorsFromJson(jsonStr, sensors, skipped, refused);
    }
    std::string sensorsToJson(const std::vector<SensorEntity>& sensors) override
    {
        census::Scope owner(census::kJson);
        return _real.sensorsToJson(sensors);
    }
    bool sensorTypeFromJson(const std::string& jsonStr, SensorType& sensorType) override
    {
        census::Scope owner(census::kJson);
        return _real.sensorTypeFromJson(jsonStr, sensorType);
    }
    std::string sensorTypeToJson(const SensorType& sensorType) override
    {
        census::Scope owner(census::kJson);
        return _real.sensorTypeToJson(sensorType);
    }
    JsonListParse sensorTypesFromJson(const std::string& jsonStr,
                                      std::vector<SensorType>& sensorTypes) override
    {
        census::Scope owner(census::kJson);
        return _real.sensorTypesFromJson(jsonStr, sensorTypes);
    }
    std::string sensorTypesToJson(const std::vector<SensorType>& sensorTypes) override
    {
        census::Scope owner(census::kJson);
        return _real.sensorTypesToJson(sensorTypes);
    }
    bool modbusRegisterConfigFromJson(const std::string& jsonStr,
                                      ModbusRegisterConfig& config) override
    {
        census::Scope owner(census::kJson);
        return _real.modbusRegisterConfigFromJson(jsonStr, config);
    }
    std::string modbusRegisterConfigToJson(const ModbusRegisterConfig& config) override
    {
        census::Scope owner(census::kJson);
        return _real.modbusRegisterConfigToJson(config);
    }
    bool modbusRegisterConfigsFromJson(const std::string& jsonStr,
                                       std::vector<ModbusRegisterConfig>& configs) override
    {
        census::Scope owner(census::kJson);
        return _real.modbusRegisterConfigsFromJson(jsonStr, configs);
    }
    std::string modbusRegisterConfigsToJson(const std::vector<ModbusRegisterConfig>& configs) override
    {
        census::Scope owner(census::kJson);
        return _real.modbusRegisterConfigsToJson(configs);
    }
    bool i2cRegisterConfigFromJson(const std::string& jsonStr, I2CRegisterConfig& config) override
    {
        census::Scope owner(census::kJson);
        return _real.i2cRegisterConfigFromJson(jsonStr, config);
    }
    std::string i2cRegisterConfigToJson(const I2CRegisterConfig& config) override
    {
        census::Scope owner(census::kJson);
        return _real.i2cRegisterConfigToJson(config);
    }
    bool i2cRegisterConfigsFromJson(const std::string& jsonStr,
                                    std::vector<I2CRegisterConfig>& configs) override
    {
        census::Scope owner(census::kJson);
        return _real.i2cRegisterConfigsFromJson(jsonStr, configs);
    }
    std::string i2cRegisterConfigsToJson(const std::vector<I2CRegisterConfig>& configs) override
    {
        census::Scope owner(census::kJson);
        return _real.i2cRegisterConfigsToJson(configs);
    }
    bool wifiNetworksFromJson(const std::string& jsonStr,
                              std::vector<WifiNetworkEntry>& networks) override
    {
        census::Scope owner(census::kJson);
        return _real.wifiNetworksFromJson(jsonStr, networks);
    }
    std::string wifiNetworksToJson(const std::vector<WifiNetworkEntry>& networks) override
    {
        census::Scope owner(census::kJson);
        return _real.wifiNetworksToJson(networks);
    }
    bool sensorValuesFromUpdateJson(const std::string& json,
                                    std::vector<RemoteSensorValue>& out) override
    {
        census::Scope owner(census::kJson);
        return _real.sensorValuesFromUpdateJson(json, out);
    }
    std::string remoteModuleCommandToJson(const std::string& command,
                                          const std::string& targetUid) override
    {
        census::Scope owner(census::kJson);
        return _real.remoteModuleCommandToJson(command, targetUid);
    }
    bool remoteModuleCommandFromJson(const std::string& json, std::string& outCommand,
                                     std::string& outTargetUid) override
    {
        census::Scope owner(census::kJson);
        return _real.remoteModuleCommandFromJson(json, outCommand, outTargetUid);
    }

private:
    IJsonConverter& _real;
};

}  // namespace hostsim
