# Описание структуры директорий проекта GrowDirector ESP32

## Корневая структура 

### `/src`
Основная директория исходного кода приложения. Содержит весь код микропрограммы ESP32.

### `/test`
Модульные тесты проекта. Содержит тестовые файлы и моки для различных компонентов системы.

### `/lib`
Директория для локальных библиотек проекта (PlatformIO convention).

### `/include`
Директория для глобальных заголовочных файлов (PlatformIO convention).

### `/doc`
Документация проекта.

### `/scripts`
Скрипты для автоматизации задач (например, тестирование, сборка).

### `/tickets`
Описание задач и тикетов разработки.

### `/old`
Устаревший код, оставленный для справки.

---

## Структура `/src` - основной код

### `/src/main.cpp`
Точка входа в приложение. Содержит функции `setup()` и `loop()`, инициализацию глобальных сервисов и задач RTOS.

### `/src/globals.h`
Глобальные переменные и экземпляры сервисов, доступные во всем приложении.

### `/src/constants.h`
Константы приложения (пины, таймауты, размеры буферов и т.д.).

---

## Слой Core (Ядро/Домен)

### `/src/Core/Domain`
**Доменный слой** - сердце бизнес-логики приложения. Не зависит от внешних библиотек и фреймворков.

#### `/src/Core/Domain/Data`
Структуры данных домена.

##### `/src/Core/Domain/Data/Entities`
Доменные сущности - базовые бизнес-объекты (Device, Sensor, Rule, Module и т.д.).

###### `/src/Core/Domain/Data/Entities/Sensor`
Сущности сенсора и его конфигурации каналов: `Sensor.h`, `SensorType.h`, `SensorValue.h`, а также конфиги подключения — `AnalogPinConfig.h`, `I2CRegisterConfig.h`, `ModbusRegisterConfig.h`.

##### `/src/Core/Domain/Data/Models`
Доменные модели - структуры данных, используемые в бизнес-логике.

#### `/src/Core/Domain/Enums`
Перечисления домена (типы устройств, состояния, статусы и т.д.).

#### `/src/Core/Domain/Interfaces`
Интерфейсы (абстракции) для реализации в других слоях. Определяют контракты между слоями.

##### `/src/Core/Domain/Interfaces/API`
Интерфейсы для API сервисов.

##### `/src/Core/Domain/Interfaces/Adapters`
Интерфейсы для адаптеров внешних систем.

###### `/src/Core/Domain/Interfaces/Adapters/Json`
Интерфейсы JSON-адаптеров (конвертеры).

###### `/src/Core/Domain/Interfaces/Adapters/Sensors`
Интерфейсы адаптеров сенсоров (чтение, ридеры).

##### `/src/Core/Domain/Interfaces/BoardTesting`
Интерфейсы для тестирования платы.

##### `/src/Core/Domain/Interfaces/Mqtt`
Интерфейсы для MQTT коммуникации.

##### `/src/Core/Domain/Interfaces/Repositories`
Интерфейсы репозиториев для работы с данными.

##### `/src/Core/Domain/Interfaces/Rules`
Интерфейсы, относящиеся к правилам автоматизации.

##### `/src/Core/Domain/Interfaces/Udp`
Интерфейсы для UDP коммуникации.

##### `/src/Core/Domain/Interfaces/UseCases`
Интерфейсы для use cases (сценариев использования).

###### `/src/Core/Domain/Interfaces/UseCases/DeviceControl`
Интерфейсы use cases управления устройствами.

###### `/src/Core/Domain/Interfaces/UseCases/Rules`
Интерфейсы use cases правил автоматизации.

###### `/src/Core/Domain/Interfaces/UseCases/SensorTypes`
Интерфейсы use cases типов сенсоров.

#### `/src/Core/Domain/UseCases`
Use Cases (сценарии использования) - бизнес-логика приложения, организованная по функциональным возможностям.

##### `/src/Core/Domain/UseCases/Buttons`
Логика работы с кнопками.

##### `/src/Core/Domain/UseCases/DeviceControl`
Логика управления устройствами.

##### `/src/Core/Domain/UseCases/Led`
Логика управления светодиодами.

##### `/src/Core/Domain/UseCases/ModuleCommand`
Логика обработки команд модуля.

##### `/src/Core/Domain/UseCases/ModuleMode`
Логика переключения режимов работы модуля.

##### `/src/Core/Domain/UseCases/Rules`
Логика работы с правилами автоматизации.

##### `/src/Core/Domain/UseCases/SensorControl`
Логика управления сенсорами.

##### `/src/Core/Domain/UseCases/SensorTypes`
Логика работы с типами сенсоров.

##### `/src/Core/Domain/UseCases/Sensors`
Логика работы с показаниями сенсоров.

### `/src/Core/Utils`
Утилиты общего назначения (например, `Optional` для работы с опциональными значениями, `HexCodec` для конвертации hex↔байты). Все header-only утилиты живут здесь в `namespace Utils`, **не** в `Core/Domain/...`. Новые общие хелперы добавлять сюда.

---

## Слой Application (Приложение)

### `/src/Application`
**Слой приложения** - оркестрация бизнес-логики, координация между доменом и инфраструктурой.

#### `/src/Application/Commands`
Паттерн Command для обработки различных типов команд.

- `CommandFactory.h` - фабрика для создания команд
- `DeviceCommand.h` - команды для устройств
- `ModuleCommand.h` - команды для модуля
- `RuleCommand.h` - команды для правил

#### `/src/Application/DTOs`
Data Transfer Objects - объекты для передачи данных между слоями.

- `CommandDto.h` - DTO для команд
- `CommandResultDto.h` - DTO для результатов команд
- `DeviceCommandDto.h` - DTO для команд устройств
- `DeviceDto.h` - DTO для устройств
- `DiagnosticDto.h` - DTO для диагностики
- `ModuleCommandDto.h` - DTO для команд модуля
- `RuleDto.h` - DTO для правил
- `SensorDto.h` - DTO для сенсоров
- `UpdateDto.h` - DTO для обновлений

#### `/src/Application/Interfaces`
Интерфейсы слоя приложения.

##### `/src/Application/Interfaces/LoopServices`
Интерфейсы для сервисов, работающих в основном цикле.

##### `/src/Application/Interfaces/WebServices`
Интерфейсы для веб-сервисов.

#### `/src/Application/Services`
Сервисы приложения - координация use cases и бизнес-логики.

- `CommandService.h` - сервис обработки команд
- `DiagnosticService.h` - сервис диагностики
- `FirmwareUpdateService.h` - сервис обновления прошивки
- `ModuleDataSyncService.h` - сервис синхронизации данных модуля
- `ModuleSetupService.h` - сервис настройки модуля
- `TimeService.h` - сервис работы со временем
- `ClearModuleDataService.h` - сервис очистки данных модуля

##### `/src/Application/Services/Devices`
Сервисы для работы с устройствами.

##### `/src/Application/Services/LoopServices`
Сервисы, работающие в основном цикле приложения.

##### `/src/Application/Services/Rules`
Сервисы для работы с правилами автоматизации.

##### `/src/Application/Services/Sensors`
Сервисы для работы с сенсорами.

---

## Слой Infrastructure (Инфраструктура)

### `/src/Infrastructure`
**Инфраструктурный слой** - реализация технических деталей, взаимодействие с внешними системами и оборудованием.

#### `/src/Infrastructure/Adapters`
Адаптеры для работы с внешними системами и периферией.

- `ArduinoAdapter.h` - адаптер для Arduino API
- `eepromAdapter.h` - адаптер для работы с EEPROM

##### `/src/Infrastructure/Adapters/Button`
Адаптер для работы с кнопками.

##### `/src/Infrastructure/Adapters/Devices`
Адаптеры для различных типов устройств (реле, диммеры и т.д.).

##### `/src/Infrastructure/Adapters/ExternalWDT`
Адаптер для внешнего Watchdog Timer.

##### `/src/Infrastructure/Adapters/Http`
Адаптер для HTTP коммуникации.

##### `/src/Infrastructure/Adapters/Json`
Адаптер для работы с JSON.

##### `/src/Infrastructure/Adapters/Led`
Адаптер для управления светодиодами.

##### `/src/Infrastructure/Adapters/Rs485`
Адаптер для коммуникации по RS485.

##### `/src/Infrastructure/Adapters/Sensors`
Адаптеры сенсоров. Верхний уровень — общий `SensorReader.h`; конкретные реализации разделены на локальные (по шине) и удалённые.

###### `/src/Infrastructure/Adapters/Sensors/HardwareSensors`
Физически подключённые сенсоры, сгруппированы по шине:
- `AnalogSensors/` — `AnalogSensorReader.h` (АЦП-каналы)
- `I2c/` — I2C-сенсоры (`CO2Sensor.h`, `LuxSensor.h`, `SHT4xSensor.h`, `SHT85Sensor.h`)
- `ModbusSensors/` — `ModbusSensorReader.h` (RS485/Modbus)

###### `/src/Infrastructure/Adapters/Sensors/RemoteSensors`
Удалённые сенсоры, получаемые по сети (`RemoteSensorClient.h`).

##### `/src/Infrastructure/Adapters/SPIFFS`
Адаптер для работы с файловой системой SPIFFS.

##### `/src/Infrastructure/Adapters/Time`
Адаптер для работы со временем (NTP, RTC).

##### `/src/Infrastructure/Adapters/WiFi`
Адаптер для управления WiFi соединением.

#### `/src/Infrastructure/BoardTesting`
Код для тестирования платы и периферии.

#### `/src/Infrastructure/Configuration`
Конфигурация приложения.

- `ModuleConfig.h` - конфигурация модуля
- `Settings.h` - настройки приложения

#### `/src/Infrastructure/Controllers`
Контроллеры для обработки входящих запросов из различных источников.

##### `/src/Infrastructure/Controllers/Api`
API контроллеры для REST API.

##### `/src/Infrastructure/Controllers/HTTP`
HTTP контроллеры.

##### `/src/Infrastructure/Controllers/MQTT`
MQTT контроллеры для обработки MQTT сообщений.

##### `/src/Infrastructure/Controllers/Serial`
Контроллеры для Serial коммуникации.

##### `/src/Infrastructure/Controllers/UDP`
Контроллеры для UDP коммуникации.

##### `/src/Infrastructure/Controllers/WebServices`
Контроллеры для веб-сервисов.

###### `/src/Infrastructure/Controllers/WebServices/Launchers`
Запускатели веб-серверов (`WebServerLauncher.h`).

##### `/src/Infrastructure/Controllers/WiFi`
Контроллеры для управления WiFi.

#### `/src/Infrastructure/Logging`
Система логирования.

#### `/src/Infrastructure/Repositories`
Реализация репозиториев для работы с данными (SPIFFS, EEPROM и т.д.).

#### `/src/Infrastructure/Scheduling`
Планировщик задач.

---

## Структура `/test` - тесты

### `/test/Mock`
Моки для тестирования.

#### `/test/Mock/ArduinoMock.h`
Мок Arduino API.

#### `/test/Mock/Application`
Моки для слоя Application.

#### `/test/Mock/Core`
Моки для слоя Core.

#### `/test/Mock/Infrastructure`
Моки для слоя Infrastructure.

#### `/test/Mock/UseCases`
Моки для use cases.

### Тестовые файлы
- `test_button_use_cases.cpp` - тесты use cases кнопок
- `test_diagnostic_service.cpp` - тесты сервиса диагностики
- `test_fill_module_structure.cpp` - тесты заполнения структуры модуля
- `test_hybrid_api_service.cpp` - тесты гибридного API сервиса
- `test_led_use_cases.cpp` - тесты use cases светодиодов
- `test_loop_service_factory.cpp` - тесты фабрики loop сервисов
- `test_loop_services.cpp` - тесты loop сервисов
- `test_main.cpp` - главный файл тестов
- `test_mock_settings.cpp` - тесты моков настроек
- `test_module_config.cpp` - тесты конфигурации модуля
- `test_module_setup_service.cpp` - тесты сервиса настройки модуля
- `test_module_structure.cpp` - тесты структуры модуля
- `test_on_change_local_ip_use_case.cpp` - тесты use case смены IP
- `test_on_change_module_mode_use_case.cpp` - тесты use case смены режима
- `test_rule_converter.cpp` - тесты конвертера правил
- `test_sensor_type_repository.cpp` - тесты репозитория типов сенсоров
- `test_spiffs_sensor_type_dao.cpp` - тесты DAO типов сенсоров
- `test_update_dto_converter.cpp` - тесты конвертера DTO обновлений

---

## Конфигурационные файлы

### `platformio.ini`
Конфигурация PlatformIO (зависимости, настройки сборки, платформы).

### `create_directories.sh`
Скрипт для создания структуры директорий.

---

## Принципы организации

1. **Разделение на слои**: Core (Domain) → Application → Infrastructure
2. **Направление зависимостей**: Infrastructure → Application → Core (Domain)
3. **Независимость домена**: Core/Domain не зависит от внешних библиотек
4. **Использование интерфейсов**: Связь между слоями через абстракции
5. **Тестируемость**: Каждый компонент может быть протестирован изолированно
