#ifndef MOCK_WEB_SERVER_LOOPER_H
#define MOCK_WEB_SERVER_LOOPER_H

#include "Application/Interfaces/WebServices/IWebServerLooper.h"

class MockWebServerLooper : public IWebServerLooper {
public:
    bool loopCalled = false;
    int loopCallCount = 0;
    
    void loop() override {
        loopCalled = true;
        loopCallCount++;
    }
    
    void reset() {
        loopCalled = false;
        loopCallCount = 0;
    }
};

#endif // MOCK_WEB_SERVER_LOOPER_H
