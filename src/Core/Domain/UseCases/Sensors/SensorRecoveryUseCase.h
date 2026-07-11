#ifndef SENSOR_RECOVERY_USE_CASE_H
#define SENSOR_RECOVERY_USE_CASE_H

#include <map>
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

    RecoveryAction decide(int portIndex, unsigned long now, const std::string &portSensorUid)
    {
        if (portIndex < 0 || portIndex >= PORT_COUNT)
        {
            return RecoveryAction::READ;
        }

        bool stuck = isStuck(portSensorUid, now);

        switch (_phase[portIndex])
        {
        case RecoveryPhase::NORMAL:
            if (stuck)
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
            if (now - _phaseStart[portIndex] > POLL_WINDOW_MS)
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
