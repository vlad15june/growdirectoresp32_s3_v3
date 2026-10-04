#ifndef I_MQTT_LINK_MACHINE_H
#define I_MQTT_LINK_MACHINE_H

/**
 * @brief The MQTT connect state machine, seen from the task that drives it.
 *
 * The manager owns the states, the timeouts and the backoff of a reconnect; what it does not own
 * is a task. The network task beats the machine between the requests it makes, and while an act is
 * in progress - a stale socket being torn down, a CONNECT waiting for its CONNACK - it makes no
 * request at all. That is what keeps a TLS handshake and a broker socket from being built in the
 * same seconds: the heap then holds the larger of the two, not their sum.
 */
class IMqttLinkMachine {
public:
    virtual ~IMqttLinkMachine() = default;

    /**
     * @brief One beat of the reconnect state machine.
     *
     * Cheap when there is nothing to do. When a reconnect is due it starts one; when one is in
     * progress it advances it by whatever the link has answered since the last beat.
     *
     * @return true while an act is in progress - the caller beats again after a short pause and
     *         holds its own requests meanwhile; false when the machine is idle: connected, no
     *         link to connect over, or waiting out its backoff
     */
    virtual bool serviceLinkOnce() = 0;
};

#endif // I_MQTT_LINK_MACHINE_H
