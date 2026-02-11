# Hardware API (module controller)

Source: Swagger UI http://78.47.183.121:8080/swagger/index.html and spec http://78.47.183.121:8080/swagger/v1/swagger.json.

## Purpose
This document describes the Hardware endpoints used by the ESP32 firmware to sync structure, exchange telemetry, receive commands, and report rule states. Interaction is built around HTTP requests with query parameters `token` and `grow_director_uid`, plus MQTT topics.

## Firmware request flow (from Confluence)

### Module startup sequence
1. Check if the module is deleted: **GET** `/api/v1/HardWare/is_deleted`.
2. If not deleted, fetch account info (currently only `accountUid`): **GET** `/api/v1/HardWare/account`.
3. Send module structure: **POST** `/api/v1/HardWare/structure`.
4. Sync rules: **GET** `/api/v1/HardWare/rulelist`.

### Working mode cadence
- Every 5 seconds: **POST** `/api/v1/HardWare/update` (module state + sensor/device values).
- Every 30 seconds: **POST** `/api/v1/HardWare/diagnostic`.

Source: [API Requests from Module (Firmware) to Server (Backend)](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/436273185/API+Requests+from+Module+Firmware+to+Server+Backend)

### Offline history (MQTT)
Firmware writes offline history into MQTT topics:
- `hardware/history/analog_control/{uid}`
- `hardware/history/device/{uid}`
- `hardware/history/sensor/{uid}`

Backend should subscribe to `hardware/history/*` to receive all three streams.

HydroDirector specifics:
- Device switch history goes to `hardware/history/device/{uid}`.
- Sensor readings go to `hardware/history/sensor/{uid}`.
- Device state history is sent only on switch events; periodic updates (every 5 seconds) include sensors only.

Important: All history channels **except** `hardware/history/sensor/{uid}` can receive data even when the module is online; control-device events are recorded to file and later replayed into history topics.

Source: [API Requests from Module (Firmware) to Server (Backend)](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/436273185/API+Requests+from+Module+Firmware+to+Server+Backend)

## Authentication and parameters
- Query parameter `token`: hardware token (stored in EEPROM, loaded from Settings).
- Query parameter `grow_director_uid`: device MAC (used as the module UID).
- Swagger declares `OAuth2` and the `X-Client-Version` header, but the hardware controller uses only query `token` in code.

## Constants and enums used by firmware

### Sensor states and device states (update payload)
From [constants.h](constants.h):
- Sensor `state` values:
	- `online`, `disconnected`
- Device `state` values:
	- `none`, `on`, `off`, `offByPowerOn`, `delayOn`, `delayOff`, `requestOn`, `requestStepOn`, `stepOn`, `stepWaiting`

### Sensor type names (structure payload)
From [constants.h](constants.h):
- `do`, `orp`, `soil_moisture`, `lux`, `par`

### Rule types, states, modes, and errors
From [ruleService.h](ruleService.h) and [ruleValidator.h](ruleValidator.h):
- Rule `ruleType` values: `ph`, `ec`, `schedule`, `sensor`, `average_sensor`, `dimmer_auto`, `dimmer_manual`, `loop`, `step`.
- Rule `state` values: `Synchronized`, `Error`, `Deleting`.
- Rule `mode` values: `On`, `Off`, `WaitingForUserConfirm`.
- Rule `errorType` values: `pars_error`, `sensor_unreachable`, `wrong_format`, `device_error`, `critical_device_error`, `protective_shutdown`, `pump_stuck`.
- Rule `ruleStopReason` values (in device updates): `setpoint_reach`, `time_reach`, `sensor_error`, `rule_manualy_off`, `rule_off_by_date`, `rule_manualy_deleted`, `time_not_available_error`, `unstable_sensor`, `protective_shutdown`.

### Command-related constants
- Device command `switchType` (GET /devices_command): `MANUAL`, `SENSOR_RULE`, `SCHEDULE_RULE`, `PH_RULE`, `EC_RULE`.
- Device command result `status` (POST /devices_command): `done`, `error`, `duplicate`, `done_by_rule`.
- Module command result `status` (POST /module_command): `Done`, `Error`, `InProgress`.


## Hardware endpoints

### POST /api/v1/Hardware/structure
Send module sensor/device structure.
- Query: `token`
- Request body: `HardwareStructureDto`
- Response: 200 OK

**Firmware payload keys (from code)**
- Top level: `GrowdirectorUid`, `firmware`, `email`, `timestamp`, `accountUid`, `lat`, `lng`.
- Node fields: `nodes[0].type`, `nodes[0].subtype`, `nodes[0].bootType`, `nodes[0].fileSystem`, `nodes[0].uid`, `nodes[0].firmware`, `nodes[0].ip`, `nodes[0].ssid`.
- Per module type, arrays are populated:
	- Sensors: `nodes[0].sensors[].uid`, `type`, `pin`.
	- Relays: `nodes[0].relays[].uid`, `pin`.
	- Sockets: `nodes[0].sockets[].uid`, `pin` (socket subtype).
	- Pumps: `nodes[0].pumps[].uid`, `pin` (hydro subtype).
	- Dimmers: `nodes[0].dimmers[].uid`, `pin` (analog subtype).

### POST /api/v1/Hardware/firmware
Report firmware version.
- Query: `token`
- Request body: `HardwareFirmwareInfoDto`
- Response: 200 OK, 403 Forbidden

**Firmware payload keys (from code)**
- `version`
- `growdirectorUid`

### POST /api/v1/Hardware/update
Send current sensor values and device states.
- Query: `token`
- Request body: `HardwareUpdateDto`
- Response: 200 OK

**Firmware payload keys (from code)**
- Top level: `GrowdirectorUid`, `email`, `accountUid`, `time`, `millis`.
- `sensors[]` items: `uid`, `value`, `state`.
- `devices[]` items (discrete devices):
	- Identity and HW state: `uid`, `state`, `state_hw`.
	- Manual/auto context: `isAuto`, `isManual`, `commandId`.
	- Rule context (when `workByRule`): `ruleUid`, `ruleType`, `ruleVersion`, `isRuleTurnOnDevice`.
	- Rule telemetry: `ruleSensorStopAt`, `ruleSensorValueStop`, `ruleStopReason`, `ruleSensorStartAt`.
	- Sensor rule params: `sensorUid`, `sensorValueLow`, `sensorValueHigh`, `sensorValue`, `ruleSensorValueStart`, `timeToSetPoint`, `cyclesToSetPoint`, `currentCycle`.
	- Schedule rule params: `ruleScheduleStartAt`, `ruleScheduleDuration`.
	- Timing: `secondsToSwitchBack`, `secondsEnabledFor`, `secondsDisabledFor`.
- `devices[]` items (analog devices):
	- Identity and HW state: `uid`, `state`, `analogControlPinValue`.
	- Manual/auto context: `isAuto`, `isManual`, `commandId`.
	- Rule context: `ruleUid`, `ruleType`, `ruleVersion`.
	- Sensor rule params: `sensorUid`, `sensorValueLow`, `sensorValueHigh`, `sensorValue`.
	- Schedule rule params: `ruleScheduleStartAt`, `ruleScheduleDuration`.
	- Timing: `secondsToSwitchBack`, `secondsEnabledFor`.

### POST /api/v1/Hardware/diagnostic
Send diagnostic info.
- Query: `token`
- Request body: `HardwareDiagnosticDto`
- Response: 200 OK

**Firmware payload keys (from code)**
- `rssi`, `uid`, `email`, `accountUid`, `timestamp`, `uptimeMs`
- `networkFails`, `apiSendFails`, `apiStatus`, `mqttStatus`, `mqttFails`, `apiSendFailRatio`
- `firmware`, `freeHeap`, `minFreeHeap`, `heapSize`, `maxAllocHeap`, `failsInHour`

### GET /api/v1/Hardware/devices_command
Fetch device commands.
- Query: `token`, `grow_director_uid`, `status[]`, `limit`
- Response: array of `DeviceCommandDto`

**Fields used by firmware (from code)**
- Input fields read: `id`, `uid`, `createdDate`, `switchType`, `requestedState`, `secondsToSwitchBack`, `needToSwitchBack`.
- Supported `switchType` values in firmware: `MANUAL`, `SENSOR_RULE`, `SCHEDULE_RULE`, `PH_RULE`, `EC_RULE`.

### POST /api/v1/Hardware/devices_command
Submit device command results.
- Query: `token`, `grow_director_uid`
- Request body: array of `DeviceCommandUpdateDto`
- Response: 200 OK

**Firmware result payload (from code)**
- Per item: `status`, `uid`, `errorMessage`, `timestamp`, `growDirectorUid`, `accountUid`.
- Status values used by firmware: `done`, `error`, `duplicate`, `done_by_rule`.

### GET /api/v1/Hardware/module_command
Fetch module-level commands.
- Query: `token`, `grow_director_uid`, `statuses[]`, `limit`
- Response: array of `ModuleCommandHwDto`

**Fields used by firmware (from code)**
- Input fields read: `uid`, `createdAt`, `command`, `options`.
- Supported commands:
	- `AddWifi` with options `ssid`, `password`.
	- `UpdateFirmware` with options `url`, `version`.

### POST /api/v1/Hardware/module_command
Submit module command results.
- Query: `token`, `grow_director_uid`
- Request body: array of `ModuleCommandHwUpdateDto`
- Response: 200 OK

**Firmware result payload (from code)**
- Per item: `uid`, `status`, `errorMessage` (optional).
- Status values used by firmware: `Done`, `Error`, `InProgress`.

### GET /api/v1/Hardware/is_deleted
Check whether the module is deleted.
- Query: `token`, `grow_director_uid`
- Response: `GrowDirectorProIsDeletedDto`

### GET /api/v1/Hardware/account
Get account info.
- Query: `token`, `grow_director_uid`
- Response: `HardwareAccountInfoDto`

### GET /api/v1/Hardware/rulelist
Get rules for the module.
- Query: `token`, `grow_director_uid`
- Response: array of `BaseHwRuleDto`

### POST /api/v1/Hardware/Rules/state
Send rule state (bulk).
- Query: `token`, `grow_director_uid`
- Request body: `RuleStateBulkDto`
- Response: 200 OK, 403 Forbidden

### POST /api/v1/Hardware/Rules/{uid}/state
Send single rule state.
- Query: `token`, `grow_director_uid`
- Path: `uid`
- Request body: `RuleStateSingleDto`
- Response: 200 OK, 403 Forbidden

### GET /api/v1/Hardware/sensor_value
Get sensor values for rule execution.
- Query: `token`, `grow_director_uid`
- Response: array of `SensorValueDto`

### GET /api/v1/Hardware/TestConnection
API connectivity check.
- Response: 200 OK

### GET /api/v1/Hardware/sensor_models
Get sensor model connection configs.
- Query: `token`, `grow_director_uid`
- Response: array of `SensorModelHwDto`

## MQTT topics (official docs)
From Confluence, the module publishes and consumes MQTT topics such as:
- `hardware/devices_command/post/<UID>` — same payload as POST `/api/v1/Hardware/devices_command`.
- `hardware/devices_command/get/<UID>` — responses from GET `/api/v1/HardWare/devices_command`.
- `hardware/diagnostic/<UID>`
- `hardware/update/<UID>`
- `hardware/log/file/<UID>`
- `hardware/log/sensor_error/<UID>`
- `hardware/log/rule_sensor_offline/<UID>`
- `hardware/history/device/<UID>`
- `hardware/history/analog_control/<UID>`
- `hardware/history/sensor/<UID>`
- `hardware/command_result/<UID>`

Source: [MQTT topics](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/496828418/MQTT+topics)

## Debug logs and command topics (MQTT)
For firmware debug logs and command channels:
- Logs can be read from `hw/<uid>`.
- Commands are sent to `GrowDirectorMqttTopic/<uid>`.
- Example debug command uses `SetDebugLevelCommandNotification` with keys `growDirectorUid`, `type`, `level`, `feature`, `isOnFeature`.

Source: [Firmware: Debug Logs (mqtt)](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/279183372/Firmware+Debug+Logs+mqtt)

## Troubleshooting MQTT disconnection (summary)
Checklist from support guide:
- Verify pump command presence in MQTT.
- Check module RSSI.
- Confirm module MQTT connection status.

Source: [Troubleshooting guide to check module disconnection from mqtt](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/575242242/2.+Troubleshooting+guide+to+check+module+disconnection+from+mqtt)

## Schemas used (key fields)
- `HardwareStructureDto`: `accountUid`, `timestamp`, `growdirectorUid`, `nodes[]`.
- `HardwareStructureNodeDto`: `uid`, `type`, `subtype`, `sensors[]`, `sockets[]`, `relays[]`, `pumps[]`, `dimmers[]`, `firmware`, `ip`, `ssid`, `ssids[]`, `lat`, `lng`, `bootType`, `fileSystem`, `board`, `chip`.
- `HardwareUpdateDto`: `time`, `email`, `accountUid`, `growdirectorUid`, `sensors[]`, `devices[]`.
- `SensorUpdateDto`: `uid`, `value`, `state`.
- `DeviceUpdateDto`: `uid`, `state`, `analogControlPinValue`, `isAuto`, `isManual`, `commandId`, `ruleUid`, `ruleType`, `ruleVersion`, `secondsEnabledFor`, `secondsDisabledFor`.
- `DeviceCommandDto`: `id`, `uid`, `requestedState`, `analogControlPinValue`, `status`, `needToSwitchBack`, `secondsToSwitchBack`, `switchType`, `errorMessage`, `createdDate`, `updatedDate`.
- `DeviceCommandUpdateDto`: `timestamp`, `accountUid`, `uid`, `status`, `errorMessage`.
- `ModuleCommandHwDto`: `uid`, `command`, `options`, `createdAt`.
- `ModuleCommandHwUpdateDto`: `timestamp`, `accountUid`, `uid`, `status`, `errorMessage`, `result`.
- `RuleStateBulkDto`: `timestamp`, `status`, `errorType`, `accountUid`, `rules[]`.
- `RuleStateSingleDto`: `uid`, `state`, `mode`, `modeReason`, `errorType`, `timestamp`, `accountUid`.

## How the firmware uses the API (details/keys/info)

### Structure and firmware version
- `sendToServerGrowDirectorStructure()` sends structure to /api/v1/Hardware/structure with query `token`.
- `sendFirmwareVersion()` sends version to /api/v1/Hardware/firmware with query `token` and `grow_director_uid`.
See [server_communication.h](server_communication.h).

### Device commands and results
- `executeDeviceCommand()` calls GET /api/v1/Hardware/devices_command?token=...&grow_director_uid=...&status=new.
- Results are posted to /api/v1/Hardware/devices_command via `sendDeviceCommandExecutionResult()`.
See [DeviceManager.h](DeviceManager.h) and [server_communication.h](server_communication.h).

### Rules synchronization
- `syncRulesFromServer()` calls GET /api/v1/Hardware/rulelist?token=...&grow_director_uid=....
- Rule states are sent via /api/v1/Hardware/Rules/state and /api/v1/Hardware/Rules/{uid}/state.
See [ruleSyncService.h](ruleSyncService.h) and [server_communication.h](server_communication.h).

### Connectivity checks and extra info
- `checkConnectionWithServer()` and `isModuleOnline()` use /api/v1/Hardware/TestConnection.
- `UpdateIpInStructure()` and `UpdateSsidInStructure()` update structure fields before sending (IP/SSID/SSIDs).
See [server_communication.h](server_communication.h).

### MQTT channels (in parallel with HTTP)
From architecture docs, MQTT is the preferred outbound channel:
- hardware/update/{MAC_ADDRESS}
- hardware/structure/{MAC_ADDRESS}
- hardware/rules/state/{MAC_ADDRESS}
- hardware/devices_command/get/{MAC_ADDRESS}
See [doc/projectArchitecture.md](doc/projectArchitecture.md).

## Notes
- All Hardware endpoints use query parameters `token` and `grow_director_uid` (except TestConnection and structure/firmware/update/diagnostic — Swagger does not always list `grow_director_uid`, but firmware adds it where required).
- Response validation and MQTT fallback are implemented in `server_communication.h`.
