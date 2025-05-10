#pragma once

#include "Application/DTOs/DiagnosticDto.h"

class IDiagnosticService {
public:
    virtual ~IDiagnosticService() = default;
    
    /**
     * @brief Performs diagnostics and returns the collected diagnostic data
     * @return DiagnosticDto containing the diagnostic information
     */
    virtual DiagnosticDto perform() = 0;
    
    /**
     * @brief Performs diagnostics and sends the data to the server
     * @return true if the diagnostic data was successfully sent, false otherwise
     */
    virtual bool performAndSend() = 0;
};
