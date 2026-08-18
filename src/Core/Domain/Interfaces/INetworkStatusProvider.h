#ifndef I_NETWORK_STATUS_PROVIDER_H
#define I_NETWORK_STATUS_PROVIDER_H

class INetworkStatusProvider {
public:
    virtual ~INetworkStatusProvider() = default;
    virtual bool isNetworkOffline() = 0;

    /**
     * @brief The httpDown flag the module already publishes in OFFLINE_INDICATION and apiStatus
     */
    virtual bool isHttpConnectionDown() = 0;
};

#endif
