# GrowDirector ESP32 - Relay Operations Guide

## Table of Contents

1. [Overview](#overview)
2. [Relay Architecture](#relay-architecture)
3. [Relay Models](#relay-models)
4. [Relay Board Registration](#relay-board-registration)
5. [Relay State Polling](#relay-state-polling)
6. [Command Execution](#command-execution)
7. [Rule Execution](#rule-execution)
8. [Data Transmission to Server](#data-transmission-to-server)
9. [Complete Workflow Diagram](#complete-workflow-diagram)
10. [Troubleshooting](#troubleshooting)

---

## Overview

The GrowDirector ESP32 firmware supports controlling external **Modbus RTU relay boards** connected via RS-485. This capability runs on the **Sensor module** hardware — the same ESP32 board used for Modbus sensor reading, switched to relay control through an operating mode setting.

When a Sensor module is set to **Relay mode**, it behaves identically to a Dry module: it synchronizes and executes automation rules, accepts manual commands from the app, and reports device states to the server. The RS-485 bus is used exclusively for relay control — **no sensors can be connected to this module in Relay mode**.

### Key Features

- **Modbus RTU Control**: ON/OFF/Toggle commands sent to relay channels over RS-485
- **Multiple Relay Models**: Different relay board models supported via a model registry (analogous to sensor types)
- **Full Rule Engine**: All existing rule types work — schedule, sensor, loop, step, ph, ec, dimmer
- **Cross-Module Sensor Values**: Sensor data for rules is received from other modules via MQTT and UDP (local network)
- **1-Second State Polling**: Relay board state is read via Modbus every second
- **Event-Driven Reporting**: State changes sent to server immediately; full update every 30 seconds
- **Offline Capable**: Rules continue executing without server connection

---

## Relay Architecture

The relay system follows the same three-layer Clean Architecture as the sensor system.

### 1. Core/Domain Layer

Contains relay business logic without external dependencies:

- **Entities**: relay board (model reference, Modbus address, channel count), relay channel (uid, index, current state), relay model (register map, channel count, default address, baud rate)
- **Interfaces**: reading relay states from board, sending relay commands to board, relay board/channel persistence, relay model definitions storage
- **Use Cases**: execute relay command on a channel, poll current states from the board, register a relay board

### 2. Application Layer

Orchestrates relay logic and coordinates between layers:

- **Services**: main relay service — owns the 1-second polling loop, processes incoming commands, triggers state reporting
- **DTOs**: reuses existing `DeviceDto` and `UpdateDto` from the Dry module flow — no new DTOs needed

### 3. Infrastructure Layer

Implements Modbus communication details:

- **Adapters**: Modbus relay writer (FC 0x05 Write Single Coil), Modbus relay reader (FC 0x01 Read Coils) — both reuse the existing RS-485 adapter already present in the project
- **Repositories**: relay board repository (SPIFFS), relay model repository (SPIFFS) — analogous to `SensorRepository` and `SensorTypeRepository`

---

## Relay Models

Different relay board models differ in channel count, Modbus register layout, and function codes. Each model is described by a **RelayModel** definition stored in SPIFFS and synchronized from the server — **analogous to how SensorType works for sensors**.

### What a Relay Model Describes

- Display name and internal model name
- Number of channels
- Default Modbus address and baud rate
- Register map: base register for channel control, special register for all-channels command, data values for ON / OFF / Toggle, register and function code for reading current states

### Currently Supported Models

| Model | Display Name | Channels | Default Address |
|---|---|---|---|
| `waveshare_rtu_relay_e` | Waveshare RTU Relay (E) | 8 | 0x01 |

### Adding a New Relay Model

New relay board models are added by creating a new entry in `relayModels.json` in SPIFFS with the register map from the board's datasheet. The model is synchronized from the server to the module — **no firmware recompile required** — exactly as new sensor types are added.

---

## Relay Board Registration

### Module Mode (Prerequisite)

Before relay board scanning can happen, the Sensor module must be in **Relay mode**.

**During initial module onboarding** the app asks the user to choose:
- Sensor mode — read RS-485 sensors
- Relay mode — control a Modbus relay board

**On the module settings page** the mode can be switched at any time:
- **Sensor → Relay**: blocked if any sensors are configured on the module
- **Relay → Sensor**: always allowed
- After switching, the module restarts in the new mode

### Registration Flow

The relay board registration process works **exactly the same way as sensor scanning** — the module automatically detects what is connected on the RS-485 bus by iterating through known models and probing registers.

**Step 1 — Download Relay Models**

On startup the module downloads all `RelayModel` definitions from the server and stores them in SPIFFS — analogous to how `SensorType` definitions are downloaded for sensors.

**Step 2 — Auto-Scan (same as sensor scanning)**

The module iterates through all relay models that have the `canScan` flag set. For each model it sends the appropriate Modbus read request and validates the response registers. If the response is valid and falls within expected ranges → the relay board is identified and saved to the repository with the detected model and Modbus address.

This is the exact same mechanism sensors use: iterate through types with `canScan = true`, probe the bus, validate register values, save on success.

**Step 3 — canOperate flag**

Once a relay board is detected and saved, the module checks the `canOperate` flag on the identified `RelayModel`. If `canOperate = true` → the module creates individual device entities for each relay channel (e.g., 8 devices for an 8-channel board). These devices are included in the structure sent to the server and become available for rule assignment and manual control in the app.

If `canOperate = false` → a single placeholder device is created on the pin where the board was found. This device carries the `canOperate = false` flag and a reference to the detected `RelayModel`. No controllable relay channels are created. The purpose of this placeholder is to make the board visible on the module settings page — so the user can see what was detected on that pin, manually change the model to a compatible one, or delete it. This is analogous to the `unknown` sensor type used in the sensor system: when a sensor is detected on a pin but its exact type cannot be determined, an `unknown` placeholder appears on that pin so the user can assign the correct type from the UI.

**Step 4 — Model Selection on Module Settings Page**

On the module settings page the user can manually change the relay board model. The dropdown lists only models that have the `isAvailableForSelection` flag set. This allows the user to override the auto-detected model — for example, if two board models have identical register layouts and auto-scan cannot distinguish between them, or if the user wants to switch to a different compatible board.

### RelayModel Flags Summary

| Flag | Description |
|---|---|
| `canScan` | Model is included in auto-scan on startup |
| `canOperate` | Board supports device creation — relay channels can be controlled and assigned to rules |
| `isAvailableForSelection` | Model appears in the manual model selection dropdown on the module settings page |

---

## Relay State Polling

The firmware polls the relay board state via Modbus **every 1 second**.

### How It Works

1. Firmware sends a Read Coils request (FC 0x01) to the board
2. Board responds with a bitmask — each bit represents one channel (bit 0 = CH0, bit 7 = CH7)
3. Firmware compares received states with the current DeviceModel in memory
4. If any channel state differs → DeviceModel is updated and an **immediate server update** is triggered
5. If no change → nothing happens until the 30-second periodic update

### Board Offline Detection

If the board does not respond:
1. Retry up to 3 times
2. After 3 consecutive failures → all channels marked as `disconnected`
3. Immediate update sent to server
4. Polling continues — on next successful response, channels automatically return to their actual states

---

## Command Execution

### Command Sources

Relay channels are controlled from two sources:

1. **Manual commands** — user taps ON/OFF in the app → server sends `DeviceCommand` to the module
2. **Rule engine** — rule condition is met → rule engine triggers the command internally

Both sources use the **same command execution path** that Dry modules already use. The relay board is simply a new physical actuator behind the same interface — no new command flow is needed.

### How a Command Is Executed

1. Command arrives (channel uid + requested state)
2. Channel uid is resolved to a channel index (0–7) and board Modbus address
3. Firmware sends a Write Single Coil command (FC 0x05) to the board over RS-485
4. Board echoes the same frame back → success confirmed
5. DeviceModel is updated immediately (confirmed on next poll cycle)
6. Command result (`done` / `error`) is reported back to server

### Supported Commands

| Command | Description |
|---|---|
| `on` | Close relay contact |
| `off` | Open relay contact |
| Toggle | Flip current state |

---

## Rule Execution

In Relay mode the module behaves **identically to a Dry module** for rule execution. Rules are downloaded from the server on startup, stored in SPIFFS, and executed locally and autonomously.

### Startup Synchronization

Same sequence as Dry module:
1. Check if module is deleted
2. Get account info
3. Report structure (relay channels)
4. Download rulelist and save to SPIFFS

### Sensor Values from Other Modules

Since no sensors are connected to the module in Relay mode, **all sensor values used in rules come from other modules via MQTT and UDP (local network)**. The module subscribes to update messages from other modules and feeds received sensor values into the local rule engine — the same mechanism already used for cross-module sensor rules on Dry modules.

### Supported Rule Types

All existing rule types work in Relay mode without modification: `schedule`, `sensor`, `average_sensor`, `loop`, `step`, `ph`, `ec`, `dimmer_manual`, `dimmer_auto`.

---

## Data Transmission to Server

### Timing

| Event | Action |
|---|---|
| Any channel state changes | Immediate update to server |
| Every 30 seconds | Full state update |
| Every 30 seconds | Diagnostic data (RSSI, heap, uptime) |
| On startup | Structure sent, rules fetched |

### Update Payload

Uses the **same `UpdateDto` structure as Dry modules**:
- `sensors[]` — empty (no sensors in Relay mode)
- `devices[]` — all relay channel UIDs with current states (`on` / `off` / `disconnected`)

Transmission uses the existing Hybrid API Service — MQTT preferred, REST fallback. No new transmission mechanism needed.

---

## Complete Workflow Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                    RELAY OPERATIONS WORKFLOW                      │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────┐
│  1. INITIALIZATION   │
└──────┬───────────────┘
       ▼
  Read module type from hardware resistors
  Check operating mode from SPIFFS config
  Mode = RELAY → enter Relay mode
       ▼
  Load Relay Models from SPIFFS
  Load registered Relay Board from SPIFFS
  Initialize RS-485 adapter (9600, 8N1)
       ▼
  Connect WiFi + MQTT
  Subscribe to hardware/update/+
  Listen on UDP broadcast (local network)
  (sensor values from other modules for rules)

┌──────────────────────┐
│  2. SERVER SYNC      │
└──────┬───────────────┘
       ▼
  GET  /Hardware/is_deleted
  GET  /Hardware/account
  POST /Hardware/structure  (relay channels)
  GET  /Hardware/rulelist → save to SPIFFS

┌──────────────────────────────┐
│  3. RELAY BOARD REGISTRATION │
│  (first time or on command)  │
└──────┬───────────────────────┘
       ▼
  User selects model + Modbus address in app
  → AddRelayBoard command sent to firmware
  Firmware sends FC 0x01 Read Coils to verify
  Board responds → save to SPIFFS
  POST /Hardware/structure  (updated with relay channels)

┌──────────────────────┐
│  4. MAIN LOOP        │
└──────┬───────────────┘
       │
       ├── Every 1 second ──────────────────────────────────────┐
       │   FC 0x01 Read Coils → get bitmask                     │
       │   Compare with DeviceModel                             │
       │   State changed? → update model                        │
       │                  → send immediate update to server     │
       │   3 failures?    → all channels = disconnected         │
       │                  → send immediate update to server     │
       │                                                        │
       ├── Continuously ────────────────────────────────────────┤
       │   Rule engine loop                                     │
       │   Evaluate rules using:                                │
       │   - Channel states from DeviceModel                    │
       │   - Sensor values received via MQTT and UDP                    │
       │   Rule fires → FC 0x05 Write Single Coil               │
       │                                                        │
       ├── Every 30 seconds ────────────────────────────────────┤
       │   POST /Hardware/update   (all channel states)         │
       │   POST /Hardware/diagnostic                            │
       │                                                        │
       └── On command from server ──────────────────────────────┘
           GET  /Hardware/devices_command
           FC 0x05 Write Single Coil
           POST /Hardware/devices_command  (result)
```

---

## Troubleshooting

### 1. Relay Board Not Detected During Registration

**Symptoms**: Registration fails — no response from board

**Possible Causes**: wrong Modbus address, RS-485 A/B lines swapped, board not powered, baud rate mismatch

**Solutions**:
1. Verify the board's current Modbus address using SSCOM with a broadcast read command
2. Check RS-485 wiring: A→A, B→B
3. Verify board power supply voltage
4. Confirm baud rate is 9600 (factory default for Waveshare)

---

### 2. Relay Channels Not Switching

**Symptoms**: Command sent but no state change on the board

**Possible Causes**: RS-485 communication issue, board is in Linkage control mode (disables command control), load wired to wrong terminals

**Solutions**:
1. Test manually with SSCOM: send `01 05 00 00 FF 00 8C 3A` — should hear relay click and LED should light
2. Check board control mode register — reset to Normal mode if needed
3. Verify load is wired to COM + NO terminals (not NC)

---

### 3. State Polling Reports Wrong States

**Symptoms**: App shows wrong relay state; state oscillates

**Possible Causes**: another Modbus master on the same RS-485 bus is changing states, RS-485 noise on long cable run

**Solutions**:
1. State self-corrects on next 1-second poll — no manual action needed
2. Add 120Ω termination resistor at end of cable run for runs over 10m
3. Ensure no other device is sending Modbus commands to the board

---

### 4. Rules Not Executing

**Symptoms**: Rules are active but relay does not switch

**Possible Causes**: source sensor module is offline (no fresh values arriving via MQTT), rule is in `WaitingForUserConfirm`, rule mode is `Off`

**Solutions**:
1. Check that the sensor module providing values to this rule is online
2. Confirm the rule in the app if it shows `WaitingForUserConfirm`
3. Check rule mode is set to `On`

---

### 5. Board Shows Offline After Power Cycle

**Symptoms**: All channels show `disconnected` after power restored

**Possible Causes**: board needs ~500ms to stabilize before responding to Modbus after power-on

**Solutions**:
1. Firmware retries automatically — board returns online within a few polling cycles, no action needed
2. If address was reset to factory default: re-register the board in the app with address 0x01

---

## Summary

Key design decisions for the relay system:

1. **No new module type** — Sensor module hardware is reused; mode is set in software via SPIFFS config
2. **Relay model registry** — analogous to sensor types; new board models added via `relayModels.json` without firmware changes
3. **Explicit registration** — user selects model and address in app; firmware verifies board presence via Modbus before saving
4. **1-second polling** — fast state feedback; self-correcting without server involvement
5. **Event-driven reporting** — immediate update on state change, periodic full sync every 30 seconds
6. **Identical rule engine** — no changes to rule logic; sensor values come from other modules via MQTT and UDP (local network)
7. **Shared infrastructure** — reuses existing RS-485 adapter, Hybrid API Service, DeviceModel, UpdateDto, and command pipeline
