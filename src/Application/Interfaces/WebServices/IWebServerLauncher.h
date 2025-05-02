#ifndef I_WEB_SERVER_LAUNCHER_H
#define I_WEB_SERVER_LAUNCHER_H

class IWebServerLauncher {
public:
    virtual ~IWebServerLauncher() = default;
    virtual bool launchConfigurationServer() = 0;
    virtual bool launchTestServer() = 0;
};

#endif // I_WEB_SERVER_LAUNCHER_H
