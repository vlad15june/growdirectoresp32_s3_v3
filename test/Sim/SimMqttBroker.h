#pragma once

// An MQTT 3.1.1 broker, as far as one client's delivery guarantee is concerned.
//
// This is the outside world, not a second copy of anything the firmware does: the module's own
// code makes the decisions under test (which QoS to subscribe with, whether to ask for a clean
// session, what client id to present) and this file answers the way the specification says a
// broker must answer them.
//
// What is modelled, and why each part is needed to tell a delivered message from a lost one:
//
//   * Sessions, keyed by client id (3.1.2.4). clean_session=1 throws the stored session away at
//     CONNECT and keeps nothing after the client leaves; clean_session=0 keeps the subscriptions
//     and the undelivered messages and answers the next CONNECT with Session Present = 1.
//   * Effective QoS = min(publisher QoS, subscription QoS) (3.8.4). This is the whole point of
//     the exercise: a QoS 0 subscription reduces a QoS 2 publication to fire-and-forget.
//   * Offline queueing (3.1.2.4, 4.1): a message for an offline client is kept only if its
//     session survives AND the effective QoS is at least 1. Everything else is dropped, and the
//     drop is counted rather than silent.
//   * Retained messages (3.3.1.3): the last retained publication per topic is stored and handed
//     to every new subscription that matches, with the retain flag set. A retained publication
//     with an empty payload clears the slot.
//   * PUBACK (4.3.2): a QoS 1 delivery stays in flight until the client acknowledges it. What is
//     still in flight when the connection goes away is redelivered - with DUP set - as soon as a
//     persistent session comes back. That is where the duplicate a QoS 1 subscriber must tolerate
//     comes from.
//   * One connection per client id (3.1.4): a second CONNECT with the same id takes the session
//     over and the first client is disconnected.
//
// What is NOT modelled: the wire (see the note in Mock/native/AsyncMqttClient.h), QoS 2 delivery
// to the subscriber (the module never subscribes at QoS 2, and the leg under test is capped at
// the subscription's QoS anyway), message ordering across topics, and wildcards in a publish
// topic. The queue depth and the lifetime of a session on the production broker are settings
// nothing in the protocol reveals: the cap here is a parameter of the simulation, not a
// measurement of that broker.

#include <AsyncMqttClient.h>

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace hostsim {

class SimMqttBroker : public AsyncMqttClientBrokerEndpoint
{
public:
    struct Message
    {
        std::string topic;
        std::string payload;
        uint8_t qos = 0;
        bool retain = false;
        bool dup = false;
        uint16_t packetId = 0;
    };

    /**
     * @brief How many messages one offline session may hold.
     *
     * A broker setting, not a protocol rule. Refusals are counted, so a test can say what happens
     * when the queue is full instead of assuming it never is.
     */
    size_t maxQueuedPerSession = 16;

    /**
     * @brief Lose the next delivery on the way to the client.
     *
     * The broker sent it and counts it as outstanding; the client never saw it. What happens next
     * is the whole difference between the two QoS levels, so it is a fault the bench can ask for.
     */
    bool loseNextDelivery = false;

    // --- counters a test reads ------------------------------------------------------------------

    uint32_t deliveredCount = 0;
    uint32_t queuedCount = 0;
    uint32_t droppedNoSessionCount = 0;
    uint32_t droppedQos0OfflineCount = 0;
    uint32_t droppedQueueFullCount = 0;
    uint32_t redeliveredCount = 0;
    uint32_t pubackCount = 0;
    uint32_t takenOverCount = 0;
    uint32_t lostInTransitCount = 0;

    // --- the server side ------------------------------------------------------------------------

    /**
     * @brief Publish as the backend publishes.
     *
     * The defaults are the backend's: MqttService.PublishMessageAsync() builds every push with
     * WithExactlyOnceQoS() and retain defaulting to true.
     */
    void publishFromServer(const std::string &topic, const std::string &payload, uint8_t qos = 2,
                           bool retain = true)
    {
        if (retain)
        {
            if (payload.empty())
            {
                _retained.erase(topic);
            }
            else
            {
                Message kept;
                kept.topic = topic;
                kept.payload = payload;
                kept.qos = qos;
                kept.retain = true;
                _retained[topic] = kept;
            }
        }

        for (auto &entry : _sessions)
        {
            Session &session = entry.second;
            uint8_t subscriptionQos = 0;
            if (!matchingSubscriptionQos(session, topic, subscriptionQos))
            {
                continue;
            }

            Message message;
            message.topic = topic;
            message.payload = payload;
            message.qos = effectiveQos(qos, subscriptionQos);
            message.retain = false; // set only on a retained message sent because of a subscription
            deliverOrKeep(session, message);
        }
    }

    /**
     * @brief Publish as another module publishes.
     *
     * The module's own publisher is QoS 0 and never retains (MqttManager::publish), so a peer's
     * sensor update reaches this broker with no guarantee to pass on - whatever the subscriber
     * asked for.
     */
    void publishFromPeerModule(const std::string &topic, const std::string &payload)
    {
        publishFromServer(topic, payload, 0, false);
    }

    /** Whether a topic currently has a retained message. */
    bool hasRetained(const std::string &topic) const { return _retained.find(topic) != _retained.end(); }

    /** How many messages are waiting for an offline session. */
    size_t queuedFor(const std::string &clientId) const
    {
        auto it = _sessions.find(clientId);
        return (it == _sessions.end()) ? 0 : it->second.queue.size();
    }

    /** How many deliveries to this session are still waiting for their PUBACK. */
    size_t inFlightFor(const std::string &clientId) const
    {
        auto it = _sessions.find(clientId);
        return (it == _sessions.end()) ? 0 : it->second.inflight.size();
    }

    /** Whether the broker kept a session under this client id. */
    bool hasSession(const std::string &clientId) const { return _sessions.find(clientId) != _sessions.end(); }

    /** The QoS this session's subscription to an exact filter was granted, -1 when there is none. */
    int subscriptionQosOf(const std::string &clientId, const std::string &filter) const
    {
        auto session = _sessions.find(clientId);
        if (session == _sessions.end())
        {
            return -1;
        }
        auto subscription = session->second.subscriptions.find(filter);
        return (subscription == session->second.subscriptions.end())
                   ? -1
                   : static_cast<int>(subscription->second);
    }

    // --- the client side (AsyncMqttClientBrokerEndpoint) ------------------------------------------

    bool onClientConnect(AsyncMqttClient *client, const std::string &clientId, bool cleanSession) override
    {
        // A second connection under a live client id takes the session over.
        auto existing = _sessions.find(clientId);
        if (existing != _sessions.end() && existing->second.online != nullptr &&
            existing->second.online != client)
        {
            takenOverCount++;
            existing->second.online->dropConnection();
            existing->second.online = nullptr;
        }

        bool sessionPresent = false;
        if (cleanSession)
        {
            _sessions.erase(clientId);
        }
        else
        {
            sessionPresent = (_sessions.find(clientId) != _sessions.end());
        }

        Session &session = _sessions[clientId];
        session.clientId = clientId;
        session.persistent = !cleanSession;
        session.online = client;
        _clientIds[client] = clientId;

        flushQueue(session);
        return sessionPresent;
    }

    void onClientDisconnect(AsyncMqttClient *client) override
    {
        Session *session = sessionOf(client);
        _clientIds.erase(client);
        if (session == nullptr)
        {
            return;
        }

        session->online = nullptr;

        // What was delivered but not acknowledged is owed to the client again (4.4).
        for (const Message &message : session->inflight)
        {
            Message again = message;
            again.dup = true;
            session->queue.push_back(again);
        }
        session->inflight.clear();

        if (!session->persistent)
        {
            _sessions.erase(session->clientId);
        }
    }

    void onClientSubscribe(AsyncMqttClient *client, const std::string &topic, uint8_t qos) override
    {
        Session *session = sessionOf(client);
        if (session == nullptr)
        {
            return;
        }

        session->subscriptions[topic] = qos;

        // A new subscription is served the retained message of every topic it matches (3.3.1.3).
        std::vector<Message> retained;
        for (const auto &entry : _retained)
        {
            if (!topicMatches(topic, entry.first))
            {
                continue;
            }
            Message message = entry.second;
            message.qos = effectiveQos(message.qos, qos);
            message.retain = true;
            retained.push_back(message);
        }

        for (Message &message : retained)
        {
            deliverOrKeep(*session, message);
        }
    }

    void onClientUnsubscribe(AsyncMqttClient *client, const std::string &topic) override
    {
        Session *session = sessionOf(client);
        if (session != nullptr)
        {
            session->subscriptions.erase(topic);
        }
    }

    void onClientPublish(AsyncMqttClient *client, const std::string &topic, const std::string &payload,
                         uint8_t qos, bool retain) override
    {
        (void)client;
        Message message;
        message.topic = topic;
        message.payload = payload;
        message.qos = qos;
        message.retain = retain;
        fromClients.push_back(message);

        if (retain)
        {
            _retained[topic] = message;
        }
    }

    void onClientPuback(AsyncMqttClient *client, uint16_t packetId) override
    {
        Session *session = sessionOf(client);
        if (session == nullptr)
        {
            return;
        }

        for (auto it = session->inflight.begin(); it != session->inflight.end(); ++it)
        {
            if (it->packetId == packetId)
            {
                session->inflight.erase(it);
                pubackCount++;
                return;
            }
        }
    }

    /** Everything the module published, in order. */
    std::vector<Message> fromClients;

private:
    struct Session
    {
        std::string clientId;
        bool persistent = false;
        AsyncMqttClient *online = nullptr;
        std::map<std::string, uint8_t> subscriptions;
        std::deque<Message> queue;
        std::vector<Message> inflight;
    };

    static uint8_t effectiveQos(uint8_t publishQos, uint8_t subscriptionQos)
    {
        return (publishQos < subscriptionQos) ? publishQos : subscriptionQos;
    }

    Session *sessionOf(AsyncMqttClient *client)
    {
        auto id = _clientIds.find(client);
        if (id == _clientIds.end())
        {
            return nullptr;
        }
        auto session = _sessions.find(id->second);
        return (session == _sessions.end()) ? nullptr : &session->second;
    }

    static bool matchingSubscriptionQos(const Session &session, const std::string &topic, uint8_t &qos)
    {
        bool found = false;
        uint8_t best = 0;
        for (const auto &subscription : session.subscriptions)
        {
            if (!topicMatches(subscription.first, topic))
            {
                continue;
            }
            // Overlapping filters: the specification lets a broker send one copy, and the sensible
            // copy is the one with the guarantee the client asked for most strongly (3.8.4).
            if (!found || subscription.second > best)
            {
                best = subscription.second;
                found = true;
            }
        }
        qos = best;
        return found;
    }

    /** MQTT topic filter matching: levels split on '/', '+' one level, '#' the rest. */
    static bool topicMatches(const std::string &filter, const std::string &topic)
    {
        std::vector<std::string> filterLevels = split(filter);
        std::vector<std::string> topicLevels = split(topic);

        size_t index = 0;
        for (; index < filterLevels.size(); index++)
        {
            if (filterLevels[index] == "#")
            {
                return true;
            }
            if (index >= topicLevels.size())
            {
                return false;
            }
            if (filterLevels[index] == "+")
            {
                continue;
            }
            if (filterLevels[index] != topicLevels[index])
            {
                return false;
            }
        }
        return index == topicLevels.size();
    }

    static std::vector<std::string> split(const std::string &text)
    {
        std::vector<std::string> parts;
        std::string current;
        for (char character : text)
        {
            if (character == '/')
            {
                parts.push_back(current);
                current.clear();
            }
            else
            {
                current.push_back(character);
            }
        }
        parts.push_back(current);
        return parts;
    }

    void deliverOrKeep(Session &session, Message message)
    {
        if (session.online != nullptr)
        {
            deliver(session, message);
            return;
        }

        if (message.qos == 0)
        {
            // Fire and forget: there is no one to forget it to.
            droppedQos0OfflineCount++;
            return;
        }

        if (!session.persistent)
        {
            droppedNoSessionCount++;
            return;
        }

        if (session.queue.size() >= maxQueuedPerSession)
        {
            droppedQueueFullCount++;
            return;
        }

        session.queue.push_back(message);
        queuedCount++;
    }

    void deliver(Session &session, Message message)
    {
        message.packetId = nextPacketId();
        if (message.qos >= 1)
        {
            session.inflight.push_back(message);
        }

        if (loseNextDelivery)
        {
            // Sent and outstanding as far as the broker is concerned; never seen by the client.
            loseNextDelivery = false;
            lostInTransitCount++;
            return;
        }

        deliveredCount++;
        if (message.dup)
        {
            redeliveredCount++;
        }

        AsyncMqttClient *client = session.online;
        client->deliverMessage(message.topic, message.payload, message.qos, message.retain,
                               message.dup, message.packetId);
    }

    void flushQueue(Session &session)
    {
        while (!session.queue.empty() && session.online != nullptr)
        {
            Message message = session.queue.front();
            session.queue.pop_front();
            deliver(session, message);
        }
    }

    uint16_t nextPacketId()
    {
        if (++_nextPacketId == 0)
        {
            ++_nextPacketId;
        }
        return _nextPacketId;
    }

    std::map<std::string, Session> _sessions;
    std::map<AsyncMqttClient *, std::string> _clientIds;
    std::map<std::string, Message> _retained;
    uint16_t _nextPacketId = 0;
};

} // namespace hostsim
