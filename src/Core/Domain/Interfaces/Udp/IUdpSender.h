#ifndef I_UDP_SENDER_H
#define I_UDP_SENDER_H

class IUdpSender {
public:
    virtual ~IUdpSender() = default;
    
    // Send methods
    virtual bool sendBroadcast(const char* data, uint16_t port) = 0;
    virtual bool sendBroadcast(const char* data, size_t length, uint16_t port) = 0;
    virtual bool sendUnicast(const char* data, const char* targetIP, uint16_t port) = 0;
    virtual bool sendUnicast(const char* data, size_t length, const char* targetIP, uint16_t port) = 0;
    virtual bool sendMulticast(const char* data, const char* multicastIP, uint16_t port) = 0;
    virtual bool sendMulticast(const char* data, size_t length, const char* multicastIP, uint16_t port) = 0;
    
    // Status
    virtual bool isInitialized() const = 0;
};

#endif // I_UDP_SENDER_H