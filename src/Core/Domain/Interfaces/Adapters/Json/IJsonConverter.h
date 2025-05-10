#ifndef I_JSON_CONVERTER_H
#define I_JSON_CONVERTER_H

#include <string>
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Application/DTOs/DiagnosticDto.h"

class IJsonConverter {
public:
    /**
     * @brief Convert JSON string to ModuleStructure object
     * @param jsonStr JSON string to parse
     * @param moduleStructure ModuleStructure object to populate
     * @return true if parsing was successful, false otherwise
     */
    virtual bool moduleStructureFromJson(const std::string& jsonStr, ModuleStructure& moduleStructure) = 0;
    
    /**
     * @brief Convert ModuleStructure object to JSON string
     * @param moduleStructure ModuleStructure object to convert
     * @return JSON string representation of the ModuleStructure
     */
    virtual std::string moduleStructureToJson(const ModuleStructure& moduleStructure) = 0;
    
    /**
     * @brief Convert DiagnosticDto to JSON string
     * @param diagnosticData DiagnosticDto object to convert
     * @return JSON string representation of the diagnostic data
     */
    virtual std::string diagnosticToJson(const DiagnosticDto& diagnosticData) = 0;
    
    virtual ~IJsonConverter() = default;
};

#endif // I_MODULE_STRUCTURE_JSON_H
