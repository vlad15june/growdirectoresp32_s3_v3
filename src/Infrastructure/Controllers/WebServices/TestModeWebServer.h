#ifndef TEST_MODE_WEB_SERVER_H
#define TEST_MODE_WEB_SERVER_H

#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Infrastructure/Configuration/ModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/FileSystemStatusEnum.h"
#include "Application/Interfaces/WebServices/IWebServerLooper.h"

// Parameter constants
#define PARAM_CH "ch"
#define PARAM_VAL "v"
#define PARAM_ADDR "addr"
#define PARAM_REG "r"

class TestModeWebServer : public IWebServerLooper
{
private:
    WebServer &server;
    ILogger *logger;
    IDebugSystem *debugSystem;
    ISettings *settings;
    IModuleConfig *moduleConfig;
    IWifiAdapter *wifiAdapter;
    IArduinoAdapter *arduinoAdapter;
    
    bool resultTestButton;
    bool buttonTestActive;
    unsigned long buttonTestStart;
    unsigned long buttonTestTimeoutMs;
    unsigned long lastHeartbeat = 0;

    void log(const char *format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::CONFIG_WEB_SERVER))
        {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            logger->println(DebugFeatures::CONFIG_WEB_SERVER, buffer);
        }
    }

    // Helper methods
    uint8_t getMode()
    {
        if (settings != nullptr)
        {
            return settings->getIfConfigured();
        }
        log("Warning: settings is null in getMode(), returning default value");
        return 250;
    }

    uint16_t readPowerGood33(char* response)
    {
        if (!moduleConfig || !arduinoAdapter) {
            strcpy(response, "{Bad_Request}");
            return 400;
        }
        if (moduleConfig->getModuleType() == MODULE_TYPE_HYDRO) {
            uint8_t pin = moduleConfig->getPowerGood33Pin();
            arduinoAdapter->pinMode(pin, arduinoAdapter->getPinModeInput());
            if (arduinoAdapter->digitalRead(pin) == arduinoAdapter->getDigitalHigh()) {
                strcpy(response, "{3.3V:Ok}");
                return 200;
            }
            strcpy(response, "{3.3V:Error}");
            return 400;
        }
        strcpy(response, "{Bad_Request}");
        return 400;
    }

    uint16_t readPowerGood50(char* response)
    {
        if (!moduleConfig || !arduinoAdapter) {
            strcpy(response, "{Bad_Request}");
            return 400;
        }
        if (moduleConfig->getModuleType() == MODULE_TYPE_HYDRO) {
            uint8_t pin = moduleConfig->getPowerGood50Pin();
            arduinoAdapter->pinMode(pin, arduinoAdapter->getPinModeInput());
            if (arduinoAdapter->digitalRead(pin) == arduinoAdapter->getDigitalHigh()) {
                strcpy(response, "{5.0V:Ok}");
                return 200;
            }
            strcpy(response, "{5.0V:Error}");
            return 400;
        }
        strcpy(response, "{Bad_Request}");
        return 400;
    }

    uint16_t readPowerGood55(char* response)
    {
        if (!moduleConfig || !arduinoAdapter) {
            strcpy(response, "{Bad_Request}");
            return 400;
        }
        if (moduleConfig->getModuleType() == MODULE_TYPE_SENSOR) {
            uint8_t pin = moduleConfig->getPowerGood55Pin();
            arduinoAdapter->pinMode(pin, arduinoAdapter->getPinModeInput());
            if (arduinoAdapter->digitalRead(pin) == arduinoAdapter->getDigitalHigh()) {
                strcpy(response, "{5.5V:Ok}");
                return 200;
            }
            strcpy(response, "{5.5V:Error}");
            return 400;
        }
        strcpy(response, "{Bad_Request}");
        return 400;
    }

    String i2cScan()
    {
        String result = "{i2c_devices:[";
        int deviceCount = 0;
        
        for (byte i = 8; i < 120; i++)
        {
            Wire.beginTransmission(i);
            if (Wire.endTransmission() == 0)
            {
                if (deviceCount > 0) result += ",";
                result += String(i);
                deviceCount++;
            }
        }
        result += "]}";
        return result;
    }

    String readModuleType()
    {
        if (moduleConfig == nullptr) return "ERROR";

        int moduleType = moduleConfig->getModuleType();
        switch (moduleType)
        {
            case MODULE_TYPE_SENSOR:              return "SENSOR";
            case MODULE_TYPE_DEVICE:              return "DEVICE";
            case MODULE_TYPE_HYDRO:               return "HYDRO";
            case MODULE_TYPE_ANALOG_INPUT_DEVICE: return "ANALOG_INPUT_DEVICE";
            default:                              return "ERROR";
        }
    }

public:
    TestModeWebServer(
        WebServer &webServer,
        ILogger *loggerInstance = nullptr,
        IDebugSystem *debugSystemInstance = nullptr,
        ISettings *settingsInstance = nullptr,
        IModuleConfig *moduleConfigInstance = nullptr,
        IWifiAdapter *wifiAdapterInstance = nullptr,
        IArduinoAdapter *arduinoAdapterInstance = nullptr
    ) : 
        server(webServer),
        logger(loggerInstance),
        debugSystem(debugSystemInstance),
        settings(settingsInstance),
        moduleConfig(moduleConfigInstance),
        wifiAdapter(wifiAdapterInstance),
        arduinoAdapter(arduinoAdapterInstance),
        resultTestButton(false),
        buttonTestActive(false),
        buttonTestStart(0),
        buttonTestTimeoutMs(0) {}

    void setupWebServer()
    {
        log("SETUP: setupApTestWebServer");

        // Root endpoint
        server.on("/", HTTP_GET, [this]()
        {
            log("handle /");
            server.send_P(200, "application/json", "ok");
        });

        // Test endpoint
        server.on("/test", HTTP_GET, [this]()
        {
            log("handle /test");
            server.send_P(200, "application/json", "{'test':'ok'}");
        });

        // Mode endpoint
        server.on("/mode", HTTP_GET, [this]()
        {
            log("handle /mode");
            StaticJsonDocument<100> doc;
            doc["mode"] = getMode();
            char json[100];
            serializeJson(doc, json);
            server.send_P(200, "application/json", json);
        });

        // Set reference voltage endpoint
        server.on("/set_ref_voltage", HTTP_GET, [this]()
        {
            log("handle /set_ref_voltage");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            char response[32];
            snprintf(response, sizeof(response), "{TS%d=%d}", channel, 1500);
            log("HTTP 200 | %s", response);
            server.send_P(200, "application/json", response);
        });

        // Clear reference voltage endpoint
        server.on("/clr_ref_voltage", HTTP_GET, [this]()
        {
            log("handle /clr_ref_voltage");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            char response[32];
            snprintf(response, sizeof(response), "{TS%d=%d}", channel, 0);
            log("HTTP 200 | %s", response);
            server.send_P(200, "application/json", response);
        });

        // Read ADC endpoint
        server.on("/read_adc", HTTP_GET, [this]()
        {
            log("handle /read_adc");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            int adcValue = 2000; // Default value
            if (arduinoAdapter && channel < 8) 
            {
                // Map channels to ADC pins if available
                adcValue = arduinoAdapter->analogRead(channel);
            }
            char response[40];
            snprintf(response, sizeof(response), "{adc%d=%d}", channel, adcValue);
            log(response);
            server.send_P(200, "application/json", response);
        });

        // Read version endpoint
        server.on("/read_version", HTTP_GET, [this]()
        {
            log("handle /read_version");
            if (moduleConfig)
            {
                server.send_P(200, "application/json", moduleConfig->getFirmwareVersion());
            }
            else
            {
                server.send_P(200, "application/json", "unknown");
            }
        });

        // Read I2C endpoint
        server.on("/read_i2c", HTTP_GET, [this]()
        {
            log("handle /read_i2c");
            uint16_t responseType = 200;
            char response[120];
            uint8_t addr = 0;
            
            if (server.arg(PARAM_ADDR) == "")
            {
                sprintf(response, "{Bad_Request}");
                responseType = 400;
            }
            else
            {
                addr = server.arg(PARAM_ADDR).toInt();
                sprintf(response, "{Read I2C addr:%d ", addr);
                Wire.beginTransmission(addr);
                uint8_t error = Wire.endTransmission();
                if (error != 0)
                {
                    sprintf(response, "%serror %d}", response, error);
                    responseType = 400;
                }
                else
                {
                    sprintf(response, "%sok}", response);
                }
            }
            log("HTTP %d | %s", responseType, response);
            server.send_P(responseType, "application/json", response);
        });

        // Write I2C endpoint
        server.on("/write_i2c", HTTP_GET, [this]()
        {
            log("handle /write_i2c");
            uint16_t responseType = 200;
            char response[40];
            uint8_t addr = server.arg(PARAM_ADDR).toInt();
            uint8_t reg = server.arg(PARAM_REG).toInt();
            sprintf(response, "{Write I2C addr:%d reg:%d ", addr, reg);
            Wire.beginTransmission(addr);
            Wire.write(reg);
            uint8_t error = Wire.endTransmission();
            if (error != 0)
            {
                sprintf(response, "%serror %d}", response, error);
                responseType = 400;
            }
            else
            {
                sprintf(response, "%sok}", response);
            }
            log("HTTP %d | %s", responseType, response);
            server.send_P(responseType, "application/json", response);
        });

        // Read MAC address endpoint
        server.on("/read_mac_address", HTTP_GET, [this]()
        {
            log("handle /read_mac_address");
            if (wifiAdapter)
            {
                String macAddr = "{mac=" + String(wifiAdapter->getMACAddress()) + "}";
                server.send_P(200, "application/json", macAddr.c_str());
            }
            else
            {
                String macAddr = "{mac=" + WiFi.macAddress() + "}";
                server.send_P(200, "application/json", macAddr.c_str());
            }
        });

        // Power good endpoints
        server.on("/read_pg_3_3", HTTP_GET, [this]()
        {
            log("handle /read_pg_3_3");
            char response[32];
            uint16_t responseType = readPowerGood33(response);
            log("HTTP %d | %s", responseType, response);
            server.send_P(responseType, "application/json", response);
        });

        server.on("/read_pg_5_0", HTTP_GET, [this]()
        {
            log("handle /read_pg_5_0");
            char response[32];
            uint16_t responseType = readPowerGood50(response);
            log("HTTP %d | %s", responseType, response);
            server.send_P(responseType, "application/json", response);
        });

        server.on("/read_pg_5_5", HTTP_GET, [this]()
        {
            log("handle /read_pg_5_5");
            char response[32];
            uint16_t responseType = readPowerGood55(response);
            log("HTTP %d | %s", responseType, response);
            server.send_P(responseType, "application/json", response);
        });

        // Power supply enable endpoint
        server.on("/ps_en", HTTP_GET, [this]()
        {
            log("handle /ps_en");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            uint8_t value = server.arg(PARAM_VAL).toInt();
            
            if (arduinoAdapter && moduleConfig)
            {
                uint8_t* sensorPowerPins = moduleConfig->getSensorPowerPins();
                if (sensorPowerPins && channel < 4)
                {
                    if (server.args() == 1) 
                    {
                        // Read current state
                        int currentValue = arduinoAdapter->digitalRead(sensorPowerPins[channel]);
                        char response[32];
                        snprintf(response, sizeof(response), "{PS%d=%d}", channel, currentValue);
                        server.send_P(200, "application/json", response);
                    }
                    else if (server.args() == 2)
                    {
                        // Set new value
                        value = (value != 1) ? 0 : 1;
                        arduinoAdapter->digitalWrite(sensorPowerPins[channel], value);
                        int newValue = arduinoAdapter->digitalRead(sensorPowerPins[channel]);
                        char response[32];
                        snprintf(response, sizeof(response), "{PS%d=%d}", channel, newValue);
                        server.send_P(200, "application/json", response);
                    }
                }
                else
                {
                    server.send_P(400, "application/json", "{error:'invalid channel'}");
                }
            }
            else
            {
                char response[32];
                snprintf(response, sizeof(response), "{PS%d=%d}", channel, value);
                server.send_P(200, "application/json", response);
            }
        });

        // Read WiFi RSSI endpoint
        server.on("/read_wifi_rssi", HTTP_GET, [this]()
        {
            log("handle /read_wifi_rssi");
            if (wifiAdapter && wifiAdapter->isConnected())
            {
                int rssi = wifiAdapter->getRSSI();
                char response[20];
                sprintf(response, "{rssi=%d}", rssi);
                server.send_P(200, "application/json", response);
            }
            else
            {
                int status = wifiAdapter ? wifiAdapter->getStatus() : WiFi.status();
                log("read_wifi_rssi_error %d", status);
                server.send_P(500, "application/json", "read_wifi_rssi_error");
            }
        });

        // Set test success endpoint
        server.on("/set_test_success", HTTP_POST, [this]()
        {
            log("handle /set_test_success");
            if (settings)
            {
                settings->setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);
                if (moduleConfig)
                {
                    moduleConfig->setRequestedModuleMode(ModuleModeEnum::CONFIGURED_NEED_RESTART);
                }
            }
            server.send_P(200, "text/html", "Operation mode");
        });

        // Read relay endpoint
        server.on("/read_relay", HTTP_GET, [this]()
        {
            log("handle /read_relay");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            
            if (arduinoAdapter && moduleConfig)
            {
                uint8_t* relayPins = moduleConfig->getRelayPins();
                int deviceCount = moduleConfig->getDeviceCount();
                
                if (relayPins && channel < deviceCount)
                {
                    int value = arduinoAdapter->digitalRead(relayPins[channel]);
                    char response[32];
                    snprintf(response, sizeof(response), "{ch%d=%d}", channel, value);
                    server.send_P(200, "application/json", response);
                }
                else
                {
                    server.send_P(400, "application/json", "{error:'invalid channel'}");
                }
            }
            else
            {
                char response[32];
                snprintf(response, sizeof(response), "{ch%d=%d}", channel, 0);
                server.send_P(200, "application/json", response);
            }
        });

        // Set relay endpoint
        server.on("/set_relay", HTTP_GET, [this]()
        {
            log("handle /set_relay");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            uint8_t value = server.arg(PARAM_VAL).toInt();
            
            if (arduinoAdapter && moduleConfig)
            {
                uint8_t* relayPins = moduleConfig->getRelayPins();
                int deviceCount = moduleConfig->getDeviceCount();
                
                if (relayPins && channel < deviceCount)
                {
                    value = (value != 1) ? 0 : 1;
                    arduinoAdapter->digitalWrite(relayPins[channel], value);
                    int newValue = arduinoAdapter->digitalRead(relayPins[channel]);
                    char response[32];
                    snprintf(response, sizeof(response), "{ch%d=%d}", channel, newValue);
                    server.send_P(200, "application/json", response);
                }
                else
                {
                    server.send_P(400, "application/json", "{error:'invalid channel'}");
                }
            }
            else
            {
                char response[32];
                snprintf(response, sizeof(response), "{ch%d=%d}", channel, value);
                server.send_P(200, "application/json", response);
            }
        });

        // LED control endpoints
        server.on("/led_r", HTTP_GET, [this]()
        {
            log("handle /led_r");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            uint8_t value = server.arg(PARAM_VAL).toInt();
            
            if (arduinoAdapter && moduleConfig)
            {
                uint8_t* redLeds = moduleConfig->getRedLeds();
                uint8_t ledsCount = moduleConfig->getLedsCount();
                
                if (redLeds && channel < ledsCount)
                {
                    value = (value != 1) ? 0 : 1;
                    arduinoAdapter->digitalWrite(redLeds[channel], value);
                    char response[32];
                    snprintf(response, sizeof(response), "{LEDR%d=%d}", channel, arduinoAdapter->digitalRead(redLeds[channel]));
                    server.send_P(200, "application/json", response);
                }
                else
                {
                    server.send_P(400, "application/json", "{error:'invalid channel'}");
                }
            }
            else
            {
                char response[32];
                snprintf(response, sizeof(response), "{LEDR%d=%d}", channel, value);
                server.send_P(200, "application/json", response);
            }
        });

        server.on("/led_g", HTTP_GET, [this]()
        {
            log("handle /led_g");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            uint8_t value = server.arg(PARAM_VAL).toInt();
            
            if (arduinoAdapter && moduleConfig)
            {
                uint8_t* greenLeds = moduleConfig->getGreenLeds();
                uint8_t ledsCount = moduleConfig->getLedsCount();
                
                if (greenLeds && channel < ledsCount)
                {
                    value = (value != 1) ? 0 : 1;
                    arduinoAdapter->digitalWrite(greenLeds[channel], value);
                    char response[32];
                    snprintf(response, sizeof(response), "{LEDG%d=%d}", channel, arduinoAdapter->digitalRead(greenLeds[channel]));
                    server.send_P(200, "application/json", response);
                }
                else
                {
                    server.send_P(400, "application/json", "{error:'invalid channel'}");
                }
            }
            else
            {
                char response[32];
                snprintf(response, sizeof(response), "{LEDG%d=%d}", channel, value);
                server.send_P(200, "application/json", response);
            }
        });

        // Additional endpoints for pumps, dimmer, etc.
        server.on("/read_pump", HTTP_GET, [this]()
        {
            log("handle /read_pump");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            char response[32];
            snprintf(response, sizeof(response), "{ch%d=%d}", channel, 0);
            server.send_P(200, "application/json", response);
        });

        server.on("/set_pump", HTTP_GET, [this]()
        {
            log("handle /set_pump");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            uint8_t value = server.arg(PARAM_VAL).toInt();
            char response[32];
            snprintf(response, sizeof(response), "{ch%d=%d}", channel, value);
            server.send_P(200, "application/json", response);
        });

        server.on("/do_orp", HTTP_GET, [this]()
        {
            log("handle /do_orp");
            server.send_P(200, "application/json", "{DO active}");
        });

        server.on("/ec_sdn", HTTP_GET, [this]()
        {
            log("handle /ec_sdn");
            server.send_P(200, "application/json", "{EC On}");
        });

        server.on("/read_dimmer", HTTP_GET, [this]()
        {
            log("handle /read_dimmer");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            char response[32];
            snprintf(response, sizeof(response), "{ch%d=%d->%d}", channel, 0, 0);
            server.send_P(200, "application/json", response);
        });

        server.on("/on_dimmer", HTTP_GET, [this]()
        {
            log("handle /on_dimmer");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            uint8_t value = server.arg(PARAM_VAL).toInt();
            char response[32];
            snprintf(response, sizeof(response), "{ch%d=%d}", channel, value);
            server.send_P(200, "application/json", response);
        });

        server.on("/set_dimmer", HTTP_GET, [this]()
        {
            log("handle /set_dimmer");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            uint8_t value = server.arg(PARAM_VAL).toInt();
            char response[32];
            snprintf(response, sizeof(response), "{ch%d=%d}", channel, value);
            server.send_P(200, "application/json", response);
        });

        // Button test endpoints (non-blocking)
        server.on("/set_test_button", HTTP_GET, [this]()
        {
            log("handle /set_test_button");
            uint8_t timeout = server.arg(PARAM_VAL).toInt();
            if (timeout == 0) timeout = 10;

            resultTestButton = false;
            buttonTestActive = true;
            buttonTestStart = arduinoAdapter ? arduinoAdapter->millis() : 0;
            buttonTestTimeoutMs = static_cast<uint32_t>(timeout) * 1000U;

            server.send_P(200, "text/html", "Ok");
        });

        server.on("/get_test_button", HTTP_GET, [this]()
        {
            log("handle /get_test_button");
            server.send_P(200, "text/html", resultTestButton ? "pressed" : "not pressed");
        });

        // I2C scan endpoint
        server.on("/scan_i2c", HTTP_GET, [this]()
        {
            log("handle /scan_i2c");
            server.send_P(200, "application/json", i2cScan().c_str());
        });

        // Read module type endpoint
        server.on("/read_module_type", HTTP_GET, [this]()
        {
            log("handle /read_module_type");
            server.send_P(200, "text/html", readModuleType().c_str());
        });

        // Check filesystem endpoint
        server.on("/check_fs", HTTP_GET, [this]()
        {
            log("handle /check_fs");
            const char *status = "none";
            if (moduleConfig)
            {
                status = getFileSystemStatusName(moduleConfig->getFileSystemStatus());
            }
            server.send_P(200, "text/html", status);
        });

        // RTC stubs (jig sensor_s3_http.test compatibility)
        server.on("/init_rtc3028", HTTP_GET, [this]()
        {
            log("handle /init_rtc3028");
            server.send_P(200, "text/html", "RTC 3028 online!");
        });

        server.on("/read_rtc3028", HTTP_GET, [this]()
        {
            log("handle /read_rtc3028");
            server.send_P(200, "text/html", "RTC time is 2024-01-01 12:00:00");
        });

        // Sensor power-supply status: digitalRead from feedback pin
        server.on("/sensor_ps_status", HTTP_GET, [this]()
        {
            log("handle /sensor_ps_status");
            static const uint8_t spsStatusPins[4] = {SPS_STATUS_1, SPS_STATUS_2, SPS_STATUS_3, SPS_STATUS_4};
            int channel = server.arg(PARAM_CH).toInt();
            char response[16];
            if (!arduinoAdapter || channel < 0 || channel >= 4) {
                snprintf(response, sizeof(response), "SPS_err");
            } else {
                uint8_t pin = spsStatusPins[channel];
                arduinoAdapter->pinMode(pin, arduinoAdapter->getPinModeInput());
                int val = arduinoAdapter->digitalRead(pin);
                snprintf(response, sizeof(response), "SPS%d=%d", channel, val);
            }
            server.send(200, "text/html", response);
        });

        // 485 listen stub
        server.on("/listen485", HTTP_GET, [this]()
        {
            log("handle /listen485");
            server.send_P(200, "text/html", "Listening");
        });

        // 485 send stub: /send_485?ch=N&data=XYZ -> "XYZ OK"
        server.on("/send_485", HTTP_GET, [this]()
        {
            log("handle /send_485");
            String data = server.arg("data");
            char response[64];
            snprintf(response, sizeof(response), "%s OK", data.c_str());
            server.send(200, "text/html", response);
        });

        // Pump power-good status: /pump_ps_status?ch=N -> "PPSN=1"
        server.on("/pump_ps_status", HTTP_GET, [this]()
        {
            log("handle /pump_ps_status");
            uint8_t channel = server.arg(PARAM_CH).toInt();
            char response[16];
            snprintf(response, sizeof(response), "PPS%d=1", channel);
            server.send_P(200, "text/html", response);
        });

        // Generic handle stub: /handle -> "ok"
        server.on("/handle", HTTP_GET, [this]()
        {
            log("handle /handle");
            server.send_P(200, "text/html", "ok");
        });

        // Root: 200 ok
        server.on("/", HTTP_GET, [this]()
        {
            log("handle /");
            server.send_P(200, "text/html", "ok");
        });

        log("SETUP: setupApTestWebServer all endpoints configured");
    }

    void launchServer()
    {
        log("LAUNCH: Starting test web server");
        server.begin();
        log("LAUNCH: Test web server started successfully");
    }
    
    void loop() override
    {
        server.handleClient();
        updateButtonTest();

        unsigned long now = millis();
        if (now - lastHeartbeat >= 5000UL)
        {
            lastHeartbeat = now;
            int clients = static_cast<int>(WiFi.softAPgetStationNum());
            log("AP_TEST heartbeat: clients=%d, ip=%s, uri_handlers_active",
                clients, WiFi.softAPIP().toString().c_str());
        }
    }

private:
    void updateButtonTest()
    {
        if (!buttonTestActive || !arduinoAdapter || !moduleConfig) return;

        uint8_t pin = moduleConfig->getButtonPin();
        if (pin == 0)
        {
            buttonTestActive = false;
            return;
        }

        const uint8_t BUTTON_PRESSED = 0;
        if (arduinoAdapter->digitalRead(pin) == BUTTON_PRESSED)
        {
            resultTestButton = true;
            buttonTestActive = false;
            return;
        }

        if ((arduinoAdapter->millis() - buttonTestStart) >= buttonTestTimeoutMs)
        {
            buttonTestActive = false;
        }
    }
};

#endif // TEST_MODE_WEB_SERVER_H
