#ifndef MOCK_NETWORK_STATUS_PROVIDER_H
#define MOCK_NETWORK_STATUS_PROVIDER_H

#include "Core/Domain/Interfaces/INetworkStatusProvider.h"

class MockNetworkStatusProvider : public INetworkStatusProvider {
public:
    bool offline = false;

    bool isNetworkOffline() override {
        return offline;
    }
};

#endif
