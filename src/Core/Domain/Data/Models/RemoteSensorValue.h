#ifndef REMOTE_SENSOR_VALUE_H
#define REMOTE_SENSOR_VALUE_H

#include <string>

struct RemoteSensorValue
{
    std::string uid;
    float value;
    std::string state;

    RemoteSensorValue() : value(0.0f) {}
};

#endif // REMOTE_SENSOR_VALUE_H
