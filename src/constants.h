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

// // Sensor states - see Core/Domain/Enums/SensorStateEnum.h
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

// ModBus address range for sensor devices
#define MODBUS_ADDRESS_MIN 0x70
#define MODBUS_ADDRESS_MAX 0x73

// Maximum number of channels per sensor
#define MAX_SENSOR_CHANNELS 4

// --- Task watchdog thresholds -------------------------------------------------------------------
//
// The task watchdog measures the gap between two feeds of a subscribed task. Its threshold must
// cover the longest *legal* gap any subscribed task can produce - the longest single blocking
// call that cannot feed from inside - or a healthy module reboots mid-operation.
//
// One REST call is read timeout + connect timeout, and the two are separate budgets:
// HTTPClient's setTimeout() only sets the read timeout, the connect timeout stays at its 5 s
// default unless setConnectTimeout() is called - RestApiService never calls it. The longest
// single holder of the shared _apiMutex is queryModuleDeletion(), which loops three attempts
// *inside* the lock: 3 x (5 s connect + 10 s read + 0.2 s pause) = ~45.6 s. FreeRTOS mutexes
// queue equal-priority waiters FIFO, so any waiter sits behind at most one full hold.
//
// The worst legal gaps, per subscribed task:
//
//   control loop   no outgoing network call at all: every request the loop can reach goes to
//                  the outbox, the command network task or the storage task, and the radio
//                  restart the diagnostics decide on is a job of the outbox too. What is left
//                  is the SPIFFS reads of the sensor type catalog - bounded by flash, not by
//                  a peer                                                           seconds
//   sensor-acq     one bounded step of a line operation: a port warmup, 10 s, plus one
//                  three-attempt ping. Longer sequences run on this task - a scan sweep, an
//                  address write with its whole fallback chain, twice as long again for a
//                  model that applies its address only after a power cycle - and each of them
//                  feeds between its steps, so the gap stays one step        WDT_SENSOR_ACQ_...
//   outbox-net     one dispatch: _apiMutex wait <= ~45.6 s + one call <= ~37 s
//                  (30 s POST + 2 s connectivity ping + 5 s connect)                ~83 s
//                  The link jobs on the same task are shorter than that. The radio
//                  restart loops for up to 60 s and feeds after every WiFiMulti::run() that
//                  fails with time left; one run() is a blocking scan (<= 10 s, the
//                  framework's scan wait) plus one association (5 s connect timeout). The
//                  run() that ends the minute is followed not by a feed but by one more
//                  scan, to log what is in range: the last run() and that scan are
//                  one unfed unit                                                  <= ~25 s
//                  The probe of the link is one unfed unit as well: a 3 s wait for the
//                  gateway and at most six 1 s echoes (boardLinkProbePlan())       <= ~9 s
//                  The MQTT connect act is beaten every LINK_BEAT_MS with a feed on each
//                  beat: the gap is one beat, whatever the broker does           ~0.2 s
//   cmd-net        one catalog attempt (~45.6 s wait + ~35 s call) ~81 s; the
//                  deletion query itself ~45.6 s; OTA on a slow link: 30 s connect
//                  + 30 s to the first data + Update.begin() before the first
//                  progress feed                                                    ~81 s
//   catalog-io     one catalog write: ~30 SPIFFS operations with no network under
//                  them, the slowest of them a write that triggers garbage
//                  collection inside the file system; bounded by flash, not by a
//                  peer, and far below the others                                   << 81 s
//
// The margin is 2x the worst of those. The binding entry is outbox-net, not the control loop:
// lowering the threshold needs the deletion query's three attempts taken out of the _apiMutex
// hold, or shorter HTTP timeouts - each a separate decision.
// The sensor-acq entry above as a number rather than a figure in a comment: the 10 s port
// warmup plus one three-attempt ping (200 + 700 + 1200 ms of response windows). Checked by the
// watchdog test (test_s13_watchdog.cpp) on the longest sequence that task can be handed, so a build that stops feeding
// between the steps of a line operation fails there instead of quietly enlarging this budget.
#define WDT_SENSOR_ACQ_LEGAL_PAUSE_S 11
#define WDT_WORST_LEGAL_PAUSE_S 90
#define WDT_TIMEOUT_S (2 * WDT_WORST_LEGAL_PAUSE_S)

// Startup runs on the same (subscribed) task, before the control loop and the background tasks
// exist, and its waits are local ones: setup makes no network call - the association, the module
// commands and the kept firmware update, and the deletion check are asked for from the loop. What
// it waits for is the button at power-on (up to 10 s, fed), the power-up of the stored sensors'
// ports and the SPIFFS mount; a mount that has to format the file system is the longest of them,
// and how long it takes on the board is not measured. setup() arms this value and re-arms the
// steady-state threshold once the control loop is about to start.
#define WDT_STARTUP_TIMEOUT_S 300

#ifdef __cplusplus
// Tautological while WDT_TIMEOUT_S stays derived; it catches exactly one real mistake - the
// threshold replaced by a literal that ignores the pause budget.
static_assert(WDT_TIMEOUT_S >= 2 * WDT_WORST_LEGAL_PAUSE_S,
              "the watchdog threshold must keep a 2x margin over the worst legal pause");
static_assert(WDT_STARTUP_TIMEOUT_S >= WDT_TIMEOUT_S,
              "startup may legally pause longer than steady state, never shorter");
#endif

#define HARD_RESET_SLEEP_MS 1500

#endif // CONSTANTS_H
