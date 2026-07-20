
---

## UX Implementation Summary (from sections 2 & 3)

**Mobile App UI Changes:**
1. Device settings: Add "Motor Protection Settings" section with T_max input (hh:mm:ss), position display, reset buttons
2. Rule config: Add "Wind Gust Safety" checkbox with wind speed threshold input field
3. Device status: New badges showing "Blocked - Fully Open/Closed", position progress bar (0-100%)
4. Manual controls: Disabled buttons for blocked directions, override checkbox with warning dialog
5. Notifications: Motor blocked alerts, wind gust activation, position unknown warnings

**Web App Desktop Features:**
6. Bulk motor configuration: Set T_max for multiple devices at once
7. Motor position dashboard: All motors displayed on one screen with positions
8. Wind safety configuration page: Centralized wind gust threshold management
9. Historical charts: Track position over time for calibration accuracy verification
10. Calibration wizard: Step-by-step guide for measuring T_max on new motors

**Configuration Workflow:**
11. Device Details → Motor Protection → Enable toggle → Enter T_max → Save → Backend syncs to firmware via /api/v1/Hardware/structure
12. If T_max wrong (too short): Edit T_max → Position recalculates percentage (not reset) → Motor unblocks if new limit allows
13. Position desync recovery: Admin reset to 0% or 100% → Logged with timestamp and user → Motor protection resumes

**Position Display System:**
14. Status badge shows state + position: "stepOn · 45%", "blockedOpen · 100%", "Position Unknown"
15. Progress bar: Blue (closed) → Green (mid) → Orange (open), red overlay when blocked
16. Real-time updates: Firmware sends motorProtection object in DeviceDto every 5 seconds → WebSocket push to clients
17. Device card layout: Badge + position text + progress bar + rule info + manual controls

**State Machine:**
18. Normal cycle: off → requestStepOn → stepOn → stepWaiting → repeat
19. Blocking: At 95% → requestStepOn → Motor Protection blocks → blockedOpen state → Open commands rejected
20. Unblocking: blockedOpen → Close command → releaseBlock state → Runs T_release (60s) → stepOn → Normal operation

---

## Open Design Problems (from full analysis)

**Problem 1: Two-Relay Motor Pairing**
- Physical motor has separate Open relay and Close relay (2 device UIDs)
- Position must be shared between both devices
- Need logical "Motor" entity grouping paired devices
- UI must show pairing configuration and prevent conflicting rules

**Problem 2: Virtual Position Sensor Design**
- Position must be accessible as sensor value for rules ("If vent > 80%, activate shade")
- Need sensor UID format: motor-pos-{deviceUid}, UUID, or {deviceUid}-position?
- Must appear in /api/v1/Hardware/update sensors array without colliding with real sensors
- Unclear if position sensor should be visible in sensor lists or hidden (internal only)

**Problem 3: Multi-Device Position Synchronization**
- relay-open and relay-close must read/write same P_est value
- Firmware data structure for shared position sensors undefined
- Configuration flow: How does second device select existing position sensor?
- Update payload: Single sensor entry or duplicated per device?

**Problem 4: Module Type Scope**
- Draft says "Stepper Rule-controlled motors" but unclear which modules support it
- SocketDirector has relays (can control motors) - include or exclude?
- HydroDirector has pumps with step control - is "position" = volume pumped or physical position?
- DimmerDirector analog control not step-based - explicitly exclude

**Problem 5: Position Persistence Failure**
- Draft requires "fail-safe: mark unknown and block until reset" if SPIFFS unavailable
- After reboot with failed SPIFFS read: block ALL commands or allow with warning?
- State = motorPositionUnknown but unclear if this persists or clears after single reset
- Recovery flow: Admin must reset, but what if admin not available?

**Problem 6: Backward Compatibility Strategy**
- Draft contradicts: "T_max not set → Rule cannot run" vs "Allow with warning + acknowledgement"
- Existing Step Rules without Motor Protection: Optional (no changes) or Mandatory (forced migration)?
- If optional: Old rules work as before, but how to encourage adoption?
- If mandatory: Migration UI required, potential user frustration

**Problem 7: T_release Configuration**
- Draft says "60s default or as pulse pre-set" - which is it?
- Fixed 60s: Simple but inflexible for fast/slow motors
- Configurable per device: More complex UI and firmware logic
- Configurable per rule: Even more complex, different unblock times per rule

**Problem 8: Manual Override Safety**
- Draft mentions "checkbox declaring you are aware" but unclear when shown
- Override during blockedOpen: Always available or only after N failed attempts?
- Override logging: Stores user email but what if offline mode?
- Override flag persistence: Does overrideProtection state persist after command completes?

**Problem 9: Wind Gust Integration**
- "Ready to operate with meteorological station" - which sensor types/models supported?
- Wind speed units "V\min" unclear - m/s, km/h, mph?
- Default threshold values undefined - needs regional greenhouse standards research
- "Force CLOSE completely" - override manual controls or only automatic rules?

**Problem 10: Position Accuracy Drift**
- Time-based estimation accumulates error: wrong T_max, motor slippage, power fluctuations
- No detection mechanism for drift - when to alert user about misconfiguration?
- Acceptable accuracy threshold undefined - 5% drift? 10% drift?
- Mitigation: "Auto-calibration mode" mentioned but not in MVP scope




## 0. Open Questions and Design Decisions

### 0.1 Two-Relay Motor Control (Open + Close)

**Question:** How to handle motors with separate Open and Close relays?  
**Current System:** Each relay = separate Device with unique UID

**Problem:**
- Physical motor = 1 unit, but system sees 2 devices
- Position must be shared between Open relay and Close relay
- If Open relay at 100%, Close relay must know to allow movement
- Both relays control SAME motor position

**Example:**
- Device `relay-1` (Open) - runs motor forward
- Device `relay-2` (Close) - runs motor backward
- Need to link them as controlling same motor

**Proposed Solutions:**
Virtual Position Sensor



### 0.2 Virtual Position Sensor

**Question:** Where does motor position value live in the system?  
**Current System:** Sensors have UIDs and appear in sensor update payloads

**Requirements:**
- Position must be accessible as sensor value (0-100%)
- Should appear in `/api/v1/Hardware/update` in `sensors` array
- Other devices/rules might read position sensor
- Must not conflict with physical sensor UIDs

**Proposed Design:**

**Virtual Sensor in Update Payload:**
```json
{
  "devices": [...],
  "sensors": [
    {
      "uid": "motor-pos-relay-1",  // Virtual sensor
      "type": "motor_position",
      "value": 75.0,
      "unit": "percent",
      "isVirtual": true,
      "linkedDeviceUid": "relay-1"
    }
  ]
}
```

**UID Format Options:**

**Option A: Prefix-based**
- `motor-pos-{deviceUid}` 
- Example: `motor-pos-relay-1`
- Pro: Clear naming, no collision with device UIDs
- Con: What if device UID already starts with "motor-pos"?

**Option B: UUID Generation**
- Generate unique UUID on first Motor Protection enable
- Store in device config
- Pro: Guaranteed uniqueness
- Con: Not human-readable, harder to debug

**Option C: Device UID + Suffix**
- `{deviceUid}-position`
- Example: `relay-1-position`
- Pro: Clear relationship, readable
- Con: Longer UIDs

**Decision Needed:** Which UID format? How to handle collisions?

### 0.4 Position Sensor Visibility

**Question:** Should virtual position sensor appear in sensor lists?  
**Implications:**

**If YES (sensor appears everywhere):**
- User can create rules based on position: "If vent > 80% open, activate shade"
- Position shows in sensor dashboard
- Historical position tracking in graphs
- Requires sensor type `motor_position` in sensor type list

**If NO (hidden, internal only):**
- Cleaner UI - users don't see "fake" sensors
- Position only visible in device card
- Cannot create rules based on position
- Simpler implementation

**Recommendation:** Make visible but clearly labeled as "Virtual - Motor Position"

### 0.5 Multi-Device Position Sharing

**Question:** If 2 devices share same motor, how is position synchronized?

**Scenario:**
- `relay-open` runs motor Open for 1 minute → position goes 0% → 25%
- `relay-close` runs motor Close for 30 seconds → position goes 25% → 12.5%
- Both devices must read/write same position value

**Proposed Flow:**

1. **Configuration Phase:**
   - Admin configures Motor Protection on `relay-open`
   - System creates virtual sensor `motor-pos-001`
   - Admin configures Motor Protection on `relay-close`
   - System asks: "Use existing position sensor?" → Select `motor-pos-001`

2. **Runtime:**
   - When `relay-open` activates → firmware updates `motor-pos-001` value
   - When `relay-close` activates → firmware reads `motor-pos-001`, updates it
   - Position stored in shared memory structure, persisted to SPIFFS

3. **Update Payload:**
   - Only ONE sensor entry for `motor-pos-001`
   - Both devices reference same sensor UID in their `motorProtection` config

**Decision Needed:** Firmware data structure for shared position sensors

### 0.6 Module Type Limitations

**Question:** Do SocketDirector and HydroDirector modules support this?  
**Current Assumption:** Feature for DryContact (relay modules)

**Reality Check:**
- SocketDirector: Has relays, can control motors → YES
- HydroDirector: Has pumps with step control → Maybe position = volume pumped?
- DimmerDirector: Analog control → NO (not step-based)
- SensorDirector: No devices → NO

**Decision Needed:**
- Confirm scope: DryContact + Socket only, or include Hydro?
- If Hydro included: Is position = volume? Or also physical position?

### 0.7 Backward Compatibility

**Question:** What happens to existing Step Rules without Motor Protection?

**Options:**

**Option A: Optional (current design)**
- Motor Protection is opt-in
- Existing rules work as before
- Users configure T_max when ready

**Option B: Required**
- All Step Rules require Motor Protection config
- Existing rules show warning
- Forced migration

**Recommendation:** Keep optional (Option A) to avoid breaking changes

### 0.8 Position Estimation Accuracy

**Question:** What if T_max is wrong or motor slips?

**Known Issues:**
- User might configure wrong T_max
- Motor slippage (mechanical wear)
- Power fluctuations affect motor speed
- Position estimate drifts over time

**Mitigation Strategies:**

1. **User Education:**
   - Documentation: How to measure T_max accurately
   - Recalibration reminders (quarterly?)

2. **Admin Reset:**
   - Easy reset to 0% or 100% from UI
   - Logged for troubleshooting

3. **Future Enhancement:**
   - Limit switch integration (hardware sensing)
   - Auto-calibration mode
   - Position drift detection algorithm

**Decision Needed:** Acceptable accuracy threshold? When to alert user?