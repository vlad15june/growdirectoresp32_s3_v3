# Workflow Sync — Configuration Download & State Reporting

> Part of: [Workflow System](./README.md)

---

## Overview

Before hardware can execute a workflow it must download the current graph from the backend and acknowledge that it received it. This download+ack cycle is called **sync**. The backend tracks sync state per workflow in both PostgreSQL (`WorkflowStateHistory`) and Redis (live `WorkflowSyncRuntimeSnapshot`).

Hardware is responsible for:
1. Downloading the workflow graph when commanded.
2. Reporting back that it successfully loaded (or failed to load) the graph.
3. Re-downloading when the backend signals a version change.
4. Re-downloading on restart if a workflow was previously active.

---

## Sequence: Activation

```mermaid
sequenceDiagram
    actor User
    participant App as Frontend App
    participant BE as Backend API
    participant DB as PostgreSQL
    participant Cache as Redis
    participant MQTT as MQTT Broker
    participant HW as Hardware (ESP32)

    User->>App: Activate workflow
    App->>BE: POST /Modules/{moduleUid}/Workflows/{uid}/commands/Activate
    BE->>DB: Validate: no other active workflow for this module
    BE->>DB: Validate: no active Rules using same devices
    BE->>DB: workflow.IsEnabled = true, increment WorkflowVersion if graph changed
    BE->>MQTT: publish WorkflowCommandNotification (commandType=Activate, workflowVersion=N)
    BE->>DB: WorkflowStateHistory (SyncState=Synchronizing, Source=User, reason=workflow_activated)
    BE->>Cache: RuntimeSnapshot (SyncState=Synchronizing, Mode=Active, version=N)
    BE-->>App: 200 OK

    HW->>MQTT: receives WorkflowCommandNotification (Activate, version=N)
    HW->>BE: GET /api/v1/hardware/workflow?token=X&grow_director_uid=Y
    BE->>DB: load active workflow for this GrowDirector
    BE-->>HW: WorkflowSyncDto (graph + version + deviceList + triggerType)

    Note over HW: Hardware stores graph locally,<br/>verifies version matches command

    HW->>BE: POST /api/v1/hardware/workflow/{uid}/state<br/>{ SyncState=Synchronized, Mode=Active, WorkflowVersion=N }
    BE->>DB: WorkflowStateHistory (SyncState=Synchronized, Source=Hardware, reason=sync_ok)
    BE->>Cache: RuntimeSnapshot updated
    BE-->>HW: 200 OK

    Note over HW: Hardware begins monitoring trigger condition
```

---

## Workflow Config Download Payload

`GET /api/v1/hardware/workflow` — returns `WorkflowSyncDto`:

```json
{
  "uid": "string",
  "workflowVersion": 3,
  "name": "string",
  "moduleUid": "string",
  "priority": 1,
  "nodeCount": 5,
  "maxExecutionSteps": 100,
  "triggerType": "Sensor | Schedule | Manual",
  "deviceList": ["deviceUid1", "deviceUid2"],
  "graph": {
    "nodes": [ /* WorkflowNode[] — see 02-execution.md */ ],
    "connections": [ /* WorkflowConnection[] — see 02-execution.md */ ]
  }
}
```

| Field | Notes |
|-------|-------|
| `workflowVersion` | Hardware must store this and include it in all state and execution reports |
| `triggerType` | Pre-resolved from the Trigger node config. Informational — hardware should use the actual node config for evaluation logic |
| `deviceList` | UIDs of all devices this workflow may control. Used for conflict checks (backend-side only) |
| `maxExecutionSteps` | Currently always `100`. Hard limit on total execution steps per run |
| `nodeCount` | Number of nodes in the graph |
| `graph` | Full graph — see [02-execution.md](./02-execution.md) for the complete node/connection model |

**When no active workflow exists**, the endpoint returns HTTP 200 with a `null` body (or empty response). Hardware should treat this as "no workflow to execute."

---

## State Report Payload

`POST /api/v1/hardware/workflow/{uid}/state` — body: `WorkflowStateSingleDto`:

```json
{
  "uid": "string",
  "workflowVersion": 3,
  "syncState": "Synchronizing | Synchronized | Error",
  "mode": "Active | Inactive",
  "modeReason": "optional string",
  "errorType": "optional string",
  "timestamp": "2026-03-26T10:00:00Z",
  "accountUid": "string"
}
```

| Field | Notes |
|-------|-------|
| `uid` | Must match the `{uid}` route parameter; backend rejects mismatches |
| `workflowVersion` | Must match the currently active version. If not, backend returns `409 Conflict` — see Version Mismatch below |
| `syncState` | `Synchronizing` → hardware received command but is still loading; `Synchronized` → graph loaded and active; `Error` → failed to load |
| `mode` | `Active` if the workflow is running on hardware; `Inactive` if deactivated |
| `modeReason` | Optional free-text reason. Backend stores as-is in state history if non-empty |
| `errorType` | Short error code if `syncState=Error` (e.g., `"parse_error"`, `"memory_error"`) |
| `timestamp` | UTC timestamp of the state change. If omitted/zero, backend uses server time |
| `accountUid` | Account that owns this module. Available from `GET /hardware/account` |

### Backend responses

| HTTP | Meaning |
|------|---------|
| `200 OK` | State accepted and recorded |
| `200 OK "None"` | Duplicate — cache deduplication already processed this exact update |
| `409 Conflict { "message": "Version mismatch", "currentVersion": N }` | Hardware sent a stale `workflowVersion` — hardware must re-download |

---

## Sync State Machine

```mermaid
stateDiagram-v2
    [*] --> Synchronizing : User activates workflow<br/>(backend publishes MQTT)

    Synchronizing --> Synchronized : Hardware reports<br/>SyncState=Synchronized, Mode=Active
    Synchronizing --> Error : Hardware reports<br/>SyncState=Error

    Synchronized --> Synchronizing : User updates workflow<br/>(version bump → new Activate command)
    Synchronized --> Error : Hardware reports<br/>SyncState=Error, or<br/>version mismatch on any report

    Error --> Synchronizing : User retries activation<br/>or workflow updated
```

**State history reasons** (recorded in `WorkflowStateHistory.Reason`):

| Reason | Triggered by |
|--------|-------------|
| `workflow_activated` | User activates |
| `workflow_updated` | User updates an active workflow (version bump) |
| `workflow_deactivated` | User deactivates |
| `activation_ack` | Hardware reports `Synchronizing + Active` |
| `deactivation_ack` | Hardware reports `Synchronizing + Inactive` |
| `sync_ok` | Hardware reports `Synchronized` |
| `sync_failed` | Hardware reports `Error` |
| `stale_version` | Hardware sent a stale `workflowVersion` |

---

## Version Mismatch

When the user updates a workflow (name, priority, or graph), the backend increments `WorkflowVersion` and sends a new `Activate` command. If hardware sends a state or execution report with an older version:

1. Backend records `WorkflowStateHistory` with `Reason = stale_version`, `IsVersionMismatch = true`.
2. Backend returns `409 Conflict { "currentVersion": N }`.
3. **Hardware must discard its stale graph and re-download** via `GET /hardware/workflow`.

Hardware should detect this immediately and not continue executing the stale graph.

```mermaid
sequenceDiagram
    participant HW as Hardware
    participant BE as Backend API

    Note over HW: Has graph version 2
    HW->>BE: POST /hardware/workflow/{uid}/state<br/>WorkflowVersion=2
    BE-->>HW: 409 Conflict { "currentVersion": 3 }
    HW->>BE: GET /hardware/workflow
    BE-->>HW: WorkflowSyncDto (version=3)
    HW->>BE: POST /hardware/workflow/{uid}/state<br/>WorkflowVersion=3, SyncState=Synchronized
    BE-->>HW: 200 OK
```

---

## Deactivation Sequence

```mermaid
sequenceDiagram
    actor User
    participant BE as Backend API
    participant MQTT as MQTT Broker
    participant HW as Hardware

    User->>BE: POST /Workflows/{uid}/commands/Deactivate
    BE->>BE: workflow.IsEnabled = false
    BE->>MQTT: publish WorkflowCommandNotification (commandType=Deactivate)
    BE-->>User: 200 OK

    HW->>MQTT: receives Deactivate command
    Note over HW: Stop monitoring trigger,<br/>abort any active run (report Aborted),<br/>release device control

    HW->>BE: POST /hardware/workflow/{uid}/state<br/>{ SyncState=Synchronized, Mode=Inactive }
    BE-->>HW: 200 OK
```

After deactivation, `GET /hardware/workflow` returns `null`. Hardware must not attempt execution.

---

## Behavior on Hardware Restart

On every boot, hardware must check whether a workflow was previously active:

```mermaid
flowchart TD
    Boot["Hardware boots"] --> GetAccount["GET /hardware/account"]
    GetAccount --> GetWF["GET /hardware/workflow"]
    GetWF --> HasWF{Response has workflow?}
    HasWF -- null --> NoAction["No workflow — only monitor for<br/>Activate command via MQTT"]
    HasWF -- WorkflowSyncDto --> StoreGraph["Store graph locally"]
    StoreGraph --> ReportSync["POST /hardware/workflow/{uid}/state<br/>SyncState=Synchronized, Mode=Active"]
    ReportSync --> StartMonitor["Begin monitoring trigger condition"]
```

Hardware should NOT resume an in-progress run from before the restart. Any previously-running execution is considered lost. If the previous run was never terminated, hardware should report its final state as `Aborted` before reporting the new `Synchronized` state — or simply not report it (backend will treat the run as lost; it was never persisted to DB since it never reached a terminal state while hardware was up).

---

## Key Backend Files

| File | Role |
|------|------|
| `GrowDirectorWebApi/Controllers/HardwareController.cs` | `GET /workflow`, `POST /workflow/{uid}/state`, `POST /workflow/{uid}/execution` endpoints |
| `GrowDirector.Application/Services/WorkflowService/WorkflowHardwareSyncApplicationService.cs` | All hardware sync + execution logic |
| `GrowDirector.Application/Dto/Hardware/WorkflowStateDto.cs` | `WorkflowStateSingleDto` payload |
| `GrowDirector.Application/Dto/Workflow/WorkflowSyncDto.cs` | Download response DTO |
| `GrowDirector.Application/Services/MqttServices/MqttMessageHandler.cs` | MQTT alt-path for state and execution |
| `GrowDirector.Domain/Models/Workflows/WorkflowRuntimeEnums.cs` | `WorkflowSyncState`, `WorkflowMode`, `WorkflowExecutionState` |
