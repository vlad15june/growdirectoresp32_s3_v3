# Release Notes

## Version 2.1.28 (2026-02-25)

### Added
- **SensorTypeRepository Logging**: Added debug logging support to `SensorTypeRepository` with `ILogger` and `IDebugSystem` dependencies for improved diagnostics across all repository operations
- **SensorReader Logging**: Enhanced ModBus sensor scanning with detailed per-attempt, per-channel logging including register config, address, port, and read result

### Changed
- **MAX_SENSOR_CHANNELS Constant**: Replaced hardcoded value `3` with new `MAX_SENSOR_CHANNELS` (4) constant in sensor channel loop iterations
- **Memory Optimization**: Increased JSON document buffer sizes for sensor type parsing (8KB→16KB for single type, 32KB→64KB and 8KB→64KB for arrays)
- **Sensor Clear Disabled**: Temporarily disabled automatic sensor data clearing on reboot pending new `clearSensor` flag implementation

---

## Version 2.1.27 (2026-02-24)

### Added
- **Sync sensor models**: Added sync sensor models befor sensor scaning

---

## Version 2.1.26 (2026-02-23)

### Added
- **Reboot with Sensor Scan**: Implemented automatic clearing of previously scanned sensors and sensor types database on module reboot with sensor scanning (noScanSensors=false), enabling complete resynchronization with fresh sensor detection; enhanced ModBus address validation to include ADDED status sensors; improved duplicate address detection and logging throughout address management pipeline

---

## Version 2.1.25 (2026-02-21)

### Changed
- **Logging Enhancement**: GDV1-3293 Improved logging for sensor type synchronization with detailed error messages, response length checks, and JSON parsing diagnostics
- **Memory Optimization**: Increased JSON document buffer sizes for better handling of large sensor type responses (4KB→8KB for single types, 8KB→32KB for arrays)

### Added
- **Documentation**: Added comprehensive sensor scanning analysis and detailed sensor models documentation

---

## Version 2.1.24 (2026-02-16)

### Fixed
- **Build System**: Fixed compilation error in native test environment (missing `<cstdarg>` include for va_start/va_end macros)

---

## Version 2.1.23 (2026-02-16)

### Fixed
- **Sensor Type Synchronization**: GDV1-3261 Fixed critical bug where sensor types weren't being updated when server changed parameters

---

## Version 2.1.22
New firmware version

