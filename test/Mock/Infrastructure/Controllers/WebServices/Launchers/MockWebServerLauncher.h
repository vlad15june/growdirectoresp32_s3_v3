#ifndef MOCK_WEB_SERVER_LAUNCHER_H
#define MOCK_WEB_SERVER_LAUNCHER_H

#include "Application/Interfaces/WebServices/IWebServerLauncher.h"

class MockWebServerLauncher : public IWebServerLauncher {
public:
    bool launchConfigurationServerCalled = false;
    bool launchTestServerCalled = false;
    bool configServerLaunchResult = true;
    bool testServerLaunchResult = true;
    
    void reset() {
        launchConfigurationServerCalled = false;
        launchTestServerCalled = false;
        configServerLaunchResult = true;
        testServerLaunchResult = true;
    }
    
    bool launchConfigurationServer() override {
        launchConfigurationServerCalled = true;
        return configServerLaunchResult;
    }
    
    bool launchTestServer() override {
        launchTestServerCalled = true;
        return testServerLaunchResult;
    }
};

#endif // MOCK_WEB_SERVER_LAUNCHER_H
