#ifndef MOCK_SETTINGS_H
#define MOCK_SETTINGS_H

#include "Core/Domain/Interfaces/ISettings.h"
#include <map>
#include <string>

// Mock implementation of ISettings for testing
class MockSettings : public ISettings
{
private:
    // Use maps to store values
    std::map<std::string, uint8_t> _byteValues;
    std::map<std::string, float> _floatValues;
    std::map<std::string, std::string> _stringValues;
    struct tm _localTime;

    // Initialize with default values
    void initDefaults()
    {
        // Set default byte values
        _byteValues["isConfigured"] = 0;
        _byteValues["needUpdate"] = 0;
        _byteValues["updateStatus"] = 0;
        _byteValues["bootType"] = 0;
        _byteValues["ruleState"] = 0;
        _byteValues["sensorDoOrpType"] = 0;
        _byteValues["keyTestMode"] = 0;
        _byteValues["tempCoefVersion"] = 0;
        _byteValues["buttonPressedBeforeReboot"] = 0;
        _byteValues["noScanSensors"] = 0;

        // Set default sensor types
        for (int i = 0; i < 4; i++)
        {
            _byteValues["sensorType" + std::to_string(i)] = NOT_CONNECTED;
        }

        // Set default location values
        _floatValues["latitude"] = 0.0f;
        _floatValues["longitude"] = 0.0f;

        // Set default string values
        _stringValues["ssid"] = "MockSSID";
        _stringValues["password"] = "MockPassword";
        _stringValues["user"] = "MockUser";
        _stringValues["token"] = "MockToken";
        _stringValues["updateLink"] = "http://example.com/update";
        _stringValues["moduleCommandUid"] = "mock-command-uid";
        _stringValues["accountUid"] = "mock-account-uid";
        _stringValues["serverUrl"] = "mqtt.example.com";

        // Initialize local time struct
        memset(&_localTime, 0, sizeof(struct tm));
    }

public:
    MockSettings()
    {
        // Constructor
        initDefaults();
    }

    // Initialization - always succeeds in mock
    bool begin() override
    {
        return true;
    }

    // Configuration status methods
    uint8_t getIfConfigured() override
    {
        return _byteValues["isConfigured"];
    }

    void setConfigured(uint8_t isConfigured) override
    {
        _byteValues["isConfigured"] = isConfigured;
    }

    // Update status methods
    uint8_t getNeedUpdate() override
    {
        return _byteValues["needUpdate"];
    }

    void setNeedUpdate(uint8_t needUpdate) override
    {
        _byteValues["needUpdate"] = needUpdate;
    }

    uint8_t getUpdateStatus() override
    {
        return _byteValues["updateStatus"];
    }

    void setUpdateStatus(uint8_t updateStatus) override
    {
        _byteValues["updateStatus"] = updateStatus;
    }

    // Boot type methods
    uint8_t getBootType() override
    {
        return _byteValues["bootType"];
    }

    void setBootType(uint8_t bootType) override
    {
        _byteValues["bootType"] = bootType;
    }

    // Time and location methods
    struct tm getLocalTime() override
    {
        return _localTime;
    }

    void setLocalTime(const struct tm &localTime) override
    {
        _localTime = localTime;
    }

    float getLatitude() override
    {
        return _floatValues["latitude"];
    }

    void setLatitude(float latitude) override
    {
        _floatValues["latitude"] = latitude;
    }

    float getLongitude() override
    {
        return _floatValues["longitude"];
    }

    void setLongitude(float longitude) override
    {
        _floatValues["longitude"] = longitude;
    }

    // Network configuration methods
    void getSsid(char *buffer, int bufferSize = BUFSIZE) override
    {
        strncpy(buffer, _stringValues["ssid"].c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
    }

    void setSsid(const char *ssid) override
    {
        _stringValues["ssid"] = ssid;
    }

    void getPassword(char *buffer, int bufferSize = BUFSIZE) override
    {
        strncpy(buffer, _stringValues["password"].c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
    }

    void setPassword(const char *password) override
    {
        _stringValues["password"] = password;
    }

    void getUser(char *buffer, int bufferSize = BUFSIZE) override
    {
        strncpy(buffer, _stringValues["user"].c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
    }

    void setUser(const char *user) override
    {
        _stringValues["user"] = user;
    }

    void getToken(char *buffer, int bufferSize = BUFSIZE) override
    {
        strncpy(buffer, _stringValues["token"].c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
    }

    void setToken(const char *token) override
    {
        _stringValues["token"] = token;
    }

    void getUpdateLink(char *buffer, int bufferSize = BUFSIZE) override
    {
        strncpy(buffer, _stringValues["updateLink"].c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
    }

    void setUpdateLink(const char *link) override
    {
        _stringValues["updateLink"] = link;
    }

    void getModuleCommandUid(char *buffer, int bufferSize = BUFSIZE) override
    {
        strncpy(buffer, _stringValues["moduleCommandUid"].c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
    }

    void setModuleCommandUid(const char *uid) override
    {
        _stringValues["moduleCommandUid"] = uid;
    }

    void getAccountUid(char *buffer, int bufferSize = BUFSIZE) override
    {
        strncpy(buffer, _stringValues["accountUid"].c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
    }

    void setAccountUid(const char *uid) override
    {
        _stringValues["accountUid"] = uid;
    }

    void getServerUrl(char *buffer, int bufferSize = BUFSIZE) override
    {
        strncpy(buffer, _stringValues["serverUrl"].c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
    }

    void setServerUrl(const char *url) override
    {
        _stringValues["serverUrl"] = url;
    }

    // Rule and sensor configuration methods
    uint8_t getRuleState() override
    {
        return _byteValues["ruleState"];
    }

    void setRuleState(uint8_t ruleState) override
    {
        _byteValues["ruleState"] = ruleState;
    }

    uint8_t getSensorTypeDoOrp() override
    {
        return _byteValues["sensorDoOrpType"];
    }

    void setSensorTypeDoOrp(uint8_t sensorType) override
    {
        _byteValues["sensorDoOrpType"] = sensorType;
    }

    uint8_t getSensorType(uint8_t channel) override
    {
        if (channel < 4)
        {
            return _byteValues["sensorType" + std::to_string(channel)];
        }
        return NOT_CONNECTED;
    }

    void setSensorType(uint8_t sensorType, uint8_t channel) override
    {
        if (channel < 4)
        {
            _byteValues["sensorType" + std::to_string(channel)] = sensorType;
        }
    }

    uint8_t getKeyTestMode() override
    {
        return _byteValues["keyTestMode"];
    }

    void setKeyTestMode(uint8_t isApTestKey) override
    {
        _byteValues["keyTestMode"] = isApTestKey;
    }

    uint8_t getTempCoefVersion() override
    {
        return _byteValues["tempCoefVersion"];
    }

    void setTempCoefVersion(uint8_t versionNumber) override
    {
        _byteValues["tempCoefVersion"] = versionNumber;
    }

    // Button state persistence for test mode
    uint8_t getButtonPressedBeforeReboot() override
    {
        return _byteValues["buttonPressedBeforeReboot"];
    }

    void setButtonPressedBeforeReboot(uint8_t wasPressed) override
    {
        _byteValues["buttonPressedBeforeReboot"] = wasPressed;
    }

    uint8_t getNoScanSensors() override
    {
        return _byteValues["noScanSensors"];
    }

    void setNoScanSensors(uint8_t noScan) override
    {
        _byteValues["noScanSensors"] = noScan;
    }

    // Reset method to reset all values to defaults
    void reset()
    {
        initDefaults();
    }

    // Check if a setting was modified from default
    bool wasModified(const std::string &key)
    {
        if (_byteValues.find(key) != _byteValues.end())
        {
            // Check byte values against defaults
            if (key == "isConfigured" && _byteValues[key] != 0)
                return true;
            if (key == "needUpdate" && _byteValues[key] != 0)
                return true;
            if (key == "updateStatus" && _byteValues[key] != 0)
                return true;
            if (key == "bootType" && _byteValues[key] != 0)
                return true;
            if (key == "ruleState" && _byteValues[key] != 0)
                return true;
            if (key == "sensorDoOrpType" && _byteValues[key] != 0)
                return true;
            if (key == "keyTestMode" && _byteValues[key] != 0)
                return true;
            if (key == "tempCoefVersion" && _byteValues[key] != 0)
                return true;
        }

        // Check for sensor types
        if (key.find("sensorType") == 0)
        {
            int index = key[10] - '0';
            if (index >= 0 && index < 4)
            {
                if (_byteValues[key] != NOT_CONNECTED)
                    return true;
            }
        }

        // Check float values
        if (key == "latitude" && _floatValues[key] != 0.0f)
            return true;
        if (key == "longitude" && _floatValues[key] != 0.0f)
            return true;

        // String values have specific defaults, just check if they've changed
        if (_stringValues.find(key) != _stringValues.end())
        {
            if (key == "ssid" && _stringValues[key] != "MockSSID")
                return true;
            if (key == "password" && _stringValues[key] != "MockPassword")
                return true;
            if (key == "user" && _stringValues[key] != "MockUser")
                return true;
            if (key == "token" && _stringValues[key] != "MockToken")
                return true;
            if (key == "updateLink" && _stringValues[key] != "http://example.com/update")
                return true;
            if (key == "moduleCommandUid" && _stringValues[key] != "mock-command-uid")
                return true;
            if (key == "accountUid" && _stringValues[key] != "mock-account-uid")
                return true;
            if (key == "serverUrl" && _stringValues[key] != "mqtt.example.com")
                return true;
        }

        return false;
    }
};

#endif // MOCK_SETTINGS_H