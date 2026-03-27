# GrowDirector — Modbus Relay Integration
## Business Analysis Document

**Audience:** Business Analysts, Product Owners  
**Date:** March 4, 2026  
**Status:** Proposal / Pre-development  
**Related Jira Epic:** [GDV1-2687 — RS-485 Support](https://growdirector.atlassian.net/browse/GDV1-2687)

---

## 1. Executive Summary

GrowDirector currently uses two types of physical modules for greenhouse automation:

- **Sensor modules** — read data from Modbus/RS-485 sensors (temperature, humidity, CO2, etc.)
- **Relay/Socket modules (Dry, Socket)** — control physical devices (fans, heaters, pumps) using onboard relays

The new feature extends the **existing Sensor module** (which already communicates over RS-485/Modbus) to also **control external Modbus relay boards** — such as the Waveshare Modbus RTU Relay (E). This means one RS-485 bus can now carry both sensor readings *and* relay control commands.

> **Key constraint:** When a Modbus relay module is attached to a Sensor module, **no RS-485 sensors are connected to the same module**. The RS-485 bus is dedicated to relay control.

---

## 2. Background: How the System Works Today

### 2.1 Module types in the current system

| Module Type | What it does | Controls devices? | Reads sensors? |
|---|---|---|---|
| **Dry** | Onboard relays (8 channels) | ✅ Yes | ❌ No |
| **Socket** | Smart sockets (power outlets) | ✅ Yes | ❌ No |
| **Hydro** | Pumps + pH/EC sensors | ✅ Yes | ✅ Yes (pH, EC) |
| **Sensor** | Reads RS-485/Modbus sensors | ❌ No (today) | ✅ Yes |

### 2.2 How rules work today

Rules are automation instructions stored in the cloud and executed **locally on the module** — the module works autonomously even when offline.

Rules link a **sensor** (data source) to a **device** (actuator). For example:
- *"When CO2 drops below 600 ppm → turn on the CO2 valve"*
- *"Every day from 08:00 to 20:00 → turn on grow lights"*

Currently, **rules that control devices can only run on Dry, Socket, or Hydro modules** — because those are the only modules that have controllable channels.

### 2.3 How Modbus sensors work today

The Sensor module communicates with RS-485/Modbus sensors (temperature, humidity, CO2, PAR, water level, etc.) via a two-wire bus. It reads sensor values every few seconds and sends them to the cloud, where they become available for rules on other modules.

---

## 3. What We Want to Build

### 3.1 The new capability: Modbus Relay on a Sensor module

We want to connect an **external Modbus relay board** (e.g., Waveshare RTU Relay (E), 8 channels, 30A) to the RS-485 port of a Sensor module. The module will then be able to:

1. **Send ON/OFF commands** to individual relay channels over Modbus RTU
2. **Report relay states** back to the cloud (so the UI can show current state)
3. **Execute rules** — the same automation rules that currently run on Dry/Socket modules

### 3.2 What does NOT change

- The cloud API, rule types, and rule execution logic remain the same
- Rules are still: schedule, sensor, average_sensor, loop, step, ph, ec, dimmer_manual, dimmer_auto
- The user experience for creating rules stays the same
- **No new module type is introduced** (see Section 4)

---

## 4. New Module Type? — Decision

### 4.1 Question: Do we need a new module type?

**A new module type is not only unnecessary — it is architecturally impossible.**

Module type in GrowDirector is not a software configuration. It is determined by **hardware resistors soldered on the PCB**. On startup, the ESP32 firmware reads the resistor combination from GPIO pins and derives the module type from that hardware code. This happens before any software logic runs. There is no way to "switch" a module to a different type through software or cloud configuration — it is physically fixed.

This means the Sensor module will always identify itself as a Sensor module. The new capability (controlling a Modbus relay board) is an **extension of what that module does**, not a change in what it is.

---

**Question from the team: Can the firmware auto-detect that a relay board is connected?**

This is a valid engineering question. Here is the honest analysis:

The Waveshare Modbus RTU Relay does have a **"Read Software Revision"** command (register `0x8000`, FC 03) that returns the firmware version of the relay board itself. In theory, firmware could send this command on startup and, if it receives a valid response, infer that a relay board is present.

However, **this is not a reliable detection method** for several reasons:

| Problem | Explanation |
|---|---|
| No universal "device type" register | Standard Modbus RTU has no mandatory identification register. Each manufacturer implements it differently — or not at all |
| Address conflict risk | A sensor and a relay board could both be at the same Modbus address; the response would be ambiguous |
| Different relay models respond differently | Other relay boards (non-Waveshare) may not implement the software revision register at all |
| Sensor could respond to same address | A Modbus sensor at address 0x01 would respond to the same query as a relay board at 0x01 |

**Conclusion:** Auto-detection is unreliable. The **recommended approach** is explicit configuration: during installation, the technician selects in the app (or via a physical DIP switch / bootloader parameter) that this Sensor module has a Modbus relay board attached. The firmware then knows to treat RS-485 traffic as relay control, not sensor reading.

### 4.2 How the operating mode is configured

Since auto-detection is unreliable (see 4.1), the mode is set **explicitly by the user through the app** in two places:

**A) During module onboarding (first time add)**
When a new Sensor module is added to the system, the app asks:
> *"How will this module be used?"*
> - 📡 Sensor mode — read RS-485 sensors
> - 🔌 Relay mode — control a Modbus relay board

The selected mode is saved on the server and sent to the module as part of its configuration.

**B) On the module settings page (mode switch)**
An existing Sensor module can be switched from Sensor mode to Relay mode (and back) directly on the module's page in the app.

Rules for switching:
- **Sensor → Relay mode**: only allowed if the module has **zero sensors configured**. If sensors exist, the switch is blocked with an explanation message.
- **Relay → Sensor mode**: allowed at any time (relay channels are removed, module reverts to sensor reading).
- After switching, the module restarts its operating cycle and begins working in the new mode.

### 4.3 How the module works in Relay mode

Once set to Relay mode, the Sensor module behaves **identically to a Dry module**:

1. On startup — sends its structure to the server, listing 8 relay channels as controllable devices
2. Syncs rules from the server for those devices
3. Executes rules locally and autonomously (offline-capable)
4. Receives sensor values from **other modules** via MQTT to evaluate sensor-based rules
5. Polls relay board state via Modbus every **1 second** (reads coil states over RS-485)
6. Reports device states to the cloud **every 30 seconds** or **immediately on state change**
7. Accepts manual ON/OFF commands from the app

### 4.4 Summary: existing vs new behavior

| Aspect | Sensor module — Sensor mode | Sensor module — Relay mode |
|---|---|---|
| RS-485 bus usage | Reads sensors | Sends relay commands |
| Sensors on this module | Yes | ❌ Not allowed |
| Sensor data for rules | From own sensors | From other modules via MQTT |
| Devices (relay channels) | No | Yes (8 channels via external board) |
| Executes rules | No | ✅ Yes — same as Dry module |
| Mode configurable in app | — | ✅ Yes, on module settings page |
| Switching condition | — | Only if no sensors are configured |

---

## 5. User Stories

### US-1: Select module mode during onboarding
> *As a technician setting up a new Sensor module, I want to choose during initial setup whether this module will work as a sensor reader or as a relay controller, so that the system is configured correctly from the start.*

**Acceptance criteria:**
- When adding a new Sensor module, the app shows a mode selection step: "Sensor mode" or "Relay mode"
- The selected mode is saved and applied to the module configuration
- In Relay mode, the module reports 8 relay channels in its structure; no sensors are expected

### US-2: Switch module mode on the settings page
> *As a grower, I want to switch an existing Sensor module from Sensor mode to Relay mode from the module settings page, so I can change the module's role without re-adding it.*

**Acceptance criteria:**
- Module settings page shows the current mode and a switch control
- Switching from Sensor → Relay mode is **blocked** if the module has any sensors configured; the app shows a message explaining why
- Switching from Relay → Sensor mode is always allowed
- After switching, the module restarts and begins working in the new mode

### US-3: Create automation rules for Modbus relay channels
> *As a grower, I want to create the same types of automation rules for Modbus relay channels as I do for regular Dry modules, including rules that react to sensors from other modules.*

**Acceptance criteria:**
- All existing rule types work: schedule, sensor, average_sensor, loop, step, etc.
- Sensor values from other modules are available as rule conditions (via MQTT, already supported)
- Rules execute locally on the module (offline-capable)
- Rule states are reported back to the cloud as usual

### US-4: Manual control of Modbus relay channels
> *As a grower, I want to manually turn a Modbus relay channel on or off from the app.*

**Acceptance criteria:**
- Manual ON/OFF command reaches the module via existing `devices_command` API
- The relay board responds within a few seconds
- Updated state is visible in the app

### US-5: Channel state visibility
> *As a grower, I want to see the current state of each relay channel in the app.*

**Acceptance criteria:**
- Firmware polls relay board state via Modbus every **1 second**
- State changes are reported to the cloud **immediately**
- Full state update is also sent to the cloud every **30 seconds** regardless of changes
- State is correctly reflected in the UI

---

## 6. How It Works — End to End

```
┌─────────────────────────────────────────────────────────────────┐
│                        CLOUD / BACKEND                          │
│  - Stores rules (schedule, sensor, loop, etc.)                  │
│  - Sends device commands (ON/OFF)                               │
│  - Receives device state updates every 5 sec                    │
└──────────────────────┬──────────────────────────────────────────┘
                       │ WiFi / MQTT + HTTP
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│              ESP32 SENSOR MODULE (firmware)                     │
│                                                                 │
│  1. On startup: scans RS-485 bus for Modbus relay board        │
│  2. Registers found relay channels as devices                   │
│  3. Syncs rules from cloud for those devices                    │
│  4. Executes rules locally (no internet needed)                 │
│  5. Reports device states to cloud every 5 sec                  │
│  6. Accepts manual ON/OFF commands from cloud                   │
└──────────────────────┬──────────────────────────────────────────┘
                       │ RS-485 / Modbus RTU (wired)
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│         MODBUS RELAY BOARD (e.g. Waveshare RTU Relay E)        │
│                                                                 │
│  - 8 relay channels, 30A each                                   │
│  - Address: configurable (default 0x01)                         │
│  - Baud rate: 9600, 8N1                                         │
│  - Receives ON/OFF commands from ESP32                          │
│  - Switches loads: pumps, heaters, fans, lights                 │
│                                                                 │
│  CH1 CH2 CH3 CH4 CH5 CH6 CH7 CH8                               │
│   │   │   │   │   │   │   │   │                                │
│  [Pump][Fan][Heater][Light]...                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Step-by-step flow when a rule fires:

1. Module evaluates rule condition locally (e.g., temperature > 28°C)
2. Rule triggers → module sends Modbus command to relay board: `01 05 00 00 FF 00` (turn ON relay 0)
3. Relay board closes its contact → connected device powers on
4. Module updates device state in its memory: `state = ON`
5. Next 5-second update cycle → module reports new state to cloud
6. Cloud updates the UI — user sees the device as ON

---

## 7. Constraint: One Bus, One Role

The RS-485 bus on a Sensor module can serve **one role at a time**:

```
Option A — Sensor mode (current behavior):
  ESP32 ──RS-485──► Temp sensor + CO2 sensor + PAR sensor ...
  (reads data, no relay control)

Option B — Relay control mode (new behavior):
  ESP32 ──RS-485──► Modbus Relay Board (8 channels)
  (controls relays, no sensors on this module)
```

**This is a hardware installation decision, not a software toggle.** The technician physically connects either sensors or a relay board to the RS-485 port. The firmware detects what is connected at startup.

Sensor data for relay-based rules (e.g., "fire relay when temperature > 30°C") can come from **another module's sensors** — this is already supported in the existing rules system via MQTT.

---

## 8. Impact on Existing Features

| Feature | Impact |
|---|---|
| Existing Dry/Socket modules | **No change** — they continue to work exactly as before |
| Existing Sensor modules | **No change** if no relay board is connected |
| Rules engine | **No change** — same rule types, same logic |
| Cloud API | **Minor extension** — relay channels appear in structure payload (already supported format) |
| Mobile app | **Minor UI** — relay channels need to be displayable as devices |
| Offline mode | **Fully supported** — rules execute locally on module |

---

## 9. Out of Scope (for this phase)

- Mixing RS-485 sensors and Modbus relay boards on the **same** module — not supported
- Analog/PWM dimmer control via Modbus — not in this phase
- New module type in the app — not required
- Support for relay boards from other manufacturers — to be validated per model

---

## 10. Open Questions for Product

| # | Question | Owner |
|---|---|---|
| 1 | What should the mode switch UI look like — toggle, dropdown, or dedicated button? Should it require confirmation? | Product / UX |
| 2 | What exact message is shown when the user tries to switch Sensor → Relay but sensors exist? | Product / UX |
| 3 | Should switching modes trigger an automatic module restart, or prompt the user to restart manually? | Product / Firmware |
| 4 | What happens if the relay board becomes unreachable after mode is set (RS-485 failure)? Should all 8 channels report error state? | Product / Firmware |
| 5 | Should we support multiple relay boards on one bus (different Modbus addresses)? | Product |
| 6 | What is the default Modbus address expected on the relay board — fixed at 0x01, or configurable per installation? | Firmware |

---

## 11. Glossary

| Term | Definition |
|---|---|
| **Modbus RTU** | Industrial serial communication protocol used to send commands and read data over RS-485 wiring |
| **RS-485** | Two-wire serial bus standard used in industrial automation; supports up to 255 devices on one bus |
| **Relay channel** | Single independently controllable output on a relay board; opens or closes an electrical circuit |
| **Sensor module** | ESP32-based GrowDirector module currently used exclusively for reading RS-485 sensors |
| **Dry module** | ESP32-based GrowDirector module with onboard relays for controlling AC/DC devices |
| **Rule** | Automation instruction (stored in cloud, executed on module) linking a sensor condition to a device action |
| **Device** | Any controllable output channel — onboard relay, socket, pump, or (new) Modbus relay channel |
| **Structure payload** | Data sent by the module to the server on startup, listing all its sensors and devices |
| **Waveshare RTU Relay (E)** | Specific Modbus relay board: 8 channels, 30A, RS-485, configurable address |
