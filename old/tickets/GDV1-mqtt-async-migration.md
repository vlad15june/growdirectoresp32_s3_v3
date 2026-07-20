# GDV1-XXXX: Migrate MQTT stack from PubSubClient to AsyncMqttClient

**Jira Link:** https://growdirector.atlassian.net/browse/GDV1-XXXX

---

## Ticket Description

### Problem Statement

Текущий MQTT-стек построен на `knolleary/PubSubClient` — это синхронная библиотека с блокирующими `connect()`, `publish()`, `subscribe()` (до `socketTimeout`, дефолт 15 с). Чтобы поддерживать keep-alive во время длительных операций, в `main.cpp` поднята отдельная `mqttTask` (Core 1, prio 2), которая бесконечно дёргает `mqttClient.loop()`. Reconnect и обработка входящих живут отдельно — в Arduino loop через `LoopServiceConfigureDone::mqttManager->loop()`. Это даёт race на одном `PubSubClient` между двумя задачами на Core 1, незащищённый `std::queue` для входящих сообщений, и блокирующий `connect()` в Arduino loop, который стопит сенсоры/правила на 5–15 с при разрыве.

### Solution

Полностью перевести MQTT на `marvinroger/AsyncMqttClient` (поверх AsyncTCP). Событийная модель, неблокирующие `publish/subscribe/connect`, своя задача AsyncTCP на Core 0. После миграции:

- `mqttTask` в `main.cpp` **удаляется** — поддержание сокета берёт на себя AsyncTCP
- `mqttManager->loop()` в Arduino loop сводится к опустошению входной очереди и тиковой машине реконнекта (без блокировок)
- `publish()` / `subscribe()` становятся неблокирующими — безопасны для вызова из любой задачи
- Reconnect — событийный (`onDisconnect` → запланировать попытку через таймер) с exponential backoff
- Входящие сообщения копируются в FreeRTOS `xQueue` (а не `std::queue`) — корректно между AsyncTCP task и Arduino loop

### Technical Approach

1. Поменять lib_deps в `platformio.ini`
2. Заменить `PubSubClient` на `AsyncMqttClient` в `globals.h`
3. Удалить `mqttTask` из `main.cpp`
4. Переписать `MqttManager` под событийный API (onConnect/onDisconnect/onMessage/onSubscribe/onPublish)
5. Заменить `std::queue` на `xQueueHandle`
6. Адаптировать `MqttLogger` под новый клиент
7. Обновить моки в `test/Mock/Infrastructure/Mqtt/`

### Related Tickets

- None directly — инфраструктурная миграция

---

## Development Plan

### **Overview**
Перевод MQTT-стека с синхронной библиотеки PubSubClient на асинхронную AsyncMqttClient. Снимает гонку на клиенте между двумя задачами, убирает блокировки в реконнекте, упрощает архитектуру до one-owner. Интерфейс `IMqttManager` сохраняется — внешние сервисы (DeviceService, SensorService, RuleService, DiagnosticService, MqttApiService) **не меняются**.

---

### **Instructions:**

1. **Обновить зависимости PlatformIO**
   - Убрать `knolleary/PubSubClient`
   - Добавить `marvinroger/AsyncMqttClient` и `esphome/AsyncTCP-esphome` (форк me-no-dev, поддерживает S3)

2. **Заменить клиент в `globals.h`**
   - Убрать `WiFiClient`, `PubSubClient mqttClient(...)`
   - Завести `AsyncMqttClient mqttClient` без аргументов в конструкторе
   - Конфигурация (`setServer`, `setCredentials`, `setClientId`, `setKeepAlive`, `setCleanSession`) — внутри `MqttManager::begin()`

3. **Удалить `mqttTask` из `main.cpp`**
   - Убрать `mqttTask()`, `mqttTaskHandle`, вызов `xTaskCreatePinnedToCore` для MQTT
   - В `setup()` после `moduleSetupService.setupModule()` — вызвать `mqttManager.begin()` (инициирует асинхронный connect)

4. **Переписать `MqttManager`**
   - Хранить `AsyncMqttClient&` вместо `PubSubClient&`
   - Регистрировать колбэки: `onConnect`, `onDisconnect`, `onMessage`, `onSubscribe`, `onUnsubscribe`, `onPublish`
   - Внутреннее состояние `_connected` обновляется только из `onConnect`/`onDisconnect`
   - Reconnect-машина: `onDisconnect` → выставить `_reconnectAt = millis() + backoff`; в `loop()` (вызывается из Arduino loop) — если `_reconnectAt` истёк и WiFi up, дёрнуть `_client.connect()`
   - В `onConnect` — ресубскрайб всех топиков из `_subscribedTopics`
   - Заменить `std::queue<MqttMessage>` на `QueueHandle_t` (FreeRTOS), создаётся в `begin()`
   - `onMessage` копирует topic + payload, делает `xQueueSend` (не блокирующий, из AsyncTCP task)
   - `loop()` делает `xQueueReceive` (non-block) и передаёт в `_messageProcessor`
   - Сохранить сигнатуры `IMqttManager` — `bool connect()` теперь значит «запрошен», `bool isConnected()` — кэшированное состояние, `bool publish/subscribe` — true если кинули в очередь библиотеки (packetId != 0)

5. **Адаптировать `MqttLogger`**
   - Поменять `PubSubClient&` на `AsyncMqttClient&`
   - `_mqttClient.publish(topic, qos, retain, payload)` вместо `_mqttClient.publish(topic, payload)`

6. **Обновить моки**
   - `test/Mock/Infrastructure/Mqtt/MockMqttManager.h` — менять не нужно если интерфейс сохранён, но проверить
   - При необходимости — обновить тесты в `test_loop_services.cpp`, `test_diagnostic_service.cpp`, `test_module_setup_service.cpp`

7. **Удалить устаревший код**
   - Health-check блок в `MqttManager::loop()` (state()-based) — у AsyncMqttClient нет `state()`, событие `onDisconnect` уже есть
   - Синхронные `connect()` внутри `publish()` / `subscribe()` — заменить на «если не подключены — вернуть false, не дёргать connect»

---

### **Files to Review Before Starting:**

1. **`src/Infrastructure/Controllers/MQTT/MqttManager.h`** — текущая реализация, понять что переезжает в события
2. **`src/Core/Domain/Interfaces/Mqtt/IMqttManager.h`** — стараться сохранить контракт без изменений
3. **`src/main.cpp` (строки 16-35, 323-332)** — `mqttTask` и его регистрация (всё удалится)
4. **`src/globals.h` (строки 92-148)** — конфигурация MQTT и инстансы
5. **`src/Infrastructure/Logging/MqttLogger.h`** — второе место с `PubSubClient&`
6. **`src/Application/Services/LoopServices/LoopServiceConfigureDone.h:65-68`** — точка вызова `mqttManager->loop()`
7. **`src/Application/Services/ModuleSetupService.h:724-740`** — где инициируется первичный connect
8. **`test/Mock/Infrastructure/Mqtt/MockMqttManager.h`** — мок для тестов

---

### **Files That Will Require Changes:**

#### **1. `platformio.ini`**
- В трёх env (`esp32-s3-devkitm-1`, `esp32-s3-devkitm-1-win`, `esp32s2dev`) заменить:
  - Удалить `knolleary/PubSubClient@^2.8.0`
  - Добавить `marvinroger/AsyncMqttClient@^0.9.0`
  - Добавить `esphome/AsyncTCP-esphome@^2.1.4`

#### **2. `src/globals.h`**
- Заменить include: `#include <PubSubClient.h>` → `#include <AsyncMqttClient.h>`
- Удалить: `WiFiClient wifiClient;`
- Удалить: глобальный stub `mqttCallback` (строки 107-112) — не нужен
- Удалить: `PubSubClient mqttClient(mqttBroker, 1883, mqttCallback, wifiClient);`
- Добавить: `AsyncMqttClient mqttClient;` (без аргументов)
- `MqttLogger mqttLogger(mqttClient, ...)` — тип параметра меняется, инстанс остаётся
- `MqttManager mqttManager(mqttClient, ...)` — тип параметра меняется

#### **3. `src/main.cpp`**
- Удалить строки 15-16 (`TaskHandle_t mqttTaskHandle = nullptr;`)
- Удалить функцию `mqttTask` целиком (строки 20-35)
- Удалить include `<PubSubClient.h>` (строка 7)
- Удалить блок `xTaskCreatePinnedToCore(mqttTask, ...)` (строки 323-332)
- В `setup()` после `moduleSetupService.setupModule()` — добавить `mqttManager.begin()` (метод, инициирующий первичный connect и регистрирующий колбэки)

#### **4. `src/Infrastructure/Controllers/MQTT/MqttManager.h`** (основная переработка)
- Поле: `PubSubClient &_mqttClient` → `AsyncMqttClient &_mqttClient`
- Добавить:
  - `volatile bool _connected = false`
  - `uint32_t _reconnectAt = 0`
  - `QueueHandle_t _messageQueue = nullptr`
  - `SemaphoreHandle_t _topicsMutex = nullptr` (для `_subscribedTopics`)
  - Метод `begin()` — конфигурирует клиент (`setServer`, `setCredentials`, `setClientId`, `setKeepAlive(30)`, `setCleanSession(true)`), регистрирует колбэки, создаёт `xQueueCreate(16, sizeof(MqttMessage*))`, делает первый `_mqttClient.connect()`
- Удалить:
  - `static MqttManager *_instance` + `static mqttCallback` (event handlers — лямбды/std::function на инстанс)
  - `_mqttClient.setBufferSize(4096)` — у AsyncMqttClient буфер настраивается иначе
  - Весь блок health-check в `loop()`
  - Декодирование MQTT_CONNECT_* кодов — у AsyncMqttClient свой `AsyncMqttClientDisconnectReason`
- Переписать методы:
  - `connect()` — вызывает `_mqttClient.connect()`, фиксирует `_lastReconnectAttempt`, возвращает `true` (запрос принят)
  - `disconnect()` — `_mqttClient.disconnect()`
  - `isConnected()` — возвращает `_connected`
  - `subscribe(topic)` — `_mqttClient.subscribe(topic, 0)`, возвращает `packetId != 0`. Если не подключены — добавить в `_subscribedTopics`, **не вызывать connect**
  - `unsubscribe(topic)` — `_mqttClient.unsubscribe(topic)`
  - `publish(topic, payload)` — `_mqttClient.publish(topic, 0, false, payload)`, возвращает `packetId != 0`. Если не подключены — false, без блокирующего connect
  - `loop()` — только две вещи: (1) drain `_messageQueue` через `xQueueReceive` → `_messageProcessor->processMessage`; (2) если `!_connected && millis() >= _reconnectAt && WiFi.isConnected()` — `_mqttClient.connect()` + обновить `_reconnectAt` с backoff
- Добавить колбэки:
  - `onMqttConnect(bool sessionPresent)` — `_connected = true`, сбросить `_reconnectInterval = 5000`, ресубскрайб `_subscribedTopics`
  - `onMqttDisconnect(AsyncMqttClientDisconnectReason)` — `_connected = false`, `_mqttFails++`, посчитать новый `_reconnectAt`, экспоненциальный backoff (5/10/20/30 с)
  - `onMqttMessage(topic, payload, properties, len, index, total)` — собрать `MqttMessage*`, `xQueueSend` (без блокировки)

#### **5. `src/Infrastructure/Logging/MqttLogger.h`**
- Поле `PubSubClient& _mqttClient` → `AsyncMqttClient& _mqttClient`
- В местах `_mqttClient.publish(topic, msg)` → `_mqttClient.publish(topic, 0, false, msg)`
- `_mqttClient.connected()` → надо завести отдельный флаг или передать `IMqttManager*` для `isConnected()`. Альтернатива — публиковать всегда, AsyncMqttClient вернёт 0 если не готов

#### **6. `src/Core/Domain/Interfaces/Mqtt/IMqttManager.h`**
- **Не меняется** — все методы остаются с теми же сигнатурами, меняется только семантика `connect()` («запрос принят», а не «подключено»). Опционально добавить `void begin()`.

#### **7. `test/Mock/Infrastructure/Mqtt/MockMqttManager.h`**
- Сверить с обновлённым интерфейсом. Если `begin()` добавили — добавить заглушку.

#### **8. `src/Application/Services/ModuleSetupService.h` (строки 724-740)**
- Логика `mqttManager->connect()` + `mqttManager->setupMqttTopicHandlers()` остаётся, но `connect()` теперь только инициирует, не ждёт. Subscribe внутри `setupMqttTopicHandlers()` сейчас сразу зовёт `_mqttClient.subscribe()` — после миграции это OK, библиотека буферизует, либо мы кладём в `_subscribedTopics` и подпишемся в `onConnect`.
- Note: убрать «костыли» `mqttManager->loop()` внутри длинных циклов (строки 1462, 1545, 1827, 1877) — после миграции это не нужно, входящие разгребаются из `xQueue` в Arduino loop, ничего не накапливается на стороне библиотеки.

#### **9. `src/Application/Services/Sensors/SensorService.cpp:265`**
- Убрать «костыль» `mqttManager->loop()` посреди опроса сенсоров — больше не требуется.

---

### **Implementation Details:**

#### **Async event flow (replaces synchronous reconnect):**
1. `setup()` → `mqttManager.begin()` → `_mqttClient.connect()` → возвращает сразу
2. AsyncTCP task (Core 0) поднимает TCP + MQTT хендшейк → колбэк `onMqttConnect`
3. `_connected = true`, ресубскрайб → `_mqttClient.subscribe(topic, qos)` (всё ещё в AsyncTCP context)
4. Брокер шлёт сообщение → AsyncTCP принимает → `onMqttMessage` → `xQueueSend(_messageQueue, msg)`
5. Arduino loop тикает `mqttManager->loop()` → `xQueueReceive` → `_messageProcessor->processMessage`
6. Разрыв → `onMqttDisconnect(reason)` → `_connected = false`, `_reconnectAt = millis() + backoff`
7. Через backoff: `mqttManager->loop()` видит `!_connected`, дёргает `_mqttClient.connect()` снова

#### **Reconnect backoff:**
- Стартовый интервал 5 с, удвоение до 30 с при последовательных фейлах (`_mqttFails`)
- Сброс к 5 с в `onMqttConnect`
- Никаких блокирующих ожиданий — только сравнение `millis()` с `_reconnectAt`

#### **Thread-safety contract:**
- `publish/subscribe/connect/disconnect` — безопасны из любой задачи (AsyncMqttClient внутри сериализует на AsyncTCP task)
- Поле `_connected` — `volatile`, чтения из Arduino loop, запись из AsyncTCP task — atomic для bool на ESP32
- `_messageQueue` — FreeRTOS xQueue, потокобезопасна по контракту
- `_subscribedTopics` (std::set<String>) — читается/пишется и из AsyncTCP task (`onConnect`), и из Arduino loop (`subscribe()` из прикладного кода) → защищать `_topicsMutex`

#### **Build configuration:**
- AsyncMqttClient требует `AsyncTCP` — fork `esphome/AsyncTCP-esphome` обязателен для ESP32-S3 (оригинальный `me-no-dev/AsyncTCP` не поддерживает S3 в актуальных версиях)
- `setKeepAlive(30)` — короче, чем дефолт у большинства брокеров (60 с)
- `setCleanSession(true)` — сохраняем текущее поведение; миграцию на `false` сделать отдельным тикетом если нужны persistent subscriptions

---

### **Testing Checklist:**

- [ ] Сборка проходит для `esp32-s3-devkitm-1`
- [ ] Native unit tests проходят (`just test`)
- [ ] При первой подаче питания устройство подключается к брокеру (`onConnect` лог)
- [ ] Принудительный обрыв WiFi → `onDisconnect` лог, Arduino loop не зависает
- [ ] Восстановление WiFi → reconnect через backoff, лог `onConnect`, ресубскрайб всех топиков
- [ ] Принудительный stop брокера → backoff экспоненциальный (5/10/20/30 с в логах)
- [ ] Запуск брокера обратно → автореконнект
- [ ] `publish()` из SensorService во время разрыва — не блокирует, не падает
- [ ] Входящая команда от бэка во время длительного Modbus-опроса — обрабатывается после возврата из опроса (а не теряется)
- [ ] Диагностический отчёт публикуется штатно
- [ ] Включить `MQTT_DEBUG` фичу в `DebugSystem` — все события логируются
- [ ] Стресс-тест: 30 минут с периодическим reset WiFi — устройство всегда восстанавливает MQTT
- [ ] Heap-фрагментация: `ESP.getFreeHeap()` стабильна после 1000 reconnect-циклов
- [ ] Старые «костыли» `mqttManager->loop()` в ModuleSetupService и SensorService удалены — Arduino loop не виснет на длинных операциях

---

## Implementation Status

**Status:** Not started
**Created:** 2026-05-27
**Completed:**

### Implementation Checklist:

- [ ] platformio.ini — обновить зависимости
- [ ] globals.h — заменить инстансы
- [ ] main.cpp — удалить mqttTask
- [ ] MqttManager.h — переписать под события
- [ ] MqttLogger.h — адаптировать API
- [ ] Удалить health-check и sync-connect-в-publish
- [ ] Убрать `mqttManager->loop()` костыли из ModuleSetupService и SensorService
- [ ] Прогнать native tests
- [ ] Smoke-test на железе

### Implementation Summary:
