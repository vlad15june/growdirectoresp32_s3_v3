#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include "Infrastructure/Adapters/eepromAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <time.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

class Settings : public ISettings
{
private:
    EepromAdapter &_eeprom;

    ILogger *_logger;
    IDebugSystem *debugSystem;

    // EEPROM address definitions
    static const int EEPROM_ADDRES_SSID = 0;
    static const int EEPROM_ADDRES_PASS = 40;
    static const int EEPROM_ADDRES_USER = 80;
    static const int EEPROM_ADDRES_TOKEN = 120;
    static const int EEPROM_ADDRES_SRV = 160;
    static const int EEPROM_ADDRES_IS_CONF_OK = 220;
    static const int EEPROM_ADDRES_RULE_STATE = 222;
    static const int EEPROM_ADDRES_SENSOR_DO_ORP_TYPE = 224;
    static const int EEPROM_ADDRES_SENSOR_1_TYPE = 226;
    static const int EEPROM_ADDRES_SENSOR_2_TYPE = 228;
    static const int EEPROM_ADDRES_SENSOR_3_TYPE = 230;
    static const int EEPROM_ADDRES_SENSOR_4_TYPE = 232;
    static const int EEPROM_KEY_TEST_MODE = 234;
    static const int EEPROM_TEMP_COEF_VERSION = 236;
    static const int EEPROM_ADDRES_ACCOUNT_UID = 240;
    static const int EEPROM_ADDRES_LAT = 280;
    static const int EEPROM_ADDRES_LONG = 288;
    static const int EEPROM_ADDRES_LOCAL_TIME = 296;
    static const int EEPROM_ADDRES_BOOT_TYPE = 376;
    static const int EEPROM_ADDRES_NEED_UPDATE = 378;
    static const int EEPROM_ADDRES_UPDATE_STATUS = 380;
    static const int EEPROM_ADDRES_UPDATE_LINK = 382;
    static const int EEPROM_ADDRES_MODULE_COMMAND_UID = 500;
    static const int EEPROM_BUTTON_PRESSED_BEFORE_REBOOT = 550;
    static const int EEPROM_NO_SCAN_SENSORS = 552;
    static const int EEPROM_WAS_INITIAL_SYNC_COMPLETED = 556;
    static const int EEPROM_NEED_CLEAR_SPIFFS = 558;
    static const int EEPROM_PREVENTIVE_RESTART_DAY = 560;

    // STEP rule cycle counters (GDV1-3811): a small table of fixed-size slots, one per digital
    // channel of the module (deviceCount is 4 at most, ModuleConfig). Layout of a slot:
    //   [0]        occupied marker
    //   [1..24]    deviceUid, zero terminated
    //   [25..64]   ruleUid, zero terminated
    //   [65..66]   cyclesToSetPoint (uint16, little endian)
    //   [67..68]   currentCycle     (uint16, little endian)
    // Table occupies 562..849 of the 1024 byte EEPROM image.
    static const int EEPROM_STEP_CYCLE_TABLE = 562;
    static const int STEP_CYCLE_SLOTS = 4;
    static const int STEP_CYCLE_SLOT_SIZE = 72;
    static const int STEP_CYCLE_DEVICE_UID_SIZE = 24;
    static const int STEP_CYCLE_RULE_UID_SIZE = 40;
    static const uint8_t STEP_CYCLE_SLOT_OCCUPIED = 0xC7;

    static int stepCycleSlotAddress(int slot)
    {
        return EEPROM_STEP_CYCLE_TABLE + slot * STEP_CYCLE_SLOT_SIZE;
    }

    void readStepCycleSlot(int slot, StepCycleCounter &counter)
    {
        int base = stepCycleSlotAddress(slot);
        char deviceUidBuffer[STEP_CYCLE_DEVICE_UID_SIZE];
        char ruleUidBuffer[STEP_CYCLE_RULE_UID_SIZE];

        _eeprom.readString(base + 1, deviceUidBuffer, STEP_CYCLE_DEVICE_UID_SIZE);
        deviceUidBuffer[STEP_CYCLE_DEVICE_UID_SIZE - 1] = 0;
        _eeprom.readString(base + 1 + STEP_CYCLE_DEVICE_UID_SIZE, ruleUidBuffer, STEP_CYCLE_RULE_UID_SIZE);
        ruleUidBuffer[STEP_CYCLE_RULE_UID_SIZE - 1] = 0;

        counter.deviceUid = deviceUidBuffer;
        counter.ruleUid = ruleUidBuffer;

        int valuesBase = base + 1 + STEP_CYCLE_DEVICE_UID_SIZE + STEP_CYCLE_RULE_UID_SIZE;
        counter.cyclesToSetPoint = static_cast<int>(_eeprom.readByte(valuesBase)) |
                                   (static_cast<int>(_eeprom.readByte(valuesBase + 1)) << 8);
        counter.currentCycle = static_cast<int>(_eeprom.readByte(valuesBase + 2)) |
                               (static_cast<int>(_eeprom.readByte(valuesBase + 3)) << 8);
    }

    bool isStepCycleSlotOccupied(int slot)
    {
        return _eeprom.readByte(stepCycleSlotAddress(slot)) == STEP_CYCLE_SLOT_OCCUPIED;
    }

    // Slot of the given device, or -1 when the device has no record yet.
    int findStepCycleSlot(const char *deviceUid)
    {
        if (deviceUid == nullptr || deviceUid[0] == 0)
        {
            return -1;
        }

        for (int slot = 0; slot < STEP_CYCLE_SLOTS; slot++)
        {
            if (!isStepCycleSlotOccupied(slot))
            {
                continue;
            }

            StepCycleCounter stored;
            readStepCycleSlot(slot, stored);
            if (stored.deviceUid == deviceUid)
            {
                return slot;
            }
        }

        return -1;
    }

    int findFreeStepCycleSlot()
    {
        for (int slot = 0; slot < STEP_CYCLE_SLOTS; slot++)
        {
            if (!isStepCycleSlotOccupied(slot))
            {
                return slot;
            }
        }
        return -1;
    }

    static uint16_t clampStepCycleValue(int value)
    {
        if (value < 0)
        {
            return 0;
        }
        if (value > 0xFFFF)
        {
            return 0xFFFF;
        }
        return static_cast<uint16_t>(value);
    }

    void log(const char *format, ...)
    {
        if (_logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SETTINGS_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* feature = debugSystem->getFeature(DebugFeatures::SETTINGS_DEBUG);
            if (feature)
            {
                _logger->println(DebugFeatures::SETTINGS_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                _logger->println(DebugFeatures::SETTINGS_DEBUG, buffer);
            }
        }
    }

public:
    // Constructor
    Settings(EepromAdapter &eeprom, ILogger *logger = nullptr,
             IDebugSystem *debugSystemInstance = nullptr)
        : _eeprom(eeprom),
          _logger(logger),
          debugSystem(debugSystemInstance)
    {
    }

    // Initialize settings
    bool begin()
    {
        return _eeprom.begin();
    }

    // Set logger and debug options
    void setLogger(void *logger)
    {
        _logger = static_cast<ILogger *>(logger);
    }

    // Configuration status methods
    uint8_t getIfConfigured()
    {
        return _eeprom.readByte(EEPROM_ADDRES_IS_CONF_OK);
    }

    void setConfigured(uint8_t isConfigured)
    {
        log("SETTINGS: setConfigured = %d", isConfigured);
        _eeprom.writeByte(EEPROM_ADDRES_IS_CONF_OK, isConfigured);
        _eeprom.commit();
    }

    // Update status methods
    uint8_t getNeedUpdate()
    {
        return _eeprom.readByte(EEPROM_ADDRES_NEED_UPDATE);
    }

    void setNeedUpdate(uint8_t needUpdate)
    {
        log("SETTINGS: needUpdate = %d", needUpdate);
        _eeprom.writeByte(EEPROM_ADDRES_NEED_UPDATE, needUpdate);
        _eeprom.commit();
    }

    uint8_t getUpdateStatus()
    {
        return _eeprom.readByte(EEPROM_ADDRES_UPDATE_STATUS);
    }

    void setUpdateStatus(uint8_t updateStatus)
    {
        log("SETTINGS: updateStatus = %d", updateStatus);
        _eeprom.writeByte(EEPROM_ADDRES_UPDATE_STATUS, updateStatus);
        _eeprom.commit();
    }

    // Boot type methods
    uint8_t getBootType()
    {
        return _eeprom.readByte(EEPROM_ADDRES_BOOT_TYPE);
    }

    void setBootType(uint8_t bootType)
    {
        log("SETTINGS: EEPROM_ADDRES_BOOT_TYPE = %d", bootType);
        _eeprom.writeByte(EEPROM_ADDRES_BOOT_TYPE, bootType);
        _eeprom.commit();
    }

    // Time and location methods
    struct tm getLocalTime()
    {
        struct tm localTime;
        _eeprom.readBytes(EEPROM_ADDRES_LOCAL_TIME, (byte *)&localTime, sizeof(struct tm));
        return localTime;
    }

    void setLocalTime(const struct tm &localTime)
    {
        _eeprom.writeBytes(EEPROM_ADDRES_LOCAL_TIME, (const byte *)&localTime, sizeof(struct tm));
        _eeprom.commit();
    }

    float getLatitude()
    {
        return _eeprom.readFloat(EEPROM_ADDRES_LAT);
    }

    void setLatitude(float latitude)
    {
        _eeprom.writeFloat(EEPROM_ADDRES_LAT, latitude);
        _eeprom.commit();
    }

    float getLongitude()
    {
        return _eeprom.readFloat(EEPROM_ADDRES_LONG);
    }

    void setLongitude(float longitude)
    {
        _eeprom.writeFloat(EEPROM_ADDRES_LONG, longitude);
        _eeprom.commit();
    }

    // Network configuration methods
    void getSsid(char *buffer, int bufferSize = BUFSIZE)
    {
        _eeprom.readString(EEPROM_ADDRES_SSID, buffer, bufferSize);
    }

    void setSsid(const char *ssid)
    {
        _eeprom.writeString(EEPROM_ADDRES_SSID, ssid);
        _eeprom.commit();
    }

    void getPassword(char *buffer, int bufferSize = BUFSIZE)
    {
        _eeprom.readString(EEPROM_ADDRES_PASS, buffer, bufferSize);
    }

    void setPassword(const char *password)
    {
        _eeprom.writeString(EEPROM_ADDRES_PASS, password);
        _eeprom.commit();
    }

    void getUser(char *buffer, int bufferSize = BUFSIZE)
    {
        _eeprom.readString(EEPROM_ADDRES_USER, buffer, bufferSize);
    }

    void setUser(const char *user)
    {
        _eeprom.writeString(EEPROM_ADDRES_USER, user);
        _eeprom.commit();
    }

    void getToken(char *buffer, int bufferSize = BUFSIZE)
    {
        _eeprom.readString(EEPROM_ADDRES_TOKEN, buffer, bufferSize);
    }

    void setToken(const char *token)
    {
        _eeprom.writeString(EEPROM_ADDRES_TOKEN, token);
        _eeprom.commit();
    }

    void getUpdateLink(char *buffer, int bufferSize = BUFSIZE)
    {
        _eeprom.readString(EEPROM_ADDRES_UPDATE_LINK, buffer, bufferSize);
    }

    void setUpdateLink(const char *link)
    {
        _eeprom.writeString(EEPROM_ADDRES_UPDATE_LINK, link);
        _eeprom.commit();
    }

    void getModuleCommandUid(char *buffer, int bufferSize = BUFSIZE)
    {
        _eeprom.readString(EEPROM_ADDRES_MODULE_COMMAND_UID, buffer, bufferSize);
    }

    void setModuleCommandUid(const char *uid)
    {
        _eeprom.writeString(EEPROM_ADDRES_MODULE_COMMAND_UID, uid);
        _eeprom.commit();
    }

    void getAccountUid(char *buffer, int bufferSize = BUFSIZE)
    {
        _eeprom.readString(EEPROM_ADDRES_ACCOUNT_UID, buffer, bufferSize);
    }

    void setAccountUid(const char *uid)
    {
        _eeprom.writeString(EEPROM_ADDRES_ACCOUNT_UID, uid);
        _eeprom.commit();
    }

    void getServerUrl(char *buffer, int bufferSize = BUFSIZE)
    {
        _eeprom.readString(EEPROM_ADDRES_SRV, buffer, bufferSize);
    }

    void setServerUrl(const char *url)
    {
        _eeprom.writeString(EEPROM_ADDRES_SRV, url);
        _eeprom.commit();
    }

    // Rule and sensor configuration methods
    uint8_t getRuleState()
    {
        return _eeprom.readByte(EEPROM_ADDRES_RULE_STATE);
    }

    void setRuleState(uint8_t ruleState)
    {
        _eeprom.writeByte(EEPROM_ADDRES_RULE_STATE, ruleState);
        _eeprom.commit();
    }

    uint8_t getSensorTypeDoOrp()
    {
        return _eeprom.readByte(EEPROM_ADDRES_SENSOR_DO_ORP_TYPE);
    }

    void setSensorTypeDoOrp(uint8_t sensorType)
    {
        _eeprom.writeByte(EEPROM_ADDRES_SENSOR_DO_ORP_TYPE, sensorType);
        _eeprom.commit();
    }

    uint8_t getSensorType(uint8_t channel)
    {
        if (channel == 0)
            return _eeprom.readByte(EEPROM_ADDRES_SENSOR_1_TYPE);
        else if (channel == 1)
            return _eeprom.readByte(EEPROM_ADDRES_SENSOR_2_TYPE);
        else if (channel == 2)
            return _eeprom.readByte(EEPROM_ADDRES_SENSOR_3_TYPE);
        else if (channel == 3)
            return _eeprom.readByte(EEPROM_ADDRES_SENSOR_4_TYPE);

        return NOT_CONNECTED;
    }

    void setSensorType(uint8_t sensorType, uint8_t channel)
    {
        if (channel == 0)
            _eeprom.writeByte(EEPROM_ADDRES_SENSOR_1_TYPE, sensorType);
        else if (channel == 1)
            _eeprom.writeByte(EEPROM_ADDRES_SENSOR_2_TYPE, sensorType);
        else if (channel == 2)
            _eeprom.writeByte(EEPROM_ADDRES_SENSOR_3_TYPE, sensorType);
        else if (channel == 3)
            _eeprom.writeByte(EEPROM_ADDRES_SENSOR_4_TYPE, sensorType);

        _eeprom.commit();
    }

    uint8_t getKeyTestMode()
    {
        return _eeprom.readByte(EEPROM_KEY_TEST_MODE);
    }

    void setKeyTestMode(uint8_t isApTestKey)
    {
        _eeprom.writeByte(EEPROM_KEY_TEST_MODE, isApTestKey);
        _eeprom.commit();
    }

    uint8_t getTempCoefVersion()
    {
        return _eeprom.readByte(EEPROM_TEMP_COEF_VERSION);
    }

    void setTempCoefVersion(uint8_t versionNumber)
    {
        _eeprom.writeByte(EEPROM_TEMP_COEF_VERSION, versionNumber);
        _eeprom.commit();
    }

    uint8_t getButtonPressedBeforeReboot() override
    {
        return _eeprom.readByte(EEPROM_BUTTON_PRESSED_BEFORE_REBOOT);
    }

    void setButtonPressedBeforeReboot(uint8_t wasPressed) override
    {
        _eeprom.writeByte(EEPROM_BUTTON_PRESSED_BEFORE_REBOOT, wasPressed);
        _eeprom.commit();
    }

    uint8_t getNoScanSensors() override
    {
        return _eeprom.readByte(EEPROM_NO_SCAN_SENSORS);
    }

    void setNoScanSensors(uint8_t noScan) override
    {
        log("SETTINGS: noScanSensors = %d", noScan);
        _eeprom.writeByte(EEPROM_NO_SCAN_SENSORS, noScan);
        _eeprom.commit();
    }

    uint8_t getWasInitialSyncCompleted() override
    {
        return _eeprom.readByte(EEPROM_WAS_INITIAL_SYNC_COMPLETED);
    }

    void setWasInitialSyncCompleted(uint8_t wasCompleted) override
    {
        log("SETTINGS: wasInitialSyncCompleted = %d", wasCompleted);
        _eeprom.writeByte(EEPROM_WAS_INITIAL_SYNC_COMPLETED, wasCompleted);
        _eeprom.commit();
    }

    uint8_t getNeedClearSpiffs() override
    {
        return _eeprom.readByte(EEPROM_NEED_CLEAR_SPIFFS);
    }

    void setNeedClearSpiffs(uint8_t needClear) override
    {
        log("SETTINGS: needClearSpiffs = %d", needClear);
        _eeprom.writeByte(EEPROM_NEED_CLEAR_SPIFFS, needClear);
        _eeprom.commit();
    }

    uint8_t getLastPreventiveRestartDay() override
    {
        return _eeprom.readByte(EEPROM_PREVENTIVE_RESTART_DAY);
    }

    void setLastPreventiveRestartDay(uint8_t day) override
    {
        log("SETTINGS: lastPreventiveRestartDay = %d", day);
        _eeprom.writeByte(EEPROM_PREVENTIVE_RESTART_DAY, day);
        _eeprom.commit();
    }

    bool getStepCycleCounter(const char *deviceUid, StepCycleCounter &counter) override
    {
        int slot = findStepCycleSlot(deviceUid);
        if (slot < 0)
        {
            return false;
        }

        readStepCycleSlot(slot, counter);
        return true;
    }

    void setStepCycleCounter(const StepCycleCounter &counter) override
    {
        if (counter.deviceUid.empty())
        {
            return;
        }

        int slot = findStepCycleSlot(counter.deviceUid.c_str());
        if (slot < 0)
        {
            slot = findFreeStepCycleSlot();
        }
        if (slot < 0)
        {
            log("SETTINGS: stepCycleCounter table full, device %s not stored", counter.deviceUid.c_str());
            return;
        }

        log("SETTINGS: stepCycleCounter slot=%d device=%s rule=%s cycle=%d/%d",
            slot, counter.deviceUid.c_str(), counter.ruleUid.c_str(),
            counter.currentCycle, counter.cyclesToSetPoint);

        int base = stepCycleSlotAddress(slot);
        char deviceUidBuffer[STEP_CYCLE_DEVICE_UID_SIZE] = {0};
        char ruleUidBuffer[STEP_CYCLE_RULE_UID_SIZE] = {0};
        strncpy(deviceUidBuffer, counter.deviceUid.c_str(), STEP_CYCLE_DEVICE_UID_SIZE - 1);
        strncpy(ruleUidBuffer, counter.ruleUid.c_str(), STEP_CYCLE_RULE_UID_SIZE - 1);

        uint16_t cycles = clampStepCycleValue(counter.cyclesToSetPoint);
        uint16_t current = clampStepCycleValue(counter.currentCycle);
        int valuesBase = base + 1 + STEP_CYCLE_DEVICE_UID_SIZE + STEP_CYCLE_RULE_UID_SIZE;

        _eeprom.writeBytes(base + 1, reinterpret_cast<const byte *>(deviceUidBuffer), STEP_CYCLE_DEVICE_UID_SIZE);
        _eeprom.writeBytes(base + 1 + STEP_CYCLE_DEVICE_UID_SIZE,
                           reinterpret_cast<const byte *>(ruleUidBuffer), STEP_CYCLE_RULE_UID_SIZE);
        _eeprom.writeByte(valuesBase, static_cast<uint8_t>(cycles & 0xFF));
        _eeprom.writeByte(valuesBase + 1, static_cast<uint8_t>((cycles >> 8) & 0xFF));
        _eeprom.writeByte(valuesBase + 2, static_cast<uint8_t>(current & 0xFF));
        _eeprom.writeByte(valuesBase + 3, static_cast<uint8_t>((current >> 8) & 0xFF));
        _eeprom.writeByte(base, STEP_CYCLE_SLOT_OCCUPIED);
        _eeprom.commit();
    }

    void clearStepCycleCounter(const char *deviceUid) override
    {
        int slot = findStepCycleSlot(deviceUid);
        if (slot < 0)
        {
            return;
        }

        log("SETTINGS: stepCycleCounter cleared slot=%d device=%s", slot, deviceUid);
        _eeprom.writeByte(stepCycleSlotAddress(slot), 0);
        _eeprom.commit();
    }
};

#endif // SETTINGS_H
