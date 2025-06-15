#ifndef I_ANALOG_SENSOR_READER_H
#define I_ANALOG_SENSOR_READER_H

#include <stdint.h>

class IAnalogSensorReader
{
public:
    virtual ~IAnalogSensorReader() = default;

    /**
     * @brief Read raw analog value from sensor
     * @param pin Analog pin number
     * @return Raw analog reading (0-4095 for ESP32)
     */
    virtual int readRawValue(uint8_t pin) = 0;

    /**
     * @brief Read voltage from analog sensor
     * @param pin Analog pin number
     * @param maxVoltage Maximum voltage reference (default 3.3V for ESP32)
     * @param adcResolution ADC resolution in bits (default 12 for ESP32)
     * @return Voltage value in volts
     */
    virtual float readVoltage(uint8_t pin, float maxVoltage = 3.3f, uint8_t adcResolution = 12) = 0;

    /**
     * @brief Read calibrated sensor value
     * @param pin Analog pin number
     * @param multiplier Calibration multiplier
     * @param offset Calibration offset
     * @return Calibrated sensor value
     */
    virtual float readCalibratedValue(uint8_t pin, float multiplier, float offset) = 0;

    /**
     * @brief Read multiple samples and return average
     * @param pin Analog pin number
     * @param samples Number of samples to average
     * @param delayMs Delay between samples in milliseconds
     * @return Average raw value
     */
    virtual int readAverageValue(uint8_t pin, uint8_t samples = 10, uint8_t delayMs = 10) = 0;
};

#endif // I_ANALOG_SENSOR_READER_H
