#pragma once

#include "Core/Domain/Interfaces/IBootEventLog.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsAdapter.h"
#include "Infrastructure/Logging/MqttLogger.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include <cstdio>
#include <string>

#define BOOT_EVENT_LOG_FILE "/boot_event.log"
#define BOOT_EVENT_LOG_SIZE 10000
#define BOOT_EVENT_MAX_UPLOAD_LINES 500

class BootEventLog : public IBootEventLog {
private:
    ISpiffsAdapter* _spiffs;
    MqttLogger* _mqttLogger;
    INTPTimeAdapter* _timeAdapter;
    IArduinoAdapter* _arduino;
    IWifiAdapter* _wifi;
    const char* _subTopic;

    unsigned long nowMs() {
        return _arduino ? _arduino->millis() : 0;
    }

    void serialDebug(const char* line) {
        if (_arduino == nullptr) {
            return;
        }
        char dbg[360];
        snprintf(dbg, sizeof(dbg), "BOOT_EVENT_LOG: %s", line);
        _arduino->serialPrintln(dbg);
    }

    void appendLine(const char* msg) {
        if (_spiffs == nullptr) {
            return;
        }

        if (_spiffs->getFileSize(BOOT_EVENT_LOG_FILE) > BOOT_EVENT_LOG_SIZE) {
            _spiffs->rewriteToFile(BOOT_EVENT_LOG_FILE, "");
        }

        const char* timeStr = _timeAdapter ? _timeAdapter->getFormattedTime() : "";
        char line[320];
        snprintf(line, sizeof(line), "%s up=%lu heap=%u rssi=%d %s",
                 timeStr, nowMs(),
                 (unsigned)(_arduino ? _arduino->getFreeHeap() : 0),
                 (_wifi ? _wifi->getRSSI() : 0), msg);

        _spiffs->appendToFile(BOOT_EVENT_LOG_FILE, line);
        serialDebug(line);
    }

public:
    BootEventLog(ISpiffsAdapter* spiffs = nullptr, MqttLogger* mqttLogger = nullptr,
                 INTPTimeAdapter* timeAdapter = nullptr, IArduinoAdapter* arduino = nullptr,
                 IWifiAdapter* wifi = nullptr, const char* subTopic = nullptr)
        : _spiffs(spiffs), _mqttLogger(mqttLogger), _timeAdapter(timeAdapter),
          _arduino(arduino), _wifi(wifi), _subTopic(subTopic) {}

    void write(const char* msg) override {
        appendLine(msg);
    }

    bool hasPending() override {
        if (_spiffs == nullptr) {
            return false;
        }
        return _spiffs->getFileSize(BOOT_EVENT_LOG_FILE) > 0;
    }

    bool flushToMqttIfAny() override {
        if (_spiffs == nullptr || _mqttLogger == nullptr || _subTopic == nullptr) {
            return false;
        }
        if (!_mqttLogger->isConnected()) {
            return false;
        }

        std::string content = _spiffs->readSPIFFSstr(BOOT_EVENT_LOG_FILE);
        if (content.empty()) {
            return true;
        }

        size_t start = 0;
        int published = 0;
        while (start < content.size() && published < BOOT_EVENT_MAX_UPLOAD_LINES) {
            size_t end = content.find('\n', start);
            std::string line = (end == std::string::npos)
                ? content.substr(start)
                : content.substr(start, end - start);

            if (!line.empty()) {
                if (!_mqttLogger->publishToTopic(_subTopic, line.c_str())) {
                    return false;
                }
                published++;
            }

            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }

        _spiffs->rewriteToFile(BOOT_EVENT_LOG_FILE, "");
        return true;
    }
};
