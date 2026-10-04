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
#include "Core/Domain/Data/Models/RemoteSensorValue.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/CommandResultDto.h"
#include "Application/DTOs/RawIoResultDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/ModuleCommandDto.h"
#include "Application/DTOs/ModuleCommandUpdateDto.h"
#include "Application/DTOs/RuleStateUpdateDto.h"
#include "Application/DTOs/RuleSensorOfflineEventDto.h"

/**
 * @brief What became of an attempt to read a JSON *list*.
 *
 * Three outcomes, because there are three states and a bool only has room for two. The two a
 * bool would fold together are the ones that matter: a list the module could not read, and a
 * list the server correctly says is empty.
 *
 * Folded together they read like this: a converter that answers "did I build at least one rule"
 * gets read as "did the parse succeed". On an account with no rules the server answers 200 and
 * `[]`, the converter says false, the transport reports a transport failure, the retry budget is
 * spent on a correct answer, and the module reports a failed synchronization every five minutes
 * forever - it lies about its own state, and a new module becomes indistinguishable from one
 * with a broken link.
 *
 * A named outcome cannot be misread this way: `Empty` and `Failed` force the caller to say which
 * one it means.
 */
enum class JsonListParse : uint8_t
{
    /** The list was read and holds at least one item. */
    Ok = 0,
    /** The list was read and is legitimately empty - the server's answer, not a fault. */
    Empty = 1,
    /** The body is not a readable list. Never treat this as "the server said there is nothing". */
    Failed = 2,
};

/**
 * @brief Decide, from the text alone, whether a body is an empty list, a non-empty list, or not a
 *        list at all.
 *
 * One copy on purpose. There are two IJsonConverter implementations in this firmware -
 * ArduinoJsonConverter, which the device runs, and JsonConverter, which most of the suite uses -
 * and what the converter decides here is a rule about empty lists. Two copies of that rule would
 * be two chances to get it wrong, and the tests would prove whichever one they happened to
 * instantiate.
 *
 * @return Failed for anything that is not `[ ... ]`, Empty for `[]` and `[  ]`, Ok otherwise.
 *         Whether the *elements* are usable is the converter's business, not this function's.
 */
inline JsonListParse classifyJsonArray(const std::string &jsonStr)
{
    // No allocation anywhere in here, and that is a requirement rather than a style. This runs in
    // front of every rule-list and sensor-type parse, including the ones that read a SPIFFS file,
    // and ArduinoJsonConverter::sensorTypesFromJson() below scans its body character by character
    // for exactly one reason: holding the whole catalog in a JsonDocument peaks the heap hard
    // enough to run the device out of memory and write "[]" over the stored catalog. Copying the
    // body here to look at it would put that peak straight back in front of the code that exists
    // to avoid it.
    static const char *const whitespace = " \t\r\n";

    const size_t first = jsonStr.find_first_not_of(whitespace);
    if (first == std::string::npos)
    {
        return JsonListParse::Failed; // no body at all
    }

    const size_t last = jsonStr.find_last_not_of(whitespace);
    if (jsonStr[first] != '[' || jsonStr[last] != ']' || last <= first)
    {
        return JsonListParse::Failed; // an object, a bare word, a truncated body
    }

    // The first non-blank character strictly inside the brackets. If there is none before the
    // closing bracket, the array is empty. Searched in place - no substring is built.
    const size_t inner = jsonStr.find_first_not_of(whitespace, first + 1);
    return (inner >= last) ? JsonListParse::Empty : JsonListParse::Ok;
}

class IJsonConverter
{
public:
    virtual bool moduleStructureFromJson(const std::string &jsonStr, ModuleStructure &moduleStructure) = 0;
    virtual std::string moduleStructureToJson(const ModuleStructure &moduleStructure) = 0;
    virtual bool ruleFromJson(const std::string &jsonStr, Rule &rule) = 0;
    virtual std::string ruleToJson(const Rule &rule) = 0;
    /**
     * @brief Read a rule list.
     * @return Ok with rules filled, Empty when the server legitimately has none, Failed when the
     *         body could not be read - which a caller must never apply as an empty snapshot.
     */
    virtual JsonListParse rulesFromJson(const std::string &jsonStr, std::vector<Rule> &rules) = 0;
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
    virtual std::string ruleSensorOfflineEventToJson(const RuleSensorOfflineEventDto& event) = 0;
    virtual std::string commandResultToJson(const CommandResultDto &commandResultDto) = 0;
    virtual bool commandResultFromJson(const std::string &jsonStr, CommandResultDto &commandResultDto) = 0;
    virtual std::string rawIoResultToJson(const RawIoResultDto &rawIoResult) = 0;
    virtual bool sensorFromJson(const std::string &jsonStr, SensorEntity &sensor) = 0;
    virtual std::string sensorToJson(const SensorEntity &sensor) = 0;
    virtual bool sensorsFromJson(const std::string &jsonStr, std::vector<SensorEntity> &sensors) = 0;
    /**
     * @brief The same read, saying what the registry cost to read.
     *
     * A registry read whole and a registry read with records missing are the same answer otherwise,
     * and a module that comes up with fewer sensors than it stored has to be able to say so.
     * `skipped` counts records this module could not read, `refused` records it would not take -
     * past its capacity, or larger than one sensor may be. The counts are out-parameters rather
     * than state on the converter: one converter is shared by every task in this firmware, and a
     * "last result" on it would be read by whoever asked next.
     *
     * The default is for converters that refuse a registry rather than read the rest of it, which
     * leaves nothing to count.
     */
    virtual bool sensorsFromJson(const std::string &jsonStr, std::vector<SensorEntity> &sensors,
                                 size_t &skipped, size_t &refused)
    {
        skipped = 0;
        refused = 0;
        return sensorsFromJson(jsonStr, sensors);
    }
    virtual std::string sensorsToJson(const std::vector<SensorEntity> &sensors) = 0;
    virtual bool sensorTypeFromJson(const std::string &jsonStr, SensorType &sensorType) = 0;
    virtual std::string sensorTypeToJson(const SensorType &sensorType) = 0;
    /** @brief Read a sensor type catalog. Same three outcomes, same reason, as rulesFromJson(). */
    virtual JsonListParse sensorTypesFromJson(const std::string &jsonStr, std::vector<SensorType> &sensorTypes) = 0;
    /**
     * @brief The same read, saying how many sensor types were left out because they could not be
     *        read.
     *
     * Ok with entries missing and Ok with all of them present are the same answer otherwise, and a
     * catalog that comes back one model short has to be something a caller can report. The count is
     * an out-parameter rather than state on the converter: one converter is shared by every task in
     * this firmware, and a "last result" on it would be read by whoever asked next.
     *
     * The default is for converters that refuse a list rather than read the rest of it, which
     * leaves nothing to count.
     */
    virtual JsonListParse sensorTypesFromJson(const std::string &jsonStr, std::vector<SensorType> &sensorTypes,
                                              size_t &skipped)
    {
        skipped = 0;
        return sensorTypesFromJson(jsonStr, sensorTypes);
    }
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
    virtual bool sensorValuesFromUpdateJson(const std::string &json, std::vector<RemoteSensorValue> &out) = 0;
    virtual std::string remoteModuleCommandToJson(const std::string &command, const std::string &targetUid) = 0;
    virtual bool remoteModuleCommandFromJson(const std::string &json, std::string &outCommand, std::string &outTargetUid) = 0;
    virtual ~IJsonConverter() = default;
};

#endif // I_JSON_CONVERTER_H
