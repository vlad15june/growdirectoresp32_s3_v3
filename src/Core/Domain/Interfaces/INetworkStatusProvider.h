#ifndef I_NETWORK_STATUS_PROVIDER_H
#define I_NETWORK_STATUS_PROVIDER_H

class INetworkStatusProvider {
public:
    virtual ~INetworkStatusProvider() = default;
    virtual bool isNetworkOffline() = 0;

    /**
     * @brief The `httpDown` flag the module already publishes in its diagnostics
     *
     * The same value that goes out in the OFFLINE_INDICATION line and as `apiStatus`, so anything
     * that has to decide whether the link is usable reads what the module reports instead of
     * counting failures of its own.
     */
    virtual bool isHttpConnectionDown() = 0;
};

#endif
