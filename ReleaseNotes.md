# Release Notes

## Version 2.17.18 (2026-07-09)

### Added
- **Wind Direction Between Rule**: GDV1-3700 Added a "between" comparison for sensor and step rules, letting rules trigger when a value (e.g. wind direction) falls within a configured range — including ranges that wrap around, such as a compass arc crossing due north.

---

## Version 2.17.17 (2026-07-08)

### Fixed
- **Step Rule Final Cycle Completion**: GDV1-3650 Fixed step-based rules so the final step's active cycle now runs to completion before the protective shutdown triggers, instead of being cut short at the cycle limit.

---

## Version 2.2.16 (2026-06-30)

### Fixed
- **Step Rule Protective Shutdown**: GDV1-3650 Fixed step-based rules with protective shutdown so that, on reaching their configured cycle limit, the device now turns the output off and reports the awaiting-confirmation state; previously the output went off but the app kept showing the device as ON.
- **Account Identification in Rule-State Reports**: GDV1-3625 Fixed rule-state reports sent to the server so they again include the account identifier on every report, restoring correct attribution of rule-state updates to the account.

---

## Version 2.2.15 (2026-06-30)

### Fixed
- **Stuck "Deleting" Rule**: GDV1-3625 Fixed a rule remaining stuck in the "Deleting" state when the device's internet (HTTP) channel was down but the messaging channel was up; the device now confirms rule deletion over messaging first and falls back to internet, so the deletion completes either way.

---

## Version 2.2.14 (2026-06-24)

### Changed
- **Rainfall Accumulation Window**: GDV1-3568 The rain sensor now reports rainfall accumulated over a 1-minute window instead of 5 minutes, so rainfall readings update more responsively.

---

## Version 2.2.12 (2026-06-24)

### Added
- **Raw ModBus Frame Logging**: New diagnostic capability that captures and logs the raw outgoing serial/ModBus frames sent to sensors, to aid troubleshooting of sensor communication.
- **Boot and Button Event Logging**: The device now records boot events and button actions, giving testers and analysts visibility into how and why the device restarted and what button interactions occurred.

### Fixed
- **Offline Factory Reset**: GDV1-3619 Fixed button-triggered factory reset so it now works when the device has no internet/WiFi connection, instead of only working while online.
- **Sensor Scan Reboot Loop**: GDV1-3618 Fixed a reboot loop that occurred while the device was scanning ModBus sensors; the device now stays running through the scan instead of restarting.
- **False Network-Offline Detection**: GDV1-3558 Fixed incorrect "network offline" detection that relied on the WiFi indicator and could trigger unnecessary automatic reboots; offline state is now determined by the actual data channels.

---

## Version 2.2.11 (2026-06-22)

### Added
- **OTA Update Session Logging**: GDV1-3602 The device now keeps a detailed record of each firmware update — start, download, install, and final outcome — stores it on the device, and uploads it to monitoring once back online, so failed or interrupted updates can be diagnosed.
- **Full Power-Cycle on Technical Restart**: GDV1-3602 Technical and offline-triggered restarts now fully power-cycle the hardware (including radio and analog circuitry) instead of a soft reboot, improving recovery from a bad state. User-initiated restarts remain a normal reboot.

### Fixed
- **Stalled Operation/Update Recovery**: GDV1-3602 The device now recovers automatically when its main operation or a firmware update gets stuck, instead of staying frozen until it is manually power-cycled.
- **Deleted Module Detection**: GDV1-3604 A module deleted on the server while it was offline or repeatedly rejected for authorization now correctly resets to factory state, instead of remaining active.

---

## Version 2.2.10 (2026-06-17)

### Added
- **Sensor Rule Protective Shutdown**: GDV1-3592 Added a safety shutdown for sensor-based rules — if the target value is not reached within the configured time-to-setpoint, the device is turned off, the rule is flagged with an error awaiting user confirmation, and it recovers reliably once confirmed

### Fixed
- **Schedule Rule Recurrence**: GDV1-3591 Fixed schedule rules firing only on the first period — they now turn the device on for every scheduled period
- **Large Rule Updates Dropped**: GDV1-3591 Fixed large rule configuration pushes from the server being lost; fragmented messages are now reassembled so the full update is applied
- **Commands Blocked During Schedule Processing**: GDV1-3591 Fixed the device becoming unresponsive to commands while schedule rules were being processed
- **Stop Reason in Device History**: GDV1-3591 Fixed missing/incorrect stop reason — device history now records the correct reason every time a rule turns a device off
- **Automatic/Manual State Reporting**: GDV1-3591 Fixed device updates reporting wrong automatic/manual state and rule start/stop context to the server

---

## Version 2.2.9 (2026-06-16)

### Changed
- **Rainfall Accumulation Window**: GDV1-3568 The rainfall accumulation period for the rain sensor is now five minutes, so cumulative rainfall is reported over a longer averaging interval.

---

## Version 2.2.8 (2026-06-15)

### Changed
- **Rainfall Accumulation Window**: GDV1-3568 The rainfall accumulation period for the rain sensor is now one minute instead of five, so cumulative rainfall is reported over a shorter, more responsive interval.

---

## Version 2.2.7 (2026-06-15)

### Changed
- **Firmware Build Stability**: Improved build consistency across environments so the firmware compiles reliably, and ensured automation rules start with a well-defined default state when their direction is not explicitly provided, avoiding undefined behavior.

---

## Version 2.2.6 (2026-06-15)

### Fixed
- **Correct Sensor Address Detection via Wildcard**: GDV1-3532 During wildcard-based sensor address detection, the device now reads the sensor's actual address from the correct position in its response and ignores out-of-range or invalid values, instead of mistaking the response marker for the address. This prevents address assignment to an incorrect target and improves reliable detection of sensors that respond to wildcard addressing.

---

## Version 2.2.5 (2026-06-15)

### Added
- **Advanced Automation Rules**: GDV1-3557 The device now supports additional rule kinds, including rules that act on the averaged value of several sensors and rules built from repeating loop steps, enabling more flexible automation scenarios.
- **Workflow Rule Attributes**: The device now understands and validates workflow-related settings on rules and devices, preparing automation for multi-step workflow control.
- **Remote Sensor Readings**: The device can now receive measurements from remote sensor modules over the network and use them when evaluating rules, allowing automation based on sensors that are not directly wired to the device.
- **Rainfall Accumulation**: The device now accumulates rainfall measurements from the XF700A rain sensor over a time window, providing cumulative rainfall values.
- **Timed Device Activation**: Device commands can now include a duration after which the output automatically returns to its previous state, supporting temporary activations without sending a separate switch-off command.
- **Per-Rule State Reporting**: The device now reports the current state of individual automation rules to the platform, improving visibility into which rules are active.

### Changed
- **More Reliable Platform Messaging**: The communication channel with the platform was rebuilt on a non-blocking messaging engine, improving responsiveness and the stability of message delivery in both directions.
- **Rule Synchronization with Deletion and Recovery**: GDV1-3502 Rule synchronization now applies rule deletions and keeps a consistent snapshot of active rules. After a connectivity outage, the device automatically re-synchronizes and restores rule state when the connection is restored.
- **Smoother Sensor-Based Triggering**: GDV1-3502 Sensor-driven rules now apply hysteresis around their thresholds, reducing rapid on/off toggling of devices near the trigger point.
- **Safe Behavior on Stale Sensor Data**: When the sensor data controlling a device becomes stale or the sensor goes offline, the device now turns off the affected outputs and clears their state, preventing automation from acting on outdated readings.
- **Wider Sensor Addressing Compatibility**: GDV1-3532 The device now accepts a broadcast/wildcard sensor address during address assignment and detection, improving compatibility with sensors that respond to wildcard addressing.
- **PWM Output Support**: GDV1-3416 The device gained PWM output capability on supported channels, enabling finer control of connected equipment.
- **Improved Manufacturing Test Mode**: Board test and diagnostic mode were extended with additional bus, power, and connectivity checks and more robust startup handling, improving reliability of production testing.

---

## Version 2.2.4 (2026-06-01)

### Added
- **Extended Memory Visibility**: The device now reports additional memory capacity and availability indicators in diagnostics and module information, making remote health checks more complete.

### Changed
- **High-Load Stability Profile**: GDV1-3449 The firmware now uses expanded external memory support on supported boards, improving resilience during intensive operations and reducing the risk of instability under load.

### Fixed
- **MQTT Disconnect Counter Drift**: GDV1-3534 Fixed a critical issue where modules that started while MQTT was already unavailable could accumulate incorrect long-disconnect statistics, causing false failure escalation in monitoring.

---

## Version 2.2.3 (2026-05-31)

### Changed
- **Firmware Build Stability**: Improved build and startup consistency so sensor handling and data exchange components are initialized reliably across environments.

---

## Version 2.2.2 (2026-05-31)

### Added
- **Remote Raw Bus Diagnostics**: The device now supports remote execution of low-level bus requests and returns the raw response payload back to the platform. This allows diagnostics and integration checks to be performed without physical access to the installation.

### Changed
- **Modbus Sensor Model Flexibility**: GDV1-3376 Sensor models can now define additional communication parameters used during address setup and availability checks, improving compatibility with devices that require model-specific communication behavior.
- **Sensor Scan Address Isolation by Port**: During Modbus scan, each port is now processed with its own temporary scan address instead of a single shared one, reducing cross-port interference during detection.

---

## Version 2.2.1 (2026-05-27)

### Changed
- **Modbus Sensor Address Assignment Reliability**: GDV1-3522 The device now verifies that a sensor actually responds at its new address after each address assignment attempt, instead of trusting the write command result alone. If the sensor does not respond at the new address, the device checks whether it is still reachable at the old address and reports the result accordingly. During a sensor port scan, the device also proceeds with detection at the assigned scan address even when the assignment command does not return an acknowledgment, improving detection of sensors that silently accept the new address without confirming the command.

---

## Version 2.2.0 (2026-05-27)

### Fixed
- **Automatic Restart After Removal from Application**: GDV1-3489 Fixed an issue where the device did not restart automatically after being removed from the application. Previously, the device had to be manually restarted to complete the removal. The device now restarts on its own as soon as the removal operation finishes.

---

## Version 2.1.49 (2026-05-13)

### Fixed
- **Sensor Scan After Startup Without Sensor Configuration**: GDV1-3431 Fixed a case where the device, when starting up without an available sensor configuration (placeholder mode), did not clear the "skip scan" flag. This could cause the device to permanently skip sensor scanning on all subsequent boots until a manual reset was performed. The device now correctly ensures a full sensor scan is performed on the next boot when entering this startup path.

---

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

