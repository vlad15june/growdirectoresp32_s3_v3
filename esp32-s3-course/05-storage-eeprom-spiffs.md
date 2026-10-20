# Урок 5. Хранение: EEPROM (Settings) и SPIFFS (DAO/репозитории)

## Теория

В прошивке два вида персистентности, и они разделены осознанно:

**EEPROM — маленькие критичные поля.** `Settings` (header-only) хранит плоские значения
по фиксированным адресам: SSID (0), пароль (40), токен (120), сервер (160), признак
configured (220), wasInitialSyncCompleted (556) и т.д. Обязательные приёмы:

- смещения — именованные константы/комментарии, не "магические числа" по всему коду;
- маркер-флаг `needClearSpiffs` (0xA5): factory reset сначала ставит маркер, а физическая
  очистка SPIFFS происходит на следующей загрузке (`main.cpp`) — так не рвутся чужие циклы.

**SPIFFS — файлы с данными.** Данные идут через слой DAO: `SpiffsSensorDao`, `SpiffsRuleDao`,
`SpiffsSensorTypeDao` (base path `/data`). Паттерны:

- запись через временный файл + rename (`sensors.json.tmp -> sensors.json`) — атомарность;
- стриминг JSON для больших массивов, чтобы не держать всё в RAM;
- репозиторий с кэшем (`SensorRepository`, `SensorTypeRepository`) — DAO знает про файлы,
  репозиторий — про сущности и инвалидацию кэша.

**JSON.** Везде ArduinoJson 6.21.6 (пин точной версии!): `PooledJsonDocument`,
`deserializeJson`, ручные проверки `containsKey`. Правило: значения с дефолтами через `| fallback`.

## Разбор в прошивке

- `src/Infrastructure/Configuration/Settings.h` — адреса EEPROM и геттеры/сеттеры.
- `src/Infrastructure/Configuration/ModuleConfig.h` — runtime-состояние (не путать с EEPROM).
- `src/Infrastructure/Adapters/SPIFFS/SpiffsDao.h` (`clear`, запись), `SpiffsSensorDao.h` (base path `/data`).
- `src/Infrastructure/Repositories/SensorTypeRepository.h:275` — пример ветвления по `canScan`.
- `src/Application/Services/ClearModuleDataService.h` — что стирает factory reset, а что
  намеренно сохраняет (токен, Wi-Fi) — обязательно к прочтению, прежде чем добавлять поле.

## Задание

Добавьте в `Settings` счётчик "число включений" (boot count):

1. Адрес EEPROM: следующий свободный после PREVENTIVE_RESTART_DAY (560) → выберите 562,
   uint16, комментарием в списке адресов.
2. Геттер `getBootCount()` / сеттер `setBootCount(uint16_t)` с `EEPROM.put/get`.
3. В `setup()` инкремент и один лог `"BOOT: count=%u"`.
4. Продумайте: что будет при OTA-обновлении (EEPROM не стирается) и при factory reset
   (решите — стирать ли счётчик в `clearAllData`, и почему).
