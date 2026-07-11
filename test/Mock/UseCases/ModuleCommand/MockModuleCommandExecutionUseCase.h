#ifndef MOCK_MODULE_COMMAND_EXECUTION_USE_CASE_H
#define MOCK_MODULE_COMMAND_EXECUTION_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include <vector>
#include <string>

class MockModuleCommandExecutionUseCase : public IModuleCommandExecutionUseCase {
public:
    bool executeCalled = false;
    bool executeCommandsCalled = false;
    bool executeRawCalled = false;

    std::vector<ModuleCommandDto> lastExecutedCommands;
    std::vector<ModuleCommandExecutionResult> executeCommandsReturnValue;
    bool executeReturnValue = true;
    RawBusCommandResult executeRawReturnValue;

    void reset() {
        executeCalled = false;
        executeCommandsCalled = false;
        executeRawCalled = false;
        lastExecutedCommands.clear();
        executeCommandsReturnValue.clear();
        executeReturnValue = true;
    }

    bool execute(const std::string &command, const std::string &options, const std::string &commandUid, const std::string &createdAt) override {
        executeCalled = true;
        return executeReturnValue;
    }

    std::vector<ModuleCommandExecutionResult> executeCommands(const std::vector<ModuleCommandDto>& moduleCommands) override {
        executeCommandsCalled = true;
        lastExecutedCommands = moduleCommands;
        return executeCommandsReturnValue;
    }

    RawBusCommandResult executeRaw(const std::string &command, const std::string &options) override {
        executeRawCalled = true;
        return executeRawReturnValue;
    }
};

#endif // MOCK_MODULE_COMMAND_EXECUTION_USE_CASE_H
