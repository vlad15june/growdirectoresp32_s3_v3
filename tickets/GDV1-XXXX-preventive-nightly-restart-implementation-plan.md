# План реализации: ночной превентивный перезапуск (02:00)

## Мета

- **Репозиторий:** `/home/vlad/dev/growdirectoresp32_s3_v3`
- **Референсная реализация:** `/home/vlad/dev/growdirectoresp32_v3` (`diagnostics.h:564-612`, `Settings.h:26,173-185`, `ruleExecutePredictionService.h:6-132`)
- **Цель:** Вернуть в S3 прошивку ежедневный превентивный перезапуск в окне 02:00–02:59 локального времени, не затрагивая offline/MQTT-триггеры.
- **Boot type:** `BOOT_TYPE_TECHNICAL` (`reboot-technical`)
- **Спецификация:** trigger #5 из BA-спецификации technical-restart. Canonical specs-репозиторий (`.specs-repo`) в данный момент указывает на пустую директорию, поэтому план построен на требованиях задачи и legacy-референсе.

## Что уже есть в S3

- Перезапуск по `CONFIGURED_NEED_RESTART` обрабатывается в `OnChangeModuleModeUseCase::loop()` (`src/Core/Domain/UseCases/ModuleMode/OnChangeModuleModeUseCase.h:36-80`).
- `BOOT_TYPE_TECHNICAL` уже приводит к `hardReset()` (deep-sleep), а не `ESP.restart()` (`OnChangeModuleModeUseCase.h:83-97`).
- Диагностика собирается и отправляется в `DiagnosticService::perform()` (`src/Application/Services/DiagnosticService.h:469-626`).
- Единственный оставшийся reboot-триггер — `DiagnosticService::checkOfflineStateAndTriggerReboot()` (полный offline: 15 мин для sensor, 4 ч для остальных).
- В `DiagnosticService` появились `checkMqttConnectionAndReconnect()` и `checkHttpConnectionAndReconnect()` — они делают throttled WiFi reconnect вместо reboot при MQTT/HTTP down >10 мин. Эти методы **не трогаем**.

## Чего не хватает в S3

1. EEPROM-маркера "последний превентивный рестарт был сегодня".
2. Предиката "все выходы выключены" для цифровых и аналоговых устройств.
3. Предиката "правило включит устройство в ближайшие N минут".
4. Защиты от перезапуска во время OTA.
5. Собственно логики принятия решения о превентивном рестарте и запроса `CONFIGURED_NEED_RESTART` + `BOOT_TYPE_TECHNICAL`.

## Архитектурные решения

- Новый domain use case — `PreventiveRestartUseCase` в `src/Core/Domain/UseCases/Diagnostics/`.
- Use case зависит только от core-интерфейсов: `IModuleConfig`, `IDeviceModel`, `IEvaluateRulesUseCase`, `IFirmwareRemoteUpdate`, `INTPTimeAdapter`, `ISettings` (через ModuleConfig), `ILogger`, `IDebugSystem`, `IBootEventLog`.
- Предикат "все выключены" реализуем внутри use case по `IDeviceModel::getAllDevices()`, чтобы не тянуть `DeviceService` из Application слоя в Core.
- Предикат "будет ли правило" добавляем как метод `willAnyRuleTurnDeviceOnWithinMinutes(int minutes)` в `IEvaluateRulesUseCase` / `EvaluateRulesUseCase`, повторно используя существующую логику schedule-правил.
- OTA-защита — метод `isUpdateInProgress()` в `IFirmwareRemoteUpdate`, реализованный через `Update.isRunning()` из Arduino `Update.h`.
- Вызов use case встраиваем в `DiagnosticService::perform()` — именно там "main diagnostics loop", частота 30 с достаточна для часового окна.
- Логирование: через `ILogger` (feature `DIAGNOSTIC_DEBUG`) и через отдельный `BootEventLog` с MQTT-топиком `restart`, который сбрасывается в основном цикле аналогично `bootEventLog`/`otaUpdateLog`.

## Файлы и конкретные изменения

### 1. EEPROM-магазин дня последнего рестарта

**`src/Infrastructure/Configuration/Settings.h`**

Добавить адрес после `EEPROM_NEED_CLEAR_SPIFFS` (558):

```cpp
static const int EEPROM_PREVENTIVE_RESTART_DAY = 560;
```

Добавить методы:

```cpp
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
```

**`src/Core/Domain/Interfaces/ISettings.h`**

Добавить pure virtual:

```cpp
virtual uint8_t getLastPreventiveRestartDay() = 0;
virtual void setLastPreventiveRestartDay(uint8_t day) = 0;
```

> **Примечание 1:** legacy хранит `tm_mday` (1 байт). Это сохраняет INV-1 внутри одного месяца, но на границе месяца возможен повторный рестарт через ~24 ч. Для абсолютной гарантии "не чаще одного за календарный день" можно хранить 3 байта (год-2000, месяц, день) по адресам 560–562. Для совместимости с legacy в этом плане предлагается 1 байт.
>
> **Примечание 2 (ESP32-S3):** В проекте уже используется `EepromAdapter` (`src/Infrastructure/Adapters/eepromAdapter.h`), который оборачивает стандартную Arduino `EEPROM.h`. В ESP32-S3 `EEPROM` — это программная эмуляция поверх NVS (Non-Volatile Storage) во flash-памяти, а не отдельный EEPROM-чип. `EEPROM.begin(1024)` резервирует 1 КБ в NVS, и адрес 560 вписывается в этот диапазон. Запись происходит через `_eeprom.writeByte()` + `_eeprom.commit()`, что на ESP32-S3 транслируется в запись NVS. Для одной записи в сутки износ flash не критичен.

### 2. OTA-защита

**`src/Core/Domain/Interfaces/IFirmwareRemoteUpdate.h`**

Добавить метод:

```cpp
virtual bool isUpdateInProgress() = 0;
```

**`src/Infrastructure/Controllers/HTTP/FirmwareRemoteUpdate.h`**

Реализовать метод:

```cpp
bool isUpdateInProgress() override {
    return Update.isRunning();
}
```

(`Update.h` уже подключён в файле.)

### 3. Предикат "правило включит устройство в ближайшие N минут"

**`src/Core/Domain/Interfaces/UseCases/Rules/IEvaluateRulesUseCase.h`**

Добавить:

```cpp
virtual bool willAnyRuleTurnDeviceOnWithinMinutes(int minutes) = 0;
```

**`src/Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h`**

Добавить публичный метод:

```cpp
bool willAnyRuleTurnDeviceOnWithinMinutes(int minutes) override {
    if (!_ruleRepository || !_ntpTimeAdapter) {
        return false;
    }

    time_t now = _ntpTimeAdapter->getEpochTime();
    if (now == 0) {
        return false;
    }

    time_t futureTime = now + minutes * 60;
    struct tm futureTm;
    if (!gmtime_r(&futureTime, &futureTm)) {
        return false;
    }

    std::vector<Rule> activeRules = _ruleRepository->getActiveRules();
    for (const auto& rule : activeRules) {
        if (rule.mode != RuleMode::ON) {
            continue;
        }
        if (rule.state == RuleState::DELETING) {
            continue;
        }

        if (rule.ruleType == RuleType::SCHEDULE) {
            if (willScheduleRuleTurnOnAt(rule, futureTm)) {
                log("EVALUATE_RULES: PREVENTIVE_RESTART schedule rule %s will be ON at +%d min",
                    rule.uid.c_str(), minutes);
                return true;
            }
        }
        else if (rule.ruleType == RuleType::LOOP) {
            Device* device = _deviceModel ? _deviceModel->getDeviceByUid(rule.deviceUid.c_str()) : nullptr;
            if (device && isDeviceOffForPreventiveRestart(device->state)) {
                log("EVALUATE_RULES: PREVENTIVE_RESTART loop rule %s will turn device %s ON soon",
                    rule.uid.c_str(), rule.deviceUid.c_str());
                return true;
            }
        }
    }

    return false;
}
```

Вынести проверку schedule-окна в приватный helper, аналогичный `evaluateScheduleRule`, но для произвольного `tm`:

```cpp
private:
bool willScheduleRuleTurnOnAt(const Rule& rule, const struct tm& futureTm) {
    for (const auto& item : rule.schedule) {
        struct tm startAt = {0};
        if (!gmtime_r(&item.startAt, &startAt)) {
            continue;
        }

        uint32_t startSec = startAt.tm_hour * 3600 + startAt.tm_min * 60;
        uint32_t endSec = startSec + item.duration;
        uint32_t futureSec = futureTm.tm_hour * 3600 + futureTm.tm_min * 60 + futureTm.tm_sec;

        // today's window
        if (startSec <= futureSec && futureSec < endSec) {
            return true;
        }

        // cross-midnight window from yesterday
        uint32_t futureSecNextDay = futureSec + 24 * 3600;
        if (startSec <= futureSecNextDay && futureSecNextDay < endSec) {
            return true;
        }
    }
    return false;
}
```

### 4. Новый domain use case — PreventiveRestartUseCase

**Новый файл:** `src/Core/Domain/UseCases/Diagnostics/PreventiveRestartUseCase.h`

```cpp
#ifndef PREVENTIVE_RESTART_USE_CASE_H
#define PREVENTIVE_RESTART_USE_CASE_H

#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/IDeviceModel.h"
#include "Core/Domain/Interfaces/UseCases/Rules/IEvaluateRulesUseCase.h"
#include "Core/Domain/Interfaces/IFirmwareRemoteUpdate.h"
#include "Core/Domain/Interfaces/Adapters/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IBootEventLog.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include <cstdarg>
#include <cstdio>

class PreventiveRestartUseCase {
private:
    IModuleConfig* _moduleConfig;
    IDeviceModel* _deviceModel;
    IEvaluateRulesUseCase* _evaluateRulesUseCase;
    IFirmwareRemoteUpdate* _firmwareRemoteUpdate;
    INTPTimeAdapter* _timeAdapter;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IBootEventLog* _restartEventLog;

    static constexpr int PREVENTIVE_RESTART_HOUR = 2;
    static constexpr int PREVENTIVE_RESTART_LOOKAHEAD_MIN = 5;

    void log(const char* format, ...) {
        if (!_logger || !_debugSystem || !_debugSystem->isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG)) {
            return;
        }
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _logger->println(DebugFeatures::DIAGNOSTIC_DEBUG, buffer);
    }

    bool isModuleFullyConfigured() {
        return _moduleConfig && _moduleConfig->getRequestedModuleMode() == CONFIGURED_DONE;
    }

    bool isOtaInProgress() {
        return _firmwareRemoteUpdate && _firmwareRemoteUpdate->isUpdateInProgress();
    }

    bool isInsideRestartWindow(const struct tm& localTime) {
        return localTime.tm_hour == PREVENTIVE_RESTART_HOUR;
    }

    bool wasAlreadyRestartedToday(const struct tm& localTime) {
        ISettings* settings = _moduleConfig ? _moduleConfig->getSettings() : nullptr;
        if (!settings) {
            return false;
        }
        uint8_t lastDay = settings->getLastPreventiveRestartDay();
        return lastDay == static_cast<uint8_t>(localTime.tm_mday);
    }

    bool areAllDevicesOff() {
        if (!_deviceModel) {
            return true; // conservative: no devices -> allow restart
        }
        for (Device* device : _deviceModel->getAllDevices()) {
            if (!device) {
                continue;
            }
            if (!isDeviceOffForPreventiveRestart(device->state)) {
                return false;
            }
        }
        return true;
    }

    bool willAnyRuleStartSoon() {
        return _evaluateRulesUseCase &&
               _evaluateRulesUseCase->willAnyRuleTurnDeviceOnWithinMinutes(PREVENTIVE_RESTART_LOOKAHEAD_MIN);
    }

    void writeRestartLogEntry(const char* message) {
        if (_restartEventLog) {
            _restartEventLog->write(message);
        }
    }

    void markRestartedToday(const struct tm& localTime) {
        ISettings* settings = _moduleConfig ? _moduleConfig->getSettings() : nullptr;
        if (settings) {
            settings->setLastPreventiveRestartDay(static_cast<uint8_t>(localTime.tm_mday));
        }
    }

    void triggerRestart() {
        ISettings* settings = _moduleConfig ? _moduleConfig->getSettings() : nullptr;
        if (!settings || !_moduleConfig) {
            log("PREVENTIVE_RESTART: cannot trigger restart, settings or module config missing");
            return;
        }

        settings->setBootType(BOOT_TYPE_TECHNICAL);
        _moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);
    }

public:
    PreventiveRestartUseCase(
        IModuleConfig* moduleConfig,
        IDeviceModel* deviceModel,
        IEvaluateRulesUseCase* evaluateRulesUseCase,
        IFirmwareRemoteUpdate* firmwareRemoteUpdate,
        INTPTimeAdapter* timeAdapter,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        IBootEventLog* restartEventLog = nullptr)
        : _moduleConfig(moduleConfig),
          _deviceModel(deviceModel),
          _evaluateRulesUseCase(evaluateRulesUseCase),
          _firmwareRemoteUpdate(firmwareRemoteUpdate),
          _timeAdapter(timeAdapter),
          _logger(logger),
          _debugSystem(debugSystem),
          _restartEventLog(restartEventLog) {}

    void perform() {
        if (!_moduleConfig || !_timeAdapter) {
            return;
        }

        if (isOtaInProgress()) {
            log("PREVENTIVE_RESTART: skipped, OTA in progress");
            return;
        }

        if (!isModuleFullyConfigured()) {
            log("PREVENTIVE_RESTART: skipped, module not in CONFIGURED_DONE");
            return;
        }

        struct tm localTime;
        if (!_timeAdapter->getLocalTime(&localTime)) {
            log("PREVENTIVE_RESTART: skipped, local time not available");
            return;
        }

        if (!isInsideRestartWindow(localTime)) {
            return;
        }

        if (wasAlreadyRestartedToday(localTime)) {
            log("PREVENTIVE_RESTART: skipped, already restarted today (day=%d)", localTime.tm_mday);
            return;
        }

        if (!areAllDevicesOff()) {
            log("PREVENTIVE_RESTART: skipped, at least one device is ON");
            return;
        }

        if (willAnyRuleStartSoon()) {
            log("PREVENTIVE_RESTART: skipped, a rule will turn a device ON within %d min",
                PREVENTIVE_RESTART_LOOKAHEAD_MIN);
            return;
        }

        log("PREVENTIVE_RESTART: conditions met, requesting technical restart at %02d:%02d",
            localTime.tm_hour, localTime.tm_min);

        char record[160];
        snprintf(record, sizeof(record),
                 "PREVENTIVE_RESTART: preventive restart at 02:%02d - all devices off and no rules will start",
                 localTime.tm_min);
        writeRestartLogEntry(record);

        markRestartedToday(localTime);
        triggerRestart();
    }
};

inline bool isDeviceOffForPreventiveRestart(DeviceStateEnum state) {
    return state == DEVICE_STATE_OFF ||
           state == DEVICE_STATE_OFF_BY_POWER_ON ||
           state == DEVICE_STATE_DELAY_OFF ||
           state == DEVICE_STATE_STEP_WAITING;
}

#endif
```

**Примечание:** `INTPTimeAdapter` не имеет метода `getLocalTime(struct tm*)`. Нужно добавить его в интерфейс и реализацию (см. раздел 7). Альтернатива — в use case вызывать `getEpochTime()` и `localtime_r()`. Но `getEpochTime()` в `NTPTimeAdapter` сам использует `getLocalTime()`. Чтобы избежать дублирования, удобнее добавить `bool getLocalTime(struct tm* out)` в `INTPTimeAdapter`.

### 5. Обновление DiagnosticService

**`src/Application/Services/DiagnosticService.h`**

Добавить член и параметр конструктора:

```cpp
#include "Core/Domain/UseCases/Diagnostics/PreventiveRestartUseCase.h"

private:
    PreventiveRestartUseCase* _preventiveRestartUseCase;

public:
    DiagnosticService(
        ... // existing params
        PreventiveRestartUseCase* preventiveRestartUseCase = nullptr)
        : ... // existing init
          _preventiveRestartUseCase(preventiveRestartUseCase) {}
```

В `perform()` после `checkWifiConnectionAndReconnect()` добавить:

```cpp
if (_preventiveRestartUseCase) {
    _preventiveRestartUseCase->perform();
}
```

> **Актуально после git pull:** в `DiagnosticService` появились дополнительные поля (`_httpDownSince`, `_lastChannelReconnectTime` и др.) и методы `checkMqttConnectionAndReconnect()` / `checkHttpConnectionAndReconnect()`. Они заменили прежний MQTT-reboot-триггер на WiFi-reconnect. Добавление `_preventiveRestartUseCase` и его вызов после `checkWifiConnectionAndReconnect()` с этими изменениями не конфликтует.

### 6. Обновление IEvaluateRulesUseCase

**`src/Core/Domain/Interfaces/UseCases/Rules/IEvaluateRulesUseCase.h`**

Добавить:

```cpp
virtual bool willAnyRuleTurnDeviceOnWithinMinutes(int minutes) = 0;
```

### 7. Добавление getLocalTime() в time adapter

**`src/Infrastructure/Adapters/Time/INTPTimeAdapter.h`**

Добавить pure virtual:

```cpp
virtual bool getLocalTime(struct tm* out) = 0;
```

**`src/Infrastructure/Adapters/Time/NTPTimeAdapter.h`**

Добавить реализацию:

```cpp
bool getLocalTime(struct tm* out) override {
    if (!out) {
        return false;
    }
    if (!::getLocalTime(out)) {
        log("TIME: Failed to obtain local time\n");
        return false;
    }
    return true;
}
```

(`getLocalTime` из `time.h`/`ESP32Time` уже используется в `NTPTimeAdapter::getEpochTime()` и `getFormattedTime()`.)

### 8. Логирование рестарта

**`src/Infrastructure/Logging/BootEventLog.h`**

Уже реализует `IBootEventLog`. Используем второй экземпляр с топиком `"restart"`.

**`src/globals.h`**

Добавить после `bootEventLog`:

```cpp
BootEventLog restartEventLog(&spiffsAdapter, &mqttLogger, &ntpTimeAdapter, &arduinoAdapter, &wifiAdapter, "restart");
```

Создать use case после `evaluateRulesUseCase` и `firmwareRemoteUpdate`:

```cpp
PreventiveRestartUseCase preventiveRestartUseCase(
    &moduleConfig,
    &deviceModel,
    &evaluateRulesUseCase,
    &firmwareRemoteUpdate,
    &ntpTimeAdapter,
    &mqttLogger,
    &debugSystem,
    &restartEventLog);
```

Передать в `DiagnosticService`:

```cpp
DiagnosticService diagnosticService(
    &restApiService,
    &moduleConfig,
    mqttLogger,
    debugSystem,
    &jsonConverter,
    &wifiAdapter,
    &ntpTimeAdapter,
    &arduinoAdapter,
    &mqttManager,
    &hybridApiService,
    &preventiveRestartUseCase);
```

**`src/Application/Services/LoopServices/LoopServiceConfigureDone.h`**

Добавить член и сеттер:

```cpp
IBootEventLog* restartEventLog = nullptr;

virtual void setRestartEventLog(IBootEventLog* log) {
    this->restartEventLog = log;
}
```

В блоке OTA-лога (`executeLoopImplementation()`, строки 96-106) добавить сброс `restartEventLog`:

```cpp
if (restartEventLog && restartEventLog->hasPending()) {
    restartEventLog->flushToMqttIfAny();
}
```

**`src/Application/Services/LoopServices/LoopServiceFactory.h`**

Передать `restartEventLog` в `LoopServiceConfigureDone` при создании (аналогично `bootEventLog`/`otaUpdateLog`).

### 9. Инициализация в ModuleSetupService / main

Убедиться, что `preventiveRestartUseCase` не требует `init()`. Все зависимости уже инициализированы к моменту первого вызова `DiagnosticService::perform()`.

## Invariants и их реализация

| Invariant | Где обеспечено |
|-----------|----------------|
| INV-1: не более одного рестарта в сутки | `wasAlreadyRestartedToday()` + `setLastPreventiveRestartDay()` |
| INV-2: только в 02:00–02:59 локального времени | `isInsideRestartWindow()` (`tm_hour == 2`) |
| INV-3: пропуск, если устройство ON или правило стартует через ≤5 мин | `areAllDevicesOff()` + `willAnyRuleStartSoon()` |
| INV-4: пропуск при OTA / не-CONFIGURED_DONE | `isOtaInProgress()` + `isModuleFullyConfigured()` |
| INV-5: маркер дня в EEPROM | `EEPROM_PREVENTIVE_RESTART_DAY` + `Settings::{get,set}LastPreventiveRestartDay()` |
| INV-6: BOOT_TYPE_TECHNICAL | `triggerRestart()` вызывает `settings->setBootType(BOOT_TYPE_TECHNICAL)`; `OnChangeModuleModeUseCase` сохраняет его и делает `hardReset()` |

## Edge cases

- **02:30, насос включён правилом** — `areAllDevicesOff()` вернёт `false`, рестарт откладывается на следующий день.
- **02:14, всё OFF, ближайшее schedule-правило в 06:00** — `willAnyRuleStartSoon()` вернёт `false`, рестарт выполняется.
- **02:58 всё OFF, но schedule-правило стартует в 03:00** — `willAnyRuleStartSoon()` для +5 мин вернёт `true` (03:00 внутри [02:58, 03:03]), рестарт пропускается.
- **Рестарт произошёл в 02:20** — `setLastPreventiveRestartDay(tm_mday)` записан. После reboot `OnChangeModuleModeUseCase` видит `CONFIGURED_NEED_RESTART`, выполняет `hardReset()` (RF power-cycle), затем загрузка с `BOOT_TYPE_TECHNICAL`. В 02:45 того же дня повторный рестарт пропускается.
- **OTA началась в 01:55 и длится до 02:30** — `isUpdateInProgress()` вернёт `true`, рестарт пропускается.

## Порядок реализации

1. Добавить `getLastPreventiveRestartDay` / `setLastPreventiveRestartDay` в `ISettings.h` и `Settings.h`.
2. Добавить `isUpdateInProgress()` в `IFirmwareRemoteUpdate.h` и `FirmwareRemoteUpdate.h`.
3. Добавить `getLocalTime(struct tm*)` в `INTPTimeAdapter.h` и `NTPTimeAdapter.h`.
4. Добавить `willAnyRuleTurnDeviceOnWithinMinutes()` в `IEvaluateRulesUseCase.h` и `EvaluateRulesUseCase.h`.
5. Создать `PreventiveRestartUseCase.h`.
6. Обновить `DiagnosticService.h` для вызова use case.
7. Обновить `globals.h`, `LoopServiceConfigureDone.h`, `LoopServiceFactory.h` для wiring и flush `restartEventLog`.
8. Добавить native unit-тесты `test_preventive_restart_use_case.cpp` и зарегистрировать в `test_main.cpp`.
9. Собрать (`just build`) и прогнать тесты (`just test`).

## Тесты

**Новый файл:** `test/test_preventive_restart_use_case.cpp`

Сценарии:

1. `test_preventive_restart_skipped_outside_window` — время 01:59 / 03:00, условия идеальные -> рестарта нет.
2. `test_preventive_restart_skipped_already_today` — `tm_mday` совпадает с EEPROM -> рестарта нет.
3. `test_preventive_restart_skipped_device_on` — одно устройство в `DEVICE_STATE_ON` -> рестарта нет.
4. `test_preventive_restart_skipped_rule_starts_soon` — schedule-правило стартует через 3 мин -> рестарта нет.
5. `test_preventive_restart_skipped_ota_in_progress` -> рестарта нет.
6. `test_preventive_restart_skipped_not_configured` -> рестарта нет.
7. `test_preventive_restart_triggers_when_safe` — 02:14, все OFF, правил нет, OTA нет -> `BOOT_TYPE_TECHNICAL`, `CONFIGURED_NEED_RESTART`, день записан, лог записан.

Моки: расширить существующие `MockModuleConfig`, `MockSettings`, `MockDeviceModel`, `MockNTPTimeAdapter`, `MockFirmwareRemoteUpdate`, `MockEvaluateRulesUseCase`, `MockBootEventLog`.

## Риски и вопросы

- `getLocalTime(struct tm*)` требует изменения `INTPTimeAdapter`. Альтернатива — не менять интерфейс, а в use case вызывать `getEpochTime()` + `localtime_r()`, но `getEpochTime()` сам по сути делает `getLocalTime`, поэтому добавление метода более чисто.
- Schedule-правила в S3 не учитывают `daysOfWeek`/`daysOfMonth` в `evaluateScheduleRule`. Предикат "включит ли через 5 мин" наследует это поведение. Если позже daysOfWeek будут реализованы, предикат нужно будет обновить.
- Аналоговые устройства: `DEVICE_STATE_OFF` считается выключенным, `DEVICE_STATE_ON` — включённым. `DEVICE_STATE_DELAY_OFF` физически включено, поэтому считаем "on" (не в списке off-like).
- Месячная граница: 1-байтовый `tm_mday` допускает рестарт 31-го и 1-го числа двух месяцев подряд. Для полного соответствия INV-1 рекомендуется обсудить переход на 3-байтовую дату.

## Чек-лист завершения

- [ ] `just build` проходит без ошибок.
- [ ] `just test` проходит, включая новые тесты.
- [ ] В `DiagnosticService::perform()` вызывается `PreventiveRestartUseCase::perform()`.
- [ ] При срабатывании в EEPROM пишется день, устанавливается `BOOT_TYPE_TECHNICAL`, запрашивается `CONFIGURED_NEED_RESTART`.
- [ ] Строка "Preventive nightly restart (02:00) — not found" в discrepancy table отмечена как closed.
