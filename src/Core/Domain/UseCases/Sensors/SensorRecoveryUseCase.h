#ifndef SENSOR_RECOVERY_USE_CASE_H
#define SENSOR_RECOVERY_USE_CASE_H

#include <map>
#include <set>
#include <string>

enum class RecoveryAction { READ, POWER_OFF, POWER_ON };
enum class RecoveryPhase { NORMAL, POWER_OFF_PHASE, POWER_ON_PHASE, POLLING };

class SensorRecoveryUseCase
{
public:
    static constexpr unsigned long DISCONNECT_THRESHOLD_MS = 60000;
    static constexpr unsigned long POLL_WINDOW_MS = 60000;
    static constexpr int PORT_COUNT = 4;

    SensorRecoveryUseCase()
    {
        for (int i = 0; i < PORT_COUNT; i++)
        {
            _phase[i] = RecoveryPhase::NORMAL;
            _phaseStart[i] = 0;
            _portNotAskedSinceTheDark[i] = false;
        }
    }

    void observe(const std::string &uid, bool isError, unsigned long now)
    {
        if (isError)
        {
            if (_disconnectedSince.find(uid) == _disconnectedSince.end())
            {
                _disconnectedSince[uid] = now;
            }
        }
        else
        {
            _disconnectedSince.erase(uid);
        }
    }

    /**
     * @brief Leave out of this sensor's silence a stretch its port could not answer in: it had no
     *        power, or had it for less than a sensor needs, and not because this recovery took it.
     *
     * Only the part of the stretch the silence has been running for, and only in NORMAL: in the
     * phases after it "the sensor is no longer stuck" means a read has answered, and moving the
     * clock would make it mean less.
     *
     * Nor does the port being back make the silence after it evidence yet: the line may be busy for
     * a while before anyone asks the sensor. So the sensor is asked once before its silence can
     * power-cycle it - see decide().
     */
    void leaveOutOfTheSilence(int portIndex, const std::string &uid, unsigned long fromMs,
                              unsigned long untilMs, unsigned long now)
    {
        if (portIndex < 0 || portIndex >= PORT_COUNT || _phase[portIndex] != RecoveryPhase::NORMAL)
        {
            return;
        }
        std::map<std::string, unsigned long>::iterator clock = _disconnectedSince.find(uid);
        if (clock == _disconnectedSince.end())
        {
            return;
        }
        const unsigned long leftOutMs = partAfter(clock->second, fromMs, untilMs, now);
        if (leftOutMs > 0)
        {
            clock->second += leftOutMs;
            _notAskedSinceTheDark.insert(uid);
        }
    }

    /**
     * @brief The same stretch, out of the port's polling window while it is in POLLING: a power
     *        cycle is retried after a minute of polling a port that could answer, not straight after
     *        the port comes back. Once per stretch, however many sensors the port carries - and,
     *        as with the silence, the port is asked once before the window can retry the cycle.
     */
    void leaveOutOfThePollingWindow(int portIndex, unsigned long fromMs, unsigned long untilMs,
                                    unsigned long now)
    {
        if (portIndex < 0 || portIndex >= PORT_COUNT || _phase[portIndex] != RecoveryPhase::POLLING)
        {
            return;
        }
        const unsigned long leftOutMs = partAfter(_phaseStart[portIndex], fromMs, untilMs, now);
        if (leftOutMs > 0)
        {
            _phaseStart[portIndex] += leftOutMs;
            _portNotAskedSinceTheDark[portIndex] = true;
        }
    }

    RecoveryAction decide(int portIndex, unsigned long now, const std::string &portSensorUid)
    {
        if (portIndex < 0 || portIndex >= PORT_COUNT)
        {
            return RecoveryAction::READ;
        }

        bool stuck = isStuck(portSensorUid, now);
        // Whatever this visit decides, the sensor is asked or acted on now: a stretch left out of
        // its clocks defers an action by one read, never by more.
        const bool askFirst = _notAskedSinceTheDark.erase(portSensorUid) > 0;
        const bool askThePortFirst = _portNotAskedSinceTheDark[portIndex];
        _portNotAskedSinceTheDark[portIndex] = false;

        switch (_phase[portIndex])
        {
        case RecoveryPhase::NORMAL:
            if (stuck && !askFirst)
            {
                _phase[portIndex] = RecoveryPhase::POWER_OFF_PHASE;
                return RecoveryAction::POWER_OFF;
            }
            return RecoveryAction::READ;

        case RecoveryPhase::POWER_OFF_PHASE:
            _phase[portIndex] = RecoveryPhase::POWER_ON_PHASE;
            return RecoveryAction::POWER_ON;

        case RecoveryPhase::POWER_ON_PHASE:
            _phase[portIndex] = RecoveryPhase::POLLING;
            _phaseStart[portIndex] = now;
            return RecoveryAction::READ;

        case RecoveryPhase::POLLING:
            if (!stuck)
            {
                _phase[portIndex] = RecoveryPhase::NORMAL;
                return RecoveryAction::READ;
            }
            if (now - _phaseStart[portIndex] > POLL_WINDOW_MS && !askThePortFirst)
            {
                _phase[portIndex] = RecoveryPhase::POWER_OFF_PHASE;
                return RecoveryAction::POWER_OFF;
            }
            return RecoveryAction::READ;
        }

        return RecoveryAction::READ;
    }

    RecoveryPhase getPhase(int portIndex) const
    {
        if (portIndex < 0 || portIndex >= PORT_COUNT)
        {
            return RecoveryPhase::NORMAL;
        }
        return _phase[portIndex];
    }

private:
    std::map<std::string, unsigned long> _disconnectedSince;
    RecoveryPhase _phase[PORT_COUNT];
    unsigned long _phaseStart[PORT_COUNT];
    /** A stretch was left out of these clocks, and nobody has asked the sensor or port since. */
    std::set<std::string> _notAskedSinceTheDark;
    bool _portNotAskedSinceTheDark[PORT_COUNT];

    /**
     * @brief How much of the stretch from `fromMs` to `untilMs` lies after `since`.
     *
     * Read off ages from now rather than off the moments, so that it stays right across the wrap of
     * millis(); the stretch has ended by now.
     */
    static unsigned long partAfter(unsigned long since, unsigned long fromMs, unsigned long untilMs,
                                   unsigned long now)
    {
        const unsigned long sinceAge = now - since;
        const unsigned long fromAge = now - fromMs;
        const unsigned long untilAge = now - untilMs;
        const unsigned long startAge = (fromAge < sinceAge) ? fromAge : sinceAge;
        return (startAge > untilAge) ? startAge - untilAge : 0;
    }

    bool isStuck(const std::string &uid, unsigned long now) const
    {
        auto it = _disconnectedSince.find(uid);
        if (it == _disconnectedSince.end())
        {
            return false;
        }
        return (now - it->second) > DISCONNECT_THRESHOLD_MS;
    }
};

#endif // SENSOR_RECOVERY_USE_CASE_H
