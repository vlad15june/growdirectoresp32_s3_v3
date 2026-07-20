## Task Description:

Add the ability to specify for each debug feature where logs should be output - only to MQTT, only to Serial, or to both channels simultaneously. Currently, the logger can direct output to different channels, but this setting is global for the entire logger. It is necessary that when registering each debug feature, you can specify its logging channel. For example, for the "ip_local_change" feature, logs should go only to MQTT, for "sensor_error" - only to Serial, and for "module_command" - to both channels. When the code checks if a debug feature is enabled and outputs a log, the system should automatically use the configured logging channel for that feature.

## Questions About Task Description:
1. Should the logging channel configuration (MQTT/Serial/Both) be global for the entire debug feature, or can it be changed in different parts of the code for the same feature?
2. Should the logging channel configuration for debug features be persisted to permanent storage (SPIFFS/EEPROM), or is it sufficient to set it during initialization?
3. Should there be the ability to change the logging channel for a debug feature via MQTT commands or web API during module operation?

## Documentation to Review:
[doc/projectArchitecture.md](../../doc/projectArchitecture.md)

## Previously Completed Related Tasks:
[tickets/GDV1-3202.md](../GDV1-3202.md)

## Code Files to Study:
[src/Core/Domain/Interfaces/ILogger.h](../../src/Core/Domain/Interfaces/ILogger.h)
[src/Core/Domain/Interfaces/IDebugSystem.h](../../src/Core/Domain/Interfaces/IDebugSystem.h)
[src/Infrastructure/Logging/MqttLogger.h](../../src/Infrastructure/Logging/MqttLogger.h)
[src/Infrastructure/Logging/DebugSystem.h](../../src/Infrastructure/Logging/DebugSystem.h)
[src/Application/Services/Devices/DeviceService.h](../../src/Application/Services/Devices/DeviceService.h)

