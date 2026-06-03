#ifndef MOCK_UDP_MANAGER_H
#define MOCK_UDP_MANAGER_H

#include "Core/Domain/Interfaces/Udp/IUdpManager.h"
#include "MockUdpController.h"

class MockUdpManager : public IUdpManager {
public:
    MockUdpController controller;
    bool controllerAvailable = true;

    bool initialize(uint16_t, const char*) override { return true; }
    void shutdown() override {}

    IUdpController* getController() override {
        return controllerAvailable ? &controller : nullptr;
    }

    IUdpSender* getSender() override { return nullptr; }
    void setLocalPort(uint16_t) override {}
    uint16_t getLocalPort() const override { return 1234; }
    void setMulticastIP(const char*) override {}
    const char* getMulticastIP() const override { return "239.1.2.3"; }
    bool isInitialized() const override { return true; }
};

#endif // MOCK_UDP_MANAGER_H
