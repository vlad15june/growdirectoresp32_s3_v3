**Stepper Rule Motor Protection** 

**Why we need this**

Stepper Rules control motors using repeating pulses (e.g., Temp > 40°C → Open 60s → Hold 10m → re-check → repeat). When a vent/roll-up is already at a physical end-stop but the condition remains crossed, the sensordirector keeps sending Open/Close commands to the drycontact. This can cause motor stalling, gearbox wear, and mechanical damage (windows/roll-ups).

**What we are adding: Motor Protection**

A device-level safety layer that prevents “overtravel” by tracking **time-based estimated position** and blocking commands at limits.

**Core behavior (high level)**

- Per motor/device, store **Full Travel Time (T\_max)** for **Closed ↔ Open**. manually adding by the user (Max open hh:mm:ss) from 0 (complete close-100 (complete open). 
- Track **estimated position (P\_est)** from accumulated runtime:
  - Open runtime increases P\_est
  - Close runtime decreases P\_est
  - Clamp to **0-100%**
- **Block** commands beyond limits:
  - Block **Open** when P\_est at/near **100% (Fully Open)**
  - Block **Close** when P\_est at/near **0% (Fully Closed)**
- **Unblock** only after moving the opposite direction for at least **T\_release** (default **60s**) or via admin reset.

**Example**

- T\_max = 4:00, T\_step = 1:00 → ~4 Open pulses from fully closed to fully open.
- After reaching 100% (estimated), additional Open pulses are **blocked** until the system closes by ≥ T\_release.



**Configuration (App / UX)**

Per-device settings (Motor)

- Full Travel Time (T\_max): mm:ss (manual entry)
- Estimated position: e.g., "~75% open (estimated)"
- Admin tools: Reset estimated position to:
  - Fully Closed (0%)
  - Fully Open (100%)

**Stepper Rule settings**

- Safety checkbox:
  - **[ ] Wind gust safety enabled**
    - Parameter: **Wind gust threshold X** (should be ready to operate with meteorological station ,units per system standard V\min)
    - Action: **Force CLOSE completely (100% CLOSED / fully shut)**

Recommended default for safety:

- If T\_max is not set -> Stepper Rule **cannot run** until manually added. 
- Allow rule to run with warning + mandatory user acknowledgement checkbox.
- manually trigger available regardless to estimated possession (P\_est) with warning if time settled manually crossed the maximum to 0 or 100.


**Functional Requirements** 

1. **Device-level enforcement:** All movement requests for the motor pass Motor Protection checks (works across multiple rules).
1. **Limit blocking:** No output trigger for a blocked direction at end-stop.
1. **Unblock rule:** Opposite direction runtime ≥ T\_release (60s default or as pulse pre-set) or admin reset.
1. **Unblock rule** #2: if manually override and marked the checkbox declaring you are aware of crossing the limit.
1. **Persistence:** Store P\_est + state across reboot; if not possible, fail-safe (mark “unknown” and block until reset).
1. **Logging:** Log calibration updates, allowed moves, blocked moves, and safety override events.

**Scope**

Applies to Stepper Rule-controlled motors:

- Roof vents/windows
- Side roll-ups
- Curtains (open/close)
- Other motors using step pulses

**MVP Deliverables (Phase 1)**

- T\_max setting + Motor Protection tracking and blocking
- Admin reset controls
- Logs + clear UI status (“Blocked at limit”)
- Wind gust safety checkbox + forced close action

**KPI’s- success**

- Fewer “motor keeps trying / stuck” issues
- Reduced mechanical damage reports
- Higher integrator confidence in Stepper Rules

**Future development (optional extra mile)**

**Two-output motor pairing (Open and Close relays)**

In many installations, a single motor has two phases:

- **Open phase** wired to one electrical output (DryContact relay)
- **Close phase** wired to a different electrical output (DryContact relay)

**Goal:** allow GrowDirector to treat these two outputs as **one logical motor** with two commands (Open, Close).

**What this enables (future):**

- Stepper Rule configuration selects a **Motor (paired outputs)** instead of selecting two independent relays at the drycontact director.
- Motor Protection tracks runtime and limits at the **logical motor level**, while triggering the correct relay per direction.
- Reduces wiring mistakes and prevents users from creating conflicting rules on the two separate outputs.
---

## Implementation Questions

1. How to implement "manually adding by the user (Max open hh:mm:ss)" - UI format and validation?
2. How to track P_est from accumulated runtime - calculation frequency and precision?
3. What exact threshold for "at/near 100%" and "at/near 0%" blocking?
4. How to implement "Unblock only after moving opposite direction for at least T_release (60s)" - timer mechanism?
5. What format for "Full Travel Time (T_max): mm:ss (manual entry)" input field?
6. How to display "Estimated position: e.g., ~75% open (estimated)" in UI?
7. What triggers "Reset estimated position to Fully Closed (0%) / Fully Open (100%)" action?
8. How to implement "Wind gust safety enabled" checkbox and threshold parameter input?
9. Which "meteorological station" sensor types and "units per system standard V\min" supported?
10. How does "Force CLOSE completely (100% CLOSED / fully shut)" action work?
11. Implementation of "If T_max is not set -> Stepper Rule cannot run" blocking logic?
12. How to show "warning + mandatory user acknowledgement checkbox" when T_max missing?
13. What triggers "manually trigger available regardless to estimated possession (P_est) with warning"?
14. How to implement "Device-level enforcement: All movement requests pass Motor Protection checks"?
15. What is "No output trigger for a blocked direction at end-stop" mechanism?
16. How to implement "T_release (60s default or as pulse pre-set)" - fixed or configurable?
17. How to implement "manually override and marked the checkbox declaring you are aware of crossing the limit"?
18. How to implement "Store P_est + state across reboot; if not possible, fail-safe (mark unknown and block until reset)"?
19. What to log in "Log calibration updates, allowed moves, blocked moves, and safety override events"?
20. How to implement "Motor (paired outputs)" entity for two-relay motor pairing?
