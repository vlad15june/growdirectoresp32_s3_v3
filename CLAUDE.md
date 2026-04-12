# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 firmware for GrowDirector — an IoT agricultural automation device. Written in C++17 using the Arduino framework on PlatformIO.

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

## Commands

**Requires `just` installed** (`brew install just` on macOS):

```bash
just build      # Build firmware for esp32-s3-devkitm-1
just test       # Run all native unit tests (verbose)
just clean      # Clean build artifacts
just monitor    # Serial monitor at 115200 baud
```

**Direct PlatformIO** (VERSION injected manually):
```bash
pio run -e esp32-s3-devkitm-1
pio test -e native -vvv
```

**Run a single test file**: There is no per-file test runner. All tests share a single entry point `test/test_main.cpp`. To run tests for one module, comment out other `run_*_tests()` calls in `test/test_main.cpp`, run `just test`, then revert.

## Architecture

Clean Architecture with strict inward-only dependency flow:

```
Infrastructure  →  Application  →  Core/Domain
(adapters,         (services,       (entities, use cases,
 repos, HTTP/MQTT)  commands, DTOs)  interfaces, enums)
```

**Core/Domain** (`src/Core/Domain/`) — zero external dependencies:
- `Data/Entities/` — Device, Sensor, Rule, Diagnostic entities
- `Data/Models/` — ModuleStructure, DeviceModel, SensorValueModel
- `UseCases/` — business logic organized by feature (Buttons, Led, Devices, Sensors, Rules, SensorTypes, ModuleMode, ModuleCommand)
- `Interfaces/` — abstract interfaces for all external dependencies (IRepository, IAdapter, IApiService, IMqtt, etc.)

**Application** (`src/Application/`) — orchestrates use cases:
- `Services/` — DeviceService, SensorService, RuleService, ModuleSetupService, DiagnosticService, HybridAPIService (routes between REST/MQTT), LoopServiceFactory
- `Commands/` — CommandFactory and command DTOs
- `LoopServices/` — state machine: CanConfigure → Configuring → ConfigureDone (also TestMode, ReConfigure)

**Infrastructure** (`src/Infrastructure/`) — all technical details:
- `Adapters/` — hardware (LED, Button, Arduino GPIO), sensors (SensorReader, ModbusSensorReader), communication (WiFi, MQTT, HTTP), storage (SPIFFS, EEPROM)
- `Controllers/` — MQTT message handlers, REST WebServer, HybridAPIService routing
- `Repositories/` — SPIFFS-backed RuleRepository, SensorRepository, SensorTypeRepository with DAO layer
- `Configuration/` — ModuleConfig (runtime), Settings (persistent EEPROM)

**Entry points**:
- `src/main.cpp` — Arduino `setup()` / `loop()`, FreeRTOS task creation (mqttTask, buttonUpdateTask)
- `src/globals.h` — all global singleton instances

## Testing

Tests use the Unity framework and run on native desktop (no hardware required). Each test file exports a `run_*_tests()` function called from `test/test_main.cpp`.

Mock implementations live in `test/Mock/` — there are mocks for all major interfaces (ISettings, IModuleConfig, IApiService, ISpiffsAdapter, etc.).

## Key Conventions

- New use cases go in `Core/Domain/UseCases/<Feature>/`
- Core never includes Infrastructure headers
- Interfaces defined in `Core/Domain/Interfaces/` — implementations in `Infrastructure/`
- JSON serialization lives in `Infrastructure/Adapters/Converters/`
- Firmware version is read from the `VERSION` file at build time
