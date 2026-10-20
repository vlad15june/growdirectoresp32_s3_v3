# Урок 6. Связь: Wi-Fi, MQTT, HTTP, outbox

## Теория

Модуль живёт на связи постоянно, но связь ненадёжна — поэтому вся исходящая коммуникация
построена вокруг **outbox**: событие кладётся в очередь, сетевая задача доставляет
с повторами и паузами (1/2/5/10 мин), пока бэкенд не ответит 200. Никаких "шлём и забыли"
для структуры — регистрация считается состоявшейся только при доставке structure.

Каналы:

- **Wi-Fi** — список сетей в `/wifiNetworks.json` + фолбэк на EEPROM ssid/pass.
- **MQTT** — основной транспорт (AsyncMqttClient). Исходящие топики:
  `hardware/update/<uid>`, `hardware/structure/<uid>`, `hardware/diagnostic/<uid>`,
  `hardware/command_result/<uid>`, история `hardware/history/...`. Входящие:
  `GrowDirectorMqttTopic/<uid>` (`ModuleCommandNotification`, `RuleCommandNotification`, ...).
- **HTTP REST** — тот же контракт (`/api/v1/HardWare/*`): structure, update, is_deleted,
  rulelist, sensor_models, module_command. Параметры аутентификации — query
  `token` + `grow_director_uid`. `HybridAPIService` выбирает/дублирует канал.
- **Удаление модуля** — boot-вопрос `GET is_deleted`: 200+isDeleted:true или 3× подряд 403
  = "меня удалили" → factory reset (`CheckModuleDeletionUseCase`). Именно поэтому 403 —
  штатный сигнал, а не ошибка.

Правила поведения при обрыве: офлайн-решения принимаются локально (правила продолжают
работать), исходящее копится в outbox и истории (`hardware/history/*`), при восстановлении
связи — синхронизация (`OnConnectivityRestoredUseCase`).

## Разбор в прошивке

- `doc/hardwareApi.md` — весь контракт endpoint'ов и топиков (читать целиком).
- `src/Application/Services/Net/Outbox.h` — очередь с повторами; комментарии про бюджет
  времени под task watchdog.
- `src/Infrastructure/Controllers/MQTT/TopicHandlers.h:130` — подписка на
  `GrowDirectorMqttTopic/<uid>`.
- `src/Infrastructure/Controllers/Api/HybridAPIService.h` — выбор REST vs MQTT.
- `src/Core/Domain/UseCases/ModuleMode/CheckModuleDeletionUseCase.h` — вердикт по 403.
- `src/Application/Services/Net/LinkSupervisor.h` — офлайн-детект и принудительная
  синхронизация времени после восстановления.

## Задание

Добавьте публикацию "каждые 30 секунд" (на самом деле — событие в outbox с таким интервалом):

1. DTO `HeartbeatDto` (uid, uptimeMs, freeHeap).
2. Событие для outbox (по образцу существующих report-классов в `Outbox.h`).
3. Сериализация в `ArduinoJsonConverter` и публикация в топик `hardware/diagnostic/<uid>`
   (или отдельный `hardware/heartbeat/<uid>` — решите и обоснуйте выбор).
4. Офлайн-тест: отключите Wi-Fi — убедитесь по логу, что события не теряются, а уходят
   пачкой после восстановления связи.
