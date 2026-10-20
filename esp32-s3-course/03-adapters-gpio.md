# Урок 3. Аппаратные абстракции: адаптеры

## Теория

Прямой вызов `digitalWrite()` из бизнес-логики в этой кодовой базе считается ошибкой.
Всё железо прячется за интерфейсом (`IArduinoAdapter`, `ILedAdapter`, `IButtonAdapter`),
а Core/Application видят только контракт. Причины:

- тестируемость — на десктопе вместо пина подставляется мок;
- сменяемость железа — пин/тайминги меняются в одном файле адаптера;
- единая точка для задержек, логирования и подпитки watchdog (см. урок 7).

Практические правила прошивки:

- Задержки — только через адаптер (`_arduino->delay(ms)`), никаких `delay()` напрямую:
  адаптер сам решает, как прожить паузу (в т.ч. кормит task watchdog).
- Каждый вызов зависимости — под guard'ом на null.
- Страп-пины типа детекции модели (`CID_0/1/2 = GPIO 5/6/7`) читаются один раз при старте
  (`ModuleConfig::configureModuleType`) — код платы, а не конфигурация.

## Разбор в прошивке

- `src/Core/Domain/Interfaces/Adapters/` — контракты: `IArduinoAdapter`, `ILedAdapter`,
  `IButtonAdapter`, `ISensorReader`, `IModbusSensorReader`.
- `src/Infrastructure/Adapters/ArduinoAdapter.h` — реализация над Arduino-API.
- `src/Infrastructure/Adapters/Button/ButtonAdapter.cpp`, `Led/` — кнопки и светодиоды.
- `src/Infrastructure/Adapters/ExternalWDT/ExternalWDT.cpp` — внешний сторожевой таймер:
  пример аппаратной сущности с собственным интерфейсом и логикой "пинать только когда
  loop действительно бьётся" (`wdt.configureLoopGate` в `main.cpp`).
- `src/Infrastructure/Configuration/ModuleConfig.h:273-325` — `configureModuleType()`:
  чтение трёх пинов, код 3 → SENSOR, код 1 → DEVICE/RELAY. Тип платы определяется железом,
  а не прошивкой или EEPROM.

## Задание

По образцу `ExternalWDT` спроектируйте адаптер пищалки (buzzer на GPIO):

1. Интерфейс `IBuzzerAdapter` в Core (метод `tone(unsigned int frequencyHz, unsigned int durationMs)`).
2. Реализация `BuzzerAdapter` в Infrastructure с использованием `IArduinoAdapter`
   (не Arduino-функции напрямую из конструктора — только через внедрённый адаптер).
3. Зарегистрируйте экземпляр в `globals.h` рядом со светодиодами.

Проверка: `IBuzzerAdapter` не должен включать ни один заголовок Arduino/PlatformIO.
