#ifndef I_BOARD_TEST_H
#define I_BOARD_TEST_H

class IBoardTest {
public:
    virtual ~IBoardTest() = default;
    
    // Main test loop processing
    virtual void processTestCommands() = 0;
    
    // Setup devices for testing
    virtual void setupDevicesForTest(IModuleConfig* moduleConfig) = 0;
    
    // Check if test commands are available
    virtual bool hasTestCommandsAvailable() = 0;
};

#endif // I_BOARD_TEST_H
