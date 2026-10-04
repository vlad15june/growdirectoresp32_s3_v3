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

    // --- The split complex-command path ---------------------------------------------------------

    bool prepareComplexCommandCalled = false;
    bool executeBusWorkCalled = false;
    bool executeNetworkWorkCalled = false;
    bool finishComplexCommandCalled = false;

    /** What prepareComplexCommand() answers; the default routes nothing and answers immediately. */
    bool prepareReturnValue = false;
    ComplexCommandWork prepareWorkReturnValue;
    ModuleCommandExecutionResult prepareImmediateReturnValue{"", "Done"};
    ComplexCommandOutcome busWorkOutcome;
    ComplexCommandOutcome networkWorkOutcome;
    ModuleCommandExecutionResult finishResultReturnValue{"", "Done"};

    bool prepareComplexCommand(const ModuleCommandDto &command,
                               ComplexCommandWork &work,
                               ModuleCommandExecutionResult &immediate) override {
        prepareComplexCommandCalled = true;
        work = prepareWorkReturnValue;
        work.command = command;
        immediate = prepareImmediateReturnValue;
        immediate.uid = command.uid;
        return prepareReturnValue;
    }

    ComplexCommandOutcome executeBusWork(const ComplexCommandWork &work) override {
        executeBusWorkCalled = true;
        return busWorkOutcome;
    }

    ComplexCommandOutcome executeNetworkWork(const ComplexCommandWork &work) override {
        executeNetworkWorkCalled = true;
        return networkWorkOutcome;
    }

    bool finishComplexCommand(const ComplexCommandWork &work,
                              const ComplexCommandOutcome &outcome,
                              ModuleCommandExecutionResult &result,
                              ComplexCommandWork &nextWork) override {
        finishComplexCommandCalled = true;
        result = finishResultReturnValue;
        result.uid = work.command.uid;
        return true;
    }
};

#endif // MOCK_MODULE_COMMAND_EXECUTION_USE_CASE_H
