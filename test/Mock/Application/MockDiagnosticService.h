#ifndef MOCK_DIAGNOSTIC_SERVICE_H
#define MOCK_DIAGNOSTIC_SERVICE_H

#include "Application/Interfaces/IDiagnosticService.h"

class MockDiagnosticService : public IDiagnosticService {
public:
    bool performCalled = false;
    bool performAndSendCalled = false;
    bool sendCalled = false;
    int sendCount = 0;
    DiagnosticDto lastDiagnosticData;
    DiagnosticDto lastSentData;
    bool performAndSendReturnValue = true;

    DiagnosticDto perform() override {
        performCalled = true;
        DiagnosticDto result;
        result.firmware = "mock-firmware";
        result.uid = "mock-uid";
        lastDiagnosticData = result;
        return result;
    }

    bool send(const DiagnosticDto& diagnosticData) override {
        sendCalled = true;
        sendCount++;
        lastSentData = diagnosticData;
        return performAndSendReturnValue;
    }

    bool performAndSend() override {
        performAndSendCalled = true;
        DiagnosticDto result = perform();
        return send(result);
    }

    void reset() {
        performCalled = false;
        performAndSendCalled = false;
        sendCalled = false;
        sendCount = 0;
        lastSentData = DiagnosticDto();
        performAndSendReturnValue = true;
        // Reset the diagnostic data
        lastDiagnosticData = DiagnosticDto();
    }
};

#endif // MOCK_DIAGNOSTIC_SERVICE_H
