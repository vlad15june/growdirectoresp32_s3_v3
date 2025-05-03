#ifndef I_WEB_SERVER_LOOPER_H
#define I_WEB_SERVER_LOOPER_H

class IWebServerLooper {
public:
    virtual ~IWebServerLooper() = default;

    virtual void loop() = 0;
};

#endif // I_WEB_SERVER_LOOPER_H
