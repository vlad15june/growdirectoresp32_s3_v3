# Курс: программирование ESP32-S3 в стиле прошивки GrowDirector

Курс для разработчика, который хочет учиться на реальном промышленном коде:
прошивке модуля GrowDirector (ESP32-S3, Arduino-фреймворк через PlatformIO, C++17).
Каждый урок опирается на конкретные файлы этой репозитории — вы изучаете не абстрактный
"blink", а те самые решения, которые работают на железе: слоистая архитектура, FreeRTOS,
RS-485/Modbus, MQTT/HTTP, watchdog'и, EEPROM/SPIFFS, OTA.

## Инструментарий

- Плата: `esp32-s3-devkitm-1` (8 MB flash, SPI RAM нет).
- Сборка: PlatformIO Core 6.1.19 (`pio`), среда VS Code или консоль.
- Основная среда сборки: `esp32-s3-devkitm-1` (производственная), прошивка на столе: `esp32-s3-devkitm-1-win` (порт COM).
- Подробности всех команд: `AGENTS.md` раздел "Build and Test Commands".

```bash
PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='$(cat VERSION)'" pio run -e esp32-s3-devkitm-1
pio run -e esp32-s3-devkitm-1-win -t upload --upload-port COM3
pio device monitor -p COM3 -b 115200
```

## Как устроен курс

Каждый урок: **Теория → Разбор в прошивке (файл:строка) → Задание**.
Задания маленькие и проверяемые; их можно делать в отдельной ветке, не трогая рабочий код.

## Оглавление

1. [C++ и стиль кодовой базы](01-cpp-and-style.md)
2. [Архитектура слоёв: Core / Application / Infrastructure](02-architecture-layers.md)
3. [Аппаратные абстракции: адаптеры GPIO, кнопки, LED, WDT](03-adapters-gpio.md)
4. [Многозадачность: setup/loop, FreeRTOS, очереди, мьютексы, bus lock](04-freertos-tasks.md)
5. [Хранение: EEPROM (Settings) и SPIFFS (DAO/репозитории)](05-storage-eeprom-spiffs.md)
6. [Связь: Wi-Fi, MQTT, HTTP, outbox, офлайн-поведение](06-network-mqtt-http.md)
7. [Надёжность: watchdog, диагностика, логирование](07-reliability-watchdog-diag.md)
8. [RS-485 и Modbus RTU: кадры, CRC, сканирование датчиков](08-rs485-modbus.md)
9. [OTA и версии: FIRMWARE_VERSION, обновление, bootType](09-ota.md)
10. [Тестирование без железа: env:native, Unity, моки](10-testing-native.md)
11. [Отладка на живом железе: serial-лог, raw-команды по MQTT, разбор кейсов](11-live-debugging.md)

## Главные файлы для изучения (карта репозитория)

- `src/main.cpp` — `setup()`/`loop()`, создание задач FreeRTOS, watchdog.
- `src/globals.h` — composition root: весь объектный граф собирается здесь.
- `src/constants.h` — аппаратные константы (диапазон modbus-адресов, таймауты WDT).
- `platformio.ini` — среды сборки, пины зависимостей, флаги.
- `doc/hardwareApi.md` — контракт firmware ↔ backend (endpoint'ы, MQTT-топики, DTO).
- `doc/sensor-models.md` — каталог датчиков (как описывается железо данными).
