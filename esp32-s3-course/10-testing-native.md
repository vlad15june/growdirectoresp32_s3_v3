# Урок 10. Тестирование без железа: env:native, Unity, моки

## Теория

Слои (урок 2) окупаются здесь: Core/Application собираются для десктопа
(`platform = native@1.2.1`) и гоняются в CI без единой платы.

Устройство тестовой сборки:

- `test_build_src = yes`, `build_src_filter = +<*> -<main.cpp> -<Infrastructure/>` —
  продакшен-исходники Core/Application компилируются в тест-бинарь автоматически;
  `.cpp` тестов никогда не инклюдят продакшен-код напрямую (multiple definition).
- Единая точка входа — `test/test_main.cpp`: ~122 объявлений `extern "C" run_*_tests()`
  и вызовы в `main()`. Новый тест = новый файл + строка регистрации там.
- Моки — `test/Mock/` (`ISettings`, `IModuleConfig`, `ISpiffsAdapter`, ArduinoMock...).
- Device-заголовки подменяются стенд-инами из `test/Mock/native/` (`Arduino.h`, `EEPROM.h`,
  `IPAddress.h`, `esp_heap_caps.h`, `freertos/`, `mbedtls/`), путь добавлен флагом
  `-I test/Mock/native`; в `src/` force-include `stdint.h`.

Фреймворк — Unity (`throwtheswitch/Unity@2.6.1`), пин совпадает с боевым.
Симуляция end-to-end — `test/Sim/`: виртуальные часы, fibers, MQTT-брокер и сеть.
Важный подводный камень (AGENTS.md): `.cpp` реализаций Sim должны лежать в корне `test/`
(`sim_host_*.cpp`) — PlatformIO не собирает `.cpp` из подпапок `test/`.

Как гонять: `pio test -e native -vvv`. Изоляция одной группы — временно закомментировать
остальные вызовы в `test_main.cpp` (отдельного per-file раннера нет).

Важно для Windows-хоста: нативная сборка требует POSIX-окружения (`ucontext.h`, `timegm`).
На чистом MinGW тесты не соберутся — используйте WSL2/Ubuntu или Docker (в репозитории
есть `Dockerfile.ci` — Python + PlatformIO база из CI).

## Разбор в прошивке

- `platformio.ini` — секция `[env:native]`.
- `test/test_main.cpp` — реестр прогонов.
- `test/Mock/Application`, `test/Mock/Core`, `test/Mock/Infrastructure` — зеркало интерфейсов.
- `test/Sim/` — сценарные тесты с виртуальным временем.

## Задание

Покройте тестом логику из урока 2:

1. `EvaluateWateringUseCase` (или маленький аналог по вашему дизайну) — решение "поливать"
   по расписанию; зависимости — от `INtpTimeAdapter`-подобного интерфейса времени.
2. Мок времени в `test/Mock/` (например, `MockClock` с сеттером).
3. Тесты Unity: до времени старта — не поливать, после — поливать, граница дня —
   корректный переход.
4. `run_watering_tests()` в `test_main.cpp`; прогон в WSL/Docker: `pio test -e native`.

Критерий готовности: тест падает, если логика сломана, и проходит без платы на столе.
