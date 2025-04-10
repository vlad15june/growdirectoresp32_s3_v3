#ifndef CONSTANTS_H
#define CONSTANTS_H

// // Module configuration states
// #define IS_USER_CAN_CONFIGURE 42
// #define IS_USER_CAN_RE_CONFIGURE 142
// #define IS_AP_TEST_MODE 166
// #define IS_CONFIGURED_DONE 106
// #define IS_CONFIGURED_NEED_RESTART 111

// // Error codes
// #define NO_ERROR 0

// // Module types
// #define MODULE_TYPE_SENSOR 1
// #define MODULE_TYPE_DEVICE 2
// #define MODULE_TYPE_HYDRO 3
// #define MODULE_TYPE_ANALOG_INPUT_DEVICE 4
// unsigned char moduleType = MODULE_TYPE_DEVICE;

// // Module subtypes
// #define MODULE_SUBTYPE_NONE 0
// #define MODULE_SUBTYPE_SOKET 1
// #define MODULE_SUBTYPE_RELAY 2
// #define MODULE_SUBTYPE_DIMMER 3
// unsigned char moduleSubType = MODULE_SUBTYPE_RELAY;

// // Sensor addresses
// #define SHT85_ADDRESS 0x44

// // Connection states
// #define NOT_CONNECTED 1
// #define AM2315_CONNECTED 2
// #define ANALOG_CONNECTED 3
// #define T67XX_CONNECTED 4
// #define ANALOG_WATER_TEMP_CONNECTED 5
// #define ANALOG_PH_CONNECTED 6
// #define ANALOG_EC_CONNECTED 7
// #define ANALOG_DO_CONNECTED 8
// #define ANALOG_ORP_CONNECTED 9
// #define ANALOG_SOIL_MOISTURE 10
// #define ANALOG_LUX_CONNECTED 11
// #define ANALOG_PAR_CONNECTED 12
// #define BH1721_CONNECTED 13
// #define SHT4X_CONNECTED 14
// #define SHT85_CONNECTED 15

// // Sensor type names
// #define SENSOR_TYPE_NAME_DO "do"
// #define SENSOR_TYPE_NAME_ORP "orp"
// #define SENSOR_TYPE_NAME_SOIL_MOISTURE "soil_moisture"
// #define SENSOR_TYPE_NAME_LUX "lux"
// #define SENSOR_TYPE_NAME_PAR "par"

// // Device states
// #define DEVICE_STATE_NONE "none"
// #define DEVICE_STATE_ON "on"
// #define DEVICE_STATE_OFF "off"
// #define DEVICE_STATE_OFF_BY_POWER_ON "offByPowerOn"
// #define DEVICE_STATE_REQUEST_ON "requestOn"
// #define DEVICE_STATE_REQUEST_STEP_ON "requestStepOn"
// #define DEVICE_STATE_STEP_ON "stepOn"
// #define DEVICE_STATE_STEP_WAITING "stepWaiting"

// // Sensor states
// #define SENSOR_STATE_ONLINE "online"
// #define SENSOR_STATE_DISCONNECTED "disconnected"

// // Sensor parameters
// #define MAX_PPM 50000
// #define MAX_ADC 8190
// #define MIN_ADC 0

// // Buffer size
// #define BUFSIZE 150

// // I2C pins
// #define SDApin 10
// #define SCLpin 11

// // CID pins
// #define CID_0 5
// #define CID_1 6
// #define CID_2 7

// // Additional power supply pins
// #define SENSOR_PG_5_5V 33
// #define HYDRO_PG_5V 15
// #define HYDRO_PG_3_3V 17

// // General states
// #define ON 1
// #define OFF 0

// // Button configuration
// #define BUTTON_PIN 9
// #define BUTTON_PRESSED 0

// // ADC reading quantity
// #define ADC_READING_QUANTITY 32

// // LED testing
// #define LEDRED 0
// #define LEDGREEN 1

// // Boot types
// #define BOOT_TYPE_USER 86
// #define BOOT_TYPE_TECHNICAL 101
// #define BOOT_TYPE_POWER_ON 19
// #define BOOT_TYPE_SENSOR_CHNG 122
// #define BOOT_TYPE_FIRMWARE_UPDATE 111

// // Reboot command timing
// #define CAN_REBOOT_BY_COMMAND_AFTER_TIME 30000

// // Analog write value conversion
// #define PERCENT_TO_ANALOG_WRITE_VALUE 2.3

// // Firmware update timing
// #define MAX_UPDATE_TIME 300000

// // Firmware update states
// #define FIRMWARE_UPDATE_STATE_NONE 0
// #define FIRMWARE_UPDATE_STATE_IN_PROGRESS 1
// #define FIRMWARE_UPDATE_STATE_DONE 2
// #define FIRMWARE_UPDATE_STATE_ERROR 3
// #define FIRMWARE_UPDATE_STATE_HTTP_ERROR 4

// // Time constants
// #define TIME_RECONFIGURE 500000
// #define BUTTON_RECONFIG_PRESSED_TIME 10000
// #define BUTTON_TEST_MODE_PRESSED_TIME 3000

// // Buffer sizes
// #define FILE_LOG_BUF_SIZE 2500
// #define LOG_BUF_SIZE 2500
// #define MODULE_UID_SIZE 50
// #define MQTT_LOGGER_TOPIC_SIZE 25

#endif // CONSTANTS_H
