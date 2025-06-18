#ifndef I_JSON_CONVERTER_H
#define I_JSON_CONVERTER_H

#include <string>
#include <vector>
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Application/DTOs/UpdateDto.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/DeviceCommandUpdateDto.h"
#include "Application/DTOs/DeviceCommandDto.h"
#include "Application/DTOs/RuleStateUpdateDto.h"

class IJsonConverter
{
public:
    /**
     * @brief Convert JSON string to ModuleStructure object
     * @param jsonStr JSON string to parse
     * @param moduleStructure ModuleStructure object to populate
     * @return true if parsing was successful, false otherwise
     */
    virtual bool moduleStructureFromJson(const std::string &jsonStr, ModuleStructure &moduleStructure) = 0;

    /**
     * @brief Convert ModuleStructure object to JSON string
     * @param moduleStructure ModuleStructure object to convert
     * @return JSON string representation of the ModuleStructure
     */
    virtual std::string moduleStructureToJson(const ModuleStructure &moduleStructure) = 0;

    /**
     * @brief Convert JSON string to Rule object
     * @param jsonStr JSON string to parse
     * @param rule Rule object to populate
     * @return true if parsing was successful, false otherwise
     */
    virtual bool ruleFromJson(const std::string &jsonStr, Rule &rule) = 0;

    /**
     * @brief Convert Rule object to JSON string
     * @param rule Rule object to convert
     * @return JSON string representation of the Rule
     */
    virtual std::string ruleToJson(const Rule &rule) = 0;

    /**
     * @brief Convert JSON string to array of Rule objects
     * @param jsonStr JSON string to parse
     * @param rules Vector of Rule objects to populate
     * @return true if parsing was successful, false otherwise
     */
    virtual bool rulesFromJson(const std::string &jsonStr, std::vector<Rule> &rules) = 0;

    /**
     * @brief Convert array of Rule objects to JSON string
     * @param rules Vector of Rule objects to convert
     * @return JSON string representation of the array of Rules
     */
    virtual std::string rulesToJson(const std::vector<Rule> &rules) = 0;

    /**
     * @brief Convert DiagnosticDto to JSON string
     * @param diagnosticData DiagnosticDto object to convert
     * @return JSON string representation of the diagnostic data
     */
    virtual std::string diagnosticToJson(const DiagnosticDto &diagnosticData) = 0;

    /**
     * @brief Convert UpdateDto to JSON string
     * @param updateData UpdateDto object to convert
     * @return JSON string representation of the update data
     */
    virtual std::string updateToJson(const UpdateDto &updateData) = 0;

    /**
     * @brief Convert JSON string to CommandDto object
     * @param jsonStr JSON string to parse
     * @param commandDto CommandDto object to populate
     * @return true if parsing was successful, false otherwise
     */
    virtual bool commandFromJson(const std::string &jsonStr, CommandDto &commandDto) = 0;

    /**
     * @brief Convert CommandDto object to JSON string
     * @param commandDto CommandDto object to convert
     * @return JSON string representation of the CommandDto
     */
    virtual std::string commandToJson(const CommandDto &commandDto) = 0;

    /**
     * @brief Convert DeviceCommandUpdateDto object to JSON string
     * @param commandUpdateDto DeviceCommandUpdateDto object to convert
     * @return JSON string representation of the DeviceCommandUpdateDto
     */
    virtual std::string deviceCommandUpdateToJson(const DeviceCommandUpdateDto& commandUpdateDto) = 0;

    /**
     * @brief Convert JSON string to DeviceCommandDto object
     * @param jsonStr JSON string to parse
     * @param commandDto DeviceCommandDto object to populate
     * @return true if parsing was successful, false otherwise
     */
    virtual bool deviceCommandFromJson(const std::string &jsonStr, DeviceCommandDto &commandDto) = 0;

    /**
     * @brief Convert DeviceCommandDto object to JSON string
     * @param commandDto DeviceCommandDto object to convert
     * @return JSON string representation of the DeviceCommandDto
     */
    virtual std::string deviceCommandToJson(const DeviceCommandDto &commandDto) = 0;

    /**
     * @brief Convert JSON string to array of DeviceCommandDto objects
     * @param jsonStr JSON string to parse
     * @param commands Vector of DeviceCommandDto objects to populate
     * @return true if parsing was successful, false otherwise
     */
    virtual bool deviceCommandsFromJson(const std::string &jsonStr, std::vector<DeviceCommandDto> &commands) = 0;

    /**
     * @brief Convert RuleStateDto object to JSON string
     * @param ruleState RuleStateDto object to convert
     * @return JSON string representation of the RuleStateDto
     */
    virtual std::string ruleStateToJson(const RuleStateDto& ruleState) = 0;

    /**
     * @brief Convert JSON string to RuleStateDto object
     * @param jsonStr JSON string to parse
     * @param ruleState RuleStateDto object to populate
     * @return true if parsing was successful, false otherwise
     */
    virtual bool ruleStateFromJson(const std::string& jsonStr, RuleStateDto& ruleState) = 0;

    /**
     * @brief Convert RuleStateUpdateDto object to JSON string
     * @param ruleStateUpdate RuleStateUpdateDto object to convert
     * @return JSON string representation of the RuleStateUpdateDto
     */
    virtual std::string ruleStateUpdateToJson(const RuleStateUpdateDto& ruleStateUpdate) = 0;

    /**
     * @brief Convert JSON string to RuleStateUpdateDto object
     * @param jsonStr JSON string to parse
     * @param ruleStateUpdate RuleStateUpdateDto object to populate
     * @return true if parsing was successful, false otherwise
     */
    virtual bool ruleStateUpdateFromJson(const std::string& jsonStr, RuleStateUpdateDto& ruleStateUpdate) = 0;

    virtual ~IJsonConverter() = default;
};

#endif // I_JSON_CONVERTER_H
