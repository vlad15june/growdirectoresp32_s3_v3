# GrowDirector ESP32 - Sensor Operations Guide

## Table of Contents

1. [Overview](#overview)
2. [Sensor Architecture](#sensor-architecture)
3. [Sensor Types and Protocols](#sensor-types-and-protocols)
4. [Sensor Discovery and Scanning](#sensor-discovery-and-scanning)
5. [Sensor Data Reading](#sensor-data-reading)
6. [Data Transmission to Server](#data-transmission-to-server)
7. [Complete Workflow Diagram](#complete-workflow-diagram)
8. [Code Examples](#code-examples)
9. [Troubleshooting](#troubleshooting)

---

## Overview

The GrowDirector ESP32 firmware implements a comprehensive sensor management system that supports multiple sensor types and communication protocols. The system is designed following Clean Architecture principles, ensuring separation of concerns and testability.

### Key Features

- **Multi-Protocol Support**: Analog and ModBus/RS485 reading (I2C reading is planned, not yet implemented)
- **Automatic Discovery**: ModBus sensors are auto-scanned on ports (analog/I2C scanning currently disabled)
- **Real-time Monitoring**: Periodic sensor reading every 15 seconds
- **Dual Communication**: Data transmission via REST API and/or MQTT, plus local UDP multicast
- **Error Handling**: Per-cycle error marking, retry logic, and recovery on the next poll
- **Multi-Channel Support**: Single ModBus sensor with multiple measurement channels

---

## Sensor Architecture

The sensor system is organized into three architectural layers:

### 1. Core/Domain Layer

**Location**: `/src/Core/Domain/`

Contains business logic and entities without external dependencies:

- **Entities** (`Data/Entities/Sensor/`):
  - `SensorEntity` - Sensor data structure
  - `SensorType` - Sensor type definitions
  - `SensorValue` - Sensor reading values
  - `ModbusRegisterConfig` - ModBus register configuration
  - `I2CRegisterConfig` - I2C register configuration
  - `AnalogPinConfig` - Analog pin configuration

- **Interfaces** (`Interfaces/`):
  - `ISensorReader` - Sensor reading abstraction
  - `IAnalogSensorReader` - Analog sensor interface
  - `IModbusSensorReader` - ModBus sensor interface
  - `ISensorRepository` - Sensor data persistence

- **Use Cases** (`UseCases/Sensors/`):
  - `SensorCommandUseCase` - Sensor configuration commands
  - `ReadSensorsUseCase` - Sensor reading logic
  - `SensorDataTransmissionUseCase` - Data transmission logic

### 2. Application Layer

**Location**: `/src/Application/`

Orchestrates business logic and coordinates between layers:

- **Services** (`Services/Sensors/`):
  - `SensorService` - Main sensor orchestration service
  - Coordinates reading, processing, and transmission

- **DTOs** (`DTOs/`):
  - `SensorDto` - Sensor data transfer object
  - `UpdateDto` - System update containing sensor data
  - `SensorTypeDto` - Sensor type information

### 3. Infrastructure Layer

**Location**: `/src/Infrastructure/`

Implements technical details and hardware communication:

- **Adapters** (`Adapters/Sensors/`):
  - `SensorReader` - Main sensor reader implementation
  - `AnalogSensorReader` - Analog sensor reading
  - `ModbusSensorReader` - ModBus/RS485 communication
  - Hardware-specific implementations for I2C sensors (SHT85, SHT4x, CO2, Lux)

- **Repositories** (`Repositories/`):
  - `SensorRepository` - SPIFFS-based sensor persistence
  - `SensorTypeRepository` - Sensor type definitions storage

---

## Sensor Types and Protocols

### Supported Protocols

```cpp
enum class SensorProtocol {
    ANALOG_PROTOCOL,        // Analog sensors (0-3.3V ADC)
    ANALOG_BNC_PROTOCOL,    // BNC connector analog sensors
    ANALOG_EC2CH_PROTOCOL,  // 2-channel EC analog sensors
    ANALOG_PH_PROTOCOL,     // pH analog sensors
    I2C_PROTOCOL,           // I2C digital sensors
    MODBUS_PROTOCOL         // ModBus/RS485 sensors
};
```

### Sensor Entity Structure

Each sensor is represented by a `SensorEntity` with the following key properties:

```cpp
class SensorEntity {
public:
    std::string uid;              // Unique identifier (e.g., "84:F7:03:FE:88:2C.00")
    std::string sensorType;       // Sensor type name
    std::string model;            // Sensor model
    std::string protocol;         // Communication protocol ("Analog", "I2C", "ModBus")
    int pin;                      // Hardware pin/port number

    bool isActive;                // Sensor is enabled and included in the poll cycle
    bool isError;                 // Last read failed; sensor is reported as "disconnected"
    SensorStatusEnum status;      // new / added / error / deleted / disconnected

    float currentValue;           // Last successfully read value
    uint32_t lastReadTime;        // millis() timestamp of the last read

    // Analog calibration
    float multiplier;             // Analog calibration multiplier
    float offset;                 // Analog calibration offset

    int modbusDeviceAddress;      // ModBus slave address
    int baudRate;                 // ModBus baud rate (usually 9600)
    std::vector<ModbusRegisterConfig> modbusRegisterConfigs;

    std::string i2cAddress;       // I2C device address
    std::string chipType;         // I2C chip type
    std::vector<I2CRegisterConfig> i2cRegisterConfigs;

    std::string sensorTypeUid;    // Reference to the sensor type definition
};
```

> Note: the sensor's connectivity is expressed through `isActive` / `isError` (and the
> `SensorStatusEnum`), not through a separate `isOnline`/`value` pair. A sensor whose last
> read failed keeps `isActive = true` and gets `isError = true`, and is reported to the
> server with state `disconnected`.

### Sensor Type Configuration

Sensor types are defined with detailed configuration:

```cpp
class SensorType {
public:
    std::string uid;              // Type unique identifier
    std::string name;             // Type name (e.g., "ry_ws301")
    std::string displayName;      // Human-readable name
    bool canScan;                 // Can be auto-discovered
    SensorProtocol protocol;      // Communication protocol
    bool isSupported;             // Enabled/disabled
    
    // Protocol-specific settings
    int modbusDeviceAddress;      // Default ModBus address
    int baudRate;                 // Default baud rate
    std::vector<ModbusRegisterConfig> modbusRegisterConfig;
    
    std::string i2cAddress;       // Default I2C address
    std::vector<I2CRegisterConfig> i2cRegisterConfig;
    
    std::vector<AnalogPinConfig> analogPinConfig;
    
    // Multi-channel support
    std::vector<SubSensorType> sensorTypes;  // Individual measurement types
};
```

### Example: Multi-Channel ModBus Sensor

A typical multi-channel ModBus sensor (e.g., soil moisture + temperature sensor):

```cpp
ModbusRegisterConfig tempConfig = {
    .startRegister = 0x0000,      // Temperature register
    .numRegisters = 1,
    .functionCode = 0x03,         // Read Holding Registers
    .multiplier = 0.1,            // Convert to °C
    .offset = 0.0,
    .channel = 0,
    .channelName = "temperature",
    .canConnectMinValue = -40.0,  // Valid range for connection check
    .canConnectMaxValue = 80.0
};

ModbusRegisterConfig moistureConfig = {
    .startRegister = 0x0001,      // Moisture register
    .numRegisters = 1,
    .functionCode = 0x03,
    .multiplier = 0.1,            // Convert to %
    .offset = 0.0,
    .channel = 1,
    .channelName = "soil_moisture",
    .canConnectMinValue = 0.0,
    .canConnectMaxValue = 100.0
};
```

---

## Sensor Discovery and Scanning

### Scanning Process Overview

The sensor scanning process automatically detects connected sensors on available hardware ports.

#### 1. Module Setup Scan

**Trigger**: During initial module configuration
**Location**: `ModuleSetupService::scanPinForSensorTypes()`

```
┌─────────────────────────────────────────────┐
│  Module Setup Service                       │
│  Scans all pins for sensors                 │
└────────────────┬────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────┐
│  For Each Hardware Pin/Port:                │
│  - Check if pin is occupied                 │
│  - Check available power                    │
│  - Try each sensor type from repository     │
└────────────────┬────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────┐
│  For Each Sensor Type:                      │
│  1. Configure temporary sensor entity       │
│  2. Try default address (ModBus) or config  │
│  3. If failed, scan address range           │
│  4. On success, save to repository          │
└─────────────────────────────────────────────┘
```

#### 2. Manual Scan Command

**Trigger**: User-initiated via command
**Location**: `ModuleCommandExecutionUseCase::handleScanSensors()`

The scan command allows scanning specific pins for sensors:

```json
{
  "command": "ScanSensors",
  "options": {
    "pin": 2
  }
}
```

### ModBus Sensor Scanning

ModBus sensors require address discovery since devices can be configured with different addresses.

**Address probing order** (firmware tries them in this sequence):
1. **Scan address `0x22`** — the firmware first broadcasts a "set address" command to assign the
   dedicated scan address `0x22`, then tries to detect the sensor there.
2. **Sensor type default address** — the default address declared by the sensor type
   (e.g. `0xFF` / `255` or `0x01`).
3. **Scan range `0x70 - 0x73`** — iterated only if the sensor was not found at the previous addresses.

#### ModBus Scan Algorithm

```cpp
bool scanModbusSensor(const SensorEntity& sensor) {
    // 1. Configure port states - isolate target port
    powerOffAllPortsExcept(sensor.pin);
    powerOnPort(sensor.pin);
    // If any port's power state changed, warm up for 10 seconds so sensors stabilize
    if (portPowerStateChanged) {
        delay(10000);
    }

    // 2. Ping device at specified address
    if (!pingAddress(sensor.modbusDeviceAddress, sensor.modbusRegisterConfigs[0], sensor.pin)) {
        return false; // Device not responding
    }
    
    // 3. Validate each register configuration
    for (auto& registerConfig : sensor.modbusRegisterConfigs) {
        float value;
        bool success = false;
        
        // Retry up to 3 times
        for (int attempt = 0; attempt < 3; attempt++) {
            if (readRegister(sensor.modbusDeviceAddress, registerConfig, value, sensor.pin)) {
                // Check if value is within expected range
                if (value >= registerConfig.canConnectMinValue && 
                    value <= registerConfig.canConnectMaxValue) {
                    success = true;
                    break;
                }
            }
            delay(100);
        }
        
        if (!success) {
            return false; // Register validation failed
        }
    }
    
    return true; // All registers validated
}
```

#### Port Power Management

To ensure accurate scanning, ports are isolated:

```cpp
// Power configuration for scanning port 2
SensorReader: Port 0 powered OFF
SensorReader: Port 1 powered OFF
SensorReader: Port 2 powered ON  ← Target port
SensorReader: Port 3 powered OFF
```

This prevents interference from sensors on other ports.

#### Address Discovery

When a sensor is not found at its default address:

```cpp
// Scan address range
for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++) {
    tempSensor.modbusDeviceAddress = address;
    
    if (sensorReader->scanSensor(tempSensor)) {
        // Found sensor at this address
        sensor.modbusDeviceAddress = address;
        sensorRepository->save(sensor);
        return true;
    }
}
```

**Example scan log**:
```
ModuleCommandUseCase: Sensor not found at default address, scanning range 0x70-0x73
ModuleCommandUseCase: Scanning ModBus address 0x70 for sensor type ry_ws301
SensorReader: Device does not respond at address 112, skipping scan
ModuleCommandUseCase: Scanning ModBus address 0x71 for sensor type ry_ws301
ModbusSensorReader: Device 113 responded to ping on port 2
SensorReader: Device responds at address 113, proceeding with scan
ModbusSensorReader: Read value 25.30 from device 113 on port 2
ModuleCommandUseCase: Found ry_ws301 sensor on pin 2 at address 0x71
```

### I2C Sensor Scanning

> **Not active in the current firmware.** I2C scanning is temporarily disabled — the scan entry
> point returns `false` for the I2C protocol, so I2C sensors are not auto-detected. The algorithm
> below describes the intended logic only.

I2C sensors are typically identified by fixed addresses:

```cpp
bool scanI2CSensor(const SensorEntity& sensor) {
    // Parse I2C address (e.g., "0x44")
    uint8_t i2cAddr = parseI2CAddress(sensor.i2cAddress);
    
    // Try to communicate with device
    Wire.beginTransmission(i2cAddr);
    int error = Wire.endTransmission();
    
    if (error == 0) {
        // Device responded, validate by reading
        return validateI2CRegister(sensor);
    }
    
    return false;
}
```

### Analog Sensor Scanning

> **Not active in the current firmware.** Analog scanning is temporarily disabled — the scan entry
> point returns `false` for the analog protocol, so analog sensors are not auto-detected. The
> algorithm below describes the intended logic only.

Analog sensors are verified by checking if readings are within valid ranges:

```cpp
bool scanAnalogSensor(const SensorEntity& sensor) {
    // Read raw ADC value
    int rawValue = analogRead(sensor.pin);
    
    // Check if value indicates sensor presence
    // (not 0 and not max value)
    if (rawValue > 100 && rawValue < 4000) {
        return true; // Sensor likely connected
    }
    
    return false;
}
```

---

## Sensor Data Reading

### Reading Service Architecture

The `SensorService` orchestrates periodic sensor reading:

```cpp
class SensorService : public ISensorService {
private:
    const unsigned long SENSOR_READ_INTERVAL = 15000;  // 15 seconds
    unsigned long lastSensorReadTime = 0;
    
public:
    void loop() override {
        unsigned long currentTime = arduinoAdapter->millis();
        
        if (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
            readAllSensors();
            lastSensorReadTime = currentTime;
        }
    }
};
```

### Reading Process Flow

```
┌─────────────────────────────────────────────┐
│  SensorService::loop()                      │
│  Timer: Every 15 seconds                    │
└────────────────┬────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────┐
│  readAllSensors()                           │
│  - Get active sensors from repository       │
│  - Read by protocol group (analog/i2c/modbus)│
│  - Update value + status per sensor         │
└────────────────┬────────────────────────────┘
                 ▼
        ┌────────┴────────┐
        ▼                 ▼
┌──────────────┐   ┌──────────────┐
│  readAnalog  │   │  readModBus  │
│  Sensors     │   │  Sensors     │
└──────┬───────┘   └──────┬───────┘
       │                  │
       └────────┬─────────┘
                ▼
┌─────────────────────────────────────────────┐
│  Update Sensor Value Model                  │
│  - Store in memory                          │
│  - Update timestamp                         │
└────────────────┬────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────┐
│  Prepare UpdateDto                          │
│  - Collect all sensor values                │
│  - Add device states                        │
└────────────────┬────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────┐
│  Transmit to Server                         │
│  (See Data Transmission section)            │
└─────────────────────────────────────────────┘
```

### Protocol-Specific Reading

#### Analog Sensor Reading

```cpp
bool readAnalogSensor(const SensorEntity& sensor, float& outValue) {
    // Read raw ADC value (0-4095 for ESP32)
    int rawValue = arduinoAdapter->analogRead(sensor.pin);
    
    // Apply calibration
    outValue = rawValue * sensor.multiplier + sensor.offset;
    
    return true;
}
```

**Example**: Soil moisture sensor
- Raw ADC: 2048
- Multiplier: 0.024414 (converts to 0-100%)
- Offset: 0
- Result: 50% moisture

#### ModBus Sensor Reading

ModBus reading involves constructing and sending request packets:

```cpp
bool readModbusSensor(const SensorEntity& sensor, float& outValue) {
    if (sensor.modbusRegisterConfigs.empty()) {
        return false;
    }
    
    const ModbusRegisterConfig& config = sensor.modbusRegisterConfigs[0];
    int portIndex = sensor.pin;
    
    // Construct ModBus RTU request
    uint8_t request[8] = {
        sensor.modbusDeviceAddress,        // Slave address
        config.functionCode,               // Function code (usually 0x03)
        (uint8_t)(config.startRegister >> 8),   // Register high byte
        (uint8_t)(config.startRegister & 0xFF), // Register low byte
        (uint8_t)(config.numRegisters >> 8),    // Num registers high
        (uint8_t)(config.numRegisters & 0xFF)   // Num registers low
    };
    
    // Calculate and append CRC
    uint16_t crc = calculateCRC(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8);
    
    // Send request via RS485
    modbusPorts[portIndex]->sendData(request, sizeof(request));
    
    // Wait for response
    delay(300);
    
    // Read response
    uint8_t response[64];
    int bytesRead = modbusPorts[portIndex]->readAvailable(response, sizeof(response));
    
    // Parse response
    if (bytesRead >= 5 && response[1] == config.functionCode) {
        uint8_t byteCount = response[2];
        if (byteCount == config.numRegisters * 2) {
            // Extract value from registers
            uint16_t rawValue = (response[3] << 8) | response[4];
            outValue = rawValue * config.multiplier + config.offset;
            return true;
        }
    }
    
    return false;
}
```

**Example ModBus Transaction**:
```
Request:  0x71 0x03 0x00 0x00 0x00 0x01 0x8F 0x3A
          ^^^^ ^^^^ ^^^^^^^^^^^^ ^^^^^^^^^^^^ ^^^^^^^
          Addr Func Register     Num Regs     CRC

Response: 0x71 0x03 0x02 0x00 0xFD 0x38 0x45
          ^^^^ ^^^^ ^^^^ ^^^^^^^^^^^^^ ^^^^^^^
          Addr Func Bytes Value (253)  CRC
          
Value: 253 * 0.1 = 25.3°C
```

#### I2C Sensor Reading

> **Not implemented in the current firmware.** The I2C read path returns `false` ("not yet
> implemented"), and the hardware classes for specific I2C chips (CO2, Lux, SHT4x, SHT85) exist
> but are not wired into the read cycle. Only Analog and ModBus sensors produce values today.

#### Multi-Channel Sensor Reading

Some sensors provide multiple measurement channels:

```cpp
bool readMultiChannelSensor(const SensorEntity& sensor, 
                           map<string, float>& channelValues) {
    for (const auto& registerConfig : sensor.modbusRegisterConfigs) {
        float value;
        
        // Retry up to 3 times
        for (int attempt = 0; attempt < 3; attempt++) {
            if (readRegister(sensor.modbusDeviceAddress, registerConfig, 
                           value, sensor.pin)) {
                // Generate channel UID: base_uid.channel_number
                string channelUid = sensor.uid + "." + 
                    formatChannelNumber(registerConfig.channel);
                channelValues[channelUid] = value;
                break;
            }
            delay(100);
        }
    }
    
    return !channelValues.empty();
}
```

**Example**: 2-channel sensor `84:F7:03:FE:88:2C.00`
- Channel 0 (temperature): `84:F7:03:FE:88:2C.00.00` = 25.3°C
- Channel 1 (moisture): `84:F7:03:FE:88:2C.00.01` = 45.2%

### Error Handling and Retry Logic

```cpp
SensorReadResult readSensorDetailed(const SensorEntity& sensor) {
    uint32_t readTime = arduinoAdapter->millis();
    
    // Try to read sensor
    float value;
    if (readSensor(sensor, value)) {
        return SensorReadResult::createSuccess(value, readTime);
    }
    
    // Reading failed, try to scan (check if online)
    if (!scanSensor(sensor)) {
        // Mark as offline
        return SensorReadResult::createError(
            "Sensor is offline", 
            SensorReadStatus::SENSOR_OFFLINE
        );
    }
    
    // Sensor is online but communication failed
    return SensorReadResult::createError(
        "Communication error", 
        SensorReadStatus::COMMUNICATION_ERROR
    );
}
```

### Offline Sensor Detection

When a sensor fails to respond, the firmware does **not** maintain a separate offline queue or a
dedicated background rescan task. Detection and recovery happen inside the normal 15-second poll
cycle:

1. **Read fails**: for multi-channel ModBus reads, each channel is retried up to 3 times (with a
   100 ms gap). A single-channel read is attempted once per cycle.
2. **Mark as error**: on persistent failure the sensor's status is updated to `isError = true`.
   The sensor stays `isActive` and remains in the poll cycle — it is **not** removed or disabled.
3. **Reported state**: while `isError = true`, the sensor is sent to the server with state
   `disconnected` and value `0`.
4. **Automatic recovery**: on the next poll cycle (every 15 seconds) the firmware reads the sensor
   again. As soon as a read succeeds, `isError` is cleared and the sensor is reported as `online`
   again. No manual intervention and no separate rescan interval are involved.

```cpp
// Inside the regular poll cycle (every 15 seconds), per active sensor:
if (readSensor(sensor, value)) {
    // Success -> clear error, report "online"
    sensorRepository->updateSensorStatus(sensor.uid, /*isActive*/ true, /*isError*/ false);
} else {
    // Failure -> keep active, mark error, report "disconnected"
    sensorRepository->updateSensorStatus(sensor.uid, sensor.isActive, /*isError*/ true);
}
```

---

## Data Transmission to Server

### Transmission Architecture

The system uses a **Hybrid API Service** that can send data via REST API, MQTT, or both:

```cpp
enum class ApiPriority {
    REST_ONLY,       // Only use REST API
    MQTT_ONLY,       // Only use MQTT API
    REST_PREFERRED,  // Try REST first, fallback to MQTT
    MQTT_PREFERRED,  // Try MQTT first, fallback to REST
    BOTH             // Send through both APIs
};
```

### Transmission Flow

```
┌─────────────────────────────────────────────┐
│  SensorService                              │
│  Prepares UpdateDto with sensor data        │
└────────────────┬────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────┐
│  createSensorUpdateDto()                    │
│  - Module UID (MAC address)                 │
│  - Account UID                              │
│  - Current timestamp                        │
│  - Device states                            │
│  - Sensor readings                          │
└────────────────┬────────────────────────────┘
                 ▼
┌─────────────────────────────────────────────┐
│  HybridAPIService::sendModuleUpdateData()   │
│  Decides which API to use                   │
└────────────────┬────────────────────────────┘
                 │
        ┌────────┴────────┐
        ▼                 ▼
┌──────────────┐   ┌──────────────┐
│  REST API    │   │  MQTT API    │
│  HTTP POST   │   │  PUBLISH     │
└──────┬───────┘   └──────┬───────┘
       │                  │
       └────────┬─────────┘
                ▼
┌─────────────────────────────────────────────┐
│  Server (GrowDirector Backend)              │
│  Processes sensor data and updates DB       │
└─────────────────────────────────────────────┘
```

### Data Structures

#### UpdateDto Structure

```cpp
class UpdateDto {
public:
    string growdirectorUid;    // Module MAC address (e.g., "84:F7:03:FE:88:2C")
    string email;              // User email
    string accountUid;         // Account unique identifier
    string time;               // ISO 8601 timestamp
    unsigned long millis;      // System uptime in milliseconds
    vector<DeviceDto> devices; // Device states (relays, pumps)
    vector<SensorDto> sensors; // Sensor readings
};
```

#### SensorDto Structure

```cpp
class SensorDto {
public:
    string uid;     // Sensor UID (e.g., "84:F7:03:FE:88:2C.00.01")
    float value;    // Current reading (0 when the sensor is in error)
    string state;   // "online" or "disconnected"
};
```

### Creating Update DTO

```cpp
UpdateDto SensorService::createSensorUpdateDto() {
    UpdateDto updateDto;
    
    // Module identification
    updateDto.growdirectorUid = moduleConfig->getUid();  // MAC address
    updateDto.email = settings->getUser();
    updateDto.accountUid = settings->getAccountUid();
    
    // Timestamp
    updateDto.time = timeAdapter->getFormattedTime();  // ISO 8601
    updateDto.millis = arduinoAdapter->millis();
    
    // Collect sensor data (only active sensors are reported)
    auto sensors = sensorRepository->getActiveSensors();
    for (const auto& sensor : sensors) {
        SensorDto sensorDto;
        sensorDto.uid = sensor.uid;
        sensorDto.value = sensor.isError ? 0.0f : sensorValueModel->getSensorValue(sensor.uid);
        sensorDto.state = sensor.isError ? "disconnected" : "online";

        updateDto.sensors.push_back(sensorDto);
    }
    // Multi-channel sensors emit one entry per channel (UID = sensorUid + ".NN").
    
    // Collect device states
    auto devices = deviceModel->getAllDevices();
    for (auto* device : devices) {
        DeviceDto deviceDto;
        deviceDto.uid = device->deviceUid;
        deviceDto.state = (device->state == DEVICE_STATE_ON) ? "on" : "off";
        
        updateDto.devices.push_back(deviceDto);
    }
    
    return updateDto;
}
```

### JSON Serialization

The UpdateDto is converted to JSON before transmission:

```json
{
  "growdirectorUid": "84:F7:03:FE:88:2C",
  "email": "user@example.com",
  "accountUid": "acc-12345-67890",
  "time": "2026-01-08T14:30:00Z",
  "millis": 1234567890,
  "devices": [
    {
      "uid": "84:F7:03:FE:88:2C.relay.00",
      "state": "on"
    }
  ],
  "sensors": [
    {
      "uid": "84:F7:03:FE:88:2C.00.00",
      "value": 25.3,
      "state": "online"
    },
    {
      "uid": "84:F7:03:FE:88:2C.00.01",
      "value": 45.2,
      "state": "online"
    }
  ]
}
```

### REST API Transmission

```cpp
bool RestAPIService::sendModuleUpdateData(const UpdateDto& updateData) {
    // Convert to JSON
    string jsonPayload = jsonConverter->updateToJson(updateData);
    
    // Construct endpoint (token and module UID are passed as query parameters)
    string endpoint = "/api/v1/HardWare/update?token=" + getAuthToken() +
                      "&grow_director_uid=" + updateData.growdirectorUid;

    // Send HTTP POST request
    HTTPClient http;
    http.begin(apiBaseUrl + endpoint);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(jsonPayload);
    
    bool success = (httpCode == 200 || httpCode == 201);
    http.end();
    
    return success;
}
```

**Example REST Request**:
```
POST https://api.growdirector.com/api/v1/HardWare/update?token=eyJhbGciOiJIUzI1NiIs...&grow_director_uid=84:F7:03:FE:88:2C
Content-Type: application/json

{
  "growdirectorUid": "84:F7:03:FE:88:2C",
  "sensors": [...],
  ...
}
```

### MQTT Transmission

```cpp
bool MqttApiService::sendModuleUpdateData(const UpdateDto& updateData) {
    // Convert to JSON
    string jsonPayload = jsonConverter->updateToJson(updateData);
    
    // Construct topic
    string topic = "hardware/update/" + updateData.growdirectorUid;
    
    // Publish message
    bool success = mqttManager->publish(
        topic.c_str(), 
        jsonPayload.c_str(), 
        jsonPayload.length(),
        0,  // QoS 0
        false  // Not retained
    );
    
    return success;
}
```

**Example MQTT Message**:
```
Topic: hardware/update/84:F7:03:FE:88:2C
Payload: {
  "growdirectorUid": "84:F7:03:FE:88:2C",
  "sensors": [...],
  ...
}
```

### Transmission Timing

```cpp
class SensorService {
private:
    const unsigned long SENSOR_READ_INTERVAL = 15000;    // 15 sec
    const unsigned long SENSOR_UPDATE_INTERVAL = 15000;  // 15 sec
    
    unsigned long lastSensorReadTime = 0;
    unsigned long lastSensorUpdateTime = 0;
    
public:
    void loop() {
        unsigned long currentTime = arduinoAdapter->millis();
        
        // Read sensors every 15 seconds
        if (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
            readAllSensors();
            lastSensorReadTime = currentTime;
        }
        
        // Send updates to server every 15 seconds
        if (currentTime - lastSensorUpdateTime >= SENSOR_UPDATE_INTERVAL) {
            UpdateDto updateDto = createSensorUpdateDto();
            hybridApiService->sendModuleUpdateData(updateDto);
            lastSensorUpdateTime = currentTime;
        }
    }
};
```

### UDP Transmission (Local Network)

In addition to server communication, sensor data can be broadcast on the local network via UDP:

```cpp
void SensorService::sendUpdateViaUdp(const UpdateDto& updateDto) {
    if (!udpManager || !udpManager->isInitialized()) return;

    // Convert to JSON
    string jsonPayload = jsonConverter->updateToJson(updateDto);

    // Send via UDP multicast first; fall back to broadcast on failure.
    // Both the multicast group address and the port come from the UDP manager config.
    const char* multicastIP = udpManager->getMulticastIP();
    uint16_t port = udpManager->getLocalPort();

    if (!udpManager->getSender()->sendMulticast(jsonPayload.c_str(), multicastIP, port)) {
        udpManager->getSender()->sendBroadcast(jsonPayload.c_str(), port);
    }
}
```

This allows local applications to receive real-time sensor data without server roundtrip.

---

## Complete Workflow Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                    SENSOR OPERATIONS WORKFLOW                     │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────┐
│  1. INITIALIZATION   │
└──────┬───────────────┘
       │
       ▼
┌─────────────────────────────────────────────┐
│  Load Sensor Types from SPIFFS              │
│  /data/sensorTypes.json                     │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────┐
│  Load Existing Sensors from SPIFFS          │
│  /data/sensors.json                         │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────┐
│  Initialize Hardware Adapters               │
│  - ModBus/RS485 ports                       │
│  - I2C bus                                  │
│  - ADC channels                             │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌──────────────────────┐
│  2. SENSOR SCANNING  │
└──────┬───────────────┘
       │
       ▼
┌─────────────────────────────────────────────┐
│  Scan Each Hardware Pin/Port                │
└────────────────┬────────────────────────────┘
                 │
        ┌────────┴────────┐
        ▼                 ▼
┌──────────────┐   ┌──────────────┐
│ Analog Scan  │   │ ModBus Scan  │
│ - Read ADC   │   │ - Power port │
│ - Check range│   │ - Ping addr  │
│              │   │ - Read regs  │
│              │   │ - Validate   │
└──────┬───────┘   └──────┬───────┘
       │                  │
       └────────┬─────────┘
                ▼
┌─────────────────────────────────────────────┐
│  Sensor Detected                            │
│  - Create SensorEntity                      │
│  - Set protocol-specific config             │
│  - Save to repository                       │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌──────────────────────┐
│  3. PERIODIC READING │
└──────┬───────────────┘
       │
       ▼ (Every 15 seconds)
┌─────────────────────────────────────────────┐
│  SensorService::loop()                      │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────┐
│  Get All Sensors from Repository            │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────┐
│  For Each Sensor:                           │
│  - Check protocol                           │
│  - Read using appropriate adapter           │
│  - Store value in SensorValueModel          │
└────────────────┬────────────────────────────┘
                 │
        ┌────────┴────────┬─────────────┐
        ▼                 ▼             ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│ readAnalog() │   │ readModBus() │   │ readI2C()    │
└──────┬───────┘   └──────┬───────┘   └──────┬───────┘
       │                  │                  │
       └────────┬─────────┴──────────────────┘
                ▼
┌─────────────────────────────────────────────┐
│  Update SensorValue in Memory               │
│  - lastReadTime = now()                     │
│  - value = reading                          │
│  - isError = false (cleared on success)     │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────┐
│  Error Handling                             │
│  If read fails:                             │
│  - Multi-channel: retry up to 3x per channel│
│  - Mark sensor isError = true               │
│  - Keep sensor active (stays in poll cycle) │
│  - Report state as "disconnected"           │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌──────────────────────┐
│  4. DATA PREPARATION │
└──────┬───────────────┘
       │
       ▼ (Every 15 seconds)
┌─────────────────────────────────────────────┐
│  createSensorUpdateDto()                    │
│  - Module UID (MAC)                         │
│  - Account info                             │
│  - Timestamp                                │
│  - All sensor values                        │
│  - Device states                            │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────┐
│  Convert to JSON                            │
│  jsonConverter->updateToJson(updateDto)     │
└────────────────┬────────────────────────────┘
                 │
                 ▼
┌──────────────────────┐
│  5. TRANSMISSION     │
└──────┬───────────────┘
       │
       ▼
┌─────────────────────────────────────────────┐
│  HybridAPIService::sendModuleUpdateData()   │
│  Based on ApiPriority setting               │
└────────────────┬────────────────────────────┘
                 │
        ┌────────┴────────┬──────────────┐
        ▼                 ▼              ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│  REST API    │   │  MQTT API    │   │  UDP         │
│  POST /api/  │   │  PUBLISH     │   │  Multicast   │
│  v1/HardWare │   │  hardware/   │   │  (+broadcast │
│  /update     │   │  update/{uid}│   │   fallback)  │
└──────┬───────┘   └──────┬───────┘   └──────┬───────┘
       │                  │                  │
       └────────┬─────────┴──────────────────┘
                ▼
┌─────────────────────────────────────────────┐
│  Server / Local Clients                     │
│  - Process sensor data                      │
│  - Update database                          │
│  - Trigger automations                      │
│  - Display in UI                            │
└─────────────────────────────────────────────┘

┌──────────────────────┐
│  6. RECOVERY & SYNC  │
└──────┬───────────────┘
       │
       ▼ (Every 15 seconds, inside the poll cycle)
┌─────────────────────────────────────────────┐
│  Error Recovery (no separate queue)         │
│  - Re-read every active sensor              │
│  - On success: clear isError -> "online"    │
│  - On failure: keep active, set isError     │
│    -> reported as "disconnected"            │
└────────────────┬────────────────────────────┘
                 │
                 ▼ (Every 5 minutes)
┌─────────────────────────────────────────────┐
│  Sensor Types Sync                          │
│  - GET sensor_models from server            │
│  - Merge by updatedAt                       │
│  - Refresh registers of existing sensors    │
└─────────────────────────────────────────────┘
```

---

## Code Examples

### Example 1: Reading All Sensors

```cpp
bool SensorService::readAllSensors() {
    // Sensors are read in protocol groups. Each group iterates the active sensors of
    // that protocol; per-sensor reading updates the value model and the sensor status.
    bool success = true;

    if (!readAnalogSensors()) success = false;
    if (!readI2CSensors())    success = false;
    if (!readModbusSensors()) success = false;

    return success;
}

// One sensor of any protocol; called by each protocol group above.
bool SensorService::readSensor(SensorEntity sensor) {
    if (!sensor.isActive) {
        return false;
    }

    if (sensor.isMultiChannel()) {
        std::map<std::string, float> channelValues;
        if (sensorReader->readMultiChannelSensor(sensor, channelValues)) {
            for (const auto& pair : channelValues) {
                sensorValueModel->setChannelValue(sensor.uid, pair.first, pair.second);
            }
            sensorRepository->updateSensorStatus(sensor.uid, true, false);  // clear error
            return true;
        }
        sensorRepository->updateSensorStatus(sensor.uid, sensor.isActive, true);  // mark error
        return false;
    }

    float value;
    if (sensorReader->readSensor(sensor, value)) {
        sensorValueModel->setSensorValue(sensor.uid, value);
        sensorRepository->updateSensorStatus(sensor.uid, true, false);  // clear error
        return true;
    }
    sensorRepository->updateSensorStatus(sensor.uid, sensor.isActive, true);  // mark error
    return false;
}
```

### Example 2: Scanning a Specific Sensor Type

```cpp
bool detectSensor(const SensorType& sensorType, int pin) {
    // Create temporary sensor entity for scanning
    SensorEntity tempSensor;
    tempSensor.sensorType = sensorType.name;
    tempSensor.model = sensorType.name;
    tempSensor.protocol = sensorType.getProtocolString();
    tempSensor.pin = pin;
    
    if (sensorType.isModbusSensor()) {
        // ModBus sensor configuration
        tempSensor.baudRate = sensorType.baudRate;
        tempSensor.modbusDeviceAddress = sensorType.modbusDeviceAddress;
        tempSensor.modbusRegisterConfigs = sensorType.modbusRegisterConfig;
        
        // Try default address first
        if (sensorReader->scanSensor(tempSensor)) {
            // Found at default address
            saveSensorToRepository(tempSensor);
            return true;
        }
        
        // Scan address range
        for (int addr = MODBUS_ADDRESS_MIN; addr <= MODBUS_ADDRESS_MAX; addr++) {
            tempSensor.modbusDeviceAddress = addr;
            
            if (sensorReader->scanSensor(tempSensor)) {
                // Found at this address
                saveSensorToRepository(tempSensor);
                return true;
            }
        }
        
        return false;
    }
    else if (sensorType.isI2CSensor()) {
        // I2C sensor configuration
        tempSensor.i2cAddress = sensorType.i2cAddress;
        tempSensor.i2cRegisterConfigs = sensorType.i2cRegisterConfig;
        
        if (sensorReader->scanSensor(tempSensor)) {
            saveSensorToRepository(tempSensor);
            return true;
        }
        
        return false;
    }
    else if (sensorType.isAnalogSensor()) {
        // Analog sensor configuration
        tempSensor.multiplier = sensorType.analogPinConfig[0].multiplier;
        tempSensor.offset = sensorType.analogPinConfig[0].offset;
        
        if (sensorReader->scanSensor(tempSensor)) {
            saveSensorToRepository(tempSensor);
            return true;
        }
        
        return false;
    }
    
    return false;
}
```

### Example 3: Sending Sensor Data via Hybrid API

```cpp
void SensorService::sendSensorUpdate() {
    // Create update DTO
    UpdateDto updateDto = createSensorUpdateDto();
    
    // Send via hybrid API service
    bool success = hybridApiService->sendModuleUpdateData(
        updateDto, 
        ApiPriority::MQTT_PREFERRED  // Try MQTT first, fallback to REST
    );
    
    if (success) {
        log("SensorService: Update sent successfully");
    } else {
        log("SensorService: Failed to send update");
    }
}

UpdateDto SensorService::createSensorUpdateDto() {
    UpdateDto updateDto;
    
    // Module identification
    char buffer[64];
    moduleConfig->getSettings()->getUid(buffer, sizeof(buffer));
    updateDto.growdirectorUid = buffer;
    
    moduleConfig->getSettings()->getUser(buffer, sizeof(buffer));
    updateDto.email = buffer;
    
    moduleConfig->getSettings()->getAccountUid(buffer, sizeof(buffer));
    updateDto.accountUid = buffer;
    
    // Timestamp
    updateDto.time = timeAdapter->getFormattedTime();
    updateDto.millis = arduinoAdapter->millis();
    
    // Collect sensor data (only active sensors are reported)
    auto sensors = sensorRepository->getActiveSensors();
    for (const auto& sensor : sensors) {
        SensorDto sensorDto;
        sensorDto.uid = sensor.uid;
        sensorDto.value = sensor.isError ? 0.0f : sensorValueModel->getSensorValue(sensor.uid);
        sensorDto.state = sensor.isError ? "disconnected" : "online";

        updateDto.sensors.push_back(sensorDto);
    }

    return updateDto;
}
```

---

## Troubleshooting

### Common Issues and Solutions

#### 1. Sensor Not Detected During Scan

**Symptoms**:
```
SensorReader: Device does not respond at address 112, skipping scan
```

**Possible Causes**:
- Incorrect ModBus address
- Wrong baud rate configuration
- Wiring issues (A/B lines swapped)
- Power supply problems
- Port not powered on

**Solutions**:
1. Verify sensor default address from datasheet
2. Check baud rate matches sensor configuration (usually 9600)
3. Verify RS485 wiring: A to A, B to B
4. Check power supply voltage (usually 12V or 24V)
5. Ensure port power is enabled:
   ```cpp
   modbusSensorReader->powerOnPort(portIndex);
   delay(1000); // Wait for sensor to stabilize
   ```

#### 2. Sensor Reads Offline Intermittently

**Symptoms**:
```
SensorService: Failed to read sensor 84:F7:03:FE:88:2C.00, marking as error
// Later (next 15s cycle)...
SensorService: Read sensor 84:F7:03:FE:88:2C.00, value: 25.30
```

**Possible Causes**:
- Loose connections
- EMI interference
- Power supply noise
- Long cable runs
- Incorrect termination resistors

**Solutions**:
1. Check all connections for firmness
2. Use shielded cable for RS485
3. Add termination resistors (120Ω) at both ends of RS485 bus
4. Keep cable length under 1000m (ModBus spec)
5. Separate sensor cables from high-power lines

#### 3. Wrong Sensor Values

**Symptoms**:
```
ModbusSensorReader: Read value 655.10 from device 114 on port 2
SensorReader: Read value 655.10 is outside expected range [0.00, 50.00]
```

**Possible Causes**:
- Wrong multiplier/offset calibration
- Wrong register address
- Byte order issues (endianness)
- Sensor reporting error code

**Solutions**:
1. Verify register configuration from sensor datasheet
2. Check multiplier and offset values:
   ```cpp
   config.multiplier = 0.1;  // For temperature in 0.1°C increments
   config.offset = 0.0;
   ```
3. Check if sensor uses big-endian or little-endian byte order
4. Consult sensor documentation for error codes

#### 4. Data Not Reaching Server

**Symptoms**:
- Sensors reading correctly locally
- No data visible on server dashboard

**Possible Causes**:
- Network connectivity issues
- Authentication failure
- Incorrect API endpoint
- Server-side issues

**Solutions**:
1. Check network connectivity:
   ```cpp
   if (WiFi.status() != WL_CONNECTED) {
       // Reconnect WiFi
   }
   ```
2. Verify API credentials and tokens
3. Check server endpoint URL
4. Monitor HTTP response codes:
   ```
   HTTP POST: 200 OK        ← Success
   HTTP POST: 401 Unauthorized  ← Auth issue
   HTTP POST: 500 Internal Error  ← Server problem
   ```
5. Enable debug logging to see transmission details:
   ```cpp
   debugSystem->enableFeature(DebugFeatures::SENSOR_DATA_DEBUG);
   ```

#### 5. High Memory Usage

**Symptoms**:
```
diagnosticData.freeHeap = 50000  // Low free heap
```

**Possible Causes**:
- Too many sensors
- Memory leaks
- Large JSON payloads
- String concatenation

**Solutions**:
1. Limit concurrent sensor operations
2. Use string reserve for known sizes:
   ```cpp
   std::string json;
   json.reserve(1024);  // Pre-allocate
   ```
3. Clear sensor value history periodically
4. Check for memory leaks with ESP32 heap analyzer

#### 6. Slow Sensor Reading

**Symptoms**:
- Reading cycle takes longer than expected
- System becomes unresponsive

**Possible Causes**:
- Too many retry attempts
- Long delays between operations
- Blocking operations

**Solutions**:
1. Reduce retry count for offline sensors
2. Use non-blocking delays where possible
3. Implement timeout mechanisms:
   ```cpp
   unsigned long startTime = millis();
   while (serial.available() == 0) {
       if (millis() - startTime > 1000) {
           return false;  // Timeout after 1 second
       }
   }
   ```
4. Read sensors in separate RTOS task

### Debug Logging

Enable detailed sensor logging:

```cpp
// In main.cpp or setup
debugSystem.enableFeature(DebugFeatures::SENSOR_DATA_DEBUG);
debugSystem.enableFeature(DebugFeatures::MODULE_COMMAND_DEBUG);

// This will output detailed logs:
// ModbusSensorReader: Ping attempt 1/3 for device 113 on port 2
// [ModbusRS485_1] Sending request: 0x71 0x03 0x00 0x00 0x00 0x01 0x8F 0x3A
// ModbusSensorReader: Ping device 113 on port 2 - received 7 bytes
// ModbusSensorReader: Raw bytes: 71 03 02 00 FD 38 45
// ModbusSensorReader: Read value 25.30 from device 113 on port 2
```

### Testing Tools

#### Board Test Commands

The firmware includes built-in test commands:

```
#test_modbus -re_de 19 -rx 18 -tx 17 -baud 9600 -data [0x01,0x03,0x00,0x00,0x00,0x01]%
```

This sends a raw ModBus command for testing communication.

---

## Summary

The GrowDirector ESP32 sensor system provides:

1. **Protocol Support**: Analog and ModBus reading today; I2C reading is planned (not yet implemented)
2. **Automatic Discovery**: ModBus sensors are auto-scanned; analog/I2C scanning is currently disabled
3. **Robust Reading**: Multi-attempt retry logic (multi-channel ModBus) and error handling
4. **Dual Communication**: REST API and MQTT support with fallback, plus local UDP multicast
5. **Real-time Updates**: 15-second intervals for reading and transmission
6. **Error Recovery**: Failed sensors stay active and recover automatically on the next poll cycle
7. **Clean Architecture**: Testable, maintainable, and extensible design

The system ensures reliable sensor monitoring with comprehensive error handling and multiple communication pathways to the server.
