#ifndef I_NETWORK_STATUS_PROVIDER_H
#define I_NETWORK_STATUS_PROVIDER_H

class INetworkStatusProvider {
public:
    virtual ~INetworkStatusProvider() = default;
    virtual bool isNetworkOffline() = 0;
};

#endif
