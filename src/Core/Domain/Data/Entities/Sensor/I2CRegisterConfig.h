#ifndef I2C_REGISTER_CONFIG_H
#define I2C_REGISTER_CONFIG_H

#include <string>

/**
 * @brief I2C register configuration for I2C sensors
 */
struct I2CRegisterConfig {
    uint16_t registerAddr;      // Register address
    uint8_t length;             // Data length in bytes
    float multiplier;           // Value multiplier
    float offset;               // Value offset

    I2CRegisterConfig() : registerAddr(0), length(1), multiplier(1.0f), offset(0.0f) {}
};

#endif
