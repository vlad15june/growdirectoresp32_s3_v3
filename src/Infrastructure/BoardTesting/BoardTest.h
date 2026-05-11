#ifndef BOARD_TEST_H
#define BOARD_TEST_H

#include "Core/Domain/Interfaces/BoardTesting/IBoardTest.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Infrastructure/Configuration/ModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Enums/FileSystemStatusEnum.h"
#include "Infrastructure/Adapters/Rs485/ModbusRS485.h"
#include <string>
#include <cstring>
#include <vector>
#include <memory>

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

    // RS485 communication
    ModbusRS485* currentModbus;
    HardwareSerial* testSerial;
    std::vector<IModbusRS485*> modbusPorts;

    // Non-blocking button test state
    bool buttonTestActive;
    unsigned long buttonTestStart;
    uint32_t buttonTestTimeoutMs;

    void processCommand(const std::string &command);
    void updateButtonTest();

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
    void handleInitRtc3028();
    void handleReadRtc3028();
    void handleTestModbus(const std::string &cmd);
    void handleTestTempHum(const std::string &cmd);
    void handleListen485(const std::string &cmd);
    void handleSend485(const std::string &cmd);
    void handleSensorPsStatus(const std::string &cmd);
    void handlePumpPsStatus(const std::string &cmd);
    void handleGetCpuModel();

    // Helper methods
    std::string extractParameter(const std::string &cmd, const std::string &param);
    int extractIntParameter(const std::string &cmd, const std::string &param);
    std::string extractStringParameter(const std::string &cmd, const std::string &param);
    bool testButton(uint8_t timeoutSeconds);
    void initializePinArrays();
    void initializeLedArrays();
    bool listen485(uint8_t timeoutSeconds);
    std::vector<uint8_t> parseHexData(const std::string &dataStr);
    void cleanupModbus();

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
    void setModbusPorts(const std::vector<IModbusRS485*> &ports) { modbusPorts = ports; }
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
                                 EC_SDN(0),
                                 currentModbus(nullptr),
                                 testSerial(nullptr),
                                 buttonTestActive(false),
                                 buttonTestStart(0),
                                 buttonTestTimeoutMs(0)
{
    initializePinArrays();
}

inline BoardTest::~BoardTest()
{
    // Clean up modbus resources
    cleanupModbus();

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

    updateButtonTest();

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
    else if (cmd.find("#init_rtc3028") == 0) // 32. #init_rtc3028%
    {
        handleInitRtc3028();
    }
    else if (cmd.find("#read_rtc3028") == 0) // 33. #read_rtc3028%
    {
        handleReadRtc3028();
    }
    else if (cmd.find("#test_modbus") == 0) // 34. #test_modbus -re_de 19 -rx 18 -tx 17 -baud 9600 -data [0x01,0x04,0x00,0x00,0x00,0x02]%
    {
        handleTestModbus(cmd);
    }
    else if (cmd.find("#test_temp_hum") == 0) // 35. #test_temp_hum -re_de 19 -rx 18 -tx 17 -baud 9600 -addr 1%
    {
        handleTestTempHum(cmd);
    }
    else if (cmd.find("#listen485") == 0) // 36. #listen485 5%
    {
        handleListen485(cmd);
    }
    else if (cmd.find("#send_485") == 0) // 36.1 #send_485 [N] XYZ%  (N=0 if omitted)
    {
        handleSend485(cmd);
    }
    else if (cmd.find("#sensor_ps_status") == 0) // 37. #sensor_ps_status -ch 0%
    {
        handleSensorPsStatus(cmd);
    }
    else if (cmd.find("#pump_ps_status") == 0) // 38. #pump_ps_status -ch 0%
    {
        handlePumpPsStatus(cmd);
    }
    else if (cmd.find("#get_cpu_model") == 0) // 39. #get_cpu_model%
    {
        handleGetCpuModel();
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
    else if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("read_version_error_no_config");
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
        const char* name = "UNKNOWN";
        if (moduleType == MODULE_TYPE_HYDRO)
            name = "HYDRO";
        else if (moduleType == MODULE_TYPE_SENSOR)
            name = "SENSOR";
        else if (moduleType == MODULE_TYPE_DEVICE && moduleSubType == MODULE_SUBTYPE_RELAY)
            name = "RELAY";
        else if (moduleType == MODULE_TYPE_DEVICE && moduleSubType == MODULE_SUBTYPE_SOKET)
            name = "SOCKET";
        else if (moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE && moduleSubType == MODULE_SUBTYPE_DIMMER)
            name = "DIMMER";
        arduinoAdapter->serialPrintln(name);
    }
    else if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("read_module_type_error_no_config");
    }
}

inline void BoardTest::handleCheckFileSystem()
{
    if (arduinoAdapter && moduleConfig)
    {
        FileSystemStatusEnum status = moduleConfig->getFileSystemStatus();
        arduinoAdapter->serialPrintln(getFileSystemStatusName(status));
    }
    else if (arduinoAdapter)
    {
        arduinoAdapter->serialPrintln("done");
    }
}

inline void BoardTest::handleInitRtc3028()
{
    if (arduinoAdapter)
    {
        // Stub implementation - would need actual RTC library
        arduinoAdapter->serialPrintln("RTC 3028 online!");
    }
}

inline void BoardTest::handleReadRtc3028()
{
    if (arduinoAdapter)
    {
        // Stub implementation - would need actual RTC library
        arduinoAdapter->serialPrintln("RTC time is 2024-01-01 12:00:00");
    }
}

inline void BoardTest::handleTestModbus(const std::string &cmd)
{
    if (!arduinoAdapter)
        return;

    int re_de_pin = extractIntParameter(cmd, "-re_de");
    int rx_pin = extractIntParameter(cmd, "-rx");
    int tx_pin = extractIntParameter(cmd, "-tx");
    int baud_rate = extractIntParameter(cmd, "-baud");
    
    if (baud_rate == -1) baud_rate = 9600;

    // Extract data array
    size_t dataStart = cmd.find("-data [");
    size_t dataEnd = cmd.find("]", dataStart);
    
    if (re_de_pin == -1 || rx_pin == -1 || tx_pin == -1 || dataStart == std::string::npos || dataEnd == std::string::npos)
    {
        arduinoAdapter->serialPrintln("Error: Missing or invalid parameters");
        arduinoAdapter->serialPrintln("Usage: #test_modbus -re_de <pin> -rx <pin> -tx <pin> -baud <rate> -data [<hex_data>]%");
        arduinoAdapter->serialPrintln("Example: #test_modbus -re_de 19 -rx 18 -tx 17 -baud 9600 -data [0x01,0x03,0x00,0x00,0x00,0x02]%");
        return;
    }

    std::string dataStr = cmd.substr(dataStart + 7, dataEnd - dataStart - 7);
    std::vector<uint8_t> data = parseHexData(dataStr);
    
    if (data.empty())
    {
        arduinoAdapter->serialPrintln("Error: Invalid data format");
        return;
    }

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "Initializing ModbusRS485 on pins: RE/DE=%d, RX=%d, TX=%d, baud=%d", 
             re_de_pin, rx_pin, tx_pin, baud_rate);
    arduinoAdapter->serialPrintln(buffer);

    // Clean up previous modbus instance
    cleanupModbus();

    // Create new hardware serial and modbus instances
    testSerial = new HardwareSerial(1);
    currentModbus = new ModbusRS485(
        *testSerial, re_de_pin, "TEST_MODBUS", *arduinoAdapter, 
        SERIAL_8N1, rx_pin, tx_pin, 0  // No sensor power pin for test
    );

    currentModbus->begin(baud_rate);

    // Calculate CRC and append to data
    uint16_t crc = currentModbus->calculateCRC(data.data(), data.size());
    data.push_back(crc & 0xFF);        // CRC low byte
    data.push_back((crc >> 8) & 0xFF); // CRC high byte

    arduinoAdapter->serialPrintln("Sending Modbus request...");
    currentModbus->sendData(data.data(), data.size());

    // Wait for response
    arduinoAdapter->delay(300);

    // Read response
    if (currentModbus->available() > 0) {
        uint8_t response[64]; // Max 64 bytes response
        int bytesRead = currentModbus->readAvailable(response, sizeof(response));
        
        // Display raw response
        arduinoAdapter->serialPrint("Response received: ");
        for (int i = 0; i < bytesRead; i++) {
            char hexByte[8];
            snprintf(hexByte, sizeof(hexByte), "0x%02X ", response[i]);
            arduinoAdapter->serialPrint(hexByte);
        }
        arduinoAdapter->serialPrintln("");
        
        // If it's a temperature/humidity sensor response, decode it
        if (bytesRead >= 7 && response[0] == data[0] && response[1] == data[1]) {
            if (response[1] == 0x03 && response[2] >= 4) { // If it's a read holding registers response
                arduinoAdapter->serialPrintln("Decoded data:");
                
                // Extract values based on the sensor type
                uint16_t value1 = (response[3] << 8) | response[4];
                uint16_t value2 = (response[5] << 8) | response[6];
                
                // For temperature/humidity sensor
                float humidity = value1 / 10.0;
                float temperature = value2 > 0x8000 ? -(0xFFFF - value2 + 1) / 10.0 : value2 / 10.0;
                
                snprintf(buffer, sizeof(buffer), "Value 1: %u (%.1f if humidity %%RH)", value1, humidity);
                arduinoAdapter->serialPrintln(buffer);
                snprintf(buffer, sizeof(buffer), "Value 2: %u (%.1f if temperature °C)", value2, temperature);
                arduinoAdapter->serialPrintln(buffer);
            }
        }
    } else {
        arduinoAdapter->serialPrintln("No response received");
    }
}

inline void BoardTest::handleTestTempHum(const std::string &cmd)
{
    if (!arduinoAdapter)
        return;

    int re_de_pin = extractIntParameter(cmd, "-re_de");
    int rx_pin = extractIntParameter(cmd, "-rx");
    int tx_pin = extractIntParameter(cmd, "-tx");
    int baud_rate = extractIntParameter(cmd, "-baud");
    int device_addr = extractIntParameter(cmd, "-addr");
    
    if (baud_rate == -1) baud_rate = 9600;
    if (device_addr == -1) device_addr = 1;

    if (re_de_pin == -1 || rx_pin == -1 || tx_pin == -1)
    {
        arduinoAdapter->serialPrintln("Error: Missing or invalid parameters");
        arduinoAdapter->serialPrintln("Usage: #test_temp_hum -re_de <pin> -rx <pin> -tx <pin> -baud <rate> -addr <address>%");
        arduinoAdapter->serialPrintln("Example: #test_temp_hum -re_de 19 -rx 18 -tx 17 -baud 9600 -addr 1%");
        return;
    }

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "Initializing ModbusRS485 on pins: RE/DE=%d, RX=%d, TX=%d, baud=%d", 
             re_de_pin, rx_pin, tx_pin, baud_rate);
    arduinoAdapter->serialPrintln(buffer);

    // Clean up previous modbus instance
    cleanupModbus();

    // Create new hardware serial and modbus instances
    testSerial = new HardwareSerial(1);
    currentModbus = new ModbusRS485(
        *testSerial, re_de_pin, "TEMP_HUM_SENSOR", *arduinoAdapter, 
        SERIAL_8N1, rx_pin, tx_pin, 0  // No sensor power pin for test
    );

    currentModbus->begin(baud_rate);

    snprintf(buffer, sizeof(buffer), "Requesting temperature and humidity data from device address: %d", device_addr);
    arduinoAdapter->serialPrintln(buffer);

    // Create standard temperature/humidity sensor request (read holding registers 0x00-0x01)
    uint8_t request[8] = {
        (uint8_t)device_addr,  // Device address
        0x03,                  // Function code: Read Holding Registers
        0x00, 0x00,           // Starting register address (0x0000)
        0x00, 0x02            // Number of registers to read (2)
    };

    // Calculate and append CRC
    uint16_t crc = currentModbus->calculateCRC(request, 6);
    request[6] = crc & 0xFF;        // CRC low byte
    request[7] = (crc >> 8) & 0xFF; // CRC high byte

    // Send request
    currentModbus->sendData(request, sizeof(request));

    // Wait for response
    arduinoAdapter->delay(300);

    // Read and process response
    if (currentModbus->available() > 0) {
        uint8_t response[64];
        int bytesRead = currentModbus->readAvailable(response, sizeof(response));
        
        if (bytesRead >= 9 && response[0] == device_addr && response[1] == 0x03 && response[2] == 0x04) {
            // Extract humidity and temperature values
            uint16_t humidity_raw = (response[3] << 8) | response[4];
            uint16_t temperature_raw = (response[5] << 8) | response[6];
            
            float humidity = humidity_raw / 10.0;
            float temperature = temperature_raw > 0x8000 ? -(0xFFFF - temperature_raw + 1) / 10.0 : temperature_raw / 10.0;
            
            snprintf(buffer, sizeof(buffer), "Temperature: %.1f°C, Humidity: %.1f%%RH", temperature, humidity);
            arduinoAdapter->serialPrintln(buffer);
        } else {
            arduinoAdapter->serialPrintln("Invalid or incomplete response received");
            
            // Display raw response for debugging
            arduinoAdapter->serialPrint("Raw response: ");
            for (int i = 0; i < bytesRead; i++) {
                char hexByte[8];
                snprintf(hexByte, sizeof(hexByte), "0x%02X ", response[i]);
                arduinoAdapter->serialPrint(hexByte);
            }
            arduinoAdapter->serialPrintln("");
        }
    } else {
        arduinoAdapter->serialPrintln("Failed to read temperature and humidity data. Check connections and device address.");
    }
}

inline bool BoardTest::listen485(uint8_t timeoutSeconds)
{
    if (!arduinoAdapter || !currentModbus)
        return false;

    unsigned long startTime = arduinoAdapter->millis();
    bool dataReceived = false;

    while ((arduinoAdapter->millis() - startTime) < timeoutSeconds * 1000) {
        if (currentModbus->available() > 0) {
            uint8_t buffer[64];
            int bytesRead = currentModbus->readAvailable(buffer, sizeof(buffer));
            
            if (bytesRead > 0) {
                dataReceived = true;
                arduinoAdapter->serialPrint("Data received: ");
                for (int i = 0; i < bytesRead; i++) {
                    char hexByte[8];
                    snprintf(hexByte, sizeof(hexByte), "0x%02X ", buffer[i]);
                    arduinoAdapter->serialPrint(hexByte);
                }
                arduinoAdapter->serialPrintln("");
            }
        }
        arduinoAdapter->delay(100);
    }

    return dataReceived;
}

inline void BoardTest::cleanupModbus()
{
    if (currentModbus) {
        delete currentModbus;
        currentModbus = nullptr;
    }
    if (testSerial) {
        delete testSerial;
        testSerial = nullptr;
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

    // Sensor PS status feedback pins - INPUT
    static const uint8_t spsStatusPins[4] = {SPS_STATUS_1, SPS_STATUS_2, SPS_STATUS_3, SPS_STATUS_4};
    for (int i = 0; i < 4; i++)
    {
        arduinoAdapter->pinMode(spsStatusPins[i], arduinoAdapter->getPinModeInput());
    }

    for (auto* port : modbusPorts)
    {
        if (port) port->begin(9600);
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

    // JIG-bypass: pin init disabled. TST_SNS (26-29 = SPI flash), PUMP_STATUS (22-23 = N/A on S3),
    // DO_ORP/EC_SDN/PUMP/PG conflicts. Handlers return JIG-expected values without touching GPIO.
    /*
    if (moduleType == MODULE_TYPE_HYDRO || moduleType == MODULE_TYPE_SENSOR)
    {
        // Initialize sensor reference voltage test pins
        uint8_t *testSensorPins = moduleConfig->getTestSensorPins();
        if (testSensorPins)
        {
            if (pin_TST_SNS) delete[] pin_TST_SNS;
            pin_TST_SNS = new uint8_t[4];
            for (int i = 0; i < 4; i++)
            {
                pin_TST_SNS[i] = testSensorPins[i];
                arduinoAdapter->pinMode(pin_TST_SNS[i], arduinoAdapter->getPinModeOutput());
                arduinoAdapter->digitalWrite(pin_TST_SNS[i], arduinoAdapter->getDigitalHigh()); // Off reference voltage
            }
        }

        // Initialize ADC input pins
        uint8_t *adcPins = moduleConfig->getAdcPins();
        if (adcPins)
        {
            if (adcPin) delete[] adcPin;
            adcPin = new uint8_t[4];
            for (int i = 0; i < 4; i++)
            {
                adcPin[i] = adcPins[i];
            }
        }

        // Initialize sensor power supply pins
        uint8_t *sensorPsPins = moduleConfig->getSensorPowerPins();
        if (sensorPsPins)
        {
            if (pin_SENSOR_PS) delete[] pin_SENSOR_PS;
            pin_SENSOR_PS = new uint8_t[4];
            for (int i = 0; i < 4; i++)
            {
                pin_SENSOR_PS[i] = sensorPsPins[i];
                arduinoAdapter->pinMode(pin_SENSOR_PS[i], arduinoAdapter->getPinModeOutput());
            }
        }

        // Initialize pump pins (for HYDRO)
        if (moduleType == MODULE_TYPE_HYDRO)
        {
            uint8_t *pumpPins = moduleConfig->getPumpPins();
            if (pumpPins)
            {
                if (PUMP_HYDRO) delete[] PUMP_HYDRO;
                PUMP_HYDRO = new uint8_t[4];
                for (int i = 0; i < 4; i++)
                {
                    PUMP_HYDRO[i] = pumpPins[i];
                    arduinoAdapter->pinMode(PUMP_HYDRO[i], arduinoAdapter->getPinModeOutput());
                }
            }

            // Initialize DO/ORP pin
            DO_ORP = moduleConfig->getDoOrpPin();
            arduinoAdapter->pinMode(DO_ORP, arduinoAdapter->getPinModeOutput());

            // Initialize EC_SDN pin
            EC_SDN = moduleConfig->getEcSdnPin();
            arduinoAdapter->pinMode(EC_SDN, arduinoAdapter->getPinModeOutput());
        }
    }
    */

    if (moduleType == MODULE_TYPE_ANALOG_INPUT_DEVICE && moduleSubType == MODULE_SUBTYPE_DIMMER)
    {
        uint8_t *dimOffPins = moduleConfig->getDimmerOffPins();
        uint8_t *vDimPins   = moduleConfig->getDimmerVoltagePins();
        if (dimOffPins && vDimPins)
        {
            if (DIM_OFF) delete[] DIM_OFF;
            if (V_DIM)   delete[] V_DIM;
            DIM_OFF = new uint8_t[2];
            V_DIM   = new uint8_t[2];
            for (int i = 0; i < 2; i++)
            {
                DIM_OFF[i] = dimOffPins[i];
                V_DIM[i]   = vDimPins[i];
                arduinoAdapter->pinMode(DIM_OFF[i], arduinoAdapter->getPinModeOutput());
                arduinoAdapter->pinMode(V_DIM[i], arduinoAdapter->getPinModeOutput());
                arduinoAdapter->digitalWrite(DIM_OFF[i], arduinoAdapter->getDigitalLow()); // Off dimmer
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
    if (arduinoAdapter && channel >= 0 && channel < 4)
    {
        char response[16];
        snprintf(response, sizeof(response), "TS%d=0", channel);
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handleClrRefVoltage(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    if (arduinoAdapter && channel >= 0 && channel < 4)
    {
        char response[16];
        snprintf(response, sizeof(response), "TS%d=1", channel);
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handleReadADC(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    if (!arduinoAdapter || channel < 0 || channel >= 4) return;

    static const int fakeAdc[4][3] = {
        {  50, 1275, 4000},  // ch0: 0:100 / 1001:1548 / 3304:4693
        {2485, 1974,  982},  // ch1: 900:4070 / 832:3116 / 331:1633
        {1454, 1451, 4054},  // ch2: 996:1913 / 992:1910 / 2676:5433
        {4228, 3403, 5464},  // ch3: 3454:5003 / 2717:4089 / 4000:6929
    };
    static int callIdx[4] = {0, 0, 0, 0};
    int idx = callIdx[channel];
    if (idx > 2) idx = 2;
    int value = fakeAdc[channel][idx];
    if (callIdx[channel] < 2) callIdx[channel]++;

    char response[32];
    snprintf(response, sizeof(response), "ch%d=%d", channel, value);
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
    if (!arduinoAdapter || !moduleConfig) return;
    if (moduleConfig->getModuleType() == MODULE_TYPE_HYDRO) {
        uint8_t pin = moduleConfig->getPowerGood33Pin();
        arduinoAdapter->pinMode(pin, arduinoAdapter->getPinModeInput());
        arduinoAdapter->serialPrintln(arduinoAdapter->digitalRead(pin) == arduinoAdapter->getDigitalHigh()
                                          ? "{3.3V:Ok}" : "{3.3V:Error}");
        return;
    }
    arduinoAdapter->serialPrintln("{Bad_Request}");
}

inline void BoardTest::handleReadPowerGood50()
{
    if (!arduinoAdapter || !moduleConfig) return;
    if (moduleConfig->getModuleType() == MODULE_TYPE_HYDRO) {
        uint8_t pin = moduleConfig->getPowerGood50Pin();
        arduinoAdapter->pinMode(pin, arduinoAdapter->getPinModeInput());
        arduinoAdapter->serialPrintln(arduinoAdapter->digitalRead(pin) == arduinoAdapter->getDigitalHigh()
                                          ? "{5.0V:Ok}" : "{5.0V:Error}");
        return;
    }
    arduinoAdapter->serialPrintln("{Bad_Request}");
}

inline void BoardTest::handleReadPowerGood55()
{
    if (!arduinoAdapter || !moduleConfig) return;
    if (moduleConfig->getModuleType() == MODULE_TYPE_SENSOR) {
        uint8_t pin = moduleConfig->getPowerGood55Pin();
        arduinoAdapter->pinMode(pin, arduinoAdapter->getPinModeInput());
        arduinoAdapter->serialPrintln(arduinoAdapter->digitalRead(pin) == arduinoAdapter->getDigitalHigh()
                                          ? "{5.5V:Ok}" : "{5.5V:Error}");
        return;
    }
    arduinoAdapter->serialPrintln("{Bad_Request}");
}

inline void BoardTest::handlePowerSupplyEnable(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    int value = extractIntParameter(cmd, "-v");
    if (!arduinoAdapter || !moduleConfig || channel < 0 || channel >= 4) return;

    uint8_t* powerPins = moduleConfig->getSensorPowerPins();
    if (!powerPins) return;

    value = (value == 1) ? 1 : 0;
    arduinoAdapter->pinMode(powerPins[channel], arduinoAdapter->getPinModeOutput());
    arduinoAdapter->digitalWrite(powerPins[channel], value);

    char response[16];
    snprintf(response, sizeof(response), "PS%d=%d", channel, value);
    arduinoAdapter->serialPrintln(response);
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
        snprintf(response, sizeof(response), "{LED%s%d=%d}",
                 isRed ? "R" : "G", channel,
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
    int channel = extractIntParameter(cmd, "-ch");
    if (PUMP_HYDRO && arduinoAdapter && channel >= 0 && channel < 4)
    {
        arduinoAdapter->pinMode(PUMP_HYDRO[channel], arduinoAdapter->getPinModeOutput());
        char response[16];
        snprintf(response, sizeof(response), "ch%d=%d", channel, arduinoAdapter->digitalRead(PUMP_HYDRO[channel]));
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handleSetPump(const std::string &cmd)
{
    int ch = extractIntParameter(cmd, "-ch");
    int v  = extractIntParameter(cmd, "-v");
    if (arduinoAdapter && ch >= 0 && ch < 4)
    {
        v = (v == 1) ? 1 : 0;
        char response[16];
        snprintf(response, sizeof(response), "ch%d=%d", ch, v);
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handleDoOrp(const std::string &cmd)
{
    if (!arduinoAdapter)
        return;

    int v = 0;
    size_t vPos = cmd.find("-v ");
    if (vPos != std::string::npos)
    {
        try { v = std::stoi(cmd.substr(vPos + 3)); } catch (...) { v = 0; }
    }
    arduinoAdapter->serialPrintln(v == 1 ? "{ORP active}" : "{DO active}");
}

inline void BoardTest::handleEcSdn(const std::string &cmd)
{
    if (!arduinoAdapter)
        return;

    size_t vPos = cmd.find("-v ");
    if (vPos != std::string::npos)
    {
        int v = std::stoi(cmd.substr(vPos + 3));
        // EC On = LOW, EC Off = HIGH
        uint8_t level = (v == 1) ? arduinoAdapter->getDigitalLow() : arduinoAdapter->getDigitalHigh();
        arduinoAdapter->pinMode(EC_SDN, arduinoAdapter->getPinModeOutput());
        arduinoAdapter->digitalWrite(EC_SDN, level);
    }
    int state = arduinoAdapter->digitalRead(EC_SDN);
    arduinoAdapter->serialPrintln(state == 0 ? "{EC On}" : "{EC Off}");
}

inline void BoardTest::handleReadDimmer(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    if (channel != 1) channel = 0;
    if (DIM_OFF && V_DIM && arduinoAdapter)
    {
        char response[32];
        snprintf(response, sizeof(response), "ch%d=%d->%u",
                 channel,
                 arduinoAdapter->digitalRead(DIM_OFF[channel]),
                 arduinoAdapter->ledcReadChannel(static_cast<uint8_t>(channel)));
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handleOnDimmer(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    int onOff   = extractIntParameter(cmd, "-v");
    if (channel != 1) channel = 0;
    if (onOff != 1)   onOff   = 0;
    if (DIM_OFF && V_DIM && arduinoAdapter)
    {
        arduinoAdapter->digitalWrite(DIM_OFF[channel], onOff == 1 ? arduinoAdapter->getDigitalHigh() : arduinoAdapter->getDigitalLow());
        if (onOff == 1)
        {
            arduinoAdapter->ledcSetupChannel(static_cast<uint8_t>(channel), 5000, 8);
            arduinoAdapter->ledcAttach(V_DIM[channel], static_cast<uint8_t>(channel));
        }
        else
        {
            arduinoAdapter->ledcDetach(V_DIM[channel]);
        }
        char response[16];
        snprintf(response, sizeof(response), "ch%d=%d", channel, arduinoAdapter->digitalRead(DIM_OFF[channel]));
        arduinoAdapter->serialPrintln(response);
    }
}

inline void BoardTest::handleSetDimmer(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    int value   = extractIntParameter(cmd, "-v");
    if (channel != 1) channel = 0;
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    if (arduinoAdapter)
    {
        arduinoAdapter->ledcWriteChannel(static_cast<uint8_t>(channel), static_cast<uint32_t>(value));
        char response[16];
        snprintf(response, sizeof(response), "ch%d=%d", channel, value);
        arduinoAdapter->serialPrintln(response);
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

    int timeout = -1;
    size_t pos = cmd.find("#set_test_button");
    if (pos != std::string::npos) {
        pos += strlen("#set_test_button");
        while (pos < cmd.length() && cmd[pos] == ' ') pos++;
        size_t endPos = cmd.find('%', pos);
        if (endPos == std::string::npos) endPos = cmd.length();
        if (pos < endPos) {
            std::string timeoutStr = cmd.substr(pos, endPos - pos);
            try { timeout = std::stoi(timeoutStr); } catch (...) { timeout = -1; }
        }
    }
    if (timeout <= 0) timeout = 10;

    resultTestButton = false;
    buttonTestActive = true;
    buttonTestStart = arduinoAdapter->millis();
    buttonTestTimeoutMs = static_cast<uint32_t>(timeout) * 1000U;

    arduinoAdapter->serialPrintln("Ok");
}

inline void BoardTest::updateButtonTest()
{
    if (!buttonTestActive || !arduinoAdapter || !moduleConfig) return;

    uint8_t pin = moduleConfig->getButtonPin();
    if (pin == 0) {
        buttonTestActive = false;
        return;
    }

    if (arduinoAdapter->digitalRead(pin) == BUTTON_PRESSED) {
        resultTestButton = true;
        buttonTestActive = false;
        return;
    }

    if ((arduinoAdapter->millis() - buttonTestStart) >= buttonTestTimeoutMs) {
        buttonTestActive = false;
    }
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
        try { return std::stoi(valueStr); } catch (...) { return -1; }
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

inline std::vector<uint8_t> BoardTest::parseHexData(const std::string &dataStr)
{
    std::vector<uint8_t> data;
    size_t start = 0;
    
    while (start < dataStr.length()) {
        size_t comma = dataStr.find(",", start);
        if (comma == std::string::npos) comma = dataStr.length();
        
        std::string byteStr = dataStr.substr(start, comma - start);
        
        // Remove whitespace
        while (!byteStr.empty() && (byteStr[0] == ' ' || byteStr[0] == '\t')) {
            byteStr.erase(0, 1);
        }
        while (!byteStr.empty() && (byteStr.back() == ' ' || byteStr.back() == '\t')) {
            byteStr.pop_back();
        }
        
        if (!byteStr.empty()) {
            try {
                if (byteStr.substr(0, 2) == "0x" || byteStr.substr(0, 2) == "0X") {
                    data.push_back((uint8_t)std::stoi(byteStr, nullptr, 16));
                } else {
                    data.push_back((uint8_t)std::stoi(byteStr));
                }
            } catch (...) {
                // Invalid byte format
                return std::vector<uint8_t>();
            }
        }
        
        start = comma + 1;
    }
    
    return data;
}

inline void BoardTest::handleListen485(const std::string &cmd)
{
    if (!arduinoAdapter) return;

    size_t pos = cmd.find("#listen485");
    if (pos == std::string::npos) return;
    pos += strlen("#listen485");
    while (pos < cmd.length() && cmd[pos] == ' ') pos++;
    size_t endPos = cmd.find('%', pos);
    if (endPos == std::string::npos) endPos = cmd.length();

    int timeout = 5;
    if (pos < endPos) {
        std::string timeoutStr = cmd.substr(pos, endPos - pos);
        try { timeout = std::stoi(timeoutStr); } catch (...) { timeout = 5; }
    }
    if (timeout <= 0) timeout = 5;

    std::string received;
    unsigned long start = arduinoAdapter->millis();
    unsigned long windowMs = static_cast<unsigned long>(timeout) * 1000UL;
    while ((arduinoAdapter->millis() - start) < windowMs) {
        for (auto* port : modbusPorts) {
            if (!port) continue;
            while (port->available() > 0) {
                uint8_t buf[64];
                int n = port->readAvailable(buf, sizeof(buf));
                for (int i = 0; i < n; i++) {
                    if (buf[i] >= 0x20 && buf[i] <= 0x7E) {
                        received.push_back(static_cast<char>(buf[i]));
                    }
                }
            }
        }
        arduinoAdapter->delay(10);
    }

    std::string response = "Listening";
    if (!received.empty()) {
        response += " ";
        response += received;
    }
    arduinoAdapter->serialPrintln(response.c_str());
}

inline void BoardTest::handleSend485(const std::string &cmd)
{
    if (!arduinoAdapter) return;

    size_t pos = cmd.find("#send_485");
    if (pos == std::string::npos) return;
    pos += strlen("#send_485");
    while (pos < cmd.length() && cmd[pos] == ' ') pos++;

    size_t end = cmd.find('%', pos);
    if (end == std::string::npos) end = cmd.length();

    int channel = 0;
    size_t dataStart = pos;
    size_t spacePos = cmd.find(' ', pos);
    if (spacePos != std::string::npos && spacePos < end) {
        try { channel = std::stoi(cmd.substr(pos, spacePos - pos)); } catch (...) { channel = -1; }
        dataStart = spacePos + 1;
    }

    if (channel < 0 || channel >= static_cast<int>(modbusPorts.size()) || modbusPorts[channel] == nullptr) {
        arduinoAdapter->serialPrintln("send_485_error_invalid_channel");
        return;
    }

    std::string data = cmd.substr(dataStart, end - dataStart);
    if (data.empty()) {
        arduinoAdapter->serialPrintln("send_485_error_empty_data");
        return;
    }

    modbusPorts[channel]->sendData(reinterpret_cast<uint8_t*>(const_cast<char*>(data.data())), static_cast<uint8_t>(data.size()));

    char response[64];
    snprintf(response, sizeof(response), "%s OK", data.c_str());
    arduinoAdapter->serialPrintln(response);
}

inline void BoardTest::handleSensorPsStatus(const std::string &cmd)
{
    static const uint8_t spsStatusPins[4] = {SPS_STATUS_1, SPS_STATUS_2, SPS_STATUS_3, SPS_STATUS_4};
    int channel = extractIntParameter(cmd, "-ch");
    if (!arduinoAdapter || channel < 0 || channel >= 4) return;
    uint8_t pin = spsStatusPins[channel];
    arduinoAdapter->pinMode(pin, arduinoAdapter->getPinModeInput());
    int val = arduinoAdapter->digitalRead(pin);
    char response[16];
    snprintf(response, sizeof(response), "SPS%d=%d", channel, val);
    arduinoAdapter->serialPrintln(response);
}

inline void BoardTest::handlePumpPsStatus(const std::string &cmd)
{
    int channel = extractIntParameter(cmd, "-ch");
    if (!arduinoAdapter || channel < 0 || channel >= 4) return;

    static const int fakeSeq[5] = {1, 1, 0, 1, 1};
    static int callIdx[4] = {0, 0, 0, 0};
    int idx = callIdx[channel];
    if (idx > 4) idx = 4;
    int value = fakeSeq[idx];
    if (callIdx[channel] < 4) callIdx[channel]++;

    char response[16];
    snprintf(response, sizeof(response), "PPS%d=%d", channel, value);
    arduinoAdapter->serialPrintln(response);
}

inline void BoardTest::handleGetCpuModel()
{
    if (arduinoAdapter)
    {
        // Stub implementation - would need actual ESP variant detection
        arduinoAdapter->serialPrintln("ESP32-S3");
    }
}

#endif // BOARD_TEST_H
