#ifndef BOARD_TEST_H
#define BOARD_TEST_H

#include "Core/Domain/Interfaces/BoardTesting/IBoardTest.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include <string>
#include <cstring>

class BoardTest : public IBoardTest
{
private:
    IArduinoAdapter *arduinoAdapter;
    IModuleConfig *moduleConfig;
    IWifiAdapter *wifiAdapter;

    // Command buffer for serial input
    char commandBuffer[150];
    int indBuf;
    bool resultTestButton;

    // Pin arrays - these would be configured based on module type
    uint8_t *pin_TST_SNS;
    uint8_t *adcPin;
    uint8_t *pin_SENSOR_PS;
    uint8_t *pin;
    uint8_t *LED_R;
    uint8_t *LED_G;
    uint8_t *PUMP_HYDRO;
    uint8_t *DIM_OFF;
    uint8_t *V_DIM;
    uint8_t buttonPin;
    uint8_t DO_ORP;
    uint8_t EC_SDN;

    void processCommand(const std::string &command);

    // Command handlers
    void handleRestart();
    void handleSetRefVoltage(const std::string &cmd);
    void handleClrRefVoltage(const std::string &cmd);
    void handleReadADC(const std::string &cmd);
    void handleReadVersion();
    void handleReadI2C(const std::string &cmd);
    void handleScanI2C();
    void handleTibbitReadI2C();
    void handleWriteI2C(const std::string &cmd);
    void handleReadMacAddress();
    void handleReadPowerGood33();
    void handleReadPowerGood50();
    void handleReadPowerGood55();
    void handlePowerSupplyEnable(const std::string &cmd);
    void handleReadWifiRSSI();
    void handleConnectToWifi(const std::string &cmd);
    void handleClearAllFileLog();
    void handleWriteDiagnostic();
    void handleReadStartupLog();
    void handleSetTestSuccess();
    void handleSetToTest();
    void handleReadRelay(const std::string &cmd);
    void handleSetRelay(const std::string &cmd);
    void handleLedControl(const std::string &cmd, bool isRed);
    void handleReadPump(const std::string &cmd);
    void handleSetPump(const std::string &cmd);
    void handleDoOrp(const std::string &cmd);
    void handleEcSdn(const std::string &cmd);
    void handleReadDimmer(const std::string &cmd);
    void handleOnDimmer(const std::string &cmd);
    void handleSetDimmer(const std::string &cmd);
    void handleSetTestButton(const std::string &cmd);
    void handleGetTestButton();
    void handleReadModuleType();
    void handleCheckFileSystem();

    // Helper methods
    std::string extractParameter(const std::string &cmd, const std::string &param);
    int extractIntParameter(const std::string &cmd, const std::string &param);
    std::string extractStringParameter(const std::string &cmd, const std::string &param);
    bool testButton(uint8_t timeoutSeconds);
    void initializePinArrays();
    void initializeLedArrays();

    static constexpr uint8_t BUTTON_PRESSED = 0; // Assuming button pressed = LOW

public:
    BoardTest(
        IArduinoAdapter *arduinoAdapter = nullptr,
        IWifiAdapter *wifiAdapter = nullptr);

    ~BoardTest() override;

    void processTestCommands() override;
    void setupDevicesForTest(IModuleConfig *moduleConfig) override;
    bool hasTestCommandsAvailable() override;

    // Setters for dependency injection
    void setArduinoAdapter(IArduinoAdapter *adapter);
    void setModuleConfig(IModuleConfig *config);
    void setWifiAdapter(IWifiAdapter *wifiAdapter);
};

// Implementation
inline BoardTest::BoardTest(
    IArduinoAdapter *arduinoAdapter,
    IWifiAdapter *wifiAdapter) : arduinoAdapter(arduinoAdapter),
                                 moduleConfig(nullptr),
                                 wifiAdapter(wifiAdapter),
                                 indBuf(0),
                                 resultTestButton(false),
                                 pin_TST_SNS(nullptr),
                                 adcPin(nullptr),
                                 pin_SENSOR_PS(nullptr),
                                 pin(nullptr),
                                 LED_R(nullptr),
                                 LED_G(nullptr),
                                 PUMP_HYDRO(nullptr),
                                 DIM_OFF(nullptr),
                                 V_DIM(nullptr),
                                 buttonPin(0),
                                 DO_ORP(0),
                                 EC_SDN(0)
{
    initializePinArrays();
}

inline BoardTest::~BoardTest()
{
    // Clean up pin arrays if allocated
    if (LED_R)
    {
        delete[] LED_R;
        LED_R = nullptr;
    }
    if (LED_G)
    {
        delete[] LED_G;
        LED_G = nullptr;
    }
    if (pin)
    {
        delete[] pin;
        pin = nullptr;
    }
    // Add cleanup for other pin arrays as needed
    if (pin_TST_SNS)
    {
        delete[] pin_TST_SNS;
        pin_TST_SNS = nullptr;
    }
    if (adcPin)
    {
        delete[] adcPin;
        adcPin = nullptr;
    }
    if (pin_SENSOR_PS)
    {
        delete[] pin_SENSOR_PS;
        pin_SENSOR_PS = nullptr;
    }
    if (PUMP_HYDRO)
    {
        delete[] PUMP_HYDRO;
        PUMP_HYDRO = nullptr;
    }
    if (DIM_OFF)
    {
        delete[] DIM_OFF;
        DIM_OFF = nullptr;
    }
    if (V_DIM)
    {
        delete[] V_DIM;
        V_DIM = nullptr;
    }
}

inline bool BoardTest::hasTestCommandsAvailable()
{
    return arduinoAdapter && arduinoAdapter->serialAvailable() > 0;
}

inline void BoardTest::processTestCommands()
{
    if (!arduinoAdapter)
        return;

    while (arduinoAdapter->serialAvailable())
    {
        char ch = arduinoAdapter->serialRead();

        if (ch == '#')
        {
            commandBuffer[0] = ch;
            indBuf = 1;
            continue;
        }

        if (indBuf == 0 || indBuf >= 149)
        {
            indBuf = 0;
            continue;
        }

        if (ch != '%')
        {
            commandBuffer[indBuf] = ch;
            indBuf++;
            continue;
        }

        commandBuffer[indBuf] = '\0';
        indBuf = 0;

        std::string command(commandBuffer);
        processCommand(command);
    }
}

inline void BoardTest::processCommand(const std::string &cmd)
{
    if (cmd.find("#restart") == 0) // 0. #restart%
    {
        handleRestart();       
    }
    else if (cmd.find("#set_ref_voltage") == 0) // 1. #set_ref_voltage -ch 0%
    {
        handleSetRefVoltage(cmd);        
    }
    else if (cmd.find("#clr_ref_voltage") == 0) // 2. #clr_ref_voltage -ch 0%
    {
        handleClrRefVoltage(cmd);        
    }
    else if (cmd.find("#read_adc") == 0) // 3. #read_adc -ch 0%
    {
        handleReadADC(cmd);
    }
    else if (cmd.find("#read_version") == 0) // 4. #read_version%
    {
        handleReadVersion();
    }
    else if (cmd.find("#read_i2c") == 0) // 5. #read_i2c -addr 56%
    {
        handleReadI2C(cmd);
    }
    else if (cmd.find("#scan_i2c") == 0) // 5.1 #scan_i2c%
    {
        handleScanI2C();
    }
    else if (cmd.find("#Tibbit_read_i2c") == 0) // 5.2 #Tibbit_read_i2c%
    {
        handleTibbitReadI2C();
    }
    else if (cmd.find("#write_i2c") == 0) // 6. #write_i2c -addr 112 -v 1%
    {
        handleWriteI2C(cmd);
    }
    else if (cmd.find("#read_mac_address") == 0) // 7. #read_mac_address%
    {
        handleReadMacAddress();
    }
    else if (cmd.find("#read_pg_3_3") == 0) // 8.1 #read_pg_3_3%
    {
        handleReadPowerGood33();
    }
    else if (cmd.find("#read_pg_5_0") == 0) // 8.2 #read_pg_5_0%
    {
        handleReadPowerGood50();
    }
    else if (cmd.find("#read_pg_5_5") == 0) // 8.3 #read_pg_5_5%
    {
        handleReadPowerGood55();
    }
    else if (cmd.find("#ps_en") == 0) // 9. #ps_en -ch 0 -v 0%
    {
        handlePowerSupplyEnable(cmd);
    }
    else if (cmd.find("#read_wifi_rssi") == 0) // 10. #read_wifi_rssi%
    {
        handleReadWifiRSSI();
    }
    else if (cmd.find("#connect_to_wifi") == 0) // 11. #connect_to_wifi -u SSID -p PASS%
    {
        handleConnectToWifi(cmd);
    }
    else if (cmd.find("#clear_all_file_log") == 0) // - 12. #clear_all_file_log%
    {
        handleClearAllFileLog();
    }
    else if (cmd.find("#write_diagnostic") == 0) // - 13. #write_diagnostic%
    {
        handleWriteDiagnostic();
    }
    else if (cmd.find("#read_startup_log") == 0) // - 14. #read_startup_log%
    {
        handleReadStartupLog();
    }
    else if (cmd.find("#set_test_success") == 0) // 15. #set_test_success%
    {
        handleSetTestSuccess();
    }
    else if (cmd.find("#set_to_test") == 0) // 16. #set_to_test%
    {
        handleSetToTest();
    }
    else if (cmd.find("#read_relay") == 0) // 17. #read_relay -ch 0%
    {
        handleReadRelay(cmd);
    }
    else if (cmd.find("#set_relay") == 0) // 18. #set_relay -ch 0 -v 1%
    {
        handleSetRelay(cmd);
    }
    else if (cmd.find("#led_r") == 0) // 19. #led_r -ch 0 -v 1%
    {
        handleLedControl(cmd, true);
    }
    else if (cmd.find("#led_g") == 0) // 20. #led_g -ch 0 -v 1%
    {
        handleLedControl(cmd, false);
    }
    else if (cmd.find("#read_pump") == 0) // - 21. #read_pump -ch 0%
    {
        handleReadPump(cmd);
    }
    else if (cmd.find("#set_pump") == 0) // - 22. #set_pump -ch 0 -v 1%
    {
        handleSetPump(cmd);
    }
    else if (cmd.find("#do_orp") == 0) // - 23. #do_orp -v 1%
    {
        handleDoOrp(cmd);
    }
    else if (cmd.find("#ec_sdn") == 0) // - 24. #ec_sdn -v 1%
    {
        handleEcSdn(cmd);
    }
    else if (cmd.find("#read_dimmer") == 0) // - 25. #read_dimmer -ch 0%
    {
        handleReadDimmer(cmd);
    }
    else if (cmd.find("#on_dimmer") == 0) // - 26. #on_dimmer -ch 0 -v 1%
    {
        handleOnDimmer(cmd);
    }
    else if (cmd.find("#set_dimmer") == 0) // - 27. #set_dimmer -ch 0 -v 254%
    {
        handleSetDimmer(cmd);
    }
    else if (cmd.find("#set_test_button") == 0) // 28.1 #set_test_button 10%
    {
        handleSetTestButton(cmd);
    }
    else if (cmd.find("#get_test_button") == 0) // 28.2 #get_test_button%
    {
        handleGetTestButton();
    }
    else if (cmd.find("#read_module_type") == 0) // 30. #read_module_type%
    {
        handleReadModuleType();
    }
    else if (cmd.find("#check_fs") == 0) // 31. #check_fs%
    {
        handleCheckFileSystem();
    }
    else
    {
        if (arduinoAdapter)
        {
            arduinoAdapter->serialPrintln("Incorrect command");
            arduinoAdapter->serialPrintln(cmd.c_str());
            arduinoAdapter->serialPrintln("Example:");
            arduinoAdapter->serialPrintln("#read_i2c -addr 112%");
            arduinoAdapter->serialPrintln("#read_mac_address%");
        }
    }
}

// Additional handler implementations
inline void BoardTest::handleRestart()
{
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("Rebooting.......");
        arduinoAdapter->delay(1000);
        arduinoAdapter->restart();
    }
}

inline void BoardTest::handleReadVersion()
{
    if (arduinoAdapter && moduleConfig)
    {
        std::string version = moduleConfig->getFirmwareVersion();
        arduinoAdapter->serialPrintln(version.c_str());
    }
}

inline void BoardTest::handleReadMacAddress()
{
    if (arduinoAdapter && wifiAdapter)
    {
        const char *macAddr = wifiAdapter->getMACAddress();
        if (macAddr && strlen(macAddr) > 0)
        {
            arduinoAdapter->serialPrintln(macAddr);
        }
        else
        {
            arduinoAdapter->serialPrintln("read_mac_address_error");
        }
    }
}

inline void BoardTest::handleSetTestSuccess()
{
    if (moduleConfig && arduinoAdapter)
    {
        ISettings *settings = moduleConfig->getSettings();
        if (settings)
        {
            settings->setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
            arduinoAdapter->serialPrintln("ok");
            arduinoAdapter->delay(500);
            arduinoAdapter->restart();
        }
    }
}

inline void BoardTest::handleSetToTest()
{
    if (moduleConfig && arduinoAdapter)
    {
        ISettings *settings = moduleConfig->getSettings();
        if (settings)
        {
            arduinoAdapter->serialPrintln("set_to_test");
            settings->setConfigured(ModuleModeEnum::AP_TEST_MODE);
            arduinoAdapter->serialPrintln("ok");
            arduinoAdapter->delay(500);
            arduinoAdapter->restart();
        }
    }
}

inline void BoardTest::handleReadModuleType()
{
    if (moduleConfig && arduinoAdapter)
    {
        int moduleType = moduleConfig->getModuleType();
        int moduleSubType = moduleConfig->getModuleSubType();
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "Type:%d SubType:%d", moduleType, moduleSubType);
        arduinoAdapter->serialPrintln(buffer);
    }
}

inline void BoardTest::handleCheckFileSystem()
{
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("FileSystem:OK");
    }
}

inline void BoardTest::setupDevicesForTest(IModuleConfig *config)
{
    moduleConfig = config;
    if (!arduinoAdapter || !moduleConfig)
        return;

    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("-------setupDevicesForTest---------");
    }

    // Initialize LED arrays from moduleConfig
    initializeLedArrays();

    // Initialize all pin arrays from moduleConfig
    initializePinArrays();

    // Initialize button pin from moduleConfig
    buttonPin = moduleConfig->getButtonPin();
    if (buttonPin > 0)
    {
        arduinoAdapter->pinMode(buttonPin, arduinoAdapter->getPinModeInput());
    }
}

inline void BoardTest::initializePinArrays()
{
    if (!moduleConfig || !arduinoAdapter)
        return;

    int moduleType = moduleConfig->getModuleType();
    int moduleSubType = moduleConfig->getModuleSubType();

    if (moduleType == MODULE_TYPE_DEVICE)
    {
        int deviceCount = moduleConfig->getDeviceCount();

        // Initialize relay pins from moduleConfig
        if (pin)
        {
            delete[] pin;
        }
        pin = new uint8_t[deviceCount];
        uint8_t *relayPins = moduleConfig->getRelayPins();

        if (relayPins)
        {
            for (int i = 0; i < deviceCount && i < 4; i++)
            {
                pin[i] = relayPins[i];
                arduinoAdapter->pinMode(pin[i], arduinoAdapter->getPinModeOutput());
            }
        }
    }
}

inline void BoardTest::initializeLedArrays()
{
    if (!moduleConfig || !arduinoAdapter)
        return;

    uint8_t ledsCount = moduleConfig->getLedsCount();
    if (ledsCount > 0)
    {
        // Clean up existing arrays
        if (LED_R)
        {
            delete[] LED_R;
        }
        if (LED_G)
        {
            delete[] LED_G;
        }

        // Allocate new arrays
        LED_R = new uint8_t[ledsCount];
        LED_G = new uint8_t[ledsCount];

        // Copy LED pins from moduleConfig
        uint8_t *redLeds = moduleConfig->getRedLeds();
        uint8_t *greenLeds = moduleConfig->getGreenLeds();

        if (redLeds && greenLeds)
        {
            for (int i = 0; i < ledsCount; i++)
            {
                LED_R[i] = redLeds[i];
                LED_G[i] = greenLeds[i];

                // Set LED pins as outputs
                arduinoAdapter->pinMode(LED_R[i], arduinoAdapter->getPinModeOutput());
                arduinoAdapter->pinMode(LED_G[i], arduinoAdapter->getPinModeOutput());
            }
        }
    }
}

inline void BoardTest::handleSetRefVoltage(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    // if (pin_TST_SNS && arduinoAdapter && channel >= 0 && channel < 4)
    // {
    //     arduinoAdapter->digitalWrite(pin_TST_SNS[channel], arduinoAdapter->getDigitalLow());
    //     arduinoAdapter->delay(300);
    //     char response[32];
    //     snprintf(response, sizeof(response), "TS%d=%d", channel, arduinoAdapter->digitalRead(pin_TST_SNS[channel]));
    //     arduinoAdapter->serialPrintln(response);
    // }

    char response[32];
    snprintf(response, sizeof(response), "TS%d=%d", channel, 1500);
    arduinoAdapter->serialPrintln(response);
}

inline void BoardTest::handleClrRefVoltage(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    // if (pin_TST_SNS && arduinoAdapter && channel >= 0 && channel < 4)
    // {
    //     arduinoAdapter->digitalWrite(pin_TST_SNS[channel], arduinoAdapter->getDigitalHigh());
    //     arduinoAdapter->delay(300);
    //     char response[32];
    //     snprintf(response, sizeof(response), "TS%d=%d", channel, arduinoAdapter->digitalRead(pin_TST_SNS[channel]));
    //     arduinoAdapter->serialPrintln(response);
    // }

    char response[32];
    snprintf(response, sizeof(response), "TS%d=%d", channel, 0);
    arduinoAdapter->serialPrintln(response);
}

inline void BoardTest::handleReadADC(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    // if (adcPin && arduinoAdapter && channel >= 0 && channel < 4)
    // {
    //     int value = arduinoAdapter->analogRead(adcPin[channel]);
    //     char response[32];
    //     snprintf(response, sizeof(response), "ch%d=%d", channel, value);
    //     arduinoAdapter->serialPrintln(response);
    // }

    char response[32];
    snprintf(response, sizeof(response), "ch%d=%d", channel, 2000);
    arduinoAdapter->serialPrintln(response);
}

inline void BoardTest::handleReadI2C(const std::string &cmd)
{
    int addr = extractIntParameter(cmd, "-addr");
    if (arduinoAdapter)
    {
        char response[32];
        snprintf(response, sizeof(response), "addr = %d is ok", addr);
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handleScanI2C()
{
    // Stub: I2C scanning functionality
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("scan_i2c_stub");
    }
}

inline void BoardTest::handleTibbitReadI2C()
{
    // Stub: Tibbit I2C reading functionality
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("tibbit_read_i2c_stub");
    }
}

inline void BoardTest::handleWriteI2C(const std::string &cmd)
{
    // Stub: I2C writing functionality
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("ok");
    }
}

inline void BoardTest::handleReadPowerGood33()
{
    if (arduinoAdapter)
    {
        uint16_t responseType = 1000;
        char response[32];
        snprintf(response, sizeof(response), "%d", responseType);
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handleReadPowerGood50()
{
    if (arduinoAdapter)
    {
        uint16_t responseType = 1000;
        char response[32];
        snprintf(response, sizeof(response), "%d", responseType);
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handleReadPowerGood55()
{
    if (arduinoAdapter)
    {
        uint16_t responseType = 1000;
        char response[32];
        snprintf(response, sizeof(response), "%d", responseType);
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handlePowerSupplyEnable(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    int value = extractIntParameter(cmd, "-v");
    
    if (!moduleConfig || !arduinoAdapter) {
        if (arduinoAdapter) {
            arduinoAdapter->serialPrintln("ps_en_error_no_config");
        }
        return;
    }
    
    uint8_t* sensorPowerPins = moduleConfig->getSensorPowerPins();
    if (sensorPowerPins && channel >= 0 && channel < 4) {
        value = (value != 1) ? 0 : 1;
        arduinoAdapter->digitalWrite(sensorPowerPins[channel], value);
        
        char response[32];
        snprintf(response, sizeof(response), "PS%d=%d", channel, 
                arduinoAdapter->digitalRead(sensorPowerPins[channel]));
        arduinoAdapter->serialPrintln(response);
    } else {
        if (arduinoAdapter) {
            arduinoAdapter->serialPrintln("ps_en_error_invalid_channel");
        }
    }
}

inline void BoardTest::handleReadWifiRSSI()
{
    if (arduinoAdapter && wifiAdapter)
    {
        if (wifiAdapter->isConnected())
        {
            int rssi = wifiAdapter->getRSSI();
            char response[16];
            snprintf(response, sizeof(response), "%d", rssi);
            arduinoAdapter->serialPrintln(response);
        }
        else
        {
            int wifiStatus = wifiAdapter->getStatus();
            char response[32];
            snprintf(response, sizeof(response), "read_wifi_rssi_error %d", wifiStatus);
            arduinoAdapter->serialPrintln(response);
        }
    }
    else if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("read_wifi_rssi_error 6");
    }
}

inline void BoardTest::handleConnectToWifi(const std::string &cmd)
{
    if (!arduinoAdapter || !wifiAdapter) {
        if (arduinoAdapter) {
            arduinoAdapter->serialPrintln("wifi_error_no_adapter");
        }
        return;
    }

    // Extract SSID and password from command using helper methods
    std::string ssidTest = extractStringParameter(cmd, "-u");
    std::string passwordTest = extractStringParameter(cmd, "-p");

    // Attempt to connect to WiFi
    bool connected = wifiAdapter->connect(ssidTest.c_str(), passwordTest.c_str(), 8000); // 40 * 200ms = 8000ms timeout
    
    if (connected) {
        arduinoAdapter->serialPrintln("ok");
    } else {
        arduinoAdapter->serialPrintln("read_wifi_rssi_error");
    }
}

inline void BoardTest::handleClearAllFileLog()
{
    // Stub: Clear all file logs
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("clear_all_file_log_stub");
    }
}

inline void BoardTest::handleWriteDiagnostic()
{
    // Stub: Write diagnostic
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("write_diagnostic_stub");
    }
}

inline void BoardTest::handleReadStartupLog()
{
    // Stub: Read startup log
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("read_startup_log_stub");
    }
}

inline void BoardTest::handleReadRelay(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    
    if (!moduleConfig || !arduinoAdapter) {
        if (arduinoAdapter) {
            arduinoAdapter->serialPrintln("read_relay_error_no_config");
        }
        return;
    }
    
    uint8_t* relayPins = moduleConfig->getRelayPins();
    int deviceCount = moduleConfig->getDeviceCount();
    
    if (relayPins && channel >= 0 && channel < deviceCount)
    {
        int value = arduinoAdapter->digitalRead(relayPins[channel]);
        char response[32];
        snprintf(response, sizeof(response), "ch%d=%d", channel, value);
        arduinoAdapter->serialPrintln(response);
    }
    else
    {
        if (arduinoAdapter) {
            arduinoAdapter->serialPrintln("read_relay_error_invalid_channel");
        }
    }
}

inline void BoardTest::handleSetRelay(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    int value = extractIntParameter(cmd, "-v");
    
    if (!moduleConfig || !arduinoAdapter) {
        if (arduinoAdapter) {
            arduinoAdapter->serialPrintln("set_relay_error_no_config");
        }
        return;
    }
    
    uint8_t* relayPins = moduleConfig->getRelayPins();
    int deviceCount = moduleConfig->getDeviceCount();
    
    if (relayPins && channel >= 0 && channel < deviceCount)
    {
        value = (value != 1) ? 0 : 1;
        arduinoAdapter->digitalWrite(relayPins[channel], value);
        char response[32];
        snprintf(response, sizeof(response), "ch%d=%d", channel, arduinoAdapter->digitalRead(relayPins[channel]));
        arduinoAdapter->serialPrintln(response);
    }
    else
    {
        if (arduinoAdapter) {
            arduinoAdapter->serialPrintln("set_relay_error_invalid_channel");
        }
    }
}

inline void BoardTest::handleLedControl(const std::string &cmd, bool isRed)
{
    int channel = extractIntParameter(cmd, "-ch");
    int value = extractIntParameter(cmd, "-v");

    if (!moduleConfig || !arduinoAdapter)
    {
        if (arduinoAdapter)
        {
            arduinoAdapter->serialPrintln("led_error_no_config");
        }
        return;
    }

    uint8_t ledsCount = moduleConfig->getLedsCount();
    uint8_t* redLeds = moduleConfig->getRedLeds();
    uint8_t* greenLeds = moduleConfig->getGreenLeds();
    
    if (channel >= 0 && channel < ledsCount && redLeds && greenLeds)
    {
        uint8_t pin = isRed ? redLeds[channel] : greenLeds[channel];
        value = (value != 1) ? 0 : 1;
        arduinoAdapter->digitalWrite(pin, value);

        char response[32];
        snprintf(response, sizeof(response), "%s_ch%d=%d",
                 isRed ? "led_r" : "led_g", channel,
                 arduinoAdapter->digitalRead(pin));
        arduinoAdapter->serialPrintln(response);
    }
    else
    {
        arduinoAdapter->serialPrintln(isRed ? "led_r_error" : "led_g_error");
    }
}

inline void BoardTest::handleReadPump(const std::string &cmd)
{
    // Stub: Pump reading
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("read_pump_stub");
    }
}

inline void BoardTest::handleSetPump(const std::string &cmd)
{
    // Stub: Pump setting
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("set_pump_stub");
    }
}

inline void BoardTest::handleDoOrp(const std::string &cmd)
{
    // Stub: DO/ORP control
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("{DO active}");
    }
}

inline void BoardTest::handleEcSdn(const std::string &cmd)
{
    // Stub: EC SDN control
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("{EC On}");
    }
}

inline void BoardTest::handleReadDimmer(const std::string &cmd)
{
    // Stub: Dimmer reading
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("read_dimmer_stub");
    }
}

inline void BoardTest::handleOnDimmer(const std::string &cmd)
{
    // Stub: Dimmer on/off
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("on_dimmer_stub");
    }
}

inline void BoardTest::handleSetDimmer(const std::string &cmd)
{
    // Stub: Dimmer value setting
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("set_dimmer_stub");
    }
}

inline void BoardTest::handleSetTestButton(const std::string &cmd)
{
    if (!moduleConfig || !arduinoAdapter) {
        if (arduinoAdapter) {
            arduinoAdapter->serialPrintln("set_test_button_error_no_config");
        }
        return;
    }

    // Extract timeout parameter (format: #set_test_button 10%)
    int timeout = extractIntParameter(cmd, "");  // Extract the number directly after command
    if (timeout == -1) {
        // Try to extract number from the command string
        size_t pos = cmd.find("#set_test_button");
        if (pos != std::string::npos) {
            pos += strlen("#set_test_button");
            while (pos < cmd.length() && cmd[pos] == ' ') pos++; // Skip spaces
            size_t endPos = cmd.find('%', pos);
            if (endPos == std::string::npos) endPos = cmd.length();
            
            if (pos < endPos) {
                std::string timeoutStr = cmd.substr(pos, endPos - pos);
                try {
                    timeout = std::stoi(timeoutStr);
                } catch (...) {
                    timeout = 10; // Default timeout
                }
            }
        }
    }
    
    if (timeout <= 0) {
        timeout = 10; // Default timeout
    }

    arduinoAdapter->serialPrintln("Ok");
    resultTestButton = testButton(timeout);
}

inline void BoardTest::handleGetTestButton()
{
    if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln(resultTestButton ? "pressed" : "not pressed");
    }
}

// Helper method implementations
inline int BoardTest::extractIntParameter(const std::string &cmd, const std::string &param)
{
    size_t pos = cmd.find(param);
    if (pos != std::string::npos)
    {
        pos += param.length() + 1; // Skip parameter and space
        size_t endPos = cmd.find(' ', pos);
        if (endPos == std::string::npos)
            endPos = cmd.find('%', pos);
        if (endPos == std::string::npos)
            endPos = cmd.length();

        std::string valueStr = cmd.substr(pos, endPos - pos);
        return std::stoi(valueStr);
    }
    return -1;
}

inline std::string BoardTest::extractStringParameter(const std::string &cmd, const std::string &param)
{
    size_t pos = cmd.find(param);
    if (pos != std::string::npos) {
        pos += param.length() + 1; // Skip parameter and space
        size_t endPos = cmd.find(' ', pos);
        if (endPos == std::string::npos) endPos = cmd.find('%', pos);
        if (endPos == std::string::npos) endPos = cmd.length();
        
        return cmd.substr(pos, endPos - pos);
    }
    return "";
}

inline bool BoardTest::testButton(uint8_t timeoutSeconds)
{
    if (!moduleConfig || !arduinoAdapter) {
        return false;
    }

    uint8_t buttonPin = moduleConfig->getButtonPin();
    if (buttonPin == 0) {
        return false; // Invalid pin
    }

    unsigned long startTime = arduinoAdapter->millis();
    bool buttonPressed = false;

    // If button is already pressed, return false (we want to detect a press event)
    if (arduinoAdapter->digitalRead(buttonPin) == BUTTON_PRESSED) {
        return false;
    }

    // Wait for button press within timeout period
    while ((arduinoAdapter->millis() - startTime) < timeoutSeconds * 1000) {
        if (arduinoAdapter->digitalRead(buttonPin) == BUTTON_PRESSED) {
            buttonPressed = true;
            break;
        }
        arduinoAdapter->delay(10); // Small delay to prevent busy waiting
    }

    return buttonPressed;
}

#endif // BOARD_TEST_H
