# Урок 1. C++ и стиль кодовой базы

## Теория

Прошивка написана на C++17, но использует осознанно узкое подмножество языка —
всё, что помогает предсказуемости на микроконтроллере (320 КБ RAM), и ничего лишнего.

Что реально используется:

- **Классы, часто header-only** — реализация прямо в `.h` (например, `ModuleSetupService.h`).
  Компилятор всё равно встраивает; зато меньше файлов и проще зависимости.
- **Зависимости — через указатели с null-check'ами.** Инфраструктурный код перед каждым
  использованием проверяет внедрённую зависимость:
  ```cpp
  if (clearModuleDataService) { clearModuleDataService->clearAllData(); }
  ```
- **`enum class` для новых перечислений** (`RuleType::STEP`), старые plain `enum`
  (`ModuleModeEnum`) остаются как есть — стиль файла важнее идеала.
- **`static constexpr` для констант класса** — не расходуют RAM, видны при компиляции:
  ```cpp
  static constexpr const char *XF700A_MODEL = "XF700A";
  ```
- **RAII-заместитель ручных пар open/close** — `BusLockGuard` захватывает шину в конструкторе
  и отпускает в деструкторе, даже при раннем `return`.
- `const&` для входных строк и структур; `std::string` — да, но без бесконтрольных
  конкатенаций в цикле (это embedded, каждое выделение памяти — на счету).

## Стиль (сводка из AGENTS.md)

- 4 пробела, K&R-скобки, include guards вида `#ifndef FILE_NAME_H`.
- Классы — PascalCase; интерфейсы — с префиксом `I` (`ILedger`, `ISensorReader`).
- Use-case: интерфейс `I<Name>UseCase` + реализация `<Name>UseCase`.
- Логирование — через `ILogger` + флаги `DebugFeatures::...`, не через `Serial.printf` напрямую.
- В заголовках источников проекта нет `.clang-format` — конвенции держатся ревью и примером
  соседнего кода. Правило одно: **новый код не должен выбиваться из файла, который вы правите**.

## Разбор в прошивке

- `src/Core/Domain/UseCases/Led/LedUseCases.cpp` — компактный пример класса с состоянием.
- `src/Infrastructure/Adapters/Sensors/SensorReader.h:33-60` — константы модели и приватные
  хелперы внутри класса (там же прецедент модель-специфичного поведения `isAtlasSensor()`).
- `src/Application/DTOs/CommandDto.h` — простой DTO: поля + методы-предикаты
  `isModuleCommand()` и т.п.

## Задание

Напишите header-only класс `BuzzerController` в духе кодовой базы:

- конструктор принимает `ILogger* logger = nullptr` (и хранит его);
- метод `beep(int times)` — логирует через `logger` форматом `"BUZZER: beep x%d"`, но только
  если `logger != nullptr`;
- `static constexpr unsigned int BEEP_PERIOD_MS = 100;`
- include guard, 4 пробела, K&R.

Проверка: класс должен компилироваться в любом окружении, где виден `ILogger`,
и не тянуть Arduino-заголовки (это правило слоя — о нём урок 2).
