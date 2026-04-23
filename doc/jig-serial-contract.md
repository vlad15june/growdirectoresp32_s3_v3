# JIG Serial Output Contract

Документ описывает, какие строки программа JIG (`GrowDirector_prog`) ожидает увидеть в UART-выводе прошивки при загрузке платы, что выводит текущая прошивка S3, и что нужно исправить, чтобы JIG снова мог тестировать плату.

## Где это используется в JIG

Логика ожидания и парсинга находится в:

- [py/board_to_42/board_to_42.py:9-29](../../../GrowDirector_prog/py/board_to_42/board_to_42.py) — функция `wait_board_booting(timeout, keyword, serial_port, log)`
- [py/run_test_scripts/run_test_scripts.py:366-396](../../../GrowDirector_prog/py/run_test_scripts/run_test_scripts.py) — обработчик `WAITBOOTBOARD`
- Тесты, которые её вызывают: `WAITBOOTBOARD|10|9|SETUP: done` — см. [py/run_test_scripts/hydro_s3_com.test:8](../../../GrowDirector_prog/py/run_test_scripts/hydro_s3_com.test#L8) и все остальные `*.test` в папке `py/run_test_scripts/`.

Упрощённый фрагмент:

```python
if 'SETUP: isConfigured =' in incoming_data:
    mode = ''.join(filter(str.isdigit, incoming_data))
if keyword in incoming_data:        # keyword = "SETUP: done"
    return 0, mode
```

Далее в `run_test_scripts.py` (строки 379-387):

```python
if boot_result == 0 and mode == '166':
    result = 'Pass'
elif boot_result == 0 and mode != '166':
    result = 'Fail'  # "Board in mode X!!! Stop testing!!!"
elif boot_result == 1:
    result = 'Fail'  # "Booting timeout has expired!!! Stop testing!!!"
```

То есть, чтобы прошивка успешно прошла `WAITBOOTBOARD`, она должна:

1. Вывести строку, содержащую подстроку `SETUP: isConfigured =` и цифры режима (ожидается `166` — `AP_TEST_MODE`).
2. Завершить boot-последовательность строкой, содержащей подстроку `SETUP: done`.

Обе строки должны появиться **в пределах таймаута** (обычно 10 с, указано в `.test`-файле).

## Что выводит старая прошивка (работает с JIG)

Ключевые строки из `Out Logs 1.txt` (раздел «Old»):

```
SETUP: module type code: 4
SETUP: moduleType: 3 moduleSubType: 0
SETUP: Inizializing FS...
SETUP: Inizializing FS done.
SETUP: restoreAllSettings -> isConfigured: 166
Eeprom time = 2035-01-05T05:27:15Z
SETUP: 1.12.1
SETUP: hw/D8:85:AC:A8:EE:14
SETUP: isConfigured = 166        ← JIG извлекает mode
...
-------setupDevicesForTest---------
AP IP address: 192.168.4.1
AP IP address: 192.168.0.1
setupApTestWebServer
ApTestWebServer begin
ApTestWebServer all ok
...
Mode(isConfigured): 166;
WiFi.status: 255;
WiFi.RSSI: 0;
2035-01-05T05:27:17Z
SETUP: done                      ← JIG считает boot завершённым
```

## Что выводит текущая прошивка S3 (JIG НЕ работает)

Ключевые строки из `Out Logs 1.txt` (раздел «New»):

```
SETUP: SPIFFS initialized
SETUP: module type code: 4
SETUP: moduleType: 3 moduleSubType: 0 deviceCount: 3 ledsCount: 3
...
SETUP: Settings initialized for module type: 3, subtype: 0
SETUP: Module configured as Type: 3, SubType: 0, DeviceCount: 3
SETUP: LEDs initialized for 3 devices
...
SETUP: Button initialized on pin: 9
SETUP: LED on-load sequence executed
SETUP: Current configuration status: 166    ← это НЕ то, что ищет JIG
MODULE: Module mode set to: 166
MODULE: Current module mode set to: 166
SETUP: Requested module mode set to: 166
SETUP: Module is in AP_TEST_MODE
SETUP: Module is in AP_TEST_MODE - configuring for board testing
...
-------setupDevicesForTest---------
SETUP: Test devices setup completed
SETUP: LED off-load sequence executed
SETUP: Module setup completed                ← это НЕ то, что ищет JIG
SETUP: Creating loop service for module mode: 166
```

## Итог — что ломает работу с JIG

| # | Ожидает JIG | Текущая прошивка выводит | Последствие |
|---|---|---|---|
| 1 | Строка, содержащая `SETUP: isConfigured = <цифры>` | `SETUP: Current configuration status: 166` и `SETUP: Requested module mode set to: 166` — нужная подстрока отсутствует | `mode` остаётся `None`; ниже ветка `elif boot_result == 0 and mode != '166'` возвращает `Fail` с сообщением `Board in mode None!!! Stop testing!!!` |
| 2 | Строка, содержащая `SETUP: done` | `SETUP: Module setup completed`, затем `SETUP: Creating loop service for module mode: 166` — подстроки `SETUP: done` нет | `wait_board_booting` уходит в таймаут, возвращает `boot_result=1`, тест валится с `Booting timeout has expired!!! Stop testing!!!` |

## Что нужно поправить в прошивке

Добавить (или переименовать существующие) две `Serial`-строки в boot-последовательности. Достаточно совместимости по подстрокам — JIG использует `in`, а не `==`.

### 1. Вывод режима в формате, понятном JIG

**Где**: [src/Application/Services/ModuleSetupService.h:322](../src/Application/Services/ModuleSetupService.h#L322)

Сейчас:

```cpp
log("SETUP: Current configuration status: %d", isConfigured);
```

Нужно (минимальный вариант — добавить параллельную строку, не трогая существующую):

```cpp
log("SETUP: Current configuration status: %d", isConfigured);
log("SETUP: isConfigured = %d", isConfigured);
```

Либо заменить формат текущей строки на `SETUP: isConfigured = %d`, если нет других потребителей старого формата.

Требования:

- В строке должно быть **число** — только цифры из этой строки JIG склеивает в `mode` через `''.join(filter(str.isdigit, incoming_data))`. В текущем формате числа отсутствуют только до `%d`, так что подставленное значение станет режимом. ВАЖНО: никакие другие цифры в этой же строке не допускаются.
- Для режима `AP_TEST_MODE` число должно быть `166`.
- Строка должна быть выведена **до** `SETUP: done`.

### 2. Маркер окончания загрузки

**Где**: [src/Application/Services/ModuleSetupService.h:1376](../src/Application/Services/ModuleSetupService.h#L1376)

Сейчас:

```cpp
log("SETUP: Module setup completed");
```

Варианты:

- **Предпочтительно**: добавить отдельную финальную строку после завершения всей инициализации (в том числе `Creating loop service`), чтобы она была последней и однозначно сигнализировала «борд готов»:
  ```cpp
  log("SETUP: done");
  ```
  Разместить её после строки `SETUP: Creating loop service for module mode: %d` (строка 1395) либо в `setup()` в `main.cpp` после того, как `loopService` создан.

- Альтернативно — изменить текст существующей строки на `SETUP: Module setup completed. SETUP: done` (подстрока `SETUP: done` попадёт в вывод). Этот вариант хуже, потому что в логах будут висеть «лишние» хвосты инициализации loop-service после этой строки.

Порядок появления строк обязателен: сначала строка с `SETUP: isConfigured =`, затем `SETUP: done`. JIG ищет их последовательно в одном и том же цикле чтения — если `SETUP: done` появится раньше, чем `isConfigured =`, `mode` останется `None`.

## Проверка

После изменений прогон `WAITBOOTBOARD|10|9|SETUP: done` из `hydro_s3_com.test` должен завершиться:

```
RESULT WAITBOOTBOARD: SETUP: done
CYCLE 1: Boot_result= 0; mode= 166
```

Если в логах появляется `Found mode!!!!! MODE: 166` (лог уровня INFO), значит пункт 1 выполнен; если сразу следом `RESULT WAITBOOTBOARD: SETUP: done` — выполнен и пункт 2.

## Ссылки на файлы

- Прошивка, точка вывода режима: [src/Application/Services/ModuleSetupService.h:322](../src/Application/Services/ModuleSetupService.h#L322)
- Прошивка, точка завершения setup: [src/Application/Services/ModuleSetupService.h:1376](../src/Application/Services/ModuleSetupService.h#L1376)
- Прошивка, запуск loop-service: [src/Application/Services/ModuleSetupService.h:1395](../src/Application/Services/ModuleSetupService.h#L1395)
- JIG, парсер boot: [../../GrowDirector_prog/py/board_to_42/board_to_42.py](../../../GrowDirector_prog/py/board_to_42/board_to_42.py)
- JIG, runner тестов: [../../GrowDirector_prog/py/run_test_scripts/run_test_scripts.py](../../../GrowDirector_prog/py/run_test_scripts/run_test_scripts.py)
- JIG, тест-сценарий S3: [../../GrowDirector_prog/py/run_test_scripts/hydro_s3_com.test](../../../GrowDirector_prog/py/run_test_scripts/hydro_s3_com.test)
