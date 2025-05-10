#ifndef MOCK_DIAGNOSTIC_SERVICE_H
#define MOCK_DIAGNOSTIC_SERVICE_H

#include "Application/Interfaces/IDiagnosticService.h"

class MockDiagnosticService : public IDiagnosticService {
public:
    bool performCalled = false;
    bool performAndSendCalled = false;
    DiagnosticDto lastDiagnosticData;
    bool performAndSendReturnValue = true;

    DiagnosticDto perform() override {
        performCalled = true;
        DiagnosticDto result;
        result.firmware = "mock-firmware";
        result.uid = "mock-uid";
        lastDiagnosticData = result;
        return result;
    }

    bool performAndSend() override {
        performAndSendCalled = true;
        DiagnosticDto result = perform();
        return performAndSendReturnValue;
    }

    void reset() {
        performCalled = false;
        performAndSendCalled = false;
        performAndSendReturnValue = true;
        // Reset the diagnostic data
        lastDiagnosticData = DiagnosticDto();
    }
};

#endif // MOCK_DIAGNOSTIC_SERVICE_H
