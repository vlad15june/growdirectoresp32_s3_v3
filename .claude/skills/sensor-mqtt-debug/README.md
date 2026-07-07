# gd_sensor — CLI отладки RS485-датчиков через MQTT

Гоняет команды работы с шиной (`ModbusRead` / `ModbusWrite` / `SerialWrite`) на **живой модуль в поле** через MQTT-брокер и разбирает ответ в одну строку. Прошивать модуль и подключать Serial-кабель не нужно — модуль уже слушает `ModuleCommandNotification`.

Обёртка над `mosquitto_pub` / `mosquitto_sub` (нужен `brew install mosquitto`). Зависимостей Python нет.

## Что делает за один вызов

Подписка на топики ответов → публикация команды → ожидание → разбор кадра (адрес, функция, 16-битные слова, проверка CRC16-Modbus) → декод инженерных значений. Вместо ручной связки «фоновый `mosquitto_sub` + `mosquitto_pub` + чтение JSON + счёт CRC».

Креды и брокер берутся автоматически из [`src/globals.h`](../../../src/globals.h) (`mqttBroker`, `mqttPort`, `mqttUser`, `mqttPassword`) — **пароль в скрипт не зашит**.

Топики (сверены с прошивкой):
- команда → `GrowDirectorMqttTopic/{uid}`
- ack → `hardware/command_result/{uid}`
- данные → `hardware/raw_io_result/{uid}`

## Установка / требования

- `mosquitto_pub`, `mosquitto_sub` в `PATH` (`brew install mosquitto`).
- Python 3 (stdlib only).
- Сетевой доступ до брокера из `globals.h`.

## Запуск

```bash
export GD_UID=<MAC модуля, XX:XX:XX:XX:XX:XX>
python3 .claude/skills/sensor-mqtt-debug/gd_sensor.py <команда> [флаги]
```

UID обязателен — через `GD_UID` (env) или `--uid`.

### Источник кред (приоритет)

Для каждого параметра: **env → флаг → globals.h**.

| Параметр | env | флаг | дефолт |
|---|---|---|---|
| broker | `GD_BROKER` | `--broker` | из globals.h |
| port | `GD_PORT` | `--port` | из globals.h |
| user | `GD_USER` | `--user` | из globals.h |
| password | `GD_PASS` | `--password` | из globals.h |
| uid | `GD_UID` | `--uid` | — (обязателен) |
| путь к globals.h | — | `--globals` | `src/globals.h` |

## Общие флаги

| Флаг | Дефолт | Смысл |
|---|---|---|
| `--timeout <сек>` | `2.5` | сколько ждать ответа на пробу |
| `--retries <N>` | `0` | повтор по таймауту/нет-ответа перед вердиктом «мёртво» (0 = честная одна проба) |
| `--print-cmd` | off | напечатать эквивалентные mosquitto sub/pub команды |
| `--show-pass` | off | не маскировать пароль в `--print-cmd` |

`--retries` включать осознанно: таймаут — это факт (датчик молчит), ретрай лишь снимает случайные пропуски флаки-шины. Для перебора адресов/каналов ставить `1..2`.

## Команды

### `read` — ModbusRead (прошивка сама считает CRC)
```
read --channel C --address A --register R [--function 3] [--count 1] [--decode xf700a]
```
Основной пинг данных. `A`/`R` можно в hex (`0x70`, `0x0009`). CRC в полях не передавать.

### `read-addr` — прочитать ModBus-адрес станции (broadcast)
```
read-addr --channel C [--register 0x0001]
```
Шлёт по broadcast-адресу 0. `--register 0x0001` — группа MP508/CGH/WS301/GW/VS09; `--register 0x0000` — XF-серия (XF500S/SL/700A). Выделяет байт адреса из ответа. На шине должен быть **один** датчик.

### `scan-addr` — перебор адресов на одном канале
```
scan-addr --channel C --from 0x70 --to 0x73 [--register 0x0009] [--function 3] [--count 1] [--decode xf700a]
```
За один вызов проходит весь диапазон, в конце — `# responders: 0xNN, ...`.

### `scan-ports` — одна проба по нескольким каналам
```
scan-ports [--channels 0,1,2,3] --address A [--register 0x0009] [--decode xf700a]
```
В конце — `# responding channels: ...`.

### `write` — ModbusWrite (прошивка сама считает CRC). ⚠️ меняет железо
```
write --channel C --address A --register R [--function 16] --data HEX
```
`--data` — байты **после регистра**, без CRC. На шине должен быть один датчик; после записи проверять `read` / `read-addr`. Кадр смены адреса брать из [modbus-sensor-address-commands.md](../../../specs/docs/features/sensor/modbus-sensor-address-commands.md), не сочинять.

### `serial` — SerialWrite (литеральный кадр)
```
serial --channel C --bytes HEX [--no-expect]
```
Байты уходят на линию **как есть** — передаёшь полный кадр **с CRC** (готовая HEX-строка из дока по датчикам). `--no-expect` — не ждать ответа.

### `crc` — offline-хелпер (устройство не трогает)
```
crc "FF 03 00 01 00 01"    # -> FF0300010001C014
```
Дописывает CRC16-Modbus к неполному кадру (для сборки `serial`-кадров).

## Формат вывода

Одна строка на пробу:

```
ch1 addr=0x70 reg=0x0009 fn=3 n=1 | OK  frame=7003021ABD0F5E addr=0x70 words=[6845] crc=ok temp=28.45C
ch3 addr=0x70 reg=0x0009 fn=3 n=1 | TIMEOUT (Read timeout)
ch9 addr=0x70 reg=0x0009 fn=3 n=1 | ERROR: Invalid channel
```

- **OK** — датчик ответил, кадр разобран; `crc=ok` = контрольная сумма сошлась (`crc=BAD` — кадр битый/обрезан).
- **TIMEOUT** — команда дошла до модуля, датчик на шине не ответил за ~1000 мс.
- **ERROR** — модуль отверг команду (неверный канал / кривой hex), на шину ничего не ушло.
- **MODBUS_EXCEPTION code=N** — устройство вернуло modbus-исключение (или прилетел мусор/коллизия, где старший бит функции взведён).

`--decode <model>` дораскладывает слова в физические величины. Модели и формулы живут в карте `SENSOR_DECODERS` в самом `gd_sensor.py` — **это источник правды для декода**. `--decode` принимает любую модель из карты (сейчас: `xf700a`). Пример для `xf700a`: `0x0009` temp (÷100−40 °C), `0x000A` humidity (÷100 %), `0x000B` pressure (÷10 hPa), `0x000C` wind (m/s), `0x000D` dir, `0x000E` rain, `0x000F` solar, `0x0010` lux, `0x0012` CO2.

### Добавление модели

Модели добавляются по мере тестирования на живом датчике:
1. Снять сырые слова: `read --channel C --address A --register R --count N`, убедиться `crc=ok`.
2. Сверить регистры/формулы по [modbus-sensor-address-commands.md](../../../specs/docs/features/sensor/modbus-sensor-address-commands.md).
3. Добавить `"<model>": { <register>: ("<name>", <lambda v: ...>, "<unit>"), ... }` в `SENSOR_DECODERS`.
4. Прогнать `read ... --decode <model>` на живом датчике и проверить осмысленность значений.

Значения `--decode` обязаны сходиться с реальным датчиком; при расхождении приоритет за живым датчиком, карту в CLI поправить.

## Примеры

```bash
export GD_UID=9C:13:9E:9A:51:9C

# жив ли датчик на канале 1 (XF700A, адрес 0x70)
python3 gd_sensor.py read --channel 1 --address 0x70 --register 9 --decode xf700a

# на каком канале сидит станция 0x70
python3 gd_sensor.py scan-ports --address 0x70 --register 9 --decode xf700a --retries 1

# перебрать адреса 0x70..0x7F на канале 0
python3 gd_sensor.py scan-addr --channel 0 --from 0x70 --to 0x7F --retries 1

# полный блок погоды (8 регистров с 0x0009)
python3 gd_sensor.py read --channel 0 --address 0x70 --register 9 --count 8 --decode xf700a

# текущий адрес станции XF-серии (один датчик на шине)
python3 gd_sensor.py read-addr --channel 0 --register 0x0000

# показать эквивалентные mosquitto-команды (пароль замаскирован)
python3 gd_sensor.py read --channel 1 --address 0x70 --register 9 --print-cmd
```

## Коды возврата

- `0` — есть ответ (`OK`) / для `scan-*` — есть хотя бы один ответивший.
- `1` — таймаут / нет ответивших.
- `2` — ошибка аргументов или отсутствующих кред.

## Ограничения (из контракта прошивки)

| Поле | Допустимо |
|---|---|
| `channel` | 0–3 |
| `count` (ModbusRead) | 1–125 |
| `data` (ModbusWrite) | ≤ 252 байт |
| `bytes` (SerialWrite) | ≤ 255 байт |
| read timeout на модуле | ~1000 мс |

Полный контракт — [contracts/backend-firmware/06-module-commands.md](../../../specs/docs/contracts/backend-firmware/06-module-commands.md).
Транспорт и обработка ошибок — [features/module-command/raw-bus-cli-guide.md](../../../specs/docs/features/module-command/raw-bus-cli-guide.md).
Регистры и кадры по моделям — [features/sensor/modbus-sensor-address-commands.md](../../../specs/docs/features/sensor/modbus-sensor-address-commands.md).

## Безопасность

- Пароль не печатается в чат и не хардкодится: берётся из `globals.h`, в `--print-cmd` маскируется (раскрытие — только `--show-pass`).
- `write` / `serial` меняют состояние устройства (в т.ч. ModBus-адрес). На шине должен быть один датчик; смену адреса делать по готовому кадру из дока, с проверкой после.
