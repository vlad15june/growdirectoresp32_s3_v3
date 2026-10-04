#ifndef I2C_REGISTER_CONFIG_H
#define I2C_REGISTER_CONFIG_H

#include <string>

/**
 * @brief I2C register configuration for I2C sensors
 *
 * The numbers default to the ones a stored record without the field is read with. A field left at
 * whatever the stack held is written back into the registry as text - a float holding garbage
 * serializes as "-nan", which is neither a number to read nor valid JSON.
 */
struct I2CRegisterConfig {
    uint16_t registerAddr = 0;  // Register address
    uint8_t length = 1;         // Data length in bytes
    float multiplier = 1.0f;    // Value multiplier
    float offset = 0.0f;        // Value offset
    std::string channelName;
    uint8_t channel = 0;
    std::string sensorType;
};

#endif
