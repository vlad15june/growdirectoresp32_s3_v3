# GDV1-3206: Stepper Rule Motor Protection - Feature Integration Analysis

**Feature:** Stepper Rule Motor Protection (prevent overtravel) + Wind Gust Safety Override  
**Status:** To Do | **Priority:** High | **Release:** App Release 1.16 (Feb 23, 2026)  
**Assignee:** Arsen R  

**Document Type:** High-Level Feature Integration Analysis  
**Audience:** Business Analysts, Product Managers, Technical Leadership  
**Created:** February 4, 2026

---

## Executive Summary

This feature adds a device-level Motor Protection system that prevents mechanical damage to greenhouse motors (vents, roll-ups, curtains) controlled by Stepper Rules. The system tracks estimated motor position based on runtime and blocks commands when physical limits are reached, preventing costly equipment damage from continuous operation against end-stops.

**Business Impact:**
- Reduces motor stalling and mechanical wear
- Prevents gearbox damage and equipment failures
- Increases integrator confidence in automated motor control
- Adds critical safety feature for high-wind conditions
- Enables more aggressive automation strategies

---

## 1. How This Feature Integrates with Current System

### 1.1 Integration with Stepper Rules

**What Are Stepper Rules:**  
The system currently supports Step Rules (also known as Stepper Rules) - an automation type that controls motors using repeated timed pulses. 

**How They Work Today:**
1. **Sensor monitoring:** System continuously checks sensor (e.g., Temperature)
2. **Condition trigger:** When threshold crossed (e.g., Temp > 40°C), rule activates
3. **Pulse execution:** Device turns ON for configured time (e.g., Open motor for 60 seconds)
4. **Wait period:** Device turns OFF and waits (e.g., Hold 10 minutes)
5. **Repeat cycle:** System re-checks sensor and repeats if condition still met

**Key Capabilities:**
- **Pulse duration:** Configure how long each activation lasts
- **Wait time:** Control delay between pulses
- **Cycle counting:** Track how many pulses have executed
- **Target value:** Automatically stop when sensor reaches desired value

**Typical Use Case Example:**  
Greenhouse temperature is 45°C (too hot). Step Rule opens roof vent for 1 minute, waits 15 minutes to let hot air escape, then checks temperature again. If still 45°C, repeats. Continues until temperature drops to 38°C.

**The Problem:**
When a motor reaches physical end-stop but sensor condition remains crossed (e.g., temperature still high), the system continues sending Open commands. The motor stalls against the end-stop, causing:
- Motor overheating
- Gearbox wear
- Mechanical damage to windows/vents/roll-ups
- Increased maintenance costs

**Motor Protection Integration:**
The new feature adds a protective layer **between** rule evaluation and device execution:
1. Stepper Rule evaluates condition (unchanged)
2. **NEW:** Motor Protection checks estimated position
3. **NEW:** Blocks command if at limit OR allows command if position OK
4. Device executes (if allowed)

This is **device-level enforcement** - it applies to all rules controlling a motor, not per-rule logic.

### 1.2 Integration with Current Device Management

**Current Device Types:**  
The system manages multiple device types through `DeviceModel`:
- **DryContactDirector:** Relays for on/off control (used for motors)
- **SocketDirector:** Power sockets with on/off control
- **HydroDirector:** Pumps with step-based control
- **DimmerDirector:** Analog control for lighting/fans

**Motor Protection Scope:**  
Applies specifically to devices controlled by Step Rules, primarily:
- Roof vents/windows (DryContact relays)
- Side roll-ups (DryContact relays)
- Curtains with open/close motors (DryContact relays)
- Other motor actuators using pulse control

**Current States (from DeviceStateEnum.h):**
- `on` - Device actively running
- `off` - Device stopped
- `stepOn` - Pulse currently executing (motor running)
- `stepWaiting` - Waiting between pulses (motor stopped)
- `delayOn` / `delayOff` - Trigger delay states
- `requestOn` / `requestStepOn` - Queued to start

**New Motor Protection States:**
- `blockedOpen` - At upper limit (100%), Open commands blocked, Close allowed
- `blockedClose` - At lower limit (0%), Close commands blocked, Open allowed
- `motorPositionUnknown` - Position unknown after reboot (fail-safe, all commands blocked until reset)
- `motorMoving` - Currently tracking position change (combines with stepOn)
- `motorAtPosition` - Stable at estimated position (e.g., "motorAtPosition_75%" for 75% open)

---

## 2. User Experience Changes

### 2.1 Mobile App Changes

**Device Configuration Screen:**
- New section: "Motor Protection Settings"
- Input field: Full Travel Time (hh:mm:ss format)
- Display field: Estimated Position (~75% open)
- Admin controls: Reset to Fully Open / Fully Closed buttons
- Warning indicator: "Position Unknown - Calibration Required"

**Rule Configuration Screen:**
- Step Rule form adds checkbox: "Wind Gust Safety"
- Conditional field: Wind speed threshold (number input with unit)
- Warning message: "Rule cannot run without Motor Protection configured"
- Acknowledgment: Checkbox for running without T_max (with warning)

**Device Status Display:**
- New status badges: "Blocked - Fully Open", "Blocked - Fully Closed"
- Position indicator: Progress bar showing estimated position
- Safety indicators: Wind icon when gust safety active

**Manual Control:**
- Blocked directions show disabled buttons
- Override checkbox: "I understand limit may be exceeded"
- Warning dialog before override execution

### 2.2 Web App Changes

**Desktop Admin Features:**
- Bulk motor configuration (set T_max for multiple devices)
- Motor position dashboard (all motors on one screen)
- Wind safety configuration page
- Historical position tracking charts
- Calibration wizard for new motors

**Workflow Integration:**
- Motor availability checks before workflow activation
- Visual indicators on workflow nodes (motor at limit)
- Automatic workflow pause on wind gust trigger

### 2.3 Notification System

**New Notification Types:**
- "Motor Blocked: [Device] at upper limit"
- "Wind Gust Safety Activated - Closing all motors"
- "Motor Protection: Position unknown after reboot"
- "Motor Protection: T_max not configured for [Device]"

**Alert Priorities:**
- Critical: Wind gust safety activation
- Warning: Motor blocked at limit (repeated attempts)
- Info: Motor protection configuration required

---

## 3. UX Scenarios and Configuration Details

### 3.1 How to Configure Motor Protection (motor_protection_config)

**Current Device Management Pattern:**  
Based on current system architecture, device configuration happens through backend API endpoints. The firmware receives configuration via structure sync and rule sync endpoints.

**Motor Protection Configuration Flow:**

**Step 1: Device Details Screen (Mobile/Web App)**
- User navigates to device (e.g., "Roof Vent 1")
- Current device screen shows:
  - Device name and UID
  - Current hardware state: `state_hw` (on/off)
  - Current automation state: `state` (stepOn, stepWaiting, delayOn, etc.)
  - Manual controls (Open/Close buttons)
  - Rule assignments

**New Section: "Motor Protection"** (expandable)
- **Enable Motor Protection:** Toggle switch (default: OFF)
- **Full Travel Time:** Time picker in format `mm:ss` (e.g., 04:00)
  - Label: "Time to fully open from closed"
  - Helper text: "Run motor from fully closed to fully open and measure the time"
  - Validation: Must be between 00:30 and 30:00
- **Current Position:** Display-only field
  - Shows: "~75% open (estimated)" or "Position unknown - calibration needed"
  - Color-coded: Green (0-30%), Yellow (30-70%), Orange (70-100%), Red (blocked), Gray (unknown)
- **Admin Reset Buttons:**
  - "Reset to Fully Closed (0%)" - sets P_est = 0%
  - "Reset to Fully Open (100%)" - sets P_est = 100%
  - Warning dialog: "This will override current position estimate. Continue?"

**Step 2: Save Configuration**
- App sends: `PUT /api/v1/Device/{uid}/motor_protection`
```json
{
  "enabled": true,
  "fullTravelTimeSeconds": 240,
  "resetPosition": null  // or 0 or 100 if admin clicked reset
}
```

**Step 3: Backend → Firmware Sync**
- Backend stores config in database
- Next structure sync (`GET /api/v1/Hardware/structure`) includes motor protection config
- Firmware updates local device settings
- Firmware persists to SPIFFS

**Where in Current UI:**  
Motor Protection section appears:
- Mobile App: Device Details → Settings tab → "Advanced" section
- Web App: Device card → Edit → "Motor Protection" accordion panel

### 3.2 Adjusting Travel Time (T_max Too Short)

**Problem Scenario:**  
User configured T_max = 3:00 but motor needs 4:00 to fully open. System blocks Open commands at 3:00 when motor is only 75% open.

**Solution Path 1: Direct Edit**
1. Open Device Details → Motor Protection settings
2. Change "Full Travel Time" from 03:00 to 04:00
3. Save changes
4. System **keeps current position estimate** but recalculates percentages:
   - Old: 75% at 3:00 runtime
   - New: 56% at 3:00 runtime (3:00 ÷ 4:00)
5. Motor can now continue opening until 4:00 (100%)

**Solution Path 2: Manual Calibration**
1. If unsure of correct time, use "Calibration Mode" (future feature):
   - Reset position to 0% (fully closed)
   - Click "Start Calibration"
   - System runs motor Open until user clicks "Stop at Fully Open"
   - Measured time becomes new T_max
   - Position automatically set to 100%

**Important Behavior:**  
- **Position is NOT reset** when T_max changed
- System recalculates position percentage based on new T_max
- If motor was blocked, it may unblock after T_max increase

### 3.3 Recovering from Position Desync

**Problem Scenario:**  
System thinks motor is at 100% (fully open) but actual position is 50%. Possible causes:
- Manual intervention (someone physically moved motor)
- Firmware reboot during movement
- Motor slippage or mechanical failure

**Detection Methods:**

**User Reports:**
- "Motor won't open but I see it's only halfway open"
- Device status shows "Blocked at upper limit" but physically not at limit

**System Indicators:**
- Wind sensor shows high speed but vents show "fully open" (should be closed)
- Temperature not dropping despite vents showing "fully open"

**Recovery Options:**

**Option 1: Admin Position Reset (Quickest)**
1. Physically verify motor position (look at actual vent)
2. Open Device Details → Motor Protection
3. Estimate actual position:
   - Fully closed → Click "Reset to Fully Closed (0%)"
   - Fully open → Click "Reset to Fully Open (100%)"
   - Halfway → Click "Reset to Fully Closed", then manually run motor to actual position
4. Confirm warning dialog
5. System immediately updates P_est
6. Motor protection resumes with corrected position

**Option 2: Manual Run to Known Position**
1. Disable rules temporarily (set rule mode to OFF)
2. Use Manual Control to run motor to known position:
   - Run Close until physically reaches 0% (fully closed)
   - Use "Override limit protection" checkbox if needed
3. Click "Reset to Fully Closed (0%)"
4. Re-enable rules

**Option 3: Recalibration (Full Reset)**
1. Device Settings → Motor Protection → "Recalibrate"
2. System:
   - Runs Close until timeout or blocked
   - Sets P_est = 0%
   - Measures time to fully open
   - Sets new T_max and P_est = 100%
3. Calibration complete

**System Logging:**
- All position resets logged to device history
- Shows: "Position manually reset to 0% by user@email.com"
- Timestamp and reason (if provided)

### 3.4 Display Current Position on UI

**Current Device Display System:**  
Based on `DeviceDto` structure, devices display state via `state` and `state_hw` fields sent in update payloads.

**Current Device Card (Mobile/Web):**
- Device name + icon (vent, pump, light)
- **Status Badge:** Shows `state` value
  - Current values: "on", "off", "stepOn", "stepWaiting", "delayOn", etc.
  - Color-coded: Green (on), Gray (off), Blue (delayOn), Orange (stepWaiting)
- **Hardware Indicator:** Small LED dot showing `state_hw` (on/off)
- **Rule Info:** Shows active rule name if `workByRule = true`
- **Quick Actions:** Manual control buttons

**New Motor Protection Display:**

**Status Badge Enhancement:**
- **When Motor Protection Active:**
  - Badge shows state + position: "stepOn · 45%"
  - Badge shows: "blockedOpen · 100%" (red)
  - Badge shows: "stepWaiting · 67%" (orange)
  - Badge shows: "Position Unknown" (gray, requires attention)

**Position Indicator (New Component):**
- **Visual:** Horizontal progress bar (0% left, 100% right)
- **Color Gradient:** Blue (closed) → Green (mid) → Orange (open)
- **Marker:** Shows current estimated position
- **Blocked Zones:** Red overlay at 0% or 100% when blocked
- **Example:**
  ```
  [████████░░] 80% open
  ↑ Can close  ⚠️ Open blocked
  ```

**Device Card Layout (Updated):**
```
┌─────────────────────────────────┐
│ 🪟 Roof Vent 1                  │
│ ┌─────────────────────────────┐ │
│ │ stepWaiting · 75%           │ │ ← Status badge
│ └─────────────────────────────┘ │
│                                  │
│ Position: 75% open (estimated)   │
│ [███████░] ←−−−→                │ ← Progress bar
│                                  │
│ Active Rule: Temperature Control │
│ Next pulse in: 8m 45s            │
│                                  │
│ [Close]  [Stop]  [Open]         │ ← Manual controls
└─────────────────────────────────┘
```

**Where Position Data Comes From:**

**Firmware → Backend → App Flow:**
1. Firmware tracks P_est in real-time
2. Every 5 seconds, firmware sends update via `POST /api/v1/Hardware/update`
3. Update payload (`DeviceDto`) adds new fields:
```json
{
  "uid": "device-123",
  "state": "stepWaiting",
  "state_hw": "off",
  "motorProtection": {
    "enabled": true,
    "estimatedPosition": 75,
    "isBlocked": false,
    "blockedDirection": null,
    "lastUpdated": "2026-02-05T10:30:45Z"
  }
}
```
4. Backend stores in database
5. Backend pushes to connected clients via WebSocket
6. App updates UI in real-time

**Real-Time Updates:**
- Position updates while motor running (every second)
- WebSocket push to all connected devices
- Smooth animation on progress bar
- State badge updates immediately on blocking

### 3.5 Complete State Machine for Motor Protection

**Problem Identified:**  
Document listed only blocking states (`blockedOpen`, `blockedClose`, `unknown`) but missed states for **currently moving** and **at stable position**.

**Complete State Enum Extension:**

```cpp
// Additions to DeviceStateEnum.h:
enum DeviceStateEnum {
    // ... existing states ...
    DEVICE_STATE_STEP_WAITING,
    
    // New Motor Protection States:
    DEVICE_STATE_MOTOR_BLOCKED_OPEN,      // At 100%, Open blocked
    DEVICE_STATE_MOTOR_BLOCKED_CLOSE,     // At 0%, Close blocked
    DEVICE_STATE_MOTOR_POSITION_UNKNOWN,  // After reboot, position unknown
    DEVICE_STATE_MOTOR_RELEASING_BLOCK    // Running opposite direction for T_release
};
```

**State Transitions with Motor Protection:**

**Normal Operation (Not Blocked):**
1. `off` → Step Rule triggers → `requestStepOn`
2. `requestStepOn` → Motor Protection checks position → `stepOn` (if allowed)
3. `stepOn` → Pulse completes → `stepWaiting`
4. `stepWaiting` → Wait completes → `requestStepOn` (repeat)
5. Cycle: `requestStepOn` ↔ `stepOn` ↔ `stepWaiting`

**Reaching Limit (Blocking):**
1. Motor at 95%, Step Rule sends Open command
2. `stepWaiting` → `requestStepOn`
3. Motor Protection detects P_est will reach 100%
4. Command blocked, state changes: `requestStepOn` → `blockedOpen`
5. Status: "Blocked at upper limit - fully open"
6. User sees: Red badge "blockedOpen · 100%"

**Unblocking Sequence:**
1. Temperature rises again (condition to close)
2. Step Rule sends Close command
3. `blockedOpen` → `releaseBlock` (motor starts closing)
4. Motor runs for T_release (60 seconds default)
5. P_est decreases: 100% → 75%
6. `releaseBlock` → `stepOn` (block cleared)
7. Normal operation resumes: `stepOn` ↔ `stepWaiting`

**After Reboot (Position Unknown):**
1. Firmware boots, reads position from SPIFFS
2. **If read successful:** Resume with last known P_est
3. **If read fails/corrupted:** State = `motorPositionUnknown`
4. All motor commands blocked (fail-safe)
5. User sees: Gray badge "Position Unknown - Calibration Required"
6. Admin must manually reset position
7. After reset: `motorPositionUnknown` → `off`

**Manual Override (Emergency):**
1. User clicks manual Open while `blockedOpen`
2. System shows warning: "Motor at limit. Override protection?"
3. User checks "I understand limit may be exceeded"
4. System allows command with flag: `overrideProtection = true`
5. State: `blockedOpen` → `on` (manual override active)
6. Motor runs regardless of position
7. **Warning logged:** "Manual override of motor protection by user@email.com"

**State Display Mapping:**

| State | Display Text | Badge Color | Position Bar | Actions Allowed |
|-------|--------------|-------------|--------------|-----------------|
| `stepOn` | "Opening · 45%" | Blue | Animating right | Close only |
| `stepWaiting` | "Waiting · 45%" | Orange | Static | Both (if not at limits) |
| `blockedOpen` | "Blocked · 100%" | Red | At right end | Close only |
| `blockedClose` | "Blocked · 0%" | Red | At left end | Open only |
| `motorPositionUnknown` | "Position Unknown" | Gray | Hidden | None (requires reset) |
| `releaseBlock` | "Unblocking · 85%" | Yellow | Animating | None (automatic) |

**Key Insight:**  
The "current position" is **NOT a state** - it's a **property** stored in device entity and displayed alongside state. States describe **what device is doing**, position describes **where device is**.

---

## 4. Current Module Architecture and Feature Placement

### 4.1 System Logic Flow Overview

**Current Processing Flow:**  
The module (ESP32 controller) processes automation in layers:
- **Rule Logic:** Evaluates sensor conditions and determines what devices should do
- **Device Control:** Executes the actual on/off commands to relays and motors
- **Hardware Interface:** Physically switches relays and reads sensors

**How Motor Protection Fits In:**

The Motor Protection logic will act as a safety gate between rule decisions and device execution:

**Current Flow:**
1. System checks sensors (temperature, humidity, etc.)
2. Rules evaluate: "Should motor open or close?"
3. Device control executes the command
4. Motor activates

**New Flow with Motor Protection:**
1. System checks sensors (temperature, humidity, etc.)
2. Rules evaluate: "Should motor open or close?"
3. **[NEW] Motor Protection checks: "Is this command safe? Are we at position limits?"**
4. If safe: Device control executes the command
5. If blocked: Command is rejected, status logged
6. Motor activates (only if command was allowed)

### 4.2 How Position Tracking Works

**Real-Time Position Monitoring:**  
The module will continuously track each motor's estimated position based on how long it has been running:

**What the System Tracks Per Motor:**
- Current estimated position (0% = fully closed, 100% = fully open)
- Full travel time from the user configuration
- Whether Open or Close commands are currently blocked
- How long the motor has been running in the opposite direction (for unblocking)
- Last update timestamp

**Position Calculation Logic:**

When motor runs in **Open direction:**
- System calculates: How much time did motor run ÷ Total travel time = Percentage opened
- Example: Ran 1 minute ÷ 4 minute total = 25% movement
- New position = Current position + 25%

When motor runs in **Close direction:**
- Same calculation but subtracts from current position
- Position always stays between 0% and 100%

**When Commands Get Blocked:**
- **Block Open** when position reaches 100% (fully open)
- **Block Close** when position reaches 0% (fully closed)
- **Unblock** after motor runs in opposite direction for at least release time (default 60 seconds)

### 4.3 Data Persistence and Recovery

**Current Data Storage:**  
The module already stores critical data locally on its internal memory:
- Sensor calibration values
- WiFi network credentials
- Diagnostic logs

**New Motor Protection Data Storage:**
The system will save motor position data locally so it survives power outages and reboots:

**What Gets Saved:**
- Each motor's last known position (percentage)
- Configured full travel time
- Whether motor was blocked before reboot
- Timestamp of last update

**Recovery After Reboot:**

When module restarts (power outage, firmware update, etc.):

**Best Case - Data Intact:**
1. Module reads saved position data from memory
2. Resumes normal operation with last known positions
3. Motors can continue working immediately

**Worst Case - Data Lost/Corrupted:**
1. Module detects data cannot be read
2. Sets all motor positions to "Unknown" state
3. Blocks ALL motor commands (fail-safe mode)
4. Requires admin to manually reset positions
5. Logs the recovery event for troubleshooting

### 4.4 Wind Gust Safety Feature

**Continuous Wind Monitoring:**  
When Wind Gust Safety is enabled for a Stepper Rule, the system continuously monitors wind speed from a connected meteorological station sensor.

**How It Works:**
- System checks wind speed reading every second
- Compares current wind speed to configured threshold
- Takes immediate action if threshold exceeded

**Emergency Close Sequence:**

When wind speed exceeds threshold (e.g., 15 m/s):

**Step 1 - Immediate Override:**
- Wind safety has highest priority - overrides all other rules
- All affected motors receive emergency Close command
- Other automation temporarily suspended

**Step 2 - Force Close:**
- Motors begin closing immediately
- System ignores normal rule conditions
- Continues until position reaches 0% (fully closed)

**Step 3 - Lock Open Commands:**
- All Open commands blocked while wind is high
- Close commands still allowed
- Status displayed in app: "Wind Safety Active"

**Step 4 - Automatic Resume:**
- When wind speed drops below threshold
- System releases the lock
- Normal automation resumes
- Motors can open again based on regular rules

---

## 5. Related Features and Dependencies

### 5.1 Recent Features That Interact

**Trigger Delay (GDV1-2577) - Delivered 2025:**
- **Relationship:** Complementary safety mechanism
- **Integration:** Both features can be active simultaneously
- **Execution Order:** Trigger Delay → Motor Protection → Device Control
- **States:** Uses `delayOn`/`delayOff` states that Motor Protection respects

**Offline Mode (GDV1-903) - In Progress:**
- **Relationship:** Position data must persist during offline periods
- **Integration:** Motor Protection continues working without backend connectivity
- **Recovery:** Position synced to backend when connection restored
- **Data:** Offline logs include motor protection events

**Workflow Automation (GDV1-2591) - In Progress:**
- **Relationship:** Workflows may include motor movement sequences
- **Integration:** Motor Protection applies to workflow-controlled motors
- **Conflict Resolution:** Workflow blocked if motor at limit
- **Safety:** Wind gust safety can interrupt workflows

### 5.2 Hardware Module Dependencies

**Firmware Requirements:**
- **Minimum Version:** Will require new firmware (likely 1.16.x)
- **Board Support:** All current boards (ESP32-S2, ESP32-S3)
- **Memory Impact:** Additional RAM for position tracking (~100 bytes per motor)
- **SPIFFS:** Requires file system initialization for persistence

**Sensor Dependencies:**
- **Wind Sensor:** New sensor type for Wind Gust Safety
- **Sensor Communication:** Standard analog/digital input via API
- **Calibration:** Wind sensor must be calibrated (units: m/s or mph)

### 5.3 Backend System Requirements

**Database Schema Changes:**
- New table: `MotorProtectionConfig` (per device)
- New fields in `Device` table: `estimatedPosition`, `fullTravelTime`
- New rule fields: `windGustSafety` configuration
- Event logging: Motor protection events in device history

**API Service Changes:**
- New endpoints: Motor protection configuration CRUD
- Extended endpoints: Device status includes motor state
- New validation: Step Rules require T_max before activation

**Real-Time Processing:**
- MQTT subscribers: Handle motor protection position updates
- Event handlers: Wind gust emergency triggers
- State synchronization: Position updates propagated to connected apps

---

## 6. How This Feature Fits into Current API

### 6.1 Hardware API Integration (Module ↔ Backend)

**Current Communication Pattern:**  
Firmware communicates with backend via HTTP and MQTT:
- **Structure sync:** POST `/api/v1/Hardware/structure` (on startup)
- **State updates:** POST `/api/v1/Hardware/update` (every 5 seconds)
- **Rule sync:** GET `/api/v1/Hardware/rulelist` (on startup)
- **Commands:** GET/POST `/api/v1/Hardware/devices_command`

**New API Requirements for Motor Protection:**

**1. Device Configuration (per motor):**
```
GET /api/v1/Device/{uid}/motor_protection_config
{
  "fullTravelTimeSeconds": 240,  // T_max (4:00 in seconds)
  "estimatedPosition": 75,       // Current P_est (0-100%)
  "releaseTimeSeconds": 60,      // T_release for unblocking
  "calibrationStatus": "calibrated" | "uncalibrated" | "unknown"
}

POST /api/v1/Device/{uid}/motor_protection_config
{
  "fullTravelTimeSeconds": 240,
  "resetPosition": null | 0 | 100  // Admin reset: null=no change, 0=fully closed, 100=fully open
}
```

**2. Position Updates (from firmware):**  
Extend existing `/api/v1/Hardware/update` payload:
```json
{
  "devices": [
    {
      "uid": "device-123",
      "state": "blockedOpen",  // New state
      "motorProtection": {
        "estimatedPosition": 100,
        "blocked": true,
        "blockedDirection": "open"
      }
    }
  ]
}
```

**3. Rule Configuration:**  
Extend Step Rule definition in `/api/v1/Hardware/rulelist` response:
```json
{
  "ruleType": "step",
  "windGustSafety": {
    "enabled": true,
    "thresholdWindSpeed": 15.5,  // m/s or mph based on system units
    "sensorUid": "wind-sensor-001",
    "forceCloseAction": true
  }
}
```

### 6.2 Mobile/Web App API Integration

**Current App Communication:**  
Apps communicate with backend via REST API:
- Device control: `/api/v1/Device/{uid}/command`
- Rule management: `/api/v1/Rule` CRUD operations
- Sensor readings: Real-time via WebSocket or polling

**New App API Requirements:**

**1. Motor Configuration UI:**
```
GET /api/v1/Device/{uid}
Response adds:
{
  "deviceType": "dryContact",
  "motorProtection": {
    "enabled": true,
    "fullTravelTime": "00:04:00",  // User-friendly format
    "estimatedPosition": 75,
    "estimatedPositionText": "~75% open (estimated)",
    "status": "normal" | "blockedOpen" | "blockedClose" | "unknown"
  }
}

PUT /api/v1/Device/{uid}/motor_protection
{
  "fullTravelTime": "00:04:00",  // Manual entry in hh:mm:ss
  "resetPosition": 0 | 100       // Admin reset controls
}
```

**2. Rule Configuration UI:**
```
POST /api/v1/Rule
Request body for Step Rule adds:
{
  "ruleType": "step",
  "windGustSafety": {
    "enabled": true,
    "thresholdWindSpeed": 15.5,
    "unit": "m/s" | "mph"  // Based on system settings
  }
}
```

**3. Status Display:**
```
GET /api/v1/Device/{uid}/status
Response adds motor-specific status:
{
  "currentState": "blockedOpen",
  "statusMessage": "Blocked at limit - fully open",
  "canOperate": false,
  "operationBlocked": {
    "reason": "Motor at upper position limit",
    "blockedDirections": ["open"],
    "allowedDirections": ["close"]
  }
}
```

### 6.3 MQTT Topic Integration

**Current MQTT Architecture:**  
System uses MQTT for real-time communication:
- Device commands: `hardware/devices_command/get/{UID}`
- Device updates: `hardware/update/{UID}`
- Diagnostics: `hardware/diagnostic/{UID}`

**New MQTT Topics:**

**Position Updates (from module):**
```
Topic: hardware/motor_protection/{UID}
Payload:
{
  "deviceUid": "device-123",
  "estimatedPosition": 85,
  "blocked": false,
  "timestamp": 1738761600
}
```

**Emergency Wind Override:**
```
Topic: hardware/wind_safety/{UID}
Payload:
{
  "triggered": true,
  "windSpeed": 18.2,
  "threshold": 15.0,
  "action": "force_close_all",
  "affectedDevices": ["vent-1", "vent-2", "rollup-1"]
}
```

---

## References

### Confluence Documentation Reviewed

**Feature Specifications:**
- [Workflow Automation & Sequencing - Functional Specification](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/855474178) - Referenced for understanding how workflows interact with device control
- [Architecture and Solutions](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/278757448) - Core system architecture and module communication patterns

**Technical Documentation:**
- [API Requests from Module (Firmware) to Server (Backend)](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/436273185) - Hardware API communication flow and patterns
- [MQTT topics](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/496828418) - Real-time communication channels
- [Firmware: Debug Logs (mqtt)](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/279183372) - Logging infrastructure for diagnostics

### Jira Tickets Analyzed

**Related Feature Deliveries:**
- [GDV1-2577: Trigger Delay](https://growdirector.atlassian.net/browse/GDV1-2577) - Complementary safety feature with delay states
- [GDV1-2591: Workflow Automation & Sequencing](https://growdirector.atlassian.net/browse/GDV1-2591) - Integration with complex automation sequences
- [GDV1-903: Offline Mode](https://growdirector.atlassian.net/browse/GDV1-903) - Position persistence requirements during offline operation

**Current Ticket:**
- [GDV1-3206: Stepper Rule Motor Protection](https://growdirector.atlassian.net/browse/GDV1-3206) - Primary feature specification

---
