# AGENTS.md

Guidance for coding agents working in this repository.

## Project Overview

GrowDirector firmware for ESP32-S3, written in C++17 on Arduino via PlatformIO. The codebase mixes embedded runtime concerns (Wi-Fi, MQTT, RS485, SPIFFS, EEPROM, WebServer, OTA) with a layered application/domain structure.

Primary target:
- `env:esp32-s3-devkitm-1`

Other configured environments:
- `env:esp32-s3-devkitm-1-win`
- `env:esp32s2dev`
- `env:native` for desktop unit tests

## Specs Guardrail

Canonical engineering documentation for GrowDirector does not live in this repository.

Before changing architecture, contracts, or cross-component behavior:
1. Read the configured specs repository path from `.specs-repo.local` when present, otherwise from `.specs-repo`
2. If the path is not configured yet, stop and ask for it or save it manually into `.specs-repo.local`
3. Resolve that path on disk and verify it points to a valid specs repo
4. If the path does not resolve, stop work exactly as you would when the path is not configured
5. Read the primary firmware docs from that repo:
   - `docs/components/firmware/index.md`
   - `docs/contracts/backend-firmware/index.md`
   - `docs/features/workflow/index.md` when the task touches workflow behavior

Resolver rules:
- Use `.specs-repo.local` as the source of truth when it exists; otherwise fall back to the committed default in `.specs-repo`
- Both files may contain either an absolute path or a path relative to the repository root
- Treat a non-resolving path exactly like a missing config: stop and ask for the correct path instead of scanning nearby repositories
- Do not continue planning, implementation, or architecture analysis until the path resolves and the required docs can be opened

Local documentation status:
- `doc/`, `docs/`, and `tickets/` in this repository are historical and migration-era context
- If canonical docs exist in the specs repo, prefer them
- If canonical docs do not exist yet, use local docs as fallback context and promote durable knowledge into the specs repo

Docs update rule:
- If a change affects backend/firmware contracts, rule semantics, initialization flow, diagnostics, or other cross-component behavior, update the specs repo in the same workstream
- Pure implementation-local refactors may stay code-only if behavior is unchanged

Important:
- The expected resolver files are the repository-root `.specs-repo` and optional `.specs-repo.local` override
- Do not treat `tickets/.specs-repo.local` as the canonical resolver target unless the root file explicitly points you there

## Commands

Preferred entrypoint:

```bash
just build
just test
just clean
just monitor
```

Direct PlatformIO equivalents:

```bash
PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='$(cat VERSION)'" pio run -e esp32-s3-devkitm-1
PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='$(cat VERSION)'" pio test -e native -vvv
pio run --target clean
pio device monitor -e esp32-s3-devkitm-1
```

Notes:
- `just` is expected to be installed locally
- Firmware version is injected from the `VERSION` file at build/test time
- Native tests use Unity and run on desktop without hardware

## Architecture Map

Dependency direction should remain inward-only:

```text
Infrastructure -> Application -> Core
```

### Core

`src/Core/` contains domain logic and should stay free of infrastructure details.

Key areas:
- `src/Core/Domain/Data/` for entities and data models
- `src/Core/Domain/Enums/` for shared enums
- `src/Core/Domain/Interfaces/` for boundaries implemented elsewhere
- `src/Core/Domain/UseCases/` for business logic by feature
- `src/Core/Utils/` for small shared utilities

### Application

`src/Application/` orchestrates use cases and runtime flows.

Key areas:
- `src/Application/Commands/` command objects/factory logic
- `src/Application/DTOs/` transport-facing DTOs
- `src/Application/Interfaces/` application service contracts
- `src/Application/Services/` orchestration services
- `src/Application/Services/LoopServices/` state-driven runtime flow services

### Infrastructure

`src/Infrastructure/` owns all hardware, network, storage, and framework integration.

Key areas:
- `Adapters/` for hardware, sensors, SPIFFS, Wi-Fi, JSON, RS485, time
- `Configuration/` for `ModuleConfig` and persistent `Settings`
- `Controllers/` for HTTP, MQTT, UDP, serial, and web entry points
- `Repositories/` for persistence-backed domain repositories
- `Logging/` for logger/debug integrations
- `Scheduling/` and `BoardTesting/` for supporting runtime concerns

## Runtime Composition

The embedded composition root is split across:
- `src/globals.h` for global singleton wiring and object graph construction
- `src/main.cpp` for Arduino `setup()` / `loop()`, background task creation, and runtime startup

Practical implications:
- Changes that affect object lifetime, dependency wiring, or boot order often require edits in `src/globals.h`
- Changes that affect task scheduling, MQTT loop cadence, button polling, or startup initialization often require edits in `src/main.cpp`
- Be careful with global initialization order; this repo relies heavily on static/global object construction
- Default MQTT broker/user configuration is currently hardcoded in `src/globals.h`
- Board-specific RS485 port and sensor-power wiring is also defined in `src/globals.h`

Subsystem hotspots:
- Module runtime state: `src/Application/Services/LoopServices/`
- Module setup/init flow: `src/Application/Services/ModuleSetupService.*`
- Hybrid backend communication: `src/Infrastructure/Controllers/Api/HybridAPIService.*`, `src/Infrastructure/Controllers/HTTP/RestApiService.*`, `src/Infrastructure/Controllers/MQTT/MqttApiService.*`
- Rules persistence/evaluation: repositories under `src/Infrastructure/Repositories/` plus `src/Core/Domain/UseCases/Rules/`
- Sensor persistence and sensor-type sync: SPIFFS DAOs plus `SensorRepository`, `SensorTypeRepository`, and sensor-related use cases
- `src/main.cpp` contains a disabled `createTestSensorTypes()` bootstrap helper; treat it as development/bootstrap code, not the default production flow

## Storage And State

Persistent and runtime state are split:
- `Settings` in `src/Infrastructure/Configuration/Settings.*` uses EEPROM-backed persistence
- `ModuleConfig` in `src/Infrastructure/Configuration/ModuleConfig.*` holds runtime module state
- Rules, sensors, and sensor types persist through SPIFFS DAOs in `src/Infrastructure/Adapters/SPIFFS/`

When changing state shape or persistence rules:
- inspect both the repository and DAO layers
- inspect JSON conversion code under `src/Infrastructure/Adapters/Json/`
- check whether setup, clear/reset, or sync services also depend on the same schema

## Testing

Tests live under `test/` and run in the `native` PlatformIO environment using Unity.

Important details:
- `test/test_main.cpp` is the single entry point for the native test binary
- Each test file exposes a `run_*_tests()` function that is called from `test/test_main.cpp`
- Mocks live under `test/Mock/`
- There is no dedicated per-file runner today; to isolate one test group, temporarily comment unrelated `run_*_tests()` calls in `test/test_main.cpp`, run `just test`, then revert the harness change

## Conventions For Changes

- Keep Core free of PlatformIO, Arduino, networking, storage, and hardware-specific headers
- Add new abstractions in `src/Core/Domain/Interfaces/` when they represent true boundaries
- Put implementations in `src/Infrastructure/`
- Keep orchestration in Application services, not in controllers/adapters
- Prefer updating both sides of a boundary when behavior crosses REST, MQTT, persistence, and domain flow
- If a change touches workflow or module mode transitions, inspect both the loop services and the relevant use cases
- If a change touches backend communication, inspect REST, MQTT, and hybrid routing together
- If a change touches sensor behavior, inspect sensor readers, repositories, DTO/JSON conversion, and sync/use-case layers together

## Useful Entry Points

- `src/main.cpp`
- `src/globals.h`
- `src/Application/Services/ModuleSetupService.*`
- `src/Application/Services/LoopServices/LoopServiceFactory.*`
- `src/Core/Domain/UseCases/`
- `src/Infrastructure/Controllers/`
- `src/Infrastructure/Repositories/`
- `test/test_main.cpp`
