# Урок 11. Отладка на живом железе

## Теория

Плата в поле не подключена к вашему JTAG — рабочие инструменты отладки это serial-лог,
аккуратные raw-команды на шину и дисциплина «одно изменение → одно наблюдение».
Все приёмы ниже — реальные, они вскрыли каждую из проблем на стенде CC-FL-120W.

**Золотые правила:**

1. **Лог — первичен, предположения — нет.** Одна строка `DIAG-COUNTERS` или
   `STARTUP_SCAN: skipping sensor type CC-FL-120W (canScan=false, ...)` решает спор
   быстрее часа гадания. Формат лога — `ключ=значение` — грепается.
2. **Проверяйте на уровне кадров.** «Датчик не опознаётся» — это гипотеза;
   «пинг `71 03 00 00 00 01` молчит, wide `71 03 00 00 00 02` отвечает 9 байтами» — факт.
3. **Меняйте одну вещь за раз** и сохраняйте захваты логов с номерами — diff логов
   короче diff кода.
4. **Знайте, какая плата перед вами.** MAC из esptool при прошивке и `uid` в строках
   `NET: ... "uid":"..."`/`diagnostic` обязаны совпадать с вашими ожиданиями —
   иначе вы отлаживаете чужую плату (реальный случай: на стенде оказалась другая
   плата со старой прошивкой).
5. **Верьте маркерам в бинарнике.** Сборка может быть протухшей: проверяйте
   `strings .pio/build/<env>/firmware.elf | grep <уникальная строка>` — нет строки,
   нет и кода. (На нашем стенде окружение `-win` как раз шило старый бинарник.)

## Инструменты

**1. Serial-захват.** `pio device monitor -p COM3 -b 115200` — интерактивно;
для разбора — захват в файл: `pio device monitor -p COM3 -b 115200 | tee capture.log`,
потом `grep -a -E 'STARTUP_SCAN|ModbusSensorReader|DIAG-COUNTERS' capture.log`.
Только один процесс может держать порт: перед прошивкой закрывайте монитор.

**2. Raw-команды через MQTT** (без перепрошивки!). Модуль слушает
`GrowDirectorMqttTopic/<uid>` (`TopicHandlers.h`); команда `ModuleCommandNotification`:

```json
{"growDirectorUid":"<UID>","type":"ModuleCommandNotification","commandUid":"dbg-1",
 "command":"SerialWrite","channel":3,"bytes":"7104000000027AFB","expectResponse":true}
```

- `SerialWrite` — сырой кадр на RS-485 канала `channel`, ответ — в
  `hardware/command_result/<uid>` (`{"status":"error","message":"Read timeout"}` или DONE с hex).
- `ModbusRead`/`ModbusWrite` — то же по полям `address/function/register/count`.
- `Reboot` — перезагрузка модуля (удобно перегнать стартовый скан после перекоммутации).

Минимальный publisher на чистом Python (без paho; MQTT 3.1.1, QoS0):

```python
import socket, struct
def enc_str(s): return struct.pack(">H", len(s)) + s
def enc_len(n):
    o = b""
    while True:
        b = n % 128; n //= 128
        if n: b |= 0x80
        o += bytes([b])
        if not n: return o
s = socket.create_connection(("116.203.235.144", 1883), timeout=10)
vh = enc_str(b"MQTT") + bytes([4, 0xC2]) + struct.pack(">H", 60)
pl = enc_str(b"dbg-client") + enc_str(b"GrowDirectorMqtt") + enc_str(b"<PASSWORD из globals.h>")
s.sendall(bytes([0x10]) + enc_len(len(vh)+len(pl)) + vh + pl); s.recv(4)
pub = enc_str(b"GrowDirectorMqttTopic/<UID>") + b'<JSON из примера выше>'
s.sendall(bytes([0x30]) + enc_len(len(pub)) + pub)
```

Осторожно: креды в `globals.h:142-148` — секрет прошивки, не светите их в логах/тикетах;
`ModbusWrite`/запись адреса меняют состояние устройств на общей шине — на стенде
с чужими датчиками шлите адресованные кадры, не broadcast.

**3. Прямой доступ к шине.** Консоль COM-порта (USB-UART к ESP32) до RS-485 не достаёт —
нужен USB-RS485 донгл; тогда любые кадры из Python (`pyserial`) без модуля.

## Разбор в прошивке (где смотреть)

- `src/Infrastructure/Controllers/MQTT/TopicHandlers.h` — подписка и парсинг входящих.
- `src/Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h` —
  `handleSerialWrite`, `handleModbusRead/Write`; результат уходит в outbox -> MQTT.
- `src/Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h` — исполнение на acquisition task.
- Ключевые строки лога для грепа: `SETUP:`, `STARTUP_SCAN:`, `ModbusSensorReader:`,
  `SensorReader:`, `SensorService:`, `NET:`, `MQTT:`, `DIAG-COUNTERS:`,
  `OFFLINE_INDICATION`, `BOOT_SEQ:`.

## Кейсы из практики (как это выглядело)

1. **«Датчик не опознаётся»** → в логе дешёвый проб слышит устройство, подтверждающий
   пинг молчит → кадровый разбор: отвечает только на 2-регистровое чтение → фикс
   wide-probe (урок 8). Проверка после прошивки: в пинге две строки `Ping sending bytes`.
2. **«В приложении датчика нет»** → `skipping sensor type CC-FL-120W (canScan=false)` —
   проблема была в каталоге backend, не в прошивке.
3. **«Значение застыло 1.00»** → raw-кадр байт-в-байт одинаковый 30 минут при крутящихся
   колёсиках → эксперимент FC04 через `SerialWrite` → `Read timeout` → гипотеза
   отвергнута, вопрос ушёл вендору.
4. **Полный перебор адресов** нового молчащего устройства: 247 `SerialWrite` подряд
   (пейсинг ~1 с) → «шина пуста» → проверка питания/проводов, а не кода.
5. **Чужая плата на столе** — `uid` в диагностике не тот → сначала прошить/проверить,
   потом искать баг.

## Задание

Проведите полный цикл диагностики с одним реальным датчиком:

1. Захватите лог загрузки; выпишите из него 5 фактов: версию прошивки, uid платы,
   `bootType`, число stored-сensor'ов, результат сканирования (адрес/тип или тишина).
2. Пошлите через MQTT `SerialWrite`: документированное чтение вашего датчика;
   сохраните JSON результата (DONE с hex / timeout).
3. Сымитируйте «потерю» датчика (отключите питание) и добейтесь в логе осмысленной
   строки отказа; покажите её и объясните, какой счётчик в `DIAG-COUNTERS` при этом
   растёт.
4. Оформите вывод в 5 строк: симптом → факт из лога → гипотеза → проверка → вердикт.

Критерий: по вашим 5 строкам коллега без платы должен понять, где искать причину.
