# Release Notes

## Version 2.1.31 (2026-04-01)

### Changed
- **Modbus Sensor Address Detection Reliability**: When a Modbus sensor does not respond during address detection, the device now waits progressively longer before each retry attempt, reducing the chance of missed responses from slow-starting sensors

---

## Version 2.1.30 (2026-03-09)

### Added
- **Sensor Scan Priority**: Sensor types received from the server can now specify a scan priority, enabling higher-priority sensor types to be scanned first during pin detection
- **Auto-Fetch Sensor Types on Scan**: If no sensor type definitions are available on the device when a scan is triggered, the device now automatically downloads them from the server before starting the scan, eliminating the "no sensor types available" failure

### Changed
- **Sensor Type Synchronization Performance**: Sensor type synchronization now detects whether any changes actually occurred and skips writing to storage if nothing has changed, reducing unnecessary storage operations
- **Multi-Channel Sensor Disconnect Reporting**: When a multi-channel sensor is in an error state (e.g., disconnected before any successful read after reboot), each individual channel is now reported to the server as disconnected rather than being silently omitted from the status update
- **Delete Sensors Command Handling**: The "Delete Sensors" command is now executed as a full module command, ensuring it is properly validated and processed through the same pipeline as other sensor management commands
- **Stale Sensor Data Cleanup**: When a sensor is deleted or replaced during a scan operation, any previously cached readings for that sensor are immediately removed so they do not appear in subsequent status updates sent to the server

---

## Version 2.1.29 (2026-02-27)

### Added
- **Sensor Type Scan Filtering**: Sensor types with `canScan=false` or `isSupported=false` are now skipped during pin scanning, reducing unnecessary scan attempts

---

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

