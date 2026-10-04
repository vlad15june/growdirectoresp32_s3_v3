#ifndef WIFI_LINK_LOG_USE_CASE_H
#define WIFI_LINK_LOG_USE_CASE_H

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

/**
 * @brief What the station link says about itself, one line per change of state.
 *
 * A module that stops talking to the backend has one of two stories behind it, and the log has to
 * tell them apart: the link went down, or the link is up and something above it is broken. The
 * poll the diagnostics run answers the first question every few seconds and drives the reconnect,
 * but a poll only ever sees "connected" or "not connected" - the reason the radio gives for a drop
 * exists for the length of one system event and is gone. That reason is the single most useful
 * thing in a field log about a module that keeps falling off: 201 is a network that is not there,
 * 15 is a wrong password, 8 is an access point that showed the module the door, 200 is a beacon
 * that stopped arriving.
 *
 * So the changes are taken from the radio's own events and said here, and everything about them is
 * arranged around two facts of how those events arrive.
 *
 * ### The radio repeats itself
 *
 * A station that cannot associate says so every couple of seconds for as long as it keeps trying,
 * and the arriving events are all the same event. Only a change of state is a line: the second
 * disconnect in a row is not news, and neither is the address being renewed on a link that never
 * went down.
 *
 * ### A dancing link is worth less with every repetition
 *
 * A link that comes and goes every few seconds - a module at the edge of its cell, an access point
 * rebooting in a loop - produces changes that are all real and all worth one line each until the
 * point where they are worth none. Changes that keep landing inside FLAP_GAP_MS of each other are
 * said FLAP_BURST times and then held back, and while they are held back the log gets one line
 * every FLAP_REPORT_INTERVAL_MS saying how many were held and what the last reason was. A change
 * that arrives after a calm gap is said in full again, and says what was held back in the
 * meantime, so nothing is lost - only compressed.
 *
 * ### Where this runs
 *
 * On the device every call arrives on the radio's own event task, one at a time, which is why the
 * state here is plain: there is one writer. The formatting is into a buffer of this object's own
 * and the line goes out on the logger's channel, which is the serial port - the event task builds
 * no MQTT topics and publishes nothing.
 */
class WifiLinkLogUseCase
{
public:
    /** A change this close to the one before it is a link that is dancing rather than moving. */
    static const uint32_t FLAP_GAP_MS = 60000;
    /** Changes in a row inside that gap that are still said in full. */
    static const uint32_t FLAP_BURST = 4;
    /** While the link is dancing: one line instead of all of them, at most this often. */
    static const uint32_t FLAP_REPORT_INTERVAL_MS = 300000;

    WifiLinkLogUseCase(ILogger *logger = nullptr, IDebugSystem *debugSystem = nullptr,
                       IArduinoAdapter *arduinoAdapter = nullptr)
        : _logger(logger), _debugSystem(debugSystem), _arduinoAdapter(arduinoAdapter)
    {
    }

    /**
     * @brief The station has associated and holds an address.
     *
     * @param ssid Network the station joined, as the radio named it
     * @param rssi Signal of that association, in dBm
     * @param ip Address the module was given
     */
    void linkUp(const char *ssid, int rssi, const char *ip)
    {
        if (_state == LinkState::UP)
        {
            // An address renewed on a link that never went down. The link came up once.
            return;
        }

        const unsigned long now = nowMs();
        _state = LinkState::UP;
        _linkUpAtMs = now;
        _linkUpKnown = true;

        if (holdThisChangeBack(now))
        {
            reportTheDanceIfItIsDue(now);
            return;
        }

        sayWhatWasHeldBack();
        log("WIFI: connected %s rssi=%d ip=%s", ssid != nullptr ? ssid : "?", rssi,
            ip != nullptr ? ip : "?");
    }

    /**
     * @brief The station has lost the link.
     *
     * @param reason Reason code the radio gave, which exists nowhere else
     */
    void linkDown(int reason)
    {
        _lastReason = reason;

        if (_state == LinkState::DOWN)
        {
            // The radio saying again what it already said while it retries the association.
            return;
        }

        const unsigned long now = nowMs();
        const bool wasUp = (_state == LinkState::UP);
        _state = LinkState::DOWN;

        if (holdThisChangeBack(now))
        {
            reportTheDanceIfItIsDue(now);
            return;
        }

        sayWhatWasHeldBack();
        if (wasUp && _linkUpKnown)
        {
            log("WIFI: disconnected reason=%d after %lu ms", reason, now - _linkUpAtMs);
        }
        else
        {
            // The first thing this module ever heard about its link was that it has none: nothing
            // stood, so there is no length to give.
            log("WIFI: disconnected reason=%d after no link", reason);
        }
    }

private:
    enum class LinkState
    {
        UNKNOWN,
        UP,
        DOWN
    };

    /**
     * @brief Account for a change of state, and answer whether the log should keep quiet about it.
     *
     * Every change goes through here, said or not, so that the spacing this reads is the spacing
     * of the link itself rather than of the lines.
     */
    bool holdThisChangeBack(unsigned long now)
    {
        const bool soonAfterTheLast = _sawAChange && (now - _lastChangeAtMs) < FLAP_GAP_MS;
        _sawAChange = true;
        _lastChangeAtMs = now;

        if (!soonAfterTheLast)
        {
            _fastChanges = 0;
            return false;
        }

        if (_fastChanges < FLAP_BURST)
        {
            _fastChanges++;
            return false;
        }

        _heldBackChanges++;
        return true;
    }

    /** @brief One line for a stretch of dancing, rather than one line for every step of it. */
    void reportTheDanceIfItIsDue(unsigned long now)
    {
        if (_saidTheDance && (now - _dancedAtMs) < FLAP_REPORT_INTERVAL_MS)
        {
            return;
        }

        _dancedAtMs = now;
        _saidTheDance = true;
        log("WIFI: link flapping - %lu change(s) held back, last reason=%d",
            (unsigned long)_heldBackChanges, _lastReason);
    }

    /** @brief What a link that has settled owes the log about the time it spent dancing. */
    void sayWhatWasHeldBack()
    {
        if (_heldBackChanges == 0)
        {
            return;
        }

        const uint32_t held = _heldBackChanges;
        _heldBackChanges = 0;
        _saidTheDance = false;
        log("WIFI: link settled - %lu change(s) were not said while it flapped",
            (unsigned long)held);
    }

    unsigned long nowMs() const
    {
        return _arduinoAdapter != nullptr ? _arduinoAdapter->millis() : 0;
    }

    void log(const char *format, ...)
    {
        if (_logger == nullptr || _debugSystem == nullptr ||
            !_debugSystem->isFeatureEnabled(DebugFeatures::WIFI_DEBUG))
        {
            return;
        }

        char buffer[160];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        _logger->println(DebugFeatures::WIFI_DEBUG, buffer);
    }

    ILogger *_logger;
    IDebugSystem *_debugSystem;
    IArduinoAdapter *_arduinoAdapter;

    LinkState _state = LinkState::UNKNOWN;
    bool _linkUpKnown = false;
    unsigned long _linkUpAtMs = 0;

    bool _sawAChange = false;
    unsigned long _lastChangeAtMs = 0;
    uint32_t _fastChanges = 0;
    uint32_t _heldBackChanges = 0;
    bool _saidTheDance = false;
    unsigned long _dancedAtMs = 0;
    int _lastReason = 0;
};

#endif // WIFI_LINK_LOG_USE_CASE_H
