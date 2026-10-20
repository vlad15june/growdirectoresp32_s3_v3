# Урок 7. Надёжность: watchdog, диагностика, логирование

## Теория

Прошивка управляет реальным железом в поле без доступа к консоли — поэтому "почему
перезагрузилось" должно быть выяснимо по логам. Три опоры:

**1. Task watchdog.** Все долгие операции (паузы, ожидание кадров, последовательности
записи адреса) обязаны подпитывать WDT. Дисциплина видна по коду:

```cpp
_arduino->delay(responseDelay);   // адаптер внутри кормит watchdog
feedCallerWatchdog();             // там, где паузы ручные
```

После `setup()` WDT затягивается до рабочего `WDT_TIMEOUT_S`; внешний WDT (чип на плате)
пинается только когда control loop реально бьётся (`wdt.configureLoopGate`).

**2. DIAG-COUNTERS.** Раз в период в лог и в диагностику уходит строка счётчиков:
`alloc_fails`, `apiSendFails`, `apiSendFailRatio`, `mqttFails`, `scan_adopted`,
`outbox_lost_completions`, `stack_*` (водяные знаки стека задач) и др. Каждый новый
фоновый механизм обязан принести сюда свой счётчик отказов — "молчаливая потеря"
недопустима. Ищите по `DIAG-COUNTERS` в `src/`.

**3. Логирование.** `ILogger` + флаги `DebugFeatures::RULE_DEBUG`, `SETUP_DEBUG`, ...
Формат — одна строка, ключ=значение через пробел: удобно грепать и разбирать ELK.
Многие сервисы имеют приватный `log(const char* fmt, ...)`-хелпер. `Serial.printf`
напрямую — только в legacy/тестовом коде.

## Разбор в прошивке

- `src/constants.h` — `WDT_TIMEOUT_S`, `WDT_WORST_LEGAL_PAUSE_S` (бюджет паузы).
- `src/main.cpp` — затяжка WDT после setup, гейт внешнего WDT.
- `src/Infrastructure/Adapters/ExternalWDT/ExternalWDT.cpp` — внешний сторож.
- `src/Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h`
  — `feedCallerWatchdog()` в пинге и записи адреса (смотрите комментарии почему).
- `src/Infrastructure/Controllers/HTTP/RestApiService.h` — `trackApiSendFailure/`
  `trackApiSendSuccess` и `apiSendFailRatio`.
- `src/Infrastructure/Logging/` — `MqttLogger`, `BootEventLog`, `OtaUpdateLog`.

## Задание

Проведите учение счётчика дропа вашей heartbeat-очереди (урок 4) до конца:

1. В `DIAG-COUNTERS` добавьте поле `hb_dropped=...`.
2. При переполнении очереди инкремент — атомарно (критическая секция или
   `portENTER_CRITICAL`, FreeRTOS-счётчики на одном писце задаче не нужны).
3. В логе дропа — одна строка: `"HB: queue full, dropped=%u"`.
4. На стенде: забудьте читать очередь 5 минут, добейтесь дропа, покажите строку
   в `DIAG-COUNTERS` и объясните по ней, что произошло.
