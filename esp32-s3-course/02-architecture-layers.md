# Урок 2. Архитектура слоёв: Core / Application / Infrastructure

## Теория

Прошивка построена по Clean Architecture с жёстким правилом направления зависимостей:

```text
Infrastructure -> Application -> Core
```

- **Core (`src/Core/`)** — домен: сущности, enum'ы, use-case'ы, интерфейсы (контракты).
  Запрет: никаких заголовков Arduino, PlatformIO, сети, хранилищ, железа. Только чистый C++.
- **Application (`src/Application/`)** — оркестрация: сервисы, команды, DTO.
  Знает о Core и о контрактах (интерфейсах), не знает деталей железа.
- **Infrastructure (`src/Adapters`, `src/Controllers`, `src/Repositories`)** — всё железное:
  GPIO, Wi-Fi, MQTT/HTTP, EEPROM/SPIFFS, RS-485. Реализует интерфейсы Core.

Зачем так: доменную логику можно собрать и тестировать на десктопе (`env:native`, урок 10),
а железо подменять моками. Зависимость всегда "внутрь" — наружу смотрит только интерфейс.

Ключевые приёмы:

- **Интерфейс в Core, реализация в Infrastructure.** `ISensorReader` (Core/Domain/Interfaces)
  реализован `SensorReader` (Infrastructure/Adapters/Sensors).
- **Use-case** — класс с одной ответственностью: `EvaluateRulesUseCase`, `CheckModuleDeletionUseCase`.
- **DTO** пересекают границы (Application/DTOs): `CommandDto`, `UpdateDto`, `SensorDto`.
- **Composition root** — `src/globals.h`: там создаются все объекты и вручаются друг другу.
  Если вы добавили класс с зависимостями — его экземпляр и связывание добавляются туда.

## Разбор в прошивке

- `src/Core/Domain/UseCases/ModuleMode/CheckModuleDeletionUseCase.h` — эталонный малый use-case:
  зависит от `IHybridAPIService`, возвращает вердикт enum'ом.
- `src/Application/Services/CommandService.h` — разбор входящих команд: предикаты
  `isModuleCommand()`, маршрутизация в use-case'ы.
- `src/globals.h:484` — как выглядит связывание: десятки ссылок в конструктор use-case'а.
- Анти-пример для запоминания: если в `Core/` понадобился `Arduino.h` — вы нарушили слой.
  Решение — вынести нужное за интерфейс (паттерн адаптера, урок 3).

## Задание

Спроектируйте мини-фичу "полив по времени" по слоям (код не обязателен, важны границы):

1. Core: сущность `WateringSchedule`, use-case `IEvaluateWateringUseCase` и что он вернёт.
2. Application: сервис, который вызывает use-case раз в цикл и сообщает решение наружу
   через интерфейс `IWateringActuator` (куда его поместить по правилам зависимостей?).
3. Infrastructure: что реализует `IWateringActuator` на реальном пине.

Проверьте себя: может ли `EvaluateWateringUseCase` попасть в `env:native` без правок?
Если да — слои правильные.
