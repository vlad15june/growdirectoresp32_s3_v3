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
     * @brief Sends an already collected diagnostic snapshot to the server.
     *
     * Split out of performAndSend() so that the two halves can run in different tasks: perform()
     * is local bookkeeping that belongs on the control loop, send() is a blocking network call
     * that must not be.
     *
     * @return true if the snapshot was delivered
     */
    virtual bool send(const DiagnosticDto& diagnosticData) = 0;

    /**
     * @brief Performs diagnostics and sends the data to the server
     * @return true if the diagnostic data was successfully sent, false otherwise
     *
     * Note for implementations that hand the send to another task: the return value then means
     * "accepted for delivery", not "delivered". The only caller in the firmware
     * (LoopServiceConfigureDone) ignores it.
     */
    virtual bool performAndSend() = 0;
};
