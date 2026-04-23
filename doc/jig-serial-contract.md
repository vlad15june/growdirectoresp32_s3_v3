# JIG ↔ прошивка — что сломано

Сравнение: старая прошивка [old/aptestmode.h](../old/aptestmode.h) + [old/server_communication.h](../old/server_communication.h) vs новая [src/Infrastructure/BoardTesting/BoardTest.h](../src/Infrastructure/BoardTesting/BoardTest.h) + [src/Infrastructure/Controllers/WebServices/TestModeWebServer.h](../src/Infrastructure/Controllers/WebServices/TestModeWebServer.h).

Тест-сценарий JIG: [hydro_s3_com.test](../../GrowDirector_prog/py/run_test_scripts/hydro_s3_com.test), парсер boot: [board_to_42.py](../../GrowDirector_prog/py/board_to_42/board_to_42.py).

Вывод: **двух строк мало**. Ниже все места, где ответы прошивки не совпадают с тем, что JIG ожидает. Без правок в них тест падает уже через 2-3 шага после boot.

---

## 1. Boot output (обязательно, иначе не дойдёт до команд)

| Ожидает JIG (подстрока) | Сейчас прошивка | Файл |
|---|---|---|
| `SETUP: isConfigured = <N>` | `SETUP: Current configuration status: 166` | [ModuleSetupService.h:322](../src/Application/Services/ModuleSetupService.h#L322) |
| `SETUP: done` | `SETUP: Module setup completed` + `SETUP: Creating loop service for module mode: 166` | [ModuleSetupService.h:1376](../src/Application/Services/ModuleSetupService.h#L1376) |

Порядок важен: `isConfigured =` должен идти до `SETUP: done`. В строке с `isConfigured =` не должно быть других цифр кроме режима — JIG берёт все digits из строки.

---

## 2. Команды UART — несовпадения ответов

Все критичные случаи из `hydro_s3_com.test`. Фрагмент теста `UUT|<cmd>|<type>|<expected>`.

| Команда | Ожидает JIG | Прошивка возвращает | Статус | Где чинить |
|---|---|---|---|---|
| `#read_module_type%` | `HYDRO` (type=11, **критический**, exact + не логировать) | `Type:3 SubType:0` | ❌ падает сразу | [BoardTest.h:503-513](../src/Infrastructure/BoardTesting/BoardTest.h#L503) |
| `#check_fs%` | `done` (exact) | `FileSystem:OK` | ❌ | [BoardTest.h:515-521](../src/Infrastructure/BoardTesting/BoardTest.h#L515) |
| `#led_g -ch N -v M%` | `{LEDGN=M}` (exact) | `led_g_chN=M` | ❌ | [BoardTest.h:1156-1190](../src/Infrastructure/BoardTesting/BoardTest.h#L1156) |
| `#led_r -ch N -v M%` | `{LEDRN=M}` (exact) | `led_r_chN=M` | ❌ | там же |
| `#read_pg_3_3%` | `{3.3V:Ok}` (exact) | `1000` | ❌ stub | [BoardTest.h:960-969](../src/Infrastructure/BoardTesting/BoardTest.h#L960) |
| `#read_pg_5_0%` | `{5.0V:Ok}` (exact) | `1000` | ❌ stub | [BoardTest.h:971-980](../src/Infrastructure/BoardTesting/BoardTest.h#L971) |
| `#set_ref_voltage -ch N%` | `TSN=0` (exact) | `TSN=1500` | ❌ залит хардкод 1500 | [BoardTest.h:872-887](../src/Infrastructure/BoardTesting/BoardTest.h#L872) |
| `#clr_ref_voltage -ch N%` | `TSN=1` (exact) | `TSN=0` | ❌ перепутана семантика | [BoardTest.h:889-904](../src/Infrastructure/BoardTesting/BoardTest.h#L889) |
| `#read_adc -ch N%` | диапазон `min:max` (e.g. `996:1913`) | `chN=2000` всегда | ❌ hardcoded 2000, реального ADC нет | [BoardTest.h:906-920](../src/Infrastructure/BoardTesting/BoardTest.h#L906) |
| `#do_orp -v 0%` | `{DO active}` | `{DO active}` | ✅ |   |
| `#do_orp -v 1%` | `{ORP active}` | `{DO active}` (всегда) | ❌ параметр -v игнорируется | [BoardTest.h:1210-1217](../src/Infrastructure/BoardTesting/BoardTest.h#L1210) |
| `#set_pump -ch N -v M%` | `chN=M` (exact) | `set_pump_stub` | ❌ stub, пин не дёргается | [BoardTest.h:1201-1208](../src/Infrastructure/BoardTesting/BoardTest.h#L1201) |
| `#pump_ps_status -ch N%` | `PPSN=0` / `PPSN=1` | `PPSN=<read>` | ⚠ формат OK, но зависит от корректного `moduleConfig->getPumpStatusPins()` и `getPumpPins()` — если пины не привязаны в ModuleConfig для HYDRO, будет `pump_ps_status_error_pins_not_configured` | [BoardTest.h:1464-1498](../src/Infrastructure/BoardTesting/BoardTest.h#L1464) |
| `#read_mac_address%` | любой ответ (type=0) | MAC | ✅ |   |
| `#read_version%` | любой ответ | version | ✅ |   |
| `#init_rtc3028%` | `RTC 3028 online!` | `RTC 3028 online!` | ⚠ stub, не инициализирует реальный RTC — если его нет на плате, тест всё равно «пройдёт» | [BoardTest.h:523-530](../src/Infrastructure/BoardTesting/BoardTest.h#L523) |
| `#read_rtc3028%` | startsWith `RTC time is` | `RTC time is 2024-01-01 12:00:00` | ⚠ stub, фиксированная дата | [BoardTest.h:532-539](../src/Infrastructure/BoardTesting/BoardTest.h#L532) |
| `#set_test_button N%` | `Ok` (exact) | `Ok` | ✅ |   |
| `#get_test_button%` | `pressed` / `not pressed` | то же | ✅ |   |
| `#set_test_success%` | `ok` (run_test_scripts подменит на "Operation mode") | `ok` + restart | ✅ |   |

---

## 3. HTTP endpoint (WiFi-часть теста)

JIG шлёт GET на `http://192.168.0.1/read_mac_address` и ждёт `{mac=…`.

| Endpoint | Ожидает JIG (startsWith) | Прошивка | Статус |
|---|---|---|---|
| `/read_mac_address` | `{mac=` | `{mac=<MAC>}` | ✅ [TestModeWebServer.h:269-282](../src/Infrastructure/Controllers/WebServices/TestModeWebServer.h#L269) |

AP поднимается корректно (`HydroDirector__…`, IP 192.168.0.1, pwd 123456789). WiFi-тест должен работать **если** UUT до него доживёт.

---

## 4. Итоговый список правок (по приоритету)

### Блокеры (без них тест останавливается на первых шагах)

1. **`SETUP: isConfigured = %d`** — добавить в [ModuleSetupService.h:322](../src/Application/Services/ModuleSetupService.h#L322).
2. **`SETUP: done`** — добавить финальной строкой в [ModuleSetupService.h:1376](../src/Application/Services/ModuleSetupService.h#L1376) (после `Module setup completed`).
3. **`#read_module_type%`** → возвращать `HYDRO` / `SENSOR` / `DIMMER` / `SOCKET` / `RELAY` / `DRY_CONTACT` по `moduleType`/`moduleSubType` ([BoardTest.h:503-513](../src/Infrastructure/BoardTesting/BoardTest.h#L503)). Тест помечен как критический (`type=11`) — при fail вся пачка прекращается.
4. **`#check_fs%`** → печатать `done` если SPIFFS смонтирован ([BoardTest.h:515-521](../src/Infrastructure/BoardTesting/BoardTest.h#L515)).
5. **`#led_g` / `#led_r`** → формат `{LEDG<ch>=<v>}` / `{LEDR<ch>=<v>}` ([BoardTest.h:1181](../src/Infrastructure/BoardTesting/BoardTest.h#L1181)).

### Функциональные stub'ы (реального hardware-теста не происходит)

6. **`#read_pg_3_3` / `#read_pg_5_0`** → `{3.3V:Ok}` / `{5.0V:Ok}` при валидном уровне, иначе `{3.3V:Fail}`. Сейчас возвращают `1000`.
7. **`#set_ref_voltage` / `#clr_ref_voltage`** → реально дёргать `pin_TST_SNS[channel]` и читать его (закомментированный код в `BoardTest.h:876-882`/`893-899` уже это делал — нужно раскомментировать и обеспечить инициализацию `pin_TST_SNS` в `initializePinArrays`, сейчас для него init отсутствует).
8. **`#read_adc -ch N%`** → реально читать `analogRead(adcPin[channel])`. Сейчас всегда `2000` — все диапазоны в тесте (`0:100`, `996:1913`, `3454:5003` и т.п.) **не совпадут** со значением 2000.
9. **`#set_pump -ch N -v M%`** → реально переключать `PUMP_HYDRO[channel]` и возвращать `chN=M`. Сейчас просто `set_pump_stub`.
10. **`#do_orp -v M%`** → при `-v 1` печатать `{ORP active}`, переключать пин `DO_ORP`. Сейчас всегда `{DO active}`.
11. **`#pump_ps_status`** — убедиться, что `moduleConfig->getPumpStatusPins()` и `getPumpPins()` возвращают валидные массивы для HYDRO (иначе будет `pump_ps_status_error_pins_not_configured`).

### Stub'ы, которые «проходят», но ничего не проверяют

12. **`#init_rtc3028` / `#read_rtc3028`** — сейчас возвращают захардкоженные строки без общения с RTC по I²C. Формально тест JIG пройдёт, фактически RTC не проверяется.

---

## 5. `initializePinArrays` — дыра

В [BoardTest.h:800-829](../src/Infrastructure/BoardTesting/BoardTest.h#L800) инициализируются только `pin` (relay) и LEDs. **Не инициализируются** для HYDRO/SENSOR: `pin_TST_SNS`, `adcPin`, `pin_SENSOR_PS`, `PUMP_HYDRO`, `DIM_OFF`, `V_DIM`, `DO_ORP`, `EC_SDN`, `buttonPin` (buttonPin читается в `setupDevicesForTest`, остальных нет).

Пока эти массивы `nullptr`, все команды выше, которые их используют, либо упадут в guard-ветке, либо вернут hardcoded значения. Нужно либо добавить инициализацию по типу модуля (как в старом `setupDevicesForTest` из [old/aptestmode.h:600-654](../old/aptestmode.h#L600)), либо брать пины напрямую из `moduleConfig` в каждом handler.

---

## Короткий ответ

- Одними двумя строками boot не обойтись — тест упадёт на `#read_module_type%` (критический) через секунду после загрузки.
- Минимум 5 блокеров (раздел 4.1) — без них JIG останавливается на первых 5-10 командах.
- Ещё ~6 команд возвращают hardcoded/stub-значения (раздел 4.2) — они формально могут дать «ложно-PASS» или «ложно-FAIL» в зависимости от диапазонов в тесте; реального теста железа не происходит.
- Инициализация пинов для HYDRO/SENSOR неполная — это первопричина того, что многие handler'ы свели к stub'ам.
