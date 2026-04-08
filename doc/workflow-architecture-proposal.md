# Workflow Automation V1 — Architectural Proposal

## Core Idea

Workflow = **ordered chain of existing Rules**, unified under a single entity with a shared trigger, priority, and lifecycle. No new execution engine — the hardware already evaluates rules. Workflow adds **sequencing** (Step N depends on Step N-1) and **inter-module coordination** on top of the rule infrastructure.

---

## 1. Data Model Extension

### Backend (PostgreSQL)

New entities alongside existing `Rule`:

| Entity | Key Fields |
|--------|-----------|
| **Workflow** | uid, name, mode (Schedule/Sensor), priority, enabled, state (Idle/Synchronizing/Running/Completed/Error), activeHoursFrom, activeHoursUntil, activeDays[], accountUid, growDirectorUid |
| **WorkflowTrigger** | workflowUid, type (Schedule/Sensor), config JSON (cron / sensor conditions with AND/OR + debounce) |
| **WorkflowStep** | workflowUid, stepNumber, deviceUid, moduleUid, actionType (On/Off/Pulse/Regular/StepperMotor/Loop), parameters JSON, dependsOnStepNumber, status (Pending/Running/Done/Error) |

**Relationship to Rules:** each `WorkflowStep` does NOT create a `Rule` record. Workflow is a separate execution path. Rules and Workflows are mutually exclusive per device — enforced at the `Device` level via a `lockedBy` (ruleUid | workflowUid) check.

### Firmware (SPIFFS)

New file `/data/workflows.json` alongside existing `/data/rules.json`.

Structure mirrors the backend DTO:
- Workflow header: uid, mode, priority, trigger config, active hours
- Steps array: stepNumber, deviceUid, dependsOnStep, actionType, parameters, sensorUids[], fromValues[], untilValues[]

Rules and Workflows are stored and evaluated **separately**. No mixing.

---

## 2. Backend Architecture

### New Domain Layer

```
Workflow/
  WorkflowEntity, WorkflowStep, WorkflowTrigger
  WorkflowApplicationService        — CRUD + validation + conflict check
  WorkflowConflictService           — device lock: Rule ↔ Workflow mutual exclusion
  WorkflowNotificationService       — push MQTT WorkflowCommandNotification
  WorkflowStateProcessingService    — handles state/log messages from HW
```

### API Endpoints

| Method | Endpoint | Purpose |
|--------|----------|---------|
| POST | `/api/v1/Workflows` | Create workflow |
| PUT | `/api/v1/Workflows/{uid}` | Update workflow |
| DELETE | `/api/v1/Workflows/{uid}` | Delete workflow |
| POST | `/api/v1/Workflows/{uid}/enable` | Activate → sends config to HW |
| POST | `/api/v1/Workflows/{uid}/disable` | Deactivate |
| POST | `/api/v1/Workflows/{uid}/terminate` | Force stop running workflow |
| GET | `/api/v1/Workflows` | List with statuses |
| GET | `/api/v1/Workflows/{uid}/history` | Execution logs |

### Hardware API (new endpoints for ESP32)

| Method | Endpoint | Purpose |
|--------|----------|---------|
| GET | `/api/v1/Hardware/workflowlist` | Full workflow configs for module (startup sync) |
| POST | `/api/v1/Hardware/Workflows/state` | Bulk state report from HW |

### MQTT Messages (Backend → HW)

**Topic:** `GrowDirectorMqttTopic/{growDirectorUid}`

```
type: WorkflowCommandNotification
workflowUid: string
action: Activate | Update | Deactivate | Terminate
workflowPayload: [serialized WorkflowHwDto]
```

Each module involved in the Workflow receives the **full step list** — not just its own steps. This is required for inter-module dependency tracking.

### MQTT Messages (HW → Backend)

| Topic | Content |
|-------|---------|
| `hardware/workflow/state/{uid}` | Workflow sync state (Synchronized/Error) |
| `hardware/workflow/execution/{uid}` | Step execution events (StepStarted, StepCompleted, WorkflowCompleted, WorkflowError) |
| `hardware/update/{uid}` | Existing DeviceUpdateDto — extended with `workflowUid`, `workflowStepNumber` |

### Conflict Enforcement (Backend)

On Workflow save:
1. Check every Step device is not locked by a Rule → reject with `BusinessLogicException`
2. Check no other Workflow with **same priority** uses the same device → reject with `ConflictException`
3. On Workflow enable: re-validate all devices, then publish `WorkflowCommandNotification`

On Rule save:
1. Check device is not in an active Workflow → reject with `ConflictException`

---

## 3. Firmware Architecture

### New Components

| Component | Responsibility |
|-----------|---------------|
| **WorkflowRepository** | CRUD `/data/workflows.json` |
| **SyncWorkflowsUseCase** | Startup: GET `/workflowlist`. Live: MQTT `WorkflowCommandNotification` |
| **EvaluateWorkflowTriggerUseCase** | Monitors trigger conditions for Idle workflows (reuses sensor/schedule evaluation logic from existing rules) |
| **ExecuteWorkflowUseCase** | Orchestrates step-by-step execution, manages step dependencies |
| **InterModuleCoordinator** | Publishes/subscribes local MQTT topics for step "started" signals between modules |
| **WorkflowFailSafeUseCase** | 5-min timeout: if local network lost → turn off all workflow-controlled devices |

### Execution Flow on Hardware

```
1. SyncWorkflowsUseCase downloads workflows → persists to SPIFFS
2. EvaluateWorkflowTriggerUseCase (1s loop, same as rule eval):
   - Schedule trigger: check time + active hours
   - Sensor trigger: check sensor conditions + debounce
   - If trigger fires → transition workflow to Running
3. ExecuteWorkflowUseCase (1s loop):
   - For each Step in order:
     a. Check dependsOnStep "started" (local device or MQTT signal from other module)
     b. [Sensor mode] Check step sensor condition (From reached)
     c. Send ModBus command to device
     d. Track step state (Running/Done/Error)
     e. Publish "StepStarted" on local MQTT for next module
   - Handle Pulse/Loop/StepperMotor cycle counters (reuse existing Step rule cycle logic)
   - On completion → transition to Completed, report to backend
   - On error → turn off all workflow devices, report error
```

### Inter-Module Coordination (Local MQTT)

**Topic:** `workflow/{workflowUid}/step/{stepNumber}/started`

Each module subscribes to the step it depends on. When module A completes step N "started" threshold → publishes to this topic. Module B (step N+1) picks it up and evaluates its own activation conditions.

5-minute timeout on waiting for inter-module signal → Fail-Safe.

### Integration with Existing Rule System

- `EvaluateRulesUseCase` and `ExecuteWorkflowUseCase` run in **separate loops** — no interference
- Before activating a device, both check `device.lockedBy` to prevent conflicts
- Workflow steps reuse the same sensor reading pipeline (`SensorRepository`)
- Workflow steps reuse `DeviceDigitalOut` for ModBus commands
- `hardware/update` DTO extended: if device is controlled by workflow, sends `workflowUid` and `workflowStepNumber` instead of `ruleUid`

---

## 4. Frontend Architecture

### New Screens

| Screen | Content |
|--------|---------|
| **Workflow List** | Cards: name, priority, mode, trigger summary, step count, active hours, status badge, enabled toggle |
| **Workflow Create/Edit** | Wizard: mode → priority → trigger → active hours → steps |
| **Step Editor** | Device picker (red/yellow/clear), action type form, sensor rows (sensor mode) |
| **Workflow Dashboard** | Live status: running steps, sensor values vs Until, cycle progress, conflict indicators |
| **Execution History** | Log per run: steps executed, timestamps, results |
| **Notifications Config** | Per-workflow toggles: completion, failure, conflict |

### Device Picker Logic

```
Red (disabled)  = device.lockedBy is a Rule
Yellow (warning) = device.lockedBy is another Workflow
Clear (available) = device is free
```

Priority conflict check: on save, frontend calls backend which validates and returns conflict details.

### Real-time Updates

Frontend subscribes to workflow state via existing WebSocket/polling channel (5s refresh).
Backend pushes workflow execution events received from MQTT into the existing notification pipeline.

---

## 5. Deployment Sequence

| Phase | Scope | Deliverable |
|-------|-------|-------------|
| **Phase 1** | Backend | Workflow CRUD, conflict service, MQTT notification, state processing, new HW endpoints |
| **Phase 2** | Firmware | WorkflowRepository, SyncWorkflows, trigger evaluation, step execution (single module only) |
| **Phase 3** | Frontend | Workflow list, create/edit wizard, device picker with conflict colors |
| **Phase 4** | Firmware | Inter-module coordination via local MQTT, fail-safe |
| **Phase 5** | Frontend | Dashboard, execution history, notifications |
| **Phase 6** | Integration | End-to-end testing, offline scenarios, conflict edge cases |

---

## 6. Key Architectural Decisions

1. **Workflow is NOT a group of Rules** — it's a parallel entity. Rules and Workflows share devices but have separate storage, evaluation, and lifecycle. This avoids rule system complexity explosion.
2. **Full config push** — each module gets the entire Workflow (all steps), not just its own. Required for dependency graph awareness.
3. **Local MQTT for coordination** — modules already use MQTT. Adding workflow-specific topics for step signals is minimal overhead.
4. **Trigger evaluation reuses rule evaluation logic** — sensor comparison and schedule matching are the same algorithms. Extract shared utility, don't duplicate.
5. **DeviceUpdateDto extension** — backend learns about workflow execution through the same `hardware/update` message, with additional workflow fields. No new polling mechanism.
6. **Offline execution preserved** — same dual-channel (server + local) approach. Workflows continue on local network, fail-safe at 5 minutes without any connectivity.
