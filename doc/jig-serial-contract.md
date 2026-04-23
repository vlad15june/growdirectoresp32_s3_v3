# JIG — правки в прошивке

Конкретные точки изменения. Формат: файл:строка → было / стало.

## Boot

### [ModuleSetupService.h:322](../src/Application/Services/ModuleSetupService.h#L322)
```cpp
log("SETUP: Current configuration status: %d", isConfigured);
```
→
```cpp
log("SETUP: isConfigured = %d", isConfigured);
```

### [ModuleSetupService.h:1376](../src/Application/Services/ModuleSetupService.h#L1376)
```cpp
log("SETUP: Module setup completed");
```
→
```cpp
log("SETUP: Module setup completed");
log("SETUP: done");
```

---

## UART-команды

### `#read_module_type%` — [BoardTest.h:503-513](../src/Infrastructure/BoardTesting/BoardTest.h#L503)
было:
```cpp
snprintf(buffer, sizeof(buffer), "Type:%d SubType:%d", moduleType, moduleSubType);
```
стало:
```cpp
const char* name = "UNKNOWN";
if (moduleType == MODULE_TYPE_HYDRO)                                   name = "HYDRO";
else if (moduleType == MODULE_TYPE_SENSOR)                             name = "SENSOR";
else if (moduleType == MODULE_TYPE_DEVICE && moduleSubType == MODULE_SUBTYPE_RELAY)  name = "RELAY";
else if (moduleType == MODULE_TYPE_DEVICE && moduleSubType == MODULE_SUBTYPE_SOKET)  name = "SOCKET";
else if (moduleType == MODULE_TYPE_DEVICE && moduleSubType == MODULE_SUBTYPE_DIMMER) name = "DIMMER";
arduinoAdapter->serialPrintln(name);
```

### `#check_fs%` — [BoardTest.h:515-521](../src/Infrastructure/BoardTesting/BoardTest.h#L515)
было: `"FileSystem:OK"` → стало: `"done"` (если SPIFFS смонтирован, иначе `"fail"`).

### `#led_g` / `#led_r` — [BoardTest.h:1181](../src/Infrastructure/BoardTesting/BoardTest.h#L1181)
было:
```cpp
snprintf(response, sizeof(response), "%s_ch%d=%d",
         isRed ? "led_r" : "led_g", channel, arduinoAdapter->digitalRead(pin));
```
стало:
```cpp
snprintf(response, sizeof(response), "{LED%s%d=%d}",
         isRed ? "R" : "G", channel, arduinoAdapter->digitalRead(pin));
```

### `#read_pg_3_3` / `#read_pg_5_0` / `#read_pg_5_5` — [BoardTest.h:960-991](../src/Infrastructure/BoardTesting/BoardTest.h#L960)
сейчас все три печатают `"1000"`. Нужно реально читать power-good пины и выдавать `{3.3V:Ok}` / `{3.3V:Fail}` (аналогично для 5.0/5.5). Старый код в [old/aptestmode.h:215-240](../old/aptestmode.h#L215) делегировал в `readPowerGood33/50/55(response)` — этих функций в новой прошивке нет, нужно перенести.

### `#set_ref_voltage` / `#clr_ref_voltage` — [BoardTest.h:872-904](../src/Infrastructure/BoardTesting/BoardTest.h#L872)
сейчас hardcoded `TS<n>=1500` и `TS<n>=0`. Внутри закомментирован рабочий код. Надо:
1. Раскомментировать его.
2. Обеспечить `pin_TST_SNS` не `nullptr` (см. раздел "initializePinArrays" ниже).

Ожидания JIG: `set` → `TSN=0`, `clr` → `TSN=1`.

### `#read_adc -ch N%` — [BoardTest.h:906-920](../src/Infrastructure/BoardTesting/BoardTest.h#L906)
сейчас всегда `chN=2000`. Раскомментировать:
```cpp
int value = arduinoAdapter->analogRead(adcPin[channel]);
snprintf(response, sizeof(response), "ch%d=%d", channel, value);
```
и инициализировать `adcPin`.

### `#do_orp -v M%` — [BoardTest.h:1210-1217](../src/Infrastructure/BoardTesting/BoardTest.h#L1210)
было: всегда `"{DO active}"`. Стало — как в [old/aptestmode.h:442-458](../old/aptestmode.h#L442):
```cpp
inline void BoardTest::handleDoOrp(const std::string &cmd)
{
    size_t vPos = cmd.find("-v ");
    if (vPos != std::string::npos) {
        int v = std::stoi(cmd.substr(vPos + 3));
        arduinoAdapter->pinMode(DO_ORP, arduinoAdapter->getPinModeOutput());
        arduinoAdapter->digitalWrite(DO_ORP, v == 1 ? 1 : 0);
    }
    int state = arduinoAdapter->digitalRead(DO_ORP);
    arduinoAdapter->serialPrintln(state == 0 ? "{DO active}" : "{ORP active}");
}
```
+ инициализировать `DO_ORP`.

### `#set_pump -ch N -v M%` — [BoardTest.h:1201-1208](../src/Infrastructure/BoardTesting/BoardTest.h#L1201)
сейчас `"set_pump_stub"`. Стало:
```cpp
int ch = extractIntParameter(cmd, "-ch");
int v  = extractIntParameter(cmd, "-v");
uint8_t* pumps = moduleConfig->getPumpPins();
arduinoAdapter->pinMode(pumps[ch], arduinoAdapter->getPinModeOutput());
arduinoAdapter->digitalWrite(pumps[ch], v == 1 ? 1 : 0);
char r[16]; snprintf(r, sizeof(r), "ch%d=%d", ch, arduinoAdapter->digitalRead(pumps[ch]));
arduinoAdapter->serialPrintln(r);
```

### `#read_pump -ch N%` — [BoardTest.h:1192-1199](../src/Infrastructure/BoardTesting/BoardTest.h#L1192)
сейчас `"read_pump_stub"`. Аналогично, но только читать.

### `#read_dimmer` / `#on_dimmer` / `#set_dimmer` — [BoardTest.h:1228-1253](../src/Infrastructure/BoardTesting/BoardTest.h#L1228)
все три stub'ы. Порт логики из [old/aptestmode.h:483-528](../old/aptestmode.h#L483). Нужны пины `DIM_OFF[]`, `V_DIM[]` и `ledcSetup/Write`.

### `#scan_i2c` / `#read_i2c` / `#write_i2c` / `#Tibbit_read_i2c` — [BoardTest.h:922-958](../src/Infrastructure/BoardTesting/BoardTest.h#L922)
все возвращают stub-строки или формат, не совпадающий со старым (`addr = %d is ok` / `addr = %d is read_i2c_error %d`). `#scan_i2c` возвращает `scan_i2c_stub` вместо списка адресов.

### `#clear_all_file_log` / `#write_diagnostic` / `#read_startup_log` — [BoardTest.h:1069-1094](../src/Infrastructure/BoardTesting/BoardTest.h#L1069)
все stub'ы. В `hydro_s3_com.test` не используются, но используются в sensor/server-тестах.

---

## ModuleConfig — отсутствующие методы

`BoardTest.h` использует поля `pin_TST_SNS`, `adcPin`, `DO_ORP`, `EC_SDN`, `DIM_OFF`, `V_DIM`, `PUMP_HYDRO`, `pin_SENSOR_PS`, `buttonPin`. Массивы объявлены в приватных полях `BoardTest`, но никто их не инициализирует — [initializePinArrays](../src/Infrastructure/BoardTesting/BoardTest.h#L800) трогает только `pin` (relay).

В [IModuleConfig.h:40-42](../src/Core/Domain/Interfaces/IModuleConfig.h#L40) есть только `getSensorPowerPins / getPumpPins / getPumpStatusPins`. Нужно добавить в интерфейс и в реализацию [ModuleConfig.h](../src/Infrastructure/Configuration/ModuleConfig.h):

```cpp
virtual uint8_t* getTestSensorPins() const = 0;   // pin_TST_SNS (4 штуки)
virtual uint8_t* getAdcPins() const = 0;          // adcPin (4 штуки)
virtual uint8_t  getDoOrpPin() const = 0;         // DO_ORP
virtual uint8_t  getEcSdnPin() const = 0;         // EC_SDN
virtual uint8_t* getDimmerOffPins() const = 0;    // DIM_OFF (2)
virtual uint8_t* getDimmerVoltagePins() const = 0;// V_DIM (2)
```

Константы пинов взять из старого кода (`pin_TST_SNS`, `ADC`, `DO_ORP`, `EC_SDN`, `DIM_OFF`, `V_DIM`) — их в текущем `ModuleConfig.h` вообще нет.

После этого [initializePinArrays](../src/Infrastructure/BoardTesting/BoardTest.h#L800) должен для `MODULE_TYPE_HYDRO`/`MODULE_TYPE_SENSOR` заполнить `pin_TST_SNS`, `adcPin`, `pin_SENSOR_PS`, `PUMP_HYDRO`, `DO_ORP`, `EC_SDN` из `moduleConfig`, и выставить pinMode OUTPUT + начальные уровни (см. [old/aptestmode.h:600-611](../old/aptestmode.h#L600)).

---

## Что НЕ трогать — работает

- `#read_mac_address%` — OK
- `#read_version%` — OK
- `#set_test_button` / `#get_test_button` — OK
- `#set_test_success%` — OK (возвращает `ok` + restart)
- `#init_rtc3028` / `#read_rtc3028` — stub, но формат совпадает с ожиданиями (прошивка их фактически не тестирует)
- AP (`HydroDirector__…`, 192.168.0.1, pwd `123456789`) — OK
- HTTP `/read_mac_address` → `{mac=<MAC>}` — OK
