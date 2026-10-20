# Урок 4. Многозадачность: loop, задачи FreeRTOS, очереди, мьютексы

## Теория

ESP32-S3 — двухъядерный SoC с FreeRTOS. Прошивка не крутит всё в `loop()`:
`loop()` — это control loop (оркестрация, быстрые решения), а тяжёлая и ждущая работа
уехала в фоновые задачи. Состав (см. `src/main.cpp` и `src/globals.h`):

- **sensor acquisition task** — весь обмен с шинами (RS-485/I2C): сканирование, опрос
  датчиков, исполнение raw-команд (`ModbusRead/SerialWrite`).
- **network task (outbox)** — все исходящие API-вызовы (structure, update, diagnostic).
- **command network task** — входящие команды (module_command, devices_command).
- **catalog IO** — запись каталога датчиков в SPIFFS.
- кнопки — задача на core 0, опрос каждые 50 мс (`xTaskCreatePinnedToCore`).

Правила выживания на шинах:

- **Один владелец шины.** RS-485 делят несколько задач, поэтому перед каждой транзакцией —
  `BusLockGuard` (recursive mutex). Долгие последовательности (скан порта, запись адреса)
  держат шину целиком; комментарий-пояснение — в `SensorReader.h` (`setBusLock`).
- **API с мьютексом:** `RestApiService` держит `_apiMutex`, чтобы HTTP-клиент не рвали
  друг друга; комментарии к нему объясняют бюджет времени (task watchdog).
- **Очереди вместо флагов:** задачи общаются объектами-командами (`ISensorAcquisition`
  принимает raw-команды), а не глобальными bool.
- **Статика вместо кучи:** стеки задач и объекты создаются статически (`globals.h`),
  порядок конструирования важен — кодовая база полагается на глобальное построение.

## Разбор в прошивке

- `src/main.cpp` — создание задач, `WDT_TIMEOUT_S`, гейт внешнего WDT.
- `src/Infrastructure/Scheduling/FreeRtosTaskScheduler.h`, `FreeRtosMessageQueue.h` —
  обёртки над FreeRTOS.
- `src/Infrastructure/Adapters/Sensors/SensorReader.h` (начало класса) — зачем нужен
  `setBusLock`: история с портами на общем UART.
- `src/Application/Services/Sensors/SensorAcquisitionService.h` — входная точка
  raw-команд; комментарии о serial-исполнении.

## Задание

Добавьте задачу `heartbeatTask` (core 1, стек 2048, приоритет 1):

- каждую секунду кладёт в `FreeRtosMessageQueue<int>` номер тика;
- control loop раз в итерацию забирает очередь и пишет `"HB: tick=%d"` через `ILogger`.

Обязательно: подпитка watchdog внутри задачи перед каждой отправкой
(`arduinoAdapter->feedTaskWatchdog()`). Подумайте, почему отправка в полную очередь
не должна блокировать задачу навсегда (подсказка: `xQueueSend` с нулевым таймаутом
и счётчик дропа в диагностику — см. `DIAG-COUNTERS` в уроке 7).
