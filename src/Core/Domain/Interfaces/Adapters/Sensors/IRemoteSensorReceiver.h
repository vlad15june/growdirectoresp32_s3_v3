#ifndef I_REMOTE_SENSOR_RECEIVER_H
#define I_REMOTE_SENSOR_RECEIVER_H

class IRemoteSensorReceiver {
public:
    virtual ~IRemoteSensorReceiver() = default;

    virtual bool init() = 0;
    virtual void onRuleSetChanged() = 0;
};

#endif // I_REMOTE_SENSOR_RECEIVER_H
