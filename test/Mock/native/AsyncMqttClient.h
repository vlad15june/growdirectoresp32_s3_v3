#pragma once

// AsyncMqttClient stand-in for the native (host) test environment.
//
// It is the host flavour of the device library (marvinroger/AsyncMqttClient 0.9.0), and it exists
// for two production classes:
//
//   * MqttLogger, which needs connected() and publish(). The stand-in records every publish -
//     topic and payload - so a test can assert both that a message went out on the right topic
//     and that a path published nothing.
//
//   * MqttManager, which needs the whole client: the connect options, the callbacks, subscribe
//     and unsubscribe. On its own that would only record what the manager asked for, so the
//     stand-in also has a far end: attach a broker (Sim/SimMqttBroker.h) and the options stop
//     being records and start deciding what comes back - a subscription's QoS and the session
//     flag are what the broker files the session under, and a message the broker delivers reaches
//     the manager through the same onMessage callback the library calls.
//
// What is a model of the library, and where the library's own code says so:
//
//   * subscribe(topic, qos) puts the QoS byte in the SUBSCRIBE packet
//     (Packets/Out/Subscribe.cpp: `_data.push_back(qosByte[0])`), so the broker is told the QoS
//     the caller passed; a subscribe while disconnected sends nothing and returns 0
//     (AsyncMqttClient.cpp: `if (_state != CONNECTED) return 0;`).
//   * setCleanSession() and setClientId() are carried in CONNECT
//     (Packets/Out/Connect.cpp: the CLEAN_SESSION flag bit and the client id field).
//   * an inbound PUBLISH at QoS 1 is acknowledged by the library itself, after the message
//     callbacks have run (AsyncMqttClient.cpp `_onPublish()`: `if (qos == 1) { ... PubAckOutPacket
//     ... }`). The firmware does nothing for this to happen and cannot stop it, which is why the
//     ack below is the stand-in's and not the manager's.
//   * publish() returns 0 when the client is not connected and a non-zero packet id otherwise -
//     1 for QoS 0 (Packets/Out/Publish.cpp: `_packetId = (qos != 0) ? _getNextPacketId() : 1;`).
//
// What is NOT modelled: the wire itself. Packets are not encoded here, so nothing in this file
// proves the library's framing - the citations above are the reason to believe the parameters
// reach the wire, and a test against a real broker is what would prove it. Fragmented delivery
// (index/total) is not modelled either: the broker hands a message over in one piece.
//
// Reachable only through the include path of env:native (see platformio.ini); on the device the
// real library from lib_deps is used.

#include <Arduino.h>
#include <IPAddress.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The library's own enumeration, value for value.
enum class AsyncMqttClientDisconnectReason : uint8_t
{
    TCP_DISCONNECTED = 0,

    MQTT_UNACCEPTABLE_PROTOCOL_VERSION = 1,
    MQTT_IDENTIFIER_REJECTED = 2,
    MQTT_SERVER_UNAVAILABLE = 3,
    MQTT_MALFORMED_CREDENTIALS = 4,
    MQTT_NOT_AUTHORIZED = 5,

    ESP8266_NOT_ENOUGH_SPACE = 6,

    TLS_BAD_FINGERPRINT = 7
};

struct AsyncMqttClientMessageProperties
{
    uint8_t qos;
    bool dup;
    bool retain;
};

class AsyncMqttClient;

/**
 * @brief The far end of the socket, as the client sees it.
 *
 * Implemented by the broker simulation; declared here so that the stand-in does not have to know
 * what a broker is. Every method is a packet the client would have sent.
 */
class AsyncMqttClientBrokerEndpoint
{
public:
    virtual ~AsyncMqttClientBrokerEndpoint() = default;

    /** CONNECT. @return the Session Present flag of the CONNACK. */
    virtual bool onClientConnect(AsyncMqttClient *client, const std::string &clientId, bool cleanSession) = 0;
    /** DISCONNECT, or a dropped socket. */
    virtual void onClientDisconnect(AsyncMqttClient *client) = 0;
    /** SUBSCRIBE, one topic filter at the given QoS. */
    virtual void onClientSubscribe(AsyncMqttClient *client, const std::string &topic, uint8_t qos) = 0;
    /** UNSUBSCRIBE. */
    virtual void onClientUnsubscribe(AsyncMqttClient *client, const std::string &topic) = 0;
    /** PUBLISH from the client. */
    virtual void onClientPublish(AsyncMqttClient *client, const std::string &topic, const std::string &payload,
                                 uint8_t qos, bool retain) = 0;
    /** PUBACK for a message the broker delivered at QoS 1. */
    virtual void onClientPuback(AsyncMqttClient *client, uint16_t packetId) = 0;
};

class AsyncMqttClient
{
public:
    struct PublishedMessage
    {
        std::string topic;
        std::string payload;
    };

    bool connectedFlag = false;
    std::vector<PublishedMessage> published;

    // --- connect options, as the manager set them --------------------------------------------

    const char *clientId() const { return _clientId.c_str(); }
    bool cleanSession() const { return _cleanSession; }
    uint16_t keepAlive() const { return _keepAlive; }
    IPAddress serverIp() const { return _serverIp; }
    uint16_t serverPort() const { return _serverPort; }

    // --- the library's API -------------------------------------------------------------------

    AsyncMqttClient &setKeepAlive(uint16_t keepAlive)
    {
        _keepAlive = keepAlive;
        return *this;
    }

    AsyncMqttClient &setClientId(const char *clientId)
    {
        // The library keeps the pointer (`_clientId = clientId;`) and reads it at every CONNECT.
        // The copy here is deliberate: a stand-in that also kept the pointer would turn a caller's
        // dangling string into a crash in the test binary instead of a readable failure.
        _clientId = (clientId != nullptr) ? clientId : "";
        return *this;
    }

    AsyncMqttClient &setCleanSession(bool cleanSession)
    {
        _cleanSession = cleanSession;
        return *this;
    }

    AsyncMqttClient &setCredentials(const char *username, const char *password = nullptr)
    {
        _username = (username != nullptr) ? username : "";
        _password = (password != nullptr) ? password : "";
        return *this;
    }

    AsyncMqttClient &setServer(IPAddress ip, uint16_t port)
    {
        _serverIp = ip;
        _serverPort = port;
        return *this;
    }

    AsyncMqttClient &onConnect(std::function<void(bool sessionPresent)> callback)
    {
        _onConnect = callback;
        return *this;
    }

    AsyncMqttClient &onDisconnect(std::function<void(AsyncMqttClientDisconnectReason reason)> callback)
    {
        _onDisconnect = callback;
        return *this;
    }

    AsyncMqttClient &onMessage(std::function<void(char *topic, char *payload,
                                                  AsyncMqttClientMessageProperties properties,
                                                  size_t len, size_t index, size_t total)>
                                   callback)
    {
        _onMessage = callback;
        return *this;
    }

    bool connected() const { return connectedFlag; }

    void connect()
    {
        connectAttempts++;
        if (_broker == nullptr)
        {
            return;
        }

        lastSessionPresent = _broker->onClientConnect(this, _clientId, _cleanSession);
        connectedFlag = true;
        if (_onConnect)
        {
            _onConnect(lastSessionPresent);
        }
    }

    void disconnect(bool /*force*/ = false)
    {
        disconnectAttempts++;
        const bool wasConnected = connectedFlag;
        connectedFlag = false;
        if (_broker != nullptr)
        {
            _broker->onClientDisconnect(this);
        }
        if (wasConnected && _onDisconnect)
        {
            _onDisconnect(AsyncMqttClientDisconnectReason::TCP_DISCONNECTED);
        }
    }

    uint16_t subscribe(const char *topic, uint8_t qos)
    {
        if (!connectedFlag)
        {
            return 0;
        }

        subscribeCalls.push_back({topic ? topic : "", qos});
        if (_broker != nullptr)
        {
            _broker->onClientSubscribe(this, topic ? topic : "", qos);
        }
        return nextPacketId();
    }

    uint16_t unsubscribe(const char *topic)
    {
        if (!connectedFlag)
        {
            return 0;
        }

        if (_broker != nullptr)
        {
            _broker->onClientUnsubscribe(this, topic ? topic : "");
        }
        return nextPacketId();
    }

    uint16_t publish(const char *topic, uint8_t qos, bool retain,
                     const char *payload = nullptr, size_t /*length*/ = 0,
                     bool /*dup*/ = false, uint16_t /*message_id*/ = 0)
    {
        published.push_back({topic ? topic : "", payload ? payload : ""});
        if (!connectedFlag)
        {
            return 0;
        }
        if (_broker != nullptr)
        {
            _broker->onClientPublish(this, topic ? topic : "", payload ? payload : "", qos, retain);
        }
        return (qos == 0) ? 1 : nextPacketId();
    }

    // --- the far end -------------------------------------------------------------------------

    /** Put a broker behind this client. Without one the options are only recorded. */
    void attachBroker(AsyncMqttClientBrokerEndpoint *broker) { _broker = broker; }

    /**
     * @brief Hand an inbound message to the application, as the library's parser does.
     *
     * The payload is passed the way the library passes it - a pointer into a buffer, with the
     * length beside it and no terminator - so that a reader which forgets `len` reads rubbish
     * here too rather than passing by luck. At QoS 1 the PUBACK goes back afterwards, which is
     * where the library sends it: after the message callbacks, from `_onPublish()`.
     */
    void deliverMessage(const std::string &topic, const std::string &payload, uint8_t qos,
                        bool retain, bool dup, uint16_t packetId)
    {
        if (_onMessage)
        {
            std::vector<char> topicBuffer(topic.begin(), topic.end());
            topicBuffer.push_back('\0');
            std::vector<char> payloadBuffer(payload.begin(), payload.end());

            AsyncMqttClientMessageProperties properties;
            properties.qos = qos;
            properties.dup = dup;
            properties.retain = retain;

            _onMessage(topicBuffer.data(), payloadBuffer.empty() ? nullptr : payloadBuffer.data(),
                       properties, payload.size(), 0, payload.size());
        }

        if (qos == 1 && !suppressAcks && _broker != nullptr)
        {
            _broker->onClientPuback(this, packetId);
        }
    }

    /** The socket goes away without a DISCONNECT: what a lost link looks like to the client. */
    void dropConnection()
    {
        const bool wasConnected = connectedFlag;
        connectedFlag = false;
        if (wasConnected && _onDisconnect)
        {
            _onDisconnect(AsyncMqttClientDisconnectReason::TCP_DISCONNECTED);
        }
    }

    // --- what a test reads --------------------------------------------------------------------

    struct SubscribeCall
    {
        std::string topic;
        uint8_t qos;
    };

    std::vector<SubscribeCall> subscribeCalls;
    int connectAttempts = 0;
    int disconnectAttempts = 0;

    /** The Session Present flag of the last CONNACK: whether the broker knew this client already. */
    bool lastSessionPresent = false;

    /**
     * @brief Swallow the PUBACK the library would send back.
     *
     * A fault of the link, not of the firmware: the message arrived and was handled, the
     * acknowledgement did not make it back. The broker then owes the message again - which is how
     * an at-least-once subscriber meets a duplicate.
     */
    bool suppressAcks = false;

private:
    uint16_t nextPacketId()
    {
        // The library's counter: one sequence for every outgoing packet, never 0.
        if (++_nextPacketId == 0)
        {
            ++_nextPacketId;
        }
        return _nextPacketId;
    }

    AsyncMqttClientBrokerEndpoint *_broker = nullptr;
    std::function<void(bool)> _onConnect;
    std::function<void(AsyncMqttClientDisconnectReason)> _onDisconnect;
    std::function<void(char *, char *, AsyncMqttClientMessageProperties, size_t, size_t, size_t)> _onMessage;

    std::string _clientId;
    std::string _username;
    std::string _password;
    IPAddress _serverIp;
    uint16_t _serverPort = 0;
    uint16_t _keepAlive = 0;
    bool _cleanSession = true;
    uint16_t _nextPacketId = 0;
};
