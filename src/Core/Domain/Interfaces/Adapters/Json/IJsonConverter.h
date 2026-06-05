#ifndef I_JSON_CONVERTER_H
#define I_JSON_CONVERTER_H

#include <string>
#include <vector>
#include "Core/Domain/Data/Entities/WifiNetworkEntry.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/RawIoResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include "Application/DTOs/RuleStateUpdateDto.h"

class IJsonConverter
{
public:
    virtual bool moduleStructureFromJson(const std::string &jsonStr, ModuleStructure &moduleStructure) = 0;
    virtual std::string moduleStructureToJson(const ModuleStructure &moduleStructure) = 0;
    virtual bool ruleFromJson(const std::string &jsonStr, Rule &rule) = 0;
    virtual std::string ruleToJson(const Rule &rule) = 0;
    virtual bool rulesFromJson(const std::string &jsonStr, std::vector<Rule> &rules) = 0;
    virtual std::string rulesToJson(const std::vector<Rule> &rules) = 0;
    virtual std::string diagnosticToJson(const DiagnosticDto &diagnosticData) = 0;
    virtual std::string updateToJson(const UpdateDto &updateData) = 0;
    virtual bool commandFromJson(const std::string &jsonStr, CommandDto &commandDto) = 0;
    virtual std::string commandToJson(const CommandDto &commandDto) = 0;
    virtual std::string deviceCommandUpdateToJson(const DeviceCommandUpdateDto& commandUpdateDto) = 0;
    virtual bool deviceCommandFromJson(const std::string &jsonStr, DeviceCommandDto &commandDto) = 0;
    virtual std::string deviceCommandToJson(const DeviceCommandDto &commandDto) = 0;
    virtual bool deviceCommandsFromJson(const std::string &jsonStr, std::vector<DeviceCommandDto> &commands) = 0;
    virtual std::string moduleCommandUpdateToJson(const ModuleCommandUpdateDto& commandUpdateDto) = 0;
    virtual std::string moduleCommandUpdatesToJson(const std::vector<ModuleCommandUpdateDto>& commandUpdates) = 0;
    virtual bool moduleCommandFromJson(const std::string &jsonStr, ModuleCommandDto &commandDto) = 0;
    virtual std::string moduleCommandToJson(const ModuleCommandDto &commandDto) = 0;
    virtual bool moduleCommandsFromJson(const std::string &jsonStr, std::vector<ModuleCommandDto> &commands) = 0;
    virtual std::string ruleStateToJson(const RuleStateDto& ruleState) = 0;
    virtual bool ruleStateFromJson(const std::string& jsonStr, RuleStateDto& ruleState) = 0;
    virtual std::string ruleStateUpdateToJson(const RuleStateUpdateDto& ruleStateUpdate) = 0;
    virtual bool ruleStateUpdateFromJson(const std::string& jsonStr, RuleStateUpdateDto& ruleStateUpdate) = 0;
    virtual std::string ruleStateUpdateSingleToJson(const RuleStateDto& ruleState, const std::string& timestamp) = 0;
    virtual std::string commandResultToJson(const CommandResultDto &commandResultDto) = 0;
    virtual bool commandResultFromJson(const std::string &jsonStr, CommandResultDto &commandResultDto) = 0;
    virtual std::string rawIoResultToJson(const RawIoResultDto &rawIoResult) = 0;
    virtual bool sensorFromJson(const std::string &jsonStr, SensorEntity &sensor) = 0;
    virtual std::string sensorToJson(const SensorEntity &sensor) = 0;
    virtual bool sensorsFromJson(const std::string &jsonStr, std::vector<SensorEntity> &sensors) = 0;
    virtual std::string sensorsToJson(const std::vector<SensorEntity> &sensors) = 0;
    virtual bool sensorTypeFromJson(const std::string &jsonStr, SensorType &sensorType) = 0;
    virtual std::string sensorTypeToJson(const SensorType &sensorType) = 0;
    virtual bool sensorTypesFromJson(const std::string &jsonStr, std::vector<SensorType> &sensorTypes) = 0;
    virtual std::string sensorTypesToJson(const std::vector<SensorType> &sensorTypes) = 0;
    virtual bool modbusRegisterConfigFromJson(const std::string &jsonStr, ModbusRegisterConfig &config) = 0;
    virtual std::string modbusRegisterConfigToJson(const ModbusRegisterConfig &config) = 0;
    virtual bool modbusRegisterConfigsFromJson(const std::string &jsonStr, std::vector<ModbusRegisterConfig> &configs) = 0;
    virtual std::string modbusRegisterConfigsToJson(const std::vector<ModbusRegisterConfig> &configs) = 0;
    virtual bool i2cRegisterConfigFromJson(const std::string &jsonStr, I2CRegisterConfig &config) = 0;
    virtual std::string i2cRegisterConfigToJson(const I2CRegisterConfig &config) = 0;
    virtual bool i2cRegisterConfigsFromJson(const std::string &jsonStr, std::vector<I2CRegisterConfig> &configs) = 0;
    virtual std::string i2cRegisterConfigsToJson(const std::vector<I2CRegisterConfig> &configs) = 0;
    virtual bool wifiNetworksFromJson(const std::string &jsonStr, std::vector<WifiNetworkEntry> &networks) = 0;
    virtual std::string wifiNetworksToJson(const std::vector<WifiNetworkEntry> &networks) = 0;
    virtual bool sensorValuesFromUpdateJson(const std::string &json, std::vector<std::pair<std::string, float>> &out) = 0;
    virtual ~IJsonConverter() = default;
};

#endif // I_JSON_CONVERTER_H
