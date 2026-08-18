# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 firmware for GrowDirector — an IoT agricultural automation device. Written in C++17 using the Arduino framework on PlatformIO.

## Everyday Tasks (code search, small fix, debug)

For plain code search, reading, a local bugfix, or debugging — orient through the specs docs first, then go to code by address. Do not grep blindly, and do not run the full Specs Guardrail.

1. Resolve the specs root via `.specs-repo.local`, otherwise `.specs-repo` (resolver rules below). If it does not resolve, fall back to the local `doc/`.
2. Identify which **feature** the request touches.
3. From the specs root, read narrowly for that feature:
   - `docs/components/firmware/` — overall layout (`projectArchitecture.md`, `projectStructureDirectoryDescription.md`) plus the files describing specific features (e.g. sensors)
   - `docs/features/<feature>/` — the firmware part, to understand **what** to look at and **how**
4. Load docs **progressively**, as you go deeper into the question — not everything up front.
5. Go into `docs/contracts/` only when the task touches interaction with other features or components.
6. Use the docs to find the layer and the real file, then open code by address.

The full Specs Guardrail (reading the canonical contracts/flows in full) is only for changes to contracts, architecture, or cross-component behavior — not for plain search or a local fix.

## Specs Guardrail

Canonical engineering documentation for GrowDirector does not live in this repository.

This guardrail applies only when changing architecture, contracts, or cross-component behavior. Implementation-local tasks (plain search, a local bugfix, debugging, refactors with unchanged behavior) do not trigger it — see Everyday Tasks above.

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

**PlatformIO** (primary; VERSION injected manually):
```bash
pio run -e esp32-s3-devkitm-1     # Build firmware
pio test -e native -vvv           # Run all native unit tests (verbose)
pio device monitor -b 115200      # Serial monitor
```

**`just`** (optional shortcut, only if installed — `brew install just` on macOS):
```bash
just build      # Build firmware for esp32-s3-devkitm-1
just test       # Run all native unit tests (verbose)
just clean      # Clean build artifacts
just monitor    # Serial monitor at 115200 baud
```

**Run a single test file**: There is no per-file test runner. All tests share a single entry point `test/test_main.cpp`. To run tests for one module, comment out other `run_*_tests()` calls in `test/test_main.cpp`, run the tests, then revert.

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

## Commit Messages

Format: `GDV1-XXXX #tag: subject`

- `GDV1-XXXX` is the Jira key of the task the change belongs to.
- Tag is one of `#fix`, `#feat`, `#add`, `#clean`, `#refactor`, `#test`, `#docs`.
- Subject is lowercase, imperative, no trailing period; it names the real change, not the file touched.
- Version bumps are the only keyless form: `#ver 2.18.7` (made by the `prepare_new_version` skill).
- No `Co-Authored-By` trailers and no generated-with footers.

## Debugging

- Live device output: `pio device monitor -b 115200` (or `just monitor`).
- Field logs come from ELK — pull them with `test_scripts/elk_download_logs_cli.py`.
- To analyze a bug from module logs, use the `log-analysis` skill: it downloads the logs, ties each hypothesis to a `code line ↔ log line` pair, and writes a report under `doc/log-analysis/`.

## Skills

- `log-analysis` — investigate a bug from a module's ELK logs over a time window.
- `task-doc` — BA-level task statement from a template.
- `task-arch` — task architecture doc from a BA statement.
- `feature-arch` — firmware architecture doc for a feature.
- `imp-doc` — step-by-step implementation doc for a junior from a ready arch doc.
