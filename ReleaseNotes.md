# Release Notes

## Version 2.1.48 (2026-05-06)

### Fixed
- **Sensor Module Status During Network Outage**: GDV1-3456 Fixed incorrect behaviour where sensor modules were shown as offline (flashing red LED) when the router lost power or the Wi-Fi connection dropped. Sensor modules now retain their last known status during a network outage and only change to an error state when a sensor-specific fault is detected.

---

## Version 2.1.47 (2026-04-28)

### Changed
- **Sensor Scan Behaviour on Server-Initiated Reboot**: GDV1-3431 When the server sends a reboot command without an explicit sensor scan override, the device now always defaults to performing a full sensor scan on the next startup. Previously, the scan skip flag could retain a stale value from a prior operation, causing the device to unexpectedly skip sensor scanning after a server-triggered restart.

---

## Version 2.1.46 (2026-04-25)

### Added
- **LED Identification Mode**: GDV1-3421 The device now supports remote LED blinking commands from the server. When a `BlinkingStart` command is received, all LEDs switch to a fast red blinking pattern so the physical device can be visually identified in the installation. The blinking stops and normal LED behaviour resumes when a `BlinkingStop` command is received.

### Changed
- **Startup Sensor Scan Behaviour**: GDV1-3424 Removed a mechanism that forced the device to restart itself after 5 seconds if no sensor scan had been performed. The device no longer tracks or stores this scan state between reboots, simplifying startup behaviour.

---

## Version 2.1.45 (2026-04-25)

### Changed
- **Faster Sensor Initialization on Boot**: GDV1-3420 When the device has previously performed a full sensor scan, it can now start up in an accelerated mode: all previously registered sensors are loaded and activated immediately without re-scanning each port one by one. Modbus ports are powered on or off according to the saved sensor configuration, and the device waits for hardware to stabilize only when the port state actually changed. This significantly reduces the time needed for the device to become operational after a restart.

---

## Version 2.1.44 (2026-04-24)

### Fixed
- **Sensor Type Installation**: GDV1-3420 Fixed critical bug where sensor model installation failed with "Sensor type not found" after syncing types from the server. When the device was running low on memory, saving the sensor type list to storage failed silently — the stored file was overwritten with an empty list, and the device then cached that empty list as valid, making all sensor types permanently unavailable until reboot. The device now: validates that the data was saved correctly before accepting the result; never saves an empty list when real data existed; treats a missing or empty sensor type list as an error rather than a valid cached state.

---

## Version 2.1.41 (2026-04-20)

### Changed
- **Modbus Sensor Power-Up Timing**: When assigning an address to a Modbus sensor, the device now powers on only the target sensor port while powering off all others, instead of shutting everything down first. The warmup waiting period is now dynamic: if the port was already on, no waiting time is added; if the port was just turned on, the device waits 5 seconds for the hardware to stabilize before proceeding. This reduces unnecessary delays and improves the reliability of address assignment.
- **Sensor Detection Delay After Port Power Change**: When a sensor port changes its power state during a scan, the device now waits 3 seconds before attempting to detect the sensor, instead of the previous 0.5 seconds. This gives hardware more time to stabilize and reduces false "sensor not found" results.

---

## Version 2.1.39 (2026-04-20)

### Changed
- **Modbus Address Assignment Retry Logic**: When assigning a new address to a Modbus sensor, the device now makes up to three attempts using the primary method before falling back to an alternative broadcast approach. This improves the reliability of address assignment for sensors that may not respond immediately on the first or second try.

---

## Version 2.1.38 (2026-04-16)

### Added
- **Remote Wi-Fi Network Management**: The server can now send an "Add Wi-Fi" command to register a new Wi-Fi network on the device. The new credentials are saved persistently and the device will try to connect to any of the stored networks on the next boot. Previously configured networks are preserved and not overwritten.
- **Multi-Network Wi-Fi Support**: The device now maintains a list of known Wi-Fi networks and automatically connects to whichever one is available, instead of only ever connecting to a single hardcoded network.

### Changed
- **SPIFFS Storage Cleanup on Factory Reset**: When a factory reset is performed (via server command or physical button), all locally stored data files — including sensor configurations and saved Wi-Fi network lists — are now fully removed on the next boot. Previously, only the configuration stored in internal memory was cleared, leaving behind residual data files.

---

## Version 2.1.37 (2026-04-16)

### Changed
- **Automatic Restart After Factory Reset via Button**: When a factory reset is triggered by holding the physical button, the device now automatically restarts after clearing all module data. Previously, the device required a manual power cycle to apply the reset.

---

## Version 2.1.36 (2026-04-15)

### Added
- **Skip Sensor Scan After Factory Reset**: After a factory reset, the module now skips sensor scanning on the first boot following reconfiguration. On the second boot, the full sensor scan is performed automatically. This prevents incorrect scan results that can occur when the module has not yet fully settled after a reset.
- **Skip Sensor Scan When Button Held at Startup**: When the module's physical button is held during power-on, sensor scanning is skipped for that boot, allowing the module to start without altering the existing sensor configuration.

---

## Version 2.1.35 (2026-04-02)

### Changed
- **Modbus Sensor Detection During Scan**: When scanning a pin for Modbus sensors, the device now first attempts to assign a temporary address to any sensor found on the bus before detection. This improves the ability to discover and register sensors that have not yet been assigned a unique address, reducing missed detections in environments where multiple unaddressed sensors may be present.

---

## Version 2.1.34 (2026-04-02)

### Changed
- **Sensor Configuration Auto-Update on Sync**: When the server updates a sensor type's configuration (e.g., communication settings for a specific sensor model), the device now automatically applies those updated settings to all already-registered sensors of that type. Previously, connected sensors retained their original configuration even after the server changed the sensor type definition.
- **Modbus Address Assignment Strategy**: The device now first attempts to assign a new Modbus address using a broadcast command. If that fails, it retries using a different broadcast method, improving compatibility with sensor models that do not respond to the previous assignment approach.

---

## Version 2.1.33 (2026-04-01)

### Changed
- **Modbus Sensor Address Assignment Reliability**: When assigning a new address to a Modbus sensor fails using the device's current address, the device now automatically retries the operation using a broadcast command, increasing the success rate of address assignment in cases where a sensor does not respond to its known address

---

## Version 2.1.32 (2026-04-01)

### Changed
- **Modbus Sensor Availability Check**: The device now queries each sensor type using its own configured register when checking whether a sensor is present on the bus, instead of always querying a fixed default register. This improves compatibility with sensor models that do not support the previously hardcoded register.
- **Faster Modbus Detection Retry**: Reduced the wait time between each retry attempt during Modbus sensor detection, making the overall scan process complete faster when a sensor does not respond on the first attempt.

---

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

