# Архитектура проекта GrowDirector ESP32

## Обзор архитектуры

Проект построен на основе **Clean Architecture** (Чистая архитектура) с адаптацией под встраиваемые системы. Архитектура обеспечивает:

- ✅ Независимость бизнес-логики от фреймворков и библиотек
- ✅ Тестируемость всех компонентов
- ✅ Независимость от UI, баз данных и внешних систем
- ✅ Изоляцию изменений (изменения в одном слое не влияют на другие)
- ✅ Переносимость кода между платформами

## Диаграмма слоев

```
┌─────────────────────────────────────────────────────────┐
│                    Infrastructure                        │
│  (Controllers, Adapters, Repositories, Configuration)   │
│  Зависит от: Application, Core, Внешних библиотек       │
└────────────────────┬────────────────────────────────────┘
                     │ (использует)
                     ▼
┌─────────────────────────────────────────────────────────┐
│                     Application                          │
│       (Services, DTOs, Commands, Interfaces)            │
│           Зависит от: Core (Domain)                     │
└────────────────────┬────────────────────────────────────┘
                     │ (использует)
                     ▼
┌─────────────────────────────────────────────────────────┐
│                   Core / Domain                          │
│  (Entities, UseCases, Interfaces, Enums, Utils)         │
│       НЕ ЗАВИСИТ НИ ОТ ЧЕГО (полностью изолирован)      │
└─────────────────────────────────────────────────────────┘
```

## Правило зависимостей

**КРИТИЧЕСКИ ВАЖНО**: Зависимости направлены ТОЛЬКО внутрь (к центру - домену).

```
Infrastructure → Application → Core (Domain)
      ↓              ↓             ✗
   (может)       (может)      (НЕ МОЖЕТ)
 использовать  использовать  использовать
    внешние       только        ничего
   библиотеки     Core         внешнего
```

---

## Слой 1: Core (Domain) - Ядро бизнес-логики

**Расположение**: `/src/Core/`

### Назначение
Сердце приложения. Содержит чистую бизнес-логику без зависимостей от внешних библиотек, фреймворков или технических деталей.

### Структура

#### 1.1 Domain Layer (`/src/Core/Domain/`)

##### `/src/Core/Domain/Data/`
**Что хранится**: Структуры данных домена

- **`Entities/`** - Доменные сущности (базовые бизнес-объекты)
- **`Models/`** - Доменные модели (сервисы управления сущностями)

**Ключевые различия между Entities и Models:**

###### **Entities (Сущности)** - Простые структуры данных
**Назначение**: Представляют бизнес-объекты предметной области (domain objects)

**Характеристики:**
- 🎯 **Только данные** - содержат поля и их типы, минимум логики
- 📦 **POJO/POD стиль** - Plain Old C++ Objects (структуры или классы с данными)
- 🔄 **Передаются между слоями** - используются в Use Cases, Services, Repositories
- 💾 **Сериализуемые** - легко преобразуются в JSON/Binary для хранения
- 🆔 **Имеют идентификаторы** - uid, id для уникальной идентификации

**Примеры из проекта:**
- `Device.h` - представляет физическое устройство (реле, насос)
- `Rule.h` - правило автоматизации со всеми параметрами
- `Sensor/SensorValue.h` - показания сенсора
- `Command.h` - команда для выполнения
- `Diagnostic.h` - диагностическая информация

**Пример Entity:**
```cpp
// Device.h - сущность устройства (Entity)
class Device {
public:
    const char* deviceUid;           // Идентификатор
    DeviceStateEnum state;           // Состояние (on/off)
    DeviceTypeEnum deviceType;       // Тип устройства
    bool isError;                    // Флаг ошибки
    uint32_t timeOn;                 // Время включения
    uint32_t timeOff;                // Время выключения
    const char* ruleUid;             // Связанное правило
    // ... только поля данных, без сложной логики
};

// Rule.h - сущность правила (Entity)
class Rule {
public:
    std::string uid;                 // Идентификатор правила
    int version;                     // Версия
    std::string deviceUid;           // Связанное устройство
    RuleType ruleType;               // Тип правила (sensor/schedule/step)
    RuleMode mode;                   // Режим (on/off/auto)
    double valueLow;                 // Нижний порог
    double valueHigh;                // Верхний порог
    std::vector<ScheduleItem> schedule; // Расписание
    // ... структурированные данные для бизнес-логики
    
    // Простые методы проверки типа
    bool isStepRule() const { return ruleType == RuleType::STEP; }
    bool isSensorRule() const { return ruleType == RuleType::SENSOR; }
};
```

###### **Models (Модели)** - Сервисы управления коллекциями сущностей
**Назначение**: Управляют lifecycle и коллекциями сущностей, предоставляют API для работы с ними

**Характеристики:**
- 🎛️ **Содержат логику управления** - создание, поиск, удаление сущностей
- 📊 **Работают с коллекциями** - хранят std::vector, std::map сущностей в памяти
- 🔍 **Предоставляют методы поиска** - findByUid, getAll, filter
- 🏗️ **Инициализация и настройка** - методы init(), setup()
- 🔗 **Связывают данные** - агрегируют несколько сущностей, обеспечивают связи
- 📐 **Могут содержать бизнес-правила** - валидация, вычисления

**Примеры из проекта:**
- `IDeviceModel.h` + `DeviceModel.h` - управление коллекцией Device
- `ISensorValueModel.h` + `SensorValueModel.h` - управление показаниями сенсоров
- `ModuleStructure.h` - структура модуля со всеми компонентами (сенсоры, реле, насосы)
- `RuleEvaluationResult.h` - результат оценки правил
- `SensorReadResult.h` - результат чтения сенсоров

**Пример Model:**
```cpp
// IDeviceModel.h - интерфейс модели (Model Interface)
class IDeviceModel {
public:
    virtual ~IDeviceModel() = default;
    
    // Инициализация коллекции
    virtual bool init() = 0;
    
    // Доступ к сущностям
    virtual Device* getDeviceByUid(const char* deviceUid) = 0;
    virtual std::vector<Device*> getAllDevices() = 0;
    virtual int getDeviceCount() const = 0;
    
    // Операции с коллекцией
    virtual bool addDevice(Device* device) = 0;
    virtual bool removeDevice(const char* deviceUid) = 0;
    virtual void clearDevices() = 0;
};

// DeviceModel.h - реализация модели (Model Implementation)
class DeviceModel : public IDeviceModel {
private:
    std::vector<Device*> devices;        // Коллекция сущностей в памяти
    IModuleConfig* moduleConfig;         // Зависимости для инициализации
    ModuleStructure* moduleStructure;
    
public:
    bool init() override {
        // Создает Device сущности на основе конфигурации
        clearDevices();
        auto relays = moduleStructure->getAllRelays();
        for (auto relay : relays) {
            DeviceDigitalOut* device = new DeviceDigitalOut();
            device->deviceUid = relay->uid.c_str();
            device->state = DEVICE_STATE_OFF;
            addDevice(device);
        }
        return true;
    }
    
    Device* getDeviceByUid(const char* uid) override {
        // Поиск сущности по идентификатору
        for (auto device : devices) {
            if (strcmp(device->deviceUid, uid) == 0) {
                return device;
            }
        }
        return nullptr;
    }
};

// ModuleStructure.h - комплексная модель структуры модуля
class ModuleStructure {
public:
    std::string growdirectorUid;
    std::vector<Node> nodes;             // Коллекция узлов
    
    // Методы поиска и фильтрации
    Sensor* findSensorByUid(const std::string& sensorUid);
    std::vector<Sensor*> findSensorsByType(const std::string& type);
    Relay* findRelayByUid(const std::string& relayUid);
    std::vector<Relay*> getAllRelays();
    // ... методы для работы с агрегированными данными
};
```

###### **Наглядное сравнение:**

| Аспект | **Entities** | **Models** |
|--------|--------------|------------|
| **Роль** | Бизнес-объект | Менеджер коллекции |
| **Содержимое** | Только данные | Данные + логика управления |
| **Примеры** | Device, Rule, Sensor | DeviceModel, ModuleStructure |
| **Методы** | Getters/setters, простые проверки | init(), find(), add(), remove() |
| **Использование** | Передается между слоями | Живет в памяти, управляет Entity |
| **Инициализация** | Создается по требованию | Инициализируется при старте |
| **Хранение** | Одиночный объект | Коллекция объектов |
| **Зависимости** | Минимум (только типы) | Может иметь зависимости (config, другие models) |

###### **Паттерн использования:**

```cpp
// 1. Entity создаются и передаются
Device device;
device.deviceUid = "relay1";
device.state = DEVICE_STATE_ON;

// 2. Model управляет жизненным циклом Entity
DeviceModel deviceModel(&config, &structure);
deviceModel.init();                      // Создает все Device из конфигурации
deviceModel.addDevice(&device);          // Добавляет в коллекцию
Device* found = deviceModel.getDeviceByUid("relay1"); // Находит в коллекции
found->state = DEVICE_STATE_OFF;         // Изменяет состояние
```

###### **Когда использовать что:**

✅ **Создавайте Entity когда:**
- Нужно представить бизнес-объект (устройство, правило, сенсор)
- Данные будут сохраняться/загружаться (в SPIFFS, EEPROM)
- Объект передается через API (JSON serialization)
- Нужна простая структура без сложной логики

✅ **Создавайте Model когда:**
- Нужно управлять множеством однотипных Entity
- Требуется инициализация из конфигурации
- Нужны методы поиска и фильтрации
- Требуется агрегация нескольких типов Entity
- Нужна бизнес-логика над коллекцией

##### `/src/Core/Domain/Enums/`
**Что хранится**: Все перечисления домена

- `DeviceTypeEnum.h` - типы устройств
- `DeviceStateEnum.h` - состояния устройств
- `ModuleModeEnum.h` - режимы работы модуля
- `SensorStatusEnum.h` - статусы сенсоров
- `RuleEnums.h` - перечисления для правил
- И другие...

**Пример**:
```cpp
// DeviceTypeEnum.h
enum class DeviceTypeEnum : uint8_t {
    RELAY,
    DIMMER,
    PWM,
    // ...
};
```

##### `/src/Core/Domain/Interfaces/`
**Что хранится**: Абстрактные интерфейсы (контракты) для реализации в других слоях

- **`API/`** - интерфейсы API сервисов
- **`Adapters/`** - интерфейсы адаптеров
- **`Repositories/`** - интерфейсы репозиториев
- **`UseCases/`** - интерфейсы use cases
- **`Mqtt/`**, **`Udp/`** - интерфейсы коммуникаций
- Интерфейсы сервисов: `ILogger.h`, `ISettings.h`, `ITimeService.h`, etc.

**Пример**:
```cpp
// IRuleRepository.h - реальный интерфейс из проекта
class IRuleRepository : public IRepository<Rule> {
public:
    virtual ~IRuleRepository() = default;
    virtual Utils::Optional<Rule> get(const char* uid) = 0;
    virtual std::vector<Rule> getAll() = 0;
    virtual bool save(const Rule& entity) = 0;
    virtual bool remove(const char* uid) = 0;
    // ... только чистые виртуальные методы
};
```

##### `/src/Core/Domain/UseCases/`
**Что хранится**: Бизнес-логика (сценарии использования)

Организовано по доменам:
- **`Buttons/`** - логика кнопок (нажатия, долгие нажатия)
- **`DeviceControl/`** - логика управления устройствами (включение, выключение)
- **`Led/`** - логика светодиодной индикации
- **`ModuleCommand/`** - логика обработки команд модуля
- **`ModuleMode/`** - логика переключения режимов (AP/STA/Hybrid)
- **`Rules/`** - логика автоматизации (выполнение правил)
- **`SensorControl/`** - логика работы с сенсорами
- **`SensorTypes/`** - логика типов сенсоров
- **`Sensors/`** - логика обработки показаний

**Пример**:
```cpp
// DeviceCommandExecutionUseCase.h - реальный use case из проекта
class DeviceCommandExecutionUseCase : public IDeviceCommandExecutionUseCase {
    IDeviceModel* deviceModel;
    IArduinoAdapter* arduinoAdapter;
    ModuleStructure* moduleStructure;
    ILogger* logger;
    IDebugSystem* debugSystem;
public:
    bool execute(const std::string& deviceUid, const std::string& requestedState, int commandId) override {
        Device* device = deviceModel->getDeviceByUid(deviceUid.c_str());
        if (!device) return false;
        
        bool turnOn = (requestedState == "on");
        if (executeHardwareCommand(device, pin, turnOn)) {
            device->state = turnOn ? DEVICE_STATE_ON : DEVICE_STATE_OFF;
            return true;
        }
        return false;
    }
};
```

#### 1.2 Utils (`/src/Core/Utils/`)
**Что хранится**: Утилиты общего назначения

- `Optional.h` - обертка для опциональных значений
- Другие helper-классы без внешних зависимостей

### ❌ Запрещено в Core/Domain:
- `#include <Arduino.h>` или любые библиотеки Arduino
- `#include <WiFi.h>` или другие ESP32 библиотеки
- `#include` из слоев Application или Infrastructure
- Прямое использование GPIO, Serial, WiFi и т.д.
- Зависимости от конкретных реализаций (только через интерфейсы)

### ✅ Разрешено в Core/Domain:
- Использование стандартных типов C++ (`uint8_t`, `bool`, `string`, etc.)
- Include файлов из самого Core/Domain
- Определение интерфейсов
- Чистая бизнес-логика

---

## Слой 2: Application - Слой приложения

**Расположение**: `/src/Application/`

### Назначение
Оркестрация бизнес-логики. Координирует use cases, преобразует данные между слоями, управляет потоком выполнения.

### Структура

#### 2.1 Commands (`/src/Application/Commands/`)
**Что хранится**: Паттерн Command для обработки запросов

- `CommandFactory.h` - фабрика команд
- `DeviceCommand.h` - команды устройств
- `ModuleCommand.h` - команды модуля
- `RuleCommand.h` - команды правил

**Назначение**: Инкапсулирует запросы как объекты

#### 2.2 DTOs (`/src/Application/DTOs/`)
**Что хранится**: Data Transfer Objects для передачи данных между слоями

- `DeviceDto.h` - DTO для устройства
- `SensorDto.h` - DTO для сенсора
- `CommandDto.h` - DTO для команды
- `UpdateDto.h` - DTO для обновлений
- И другие...

**Назначение**: Изоляция внутренних структур от внешнего API

**Пример**:
```cpp
// DeviceCommandDto.h - реальный DTO из проекта
struct DeviceCommandDto {
    std::string deviceUid;           // уникальный идентификатор устройства
    std::string requestedState;      // запрашиваемое состояние ("on"/"off")
    int commandId;                   // ID команды
    unsigned long timestamp;         // временная метка
    // ... поля для передачи команд устройствам
};
```

#### 2.3 Interfaces (`/src/Application/Interfaces/`)
**Что хранится**: Интерфейсы слоя приложения

- **`LoopServices/`** - интерфейсы для сервисов основного цикла
- **`WebServices/`** - интерфейсы веб-сервисов
- `ICommandService.h` - интерфейс сервиса команд
- `IDiagnosticService.h` - интерфейс диагностики

#### 2.4 Services (`/src/Application/Services/`)
**Что хранится**: Сервисы приложения

**Основные сервисы**:
- `CommandService.h` - обработка команд
- `DiagnosticService.h` - диагностика системы
- `FirmwareUpdateService.h` - обновление прошивки
- `ModuleSetupService.h` - настройка модуля
- `ModuleDataSyncService.h` - синхронизация данных
- `TimeService.h` - работа со временем
- `ClearModuleDataService.h` - очистка данных

**Подпапки**:
- **`Devices/`** - сервисы устройств
- **`LoopServices/`** - сервисы основного цикла (периодические задачи)
- **`Rules/`** - сервисы правил автоматизации
- **`Sensors/`** - сервисы сенсоров

**Назначение**: Координация use cases, транзакционная логика, преобразование DTOs

**Пример**:
```cpp
// CommandService.h - реальный сервис из проекта
class CommandService : public ICommandService {
    IDeviceCommandExecutionUseCase* deviceCommandUseCase;
    IModuleCommandExecutionUseCase* moduleCommandUseCase;
    ISensorCommandUseCase* sensorCommandUseCase;
    IHybridAPIService* hybridApiService;
    ILogger* logger;
    IDebugSystem* debugSystem;
    
public:
    bool process(const CommandDto& command) override {
        // Определение типа команды и вызов соответствующего use case
        if (command.isDeviceCommand()) {
            DeviceCommandDto deviceCmd = command.toDeviceCommand();
            return deviceCommandUseCase->execute(
                deviceCmd.deviceUid, 
                deviceCmd.requestedState, 
                deviceCmd.commandId
            );
        }
        // ... обработка других типов команд
    }
};
```

### ❌ Запрещено в Application:
- Прямой доступ к оборудованию (GPIO, I2C, SPI)
- `#include` из Infrastructure (только через интерфейсы)
- Знание о конкретных реализациях адаптеров
- Прямая работа с WiFi, HTTP, MQTT

### ✅ Разрешено в Application:
- `#include` из Core/Domain
- Использование интерфейсов из Domain
- Преобразование между DTOs и доменными сущностями
- Координация нескольких use cases
- Транзакционная логика

---

## Слой 3: Infrastructure - Инфраструктурный слой

**Расположение**: `/src/Infrastructure/`

### Назначение
Реализация технических деталей, взаимодействие с внешним миром (периферия, сеть, файловая система, базы данных).

### Структура

#### 3.1 Adapters (`/src/Infrastructure/Adapters/`)
**Что хранится**: Реализация интерфейсов для внешних систем

**Основные адаптеры**:
- `ArduinoAdapter.h` - обертка над Arduino API
- `eepromAdapter.h` - работа с EEPROM

**Подпапки**:
- **`Button/`** - адаптер кнопок (чтение GPIO)
- **`Devices/`** - адаптеры устройств (управление реле, диммерами)
- **`ExternalWDT/`** - внешний Watchdog Timer
- **`Http/`** - HTTP клиент
- **`Json/`** - парсинг/сериализация JSON
- **`Led/`** - управление светодиодами
- **`Rs485/`** - коммуникация RS485
- **`Sensors/`** - чтение данных с сенсоров
- **`SPIFFS/`** - файловая система
- **`Time/`** - NTP, RTC
- **`WiFi/`** - управление WiFi

**Назначение**: Изолирует технические детали от бизнес-логики

**Пример**:
```cpp
// ButtonAdapter.h - реальный адаптер из проекта
class ButtonAdapter : public IButtonAdapter {
    IArduinoAdapter* arduinoAdapter;
    const int buttonPin = BUTTON_PIN;
    unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50;
    
public:
    ButtonAdapter(IArduinoAdapter* arduino) 
        : arduinoAdapter(arduino) {
        arduinoAdapter->pinMode(buttonPin, INPUT_PULLUP);
    }
    
    bool isPressed() override {
        return arduinoAdapter->digitalRead(buttonPin) == LOW;
    }
    
    void updateButtonStates() override {
        // Обработка дребезга контактов и обновление состояний
    }
};
```

#### 3.2 BoardTesting (`/src/Infrastructure/BoardTesting/`)
**Что хранится**: Тесты оборудования

- `BoardTest.h` - тестирование платы, периферии, пинов

#### 3.3 Configuration (`/src/Infrastructure/Configuration/`)
**Что хранится**: Конфигурация приложения

- `ModuleConfig.h` - конфигурация модуля (ID, пины, параметры)
- `Settings.h` - настройки (WiFi credentials, MQTT, API endpoints)

**Назначение**: Централизованное хранение конфигурации

#### 3.4 Controllers (`/src/Infrastructure/Controllers/`)
**Что хранится**: Контроллеры для обработки входящих запросов

**Подпапки**:
- **`Api/`** - REST API контроллеры
- **`HTTP/`** - HTTP обработчики
- **`MQTT/`** - MQTT message handlers
- **`Serial/`** - Serial commands
- **`UDP/`** - UDP packets handlers
- **`WebServices/`** - Web service endpoints
- **`WiFi/`** - WiFi events handlers

**Назначение**: Точка входа запросов. Преобразует внешние запросы → DTOs → вызов сервисов Application

**Пример**:
```cpp
// HybridAPIService.h - реальный контроллер из проекта
class HybridAPIService : public IHybridAPIService {
    RestAPIService* restApiService;
    MqttApiService* mqttApiService;
    IModuleConfig* moduleConfig;
    ApiPriority priority;
    
public:
    void sendDeviceCommand(const DeviceCommandDto& command) override {
        // Преобразование DTO в JSON
        String json = toJson(command);
        
        // Отправка через выбранный API (REST или MQTT)
        if (priority == ApiPriority::REST || priority == ApiPriority::BOTH) {
            restApiService->post("/api/devices/command", json);
        }
        if (priority == ApiPriority::MQTT || priority == ApiPriority::BOTH) {
            mqttApiService->publish("device/command", json);
        }
    }
};
```

#### 3.5 Logging (`/src/Infrastructure/Logging/`)
**Что хранится**: Реализация логирования

- Реализация `ILogger` интерфейса
- Логирование в Serial, SPIFFS, удаленный сервер

#### 3.6 Repositories (`/src/Infrastructure/Repositories/`)
**Что хранится**: Реализация репозиториев

- Реализация интерфейсов репозиториев из Domain
- Работа с SPIFFS, EEPROM, удаленными API
- DAO (Data Access Objects) для хранения/загрузки данных

**Пример**:
```cpp
// RuleRepository.h - реальный репозиторий из проекта
class RuleRepository : public Repository<Rule>, public IRuleRepository {
    ISpiffsRuleDao* ruleDao;
    INTPTimeAdapter* ntpTimeAdapter;
    
public:
    Utils::Optional<Rule> get(const char* uid) override {
        return Repository<Rule>::get(uid);
    }
    
    std::vector<Rule> getAll() override {
        return Repository<Rule>::getAll();
    }
    
    bool save(const Rule& entity) override {
        return Repository<Rule>::save(entity);
    }
    
    bool remove(const char* uid) override {
        return Repository<Rule>::remove(uid);
    }
};
```

#### 3.7 Scheduling (`/src/Infrastructure/Scheduling/`)
**Что хранится**: Вспомогательные компоненты для управления задачами

- `TaskScheduler.h` - планировщик для периодических задач
- `TimerManager.h` - управление таймерами
- Используется для планирования периодических операций (чтение сенсоров, проверка правил)
- RTOS tasks создаются напрямую в main.cpp через xTaskCreate

### ✅ Разрешено в Infrastructure:
- Использование любых внешних библиотек (Arduino, ESP32, WiFi, etc.)
- `#include` из Application и Core
- Прямая работа с оборудованием
- Реализация интерфейсов из Domain

### ⚠️ Важно в Infrastructure:
- Всегда реализовывать интерфейсы из Domain
- Не хранить бизнес-логику (только технические детали)

---

## Правила работы со слоями

### 1️⃣ Core/Domain - НИКАКИХ внешних зависимостей

```cpp
// ✅ ПРАВИЛЬНО
// /src/Core/Domain/UseCases/DeviceControl/DeviceCommandExecutionUseCase.h
#include "Core/Domain/Interfaces/UseCases/IDeviceCommandExecutionUseCase.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

class DeviceCommandExecutionUseCase : public IDeviceCommandExecutionUseCase {
    IDeviceModel* deviceModel;      // ← интерфейс из Domain
    IArduinoAdapter* arduinoAdapter; // ← интерфейс из Domain
    ModuleStructure* moduleStructure;
    // ...
};
```

```cpp
// ❌ НЕПРАВИЛЬНО - нарушение границ слоев!
#include <Arduino.h>  // ← внешняя библиотека в Domain!
#include "Infrastructure/Adapters/LedAdapter.h"  // ← Infrastructure в Domain!

class DeviceCommandExecutionUseCase {
    LedAdapter* ledAdapter;  // ← конкретная реализация вместо интерфейса!
    
    void execute() {
        digitalWrite(5, HIGH);  // ← прямая работа с железом в Domain!
        delay(1000);            // ← Arduino функции в Domain!
    }
};
```

### 2️⃣ Application - только Core/Domain

```cpp
// ✅ ПРАВИЛЬНО
// /src/Application/Services/CommandService.h
#include "Core/Domain/Interfaces/UseCases/IDeviceCommandExecutionUseCase.h"
#include "Application/DTOs/CommandDto.h"
#include "Application/DTOs/DeviceCommandDto.h"

class CommandService : public ICommandService {
    IDeviceCommandExecutionUseCase* deviceCommandUseCase;  // ← интерфейс из Domain
    IModuleCommandExecutionUseCase* moduleCommandUseCase;  // ← интерфейс из Domain
    // ...
};
```

```cpp
// ❌ НЕПРАВИЛЬНО
#include "Infrastructure/Adapters/LedAdapter.h"  // ← Infrastructure в Application!
#include "Infrastructure/Repositories/RuleRepository.h"  // ← конкретная реализация!
#include <WiFi.h>  // ← прямая работа с WiFi

class CommandService {
    LedAdapter* ledAdapter;      // ← конкретный класс вместо интерфейса!
    RuleRepository* ruleRepo;    // ← конкретная реализация вместо интерфейса!
    WiFiClient client;           // ← технические детали в Application!
};
```

### 3️⃣ Infrastructure - реализация через интерфейсы

```cpp
// ✅ ПРАВИЛЬНО
// /src/Infrastructure/Adapters/Led/LedAdapter.h
#include <Arduino.h>  // ← можно использовать Arduino в Infrastructure
#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"

class LedAdapter : public ILedAdapter {  // ← реализуем интерфейс из Domain
    ILogger* logger;
    IDebugSystem* debugSystem;
public:
    void turnOn(uint8_t pin) override {
        digitalWrite(pin, HIGH);  // ← работа с железом только здесь
        logger->log("LED on pin %d turned ON", pin);
    }
    
    void turnOff(uint8_t pin) override {
        digitalWrite(pin, LOW);
        logger->log("LED on pin %d turned OFF", pin);
    }
};
```

---

## Точка входа приложения

### `main.cpp` и `globals.h`

**Расположение**: `/src/main.cpp`, `/src/globals.h`, `/src/constants.h`

**Назначение**: Композиция приложения (Dependency Injection container)

```cpp
// globals.h - глобальные экземпляры (реальные из проекта)
// Адаптеры (Infrastructure)
ButtonAdapter buttonAdapter(&arduinoAdapter);
LedAdapter ledAdapter(&mqttLogger, &debugSystem);
WifiAdapter wifiAdapter(wifiMulti, &mqttLogger, &debugSystem);
SpiffsAdapter spiffsAdapter(&mqttLogger, &debugSystem);

// Репозитории (Infrastructure)
RuleRepository ruleRepository(&ruleDao, &ntpTimeAdapter, &debugSystem, &mqttLogger);
SensorRepository sensorRepository(&sensorDao, &debugSystem, &mqttLogger);
SensorTypeRepository sensorTypeRepository(&sensorTypeDao);

// Use Cases (Core/Domain)
DeviceCommandExecutionUseCase deviceCommandExecutionUseCase(
    &deviceModel, &arduinoAdapter, &moduleStructure, &mqttLogger, &debugSystem);
EvaluateRulesUseCase evaluateRulesUseCase(
    &deviceModel, &ruleRepository, &arduinoAdapter, &ntpTimeAdapter, &mqttLogger, &debugSystem);

// Сервисы (Application)
CommandService commandService(
    &deviceCommandExecutionUseCase, &moduleCommandExecutionUseCase,
    &sensorCommandUseCase, &hybridApiService, &mqttLogger, &debugSystem,
    &ntpTimeAdapter, &jsonConverter);

// Контроллеры/API (Infrastructure)
HybridAPIService hybridApiService(
    &restApiService, &mqttApiService, &moduleConfig, 
    &mqttLogger, &debugSystem, ApiPriority::BOTH);
```

```cpp
// main.cpp - реальная инициализация зависимостей из проекта
void setup() {
    // 1. Инициализация адаптеров (Infrastructure)
    ledAdapter.begin();  // Глобальные объекты уже созданы в globals.h
    buttonAdapter.begin();
    spiffsAdapter.begin();
    
    // 2. Инициализация WiFi и сети
    wifiManager.begin();
    mqttManager.begin();
    
    // 3. Загрузка конфигурации
    moduleConfig.load();
    settings.load();
    
    // 4. Инициализация репозиториев - загрузка данных
    ruleRepository.loadAll();
    sensorRepository.loadAll();
    
    // 5. Создание loop service через фабрику
    loopService = loopServiceFactory.create(moduleConfig.getModuleMode());
    
    // 6. Запуск веб-сервера
    webServerLauncher.start();
    
    // 7. Инициализация связей между компонентами
    initDependencies();
    
    // 8. Запуск фоновых задач RTOS
    xTaskCreate(mqttTask, "MQTT", 4096, &mqttClient, 1, &mqttTaskHandle);
    xTaskCreate(buttonUpdateTask, "Button", 2048, &buttonAdapter, 1, &buttonTaskHandle);
}
```

---

## Пример потока данных через слои

### Сценарий: Пользователь отправляет MQTT/HTTP запрос на выполнение команды устройства

```
┌─────────────────────────────────────────────────┐
│ 1. MQTT Message: topic="device/command"          │
│    Payload: { "deviceUid": "d1", "state": "on" }  │
└────────────────┬────────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────────┐
│ Infrastructure: TopicHandlerFactory             │
│ - Определяет обработчик для topic                │
│ - Парсит JSON через JsonConverter              │
│ - Создает CommandDto                             │
└────────────────┬────────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────────┐
│ Application: CommandService                     │
│ - Получает CommandDto                           │
│ - Преобразует в DeviceCommandDto              │
│ - Вызывает Use Case                            │
└────────────────┬────────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────────┐
│ Core/Domain: DeviceCommandExecutionUseCase      │
│ - Бизнес-логика проверок                        │
│ - Получает Device из IDeviceModel              │
│ - Изменяет состояние                            │
│ - Вызывает IArduinoAdapter для управления пином │
└────────────────┬────────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────────┐
│ Infrastructure: ArduinoAdapter                  │
│ - Реализует IArduinoAdapter                     │
│ - Выполняет digitalWrite(pin, HIGH)             │
└────────────────┬────────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────────┐
│ Infrastructure: DeviceModel                     │
│ - Реализует IDeviceModel                        │
│ - Управляет состоянием Device в памяти         │
└────────────────┬────────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────────┐
│ Application: CommandService                     │
│ - Получает результат из Use Case                │
│ - Создает CommandResultDto                      │
└────────────────┬────────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────────┐
│ Infrastructure: HybridAPIService                │
│ - Отправляет ответ через MQTT/REST API         │
│ - Преобразует DTO в JSON                        │
└─────────────────────────────────────────────────┘
```

---

## Тестирование

### Структура тестов: `/test`

#### Моки (`/test/Mock/`)
Для тестирования используются моки:

```cpp
// MockRuleRepository.h - реальный мок из проекта
class MockRuleRepository : public IRuleRepository {
    std::map<std::string, Rule> rules;
public:
    Utils::Optional<Rule> get(const char* uid) override {
        if (rules.count(uid)) return Utils::Optional<Rule>(rules[uid]);
        return Utils::Optional<Rule>();
    }
    
    std::vector<Rule> getAll() override {
        std::vector<Rule> result;
        for (auto& pair : rules) {
            result.push_back(pair.second);
        }
        return result;
    }
    
    bool save(const Rule& entity) override {
        rules[entity.uid] = entity;
        return true;
    }
    
    bool remove(const char* uid) override {
        return rules.erase(uid) > 0;
    }
};
```

#### Пример теста Use Case

```cpp
// test_device_command_execution.cpp - реальный тест из проекта
void test_device_command_execution() {
    // Arrange
    MockDeviceModel deviceModel;
    MockArduinoAdapter arduinoAdapter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    ModuleStructure moduleStructure;
    
    DeviceCommandExecutionUseCase useCase(
        &deviceModel, &arduinoAdapter, &moduleStructure, &logger, &debugSystem
    );
    
    Device device;
    device.uid = "d1";
    device.state = DEVICE_STATE_OFF;
    deviceModel.addDevice(device);
    
    // Act
    bool result = useCase.execute("d1", "on", 123);
    
    // Assert
    assert(result == true);
    assert(arduinoAdapter.wasCalledDigitalWrite == true);
    
    Device* updatedDevice = deviceModel.getDeviceByUid("d1");
    assert(updatedDevice->state == DEVICE_STATE_ON);
}
```

### Преимущества такого тестирования:
✅ Тестируем бизнес-логику без железа  
✅ Быстрые тесты (не нужна плата ESP32)  
✅ Изолированное тестирование каждого компонента  
✅ Легко воспроизвести багу  

---

## Преимущества архитектуры

### 1. Независимость от фреймворков
Бизнес-логика не зависит от Arduino, ESP-IDF или других библиотек. Можно легко портировать на другую платформу.

### 2. Тестируемость
Можно тестировать бизнес-логику на компьютере без загрузки на ESP32.

### 3. Изоляция изменений
Изменение в Infrastructure (например, переход с WiFi на Ethernet) не затронет Domain и Application.

### 4. Переиспользование
Use Cases можно использовать в разных контроллерах (HTTP, MQTT, Serial, UDP).

### 5. Понятность
Четкое разделение ответственности: каждый слой решает свою задачу.

---

## Чек-лист для разработчика

### ✅ При добавлении новой функции:

1. **Определить бизнес-логику** → Use Case в `/src/Core/Domain/UseCases/`
2. **Определить интерфейсы** → Interfaces в `/src/Core/Domain/Interfaces/`
3. **Создать Application сервис** → Service в `/src/Application/Services/`
4. **Реализовать адаптеры** → Adapters в `/src/Infrastructure/Adapters/`
5. **Создать контроллер** → Controller в `/src/Infrastructure/Controllers/`
6. **Написать тесты** → Tests в `/test/`

### ❌ Чего НЕ делать:

- ❌ Не включать `#include <Arduino.h>` в Core/Domain
- ❌ Не использовать `digitalWrite()` в Use Cases
- ❌ Не включать Infrastructure классы в Application
- ❌ Не писать бизнес-логику в Controllers или Adapters
- ❌ Не хранить состояние в статических переменных (использовать репозитории)

### ✅ Правило "SOLID":

- **S**ingle Responsibility - каждый класс решает одну задачу
- **O**pen/Closed - открыт для расширения, закрыт для модификации
- **L**iskov Substitution - можно подменять реализации через интерфейсы
- **I**nterface Segregation - много маленьких интерфейсов вместо одного большого
- **D**ependency Inversion - зависимости от абстракций, а не реализаций

---

## Заключение

Эта архитектура обеспечивает:
- 🎯 Четкое разделение ответственности
- 🧪 Высокую тестируемость
- 🔄 Легкость изменений
- 🚀 Масштабируемость
- 📦 Переносимость между платформами

**Главное правило**: Зависимости направлены внутрь к домену. Domain ни от чего не зависит!
