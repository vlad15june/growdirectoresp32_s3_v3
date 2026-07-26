#include <ESP32Ping.h>
#include <HTTPClient.h>

DynamicJsonDocument moduleCommands(2000);
DynamicJsonDocument moduleCommandsResult(2000);

bool isCanSendDeviceData() // NOTE: хотел сделать чтобы если осталось мало времени до выключения, то не отправлять данные, но не получилось тк есть случаи когда данные вообще не отпрвляются
{
    for (byte i = 0; i < deviceCount; i++)
    {
        if (control[i].state == DEVICE_STATE_ON)
        {
            if (control[i].needToSwitchBack)
            {
                unsigned long currentDuration = millis() - control[i].timeOn;
                if (currentDuration + 15000 > control[i].durationOn)
                {
                    return false;
                }
            }
        }

        if (control[i].state == DEVICE_STATE_STEP_ON)
        {

            unsigned long currentDuration = millis() - control[i].timeOn;
            if (currentDuration + 15000 > control[i].durationOn)
            {
                return false;
            }
        }
    }
    return true;
}

bool isModuleOnline()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    // Your Domain name with URL path or IP address with path
    String serverName = String(serverUrl) + "/api/v1/HardWare/TestConnection";

    _http->end();
    _http->setTimeout(2000);
    _http->begin(serverName);

    int httpResponseCode = _http->GET();
    _http->end();
    _http->setTimeout(30000);

    if (httpResponseCode > 0)
    {
        return true;
    }
    else
    {
        mqttLogger.printf("PING_ERROR: HTTP Response code: %d\n", httpResponseCode);

        if (_http != NULL)
        {
            _http->end();
            delete _http;
        }
        _http = new HTTPClient();

        return false;
    }
}

boolean checkConnectionWithServer()
{
    // Your Domain name with URL path or IP address with path
    String serverName = String(serverUrl) + "/api/v1/HardWare/TestConnection";

    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        Serial.printf("NET: GET %s\n", serverName.c_str()); // NOTE: for normal usage

        sprintf(log_buf, "GET %s", serverName.c_str());
        writeLog("NET", log_buf);
    }
    else
    {
        Serial.printf("NET: GET=%s\n", serverName.c_str()); // NOTE: for setup
    }

    _http->end();
    delete _http;
    _http = new HTTPClient();
    _http->setTimeout(30000);
    _http->begin(serverName);

    int httpResponseCode = _http->GET();
    _http->end();
    delete _http;
    _http = new HTTPClient();

    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        Serial.printf("NET: HTTP response %d\n", httpResponseCode); // NOTE: for normal usage

        sprintf(log_buf, "/api/v1/HardWare/TestConnection -> response %d", httpResponseCode);
        writeLog("NET", log_buf);
    }
    else
    {
        Serial.printf("NET: HTTP response %d\n", httpResponseCode); // NOTE: for setup
    }

    if (httpResponseCode > 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void UpdateIpInStructure()
{
    StaticJsonDocument<2000> growDirectorStructure;
    deserializeJson(growDirectorStructure, growDirectorStructureStr);
    growDirectorStructureStr.remove(0);
    growDirectorStructure["nodes"][0]["ip"] = getlocalIP().toString();
    serializeJson(growDirectorStructure, growDirectorStructureStr);
}

void UpdateSsidInStructure()
{
    StaticJsonDocument<2000> growDirectorStructure;
    deserializeJson(growDirectorStructure, growDirectorStructureStr);
    growDirectorStructureStr.remove(0);
    growDirectorStructure["nodes"][0]["ssid"] = WiFi.SSID();
    serializeJson(growDirectorStructure, growDirectorStructureStr);
}

void sendToServerGrowDirectorStructure()
{
    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        Serial.println("NET: --sendToServerGrowDirectorStructure--");
    }

    if (debugLevel >= DebugLevel::TRACE) //&& NET_DEBUG)
    {
        Serial.printf("NET: Structure json: %s\n", growDirectorStructureStr.c_str());

        String strForLog = growDirectorStructureStr;
        std::replace(strForLog.begin(), strForLog.end(), '"', '\'');
        sprintf(log_buf, "/api/v1/HardWare/structure -> %s", strForLog.c_str());
        writeLog("NET", log_buf);
    }

    String serverName = String(serverUrl) + "/api/v1/HardWare/structure" + "?token=" + token;

    if (_http != NULL)
    {
        _http->end();
    }
    else
    {
        _http = new HTTPClient();
    }

    byte tries = 0;
    int httpResponseCode = 0;
    while (tries < 3 && httpResponseCode != 200)
    {
        _http->setTimeout(30000);
        _http->begin(serverName);
        _http->addHeader("Content-Type", "application/json");

        httpResponseCode = _http->POST(growDirectorStructureStr);

        Serial.printf("NET: /api/v1/HardWare/structure -> response %d, try = %d\n", httpResponseCode, tries);

        if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
        {
            sprintf(log_buf, "/api/v1/HardWare/structure -> response %d", httpResponseCode);
            writeLog("NET", log_buf);
        }

        _http->end();
        tries++;
    }

    if (tries >= 3)
    {
        ESP.restart();
    }

    if (_http != NULL)
    {
        _http->end();
        delete _http;
    }
    _http = new HTTPClient();
}

void sendRuleStateCommand(String msg)
{
    if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || RULE_SYNC_DEBUG))
    {
        Serial.println("NET: --sendRuleStateCommand--");
    }

    if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || RULE_SYNC_DEBUG))
    {
        Serial.printf("NET: Rule state command: %s\n", msg.c_str());

        String strForLog = msg;
        std::replace(strForLog.begin(), strForLog.end(), '"', '\'');
        sprintf(log_buf, "/api/v1/HardWare/rules/state -> %s", strForLog.c_str());
        writeLog("NET", log_buf);
    }

    String serverName = String(serverUrl) + "/api/v1/HardWare/rules/state" + "?token=" + token + "&grow_director_uid=" + WiFi.macAddress();

    _http->end();
    _http->setTimeout(30000);
    _http->begin(serverName);
    _http->addHeader("Content-Type", "application/json");

    int httpResponseCode = _http->POST(msg);

    if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || RULE_SYNC_DEBUG))
    {
        Serial.printf("NET: HTTP response %d\n", httpResponseCode);

        sprintf(log_buf, "/api/v1/HardWare/rules/state -> response %d", httpResponseCode);
        writeLog("NET", log_buf);
    }

    // Free resources
    _http->end();
    delete _http;
    _http = new HTTPClient();

    // Publish to MQTT regardless of HTTP success
    String mqttTopic = "hardware/rules/state/" + WiFi.macAddress();
    mqttClient.publish(mqttTopic.c_str(), msg.c_str());
}

bool sendByUidRuleStateCommand(String uid, String msg)
{
    if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || RULE_SYNC_DEBUG))
    {
        Serial.println("NET: --sendRuleStateCommand--");
    }

    if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || RULE_SYNC_DEBUG))
    {
        Serial.printf("NET: Rule state command: %s\n", msg.c_str());

        String strForLog = msg;
        std::replace(strForLog.begin(), strForLog.end(), '"', '\'');
        sprintf(log_buf, "/api/v1/HardWare/rules/%s/state -> %s", uid.c_str(), strForLog.c_str());
        writeLog("NET", log_buf);
    }

    String serverName = String(serverUrl) + "/api/v1/HardWare/rules/" + uid + "/state" + "?token=" + token + "&grow_director_uid=" + WiFi.macAddress();

    _http->end();
    delete _http;
    _http = new HTTPClient();
    _http->setTimeout(30000);
    _http->begin(serverName);
    _http->addHeader("Content-Type", "application/json");

    int httpResponseCode = _http->POST(msg);

    if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || RULE_SYNC_DEBUG))
    {
        Serial.printf("NET: HTTP response %d\n", httpResponseCode);

        sprintf(log_buf, "/api/v1/HardWare/rules/%s/state -> response %d", uid.c_str(), httpResponseCode);
        writeLog("NET", log_buf);
    }

    // Free resources
    _http->end();
    delete _http;
    _http = new HTTPClient();

    // Publish to MQTT regardless of HTTP success
    String mqttTopic = "hardware/rules/state/" + WiFi.macAddress() + "/" + uid;
    mqttClient.publish(mqttTopic.c_str(), msg.c_str());

    return httpResponseCode == 200;
}

void sendFirmwareVersion()
{
    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        Serial.println("NET: --sendFirmwareVersion--");
    }

    byte tries = 0;
    int httpResponseCode = 0;
    String msg = "{\"version\": \"" + String(firmwareVersion) + "\",\"growdirectorUid\": \"" + WiFi.macAddress() + "\"}";
    String serverName = String(serverUrl) + "/api/v1/HardWare/firmware" + "?token=" + token + "&grow_director_uid=" + WiFi.macAddress();

    Serial.printf("NET: POST %s, body=%s\n", serverName.c_str(), msg.c_str());

    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        sprintf(log_buf, "POST %s, body=%s", serverName.c_str(), msg.c_str());
        writeLog("NET", log_buf);
    }

    while (tries < 3 && httpResponseCode != 200)
    {
        _http->setTimeout(30000);
        _http->begin(serverName);
        _http->addHeader("Content-Type", "application/json");

        httpResponseCode = _http->POST(msg);

        Serial.printf("NET: /api/v1/HardWare/firmware response %d, try = %d\n", httpResponseCode, tries);

        if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
        {
            sprintf(log_buf, "/api/v1/HardWare/firmware -> response %d, try = %d", httpResponseCode, tries);
            writeLog("NET", log_buf);
        }

        _http->end();
    }
    // Free resources
    if (_http != NULL)
    {
        _http->end();
        delete _http;
    }
    _http = new HTTPClient();
}

void sendDeviceCommandExecutionResult(String msg)
{
    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        Serial.println("NET: --sendDeviceCommandExecutionResult--");
    }

    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        mqttLogger.printf("NET: Device command result %s\n", msg.c_str());

        sprintf(log_buf, "Device command result %s", msg.c_str());
        writeLog("NET", log_buf);
    }

    if (msg.length() < 10)
    {
        if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
        {
            Serial.println("NET: Device command empty");

            sprintf(log_buf, "NET: Device command empty");
            writeLog("NET", log_buf);
        }
        return;
    }

    String strMqtt = msg;
    String mqttTopic = "hardware/devices_command/post/" + WiFi.macAddress();
    mqttClient.publish(mqttTopic.c_str(), strMqtt.c_str());

    byte tries = 0;
    int httpResponseCode = 0;
    String serverName = String(serverUrl) + "/api/v1/HardWare/devices_command" + "?token=" + token + "&grow_director_uid=" + WiFi.macAddress();
    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        Serial.printf("NET: POST %s\n", serverName.c_str());

        sprintf(log_buf, "POST %s", serverName.c_str());
        writeLog("NET", log_buf);
    }

    while (tries < 3 && httpResponseCode != 200)
    {
        _http->setTimeout(30000);
        _http->begin(serverName);
        _http->addHeader("Content-Type", "application/json");

        httpResponseCode = _http->POST(msg);

        if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
        {
            Serial.printf("NET: HTTP response %d, try = %d\n", httpResponseCode, tries);

            sprintf(log_buf, "/api/v1/HardWare/devices_command -> response %d, try = %d", httpResponseCode, tries);
            writeLog("NET", log_buf);
        }

        // Free resources
        _http->end();
        delete _http;
        _http = new HTTPClient();

        delay(200);
        tries++;
    }
}

void sendAnalogInputDeviceCommandExecutionResult(String msg)
{
    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        Serial.println("NET: --sendDeviceCommandExecutionResult--");
        Serial.printf("NET: Device command result %s\n", msg.c_str());

        sprintf(log_buf, "sendDeviceCommandExecutionResult -> Device command result %s", msg.c_str());
        writeLog("NET", log_buf);
    }

    if (msg.length() < 10)
    {
        if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
        {
            Serial.println("NET: Device command empty");

            sprintf(log_buf, "Device command empty");
            writeLog("NET", log_buf);
        }
        return;
    }

    byte tries = 0;
    int httpResponseCode = 0;
    String serverName = String(serverUrl) + "/api/v1/HardWare/devices_command" + "?token=" + token + "&grow_director_uid=" + WiFi.macAddress(); /// api/v1/HardWare/dimmer_command
    if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
    {
        Serial.printf("NET: POST %s\n", serverName.c_str());

        sprintf(log_buf, "POST %s", serverName.c_str());
        writeLog("NET", log_buf);
    }

    while (tries < 3 && httpResponseCode != 200)
    {
        _http->setTimeout(30000);
        _http->begin(serverName);
        _http->addHeader("Content-Type", "application/json");

        httpResponseCode = _http->POST(msg);

        if (debugLevel >= DebugLevel::TRACE && NET_DEBUG)
        {
            Serial.printf("NET: HTTP response %d, try = %d\n", httpResponseCode, tries);

            sprintf(log_buf, "/api/v1/HardWare/devices_command -> response %d, try = %d", httpResponseCode, tries);
            writeLog("NET", log_buf);
        }

        // Free resources
        _http->end();
        delete _http;
        _http = new HTTPClient();
        delay(200);
        tries++;
    }
}

bool sendModuleCommandExecutionResult(String msg)
{
    if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || MODULE_COMMAND_DEBUG))
    {
        mqttLogger.printf("NET: Module command result %s\n", msg.c_str());

        sprintf(log_buf, "Module command result %s", msg.c_str());
        writeLog("NET", log_buf);
    }

    if (msg.length() < 10)
    {
        if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || MODULE_COMMAND_DEBUG))
        {
            Serial.println("NET: Module command empty");

            sprintf(log_buf, "NET: Module command empty");
            writeLog("NET", log_buf);
        }
        return false;
    }

    String strMqtt = msg;
    String mqttTopic = "hardware/module_command/post/" + WiFi.macAddress();
    mqttClient.publish(mqttTopic.c_str(), strMqtt.c_str());

    byte tries = 0;
    int httpResponseCode = 0;
    String serverName = String(serverUrl) + "/api/v1/HardWare/module_command" + "?token=" + token + "&grow_director_uid=" + WiFi.macAddress();
    if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || MODULE_COMMAND_DEBUG))
    {
        // Serial.printf("NET: POST %s, Body: %s\n", serverName.c_str(), msg.c_str());

        sprintf(log_buf, "POST %s, Body: %s", serverName.c_str(), msg.c_str());
        writeLog("NET", log_buf);
    }

    while (tries < 3 && httpResponseCode != 200)
    {
        _http->setTimeout(30000);
        _http->begin(serverName);
        _http->addHeader("Content-Type", "application/json");

        httpResponseCode = _http->POST(msg);

        if (debugLevel >= DebugLevel::TRACE && (NET_DEBUG || MODULE_COMMAND_DEBUG))
        {
            Serial.printf("NET: HTTP response %d, try = %d\n", httpResponseCode, tries);

            sprintf(log_buf, "/api/v1/HardWare/module_command -> response %d, try = %d", httpResponseCode, tries);
            writeLog("NET", log_buf);
        }

        // Free resources
        _http->end();
        if (httpResponseCode != 200)
        {
            delay(1000 * tries);
        }
        tries++;
    }
    delete _http;
    _http = new HTTPClient();

    return httpResponseCode == 200;
}
