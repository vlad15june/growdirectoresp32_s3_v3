изучи документацию:
[doc/projectArchitecture.md](../../doc/projectArchitecture.md)

изучи ранее выполненые задачи
[tickets/GDV1-3202.md](../GDV1-3202.md)

изучи файлы кода
[src/Core/Domain/Interfaces/ILogger.h](../../src/Core/Domain/Interfaces/ILogger.h)
[src/Core/Domain/Interfaces/IDebugSystem.h](../../src/Core/Domain/Interfaces/IDebugSystem.h)
[src/Infrastructure/Logging/MqttLogger.h](../../src/Infrastructure/Logging/MqttLogger.h)
[src/Infrastructure/Logging/DebugSystem.h](../../src/Infrastructure/Logging/DebugSystem.h)
[src/Application/Services/Devices/DeviceService.h](../../src/Application/Services/Devices/DeviceService.h)

вопросы по описанию задачи
1. Должна ли настройка канала логирования (MQTT/Serial/Both) быть глобальной для всей debug-фичи или можно менять в разных местах кода для одной и той же фичи?
2. Нужно ли сохранять настройку канала логирования для debug-фичи в постоянную память (SPIFFS/EEPROM) или достаточно задавать при инициализации?
3. Должна ли быть возможность менять канал логирования для debug-фичи через MQTT команды или web API во время работы модуля?

Добавить возможность указывать для каждой debug-фичи куда должны выводиться логи - только в MQTT, только в Serial или в оба канала одновременно. Сейчас логгер умеет направлять вывод в разные каналы, но эта настройка глобальная для всего логгера. Нужно чтобы при регистрации каждой debug-фичи можно было указать ее канал логирования. Например, для фичи "ip_local_change" логи должны идти только в MQTT, для "sensor_error" - только в Serial, а для "module_command" - в оба канала. Когда код проверяет включена ли debug-фича и выводит лог, система должна автоматически использовать настроенный для этой фичи канал логирования.
