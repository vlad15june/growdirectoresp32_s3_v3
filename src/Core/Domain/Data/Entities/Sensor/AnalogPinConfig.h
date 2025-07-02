#ifndef ANALOG_PIN_CONFIG_H
#define ANALOG_PIN_CONFIG_H

#include <string>

struct AnalogPinConfig
{
    uint8_t pin;
    float multiplier;
    float offset;
    std::string channelName;
    uint8_t channel;
    std::string sensorType;
    
    AnalogPinConfig() : pin(0), multiplier(1.0f), offset(0.0f), channel(0) {}
    
    AnalogPinConfig(uint8_t p, float mult, float off, const std::string& chName, uint8_t ch, const std::string& sType)
        : pin(p), multiplier(mult), offset(off), channelName(chName), channel(ch), sensorType(sType) {}
};

#endif // ANALOG_PIN_CONFIG_H
