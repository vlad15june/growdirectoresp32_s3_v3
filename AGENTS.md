# AGENTS.md

Guidance for coding agents working in the GrowDirector ESP32-S3 firmware repository.

## Project Overview

GrowDirector firmware for ESP32-S3, written in C++17 using the Arduino framework via PlatformIO. The codebase mixes embedded runtime concerns (Wi-Fi, MQTT, RS-485, SPIFFS, EEPROM, WebServer, OTA) with a layered application/domain structure.

- Current firmware version: `2.18.7` (read from the `VERSION` file at build/test time; the file stores the value with surrounding quotes, e.g. `"2.18.7"`, and CI strips the quotes when naming artifacts).
- Primary target board: `env:esp32-s3-devkitm-1`.
- Unit-test target: `env:native` (desktop, no hardware required).
- Additional environments: `env:esp32-s3-devkitm-1-win` (same S3 board, `upload_port = COM4`), `env:esp32s2dev` (ESP32-S2 Saola, 4MB flash, PSRAM disabled).

Repository scale (as of the latest snapshot):

- 280 source files under `src/`:
  - `src/Core/` — 141 files (domain logic, entities, use cases, interfaces, enums, utilities).
  - `src/Application/` — 48 files (services, commands, DTOs, loop orchestration).
  - `src/Infrastructure/` — 88 files (hardware, network, storage, framework adapters).
- 119 files under `test/` — 65 Unity test `.cpp` files at the top level plus mocks under `test/Mock/` (Application, Core, Infrastructure, UseCases and `native` subdirectories, plus `ArduinoMock.h`).

## Specs Guardrail

Canonical engineering documentation for GrowDirector does **not** live in this repository. A `specs/` Git submodule is configured (see `.gitmodules`, pointing to `git@bitbucket.org:dimacher/growdirector-specs.git`, branch `main`). The committed resolver `.specs-repo` contains `specs` (the submodule path); a gitignored per-developer override `.specs-repo.local` may contain an absolute path to a local clone of the specs repo.

Before changing architecture, contracts, or cross-component behavior:

1. Read the configured specs repository path from `.specs-repo.local` when present, otherwise from `.specs-repo`.
2. If the path is not configured yet, stop and ask for it, or save it manually into `.specs-repo.local` (this file is gitignored — it is a per-developer override).
3. Resolve that path on disk and verify it points to a valid specs repo (it should contain `docs/components/`, `docs/contracts/`, etc.).
4. If the path does not resolve, stop work exactly as you would when the path is not configured.
5. The specs repo has its own `AGENTS.md` — read and follow it. Its read order:
   - Start with the nearest file in `docs/agent-context/` (e.g. `docs/agent-context/firmware.md`) when it exists.
   - Open the relevant `feature`, `flow`, `contract`, or `component` node and follow `related` links instead of scanning whole sections.
6. Primary firmware docs in the specs repo:
   - `docs/components/firmware/index.md`
   - `docs/contracts/backend-firmware/index.md`
   - `docs/features/workflow/index.md` when the task touches workflow behavior

Resolver rules:

- Use `.specs-repo.local` as the source of truth when it exists; otherwise fall back to the committed default in `.specs-repo`.
- Both files may contain either an absolute path or a path relative to the repository root.
- Treat a non-resolving path exactly like a missing config: stop and ask for the correct path instead of scanning nearby repositories.
- Do not continue planning, implementation, or architecture analysis until the path resolves and the required docs can be opened.

Local documentation status:

- `doc/`, `docs/`, and `tickets/` in this repository are historical and migration-era context.
- If canonical docs exist in the specs repo, prefer them.
- If canonical docs do not exist yet, use local docs as fallback context and promote durable knowledge into the specs repo.

Docs update rule:

- If a change affects backend/firmware contracts, rule semantics, initialization flow, diagnostics, or other cross-component behavior, update the specs repo in the same workstream (the specs repo's own AGENTS.md defines node types, placement, and commit-message rules).
- Pure implementation-local refactors may stay code-only if behavior is unchanged.

Important:

- The expected resolver files are the repository-root `.specs-repo` and optional `.specs-repo.local` override.
- Do not treat `tickets/.specs-repo.local` as the canonical resolver target unless the root file explicitly points you there.

## Build and Test Commands

Preferred entrypoint (requires `just` to be installed locally):

```bash
just build      # Build firmware for esp32-s3-devkitm-1
just test       # Run native unit tests
just clean      # Clean build artifacts
just monitor    # Start serial monitor at 115200 baud
just all        # Alias for build
```

Direct PlatformIO equivalents (firmware version is injected from `VERSION`):

```bash
PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='$(cat VERSION)'" pio run -e esp32-s3-devkitm-1
PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='$(cat VERSION)'" pio test -e native -vvv
pio run --target clean
pio device monitor -e esp32-s3-devkitm-1
```

Notes:

- The `native` environment uses `throwtheswitch/Unity@^2.5.2` and `bblanchon/ArduinoJson@^6.21.4`, builds with `-std=gnu++17 -g -O0`, ignores `test_embedded`, and supports gdb debugging (`debug_init_break = tbreak setup`).
- The `native` environment compiles the host-portable production sources directly into the test binary: `test_build_src = yes` with `build_src_filter` including everything under `src/` **except** `main.cpp` and the whole `Infrastructure/` layer (Arduino, WiFi, ESP drivers). A new Core/Application `.cpp` is picked up automatically without touching `platformio.ini`. Do **not** `#include` production `.cpp` files from tests — that causes `multiple definition` link errors.
- `test/Mock/native/` holds host stand-ins for device-only headers (`Arduino.h`) and is on the include path (`-I test/Mock/native`), so production headers that depend on Arduino types can be compiled on the host. `stdint.h` is force-pre-included into `src/` translation units (`build_src_flags = -include stdint.h`) because entity headers take fixed-width integer types for granted.
- `lib_compat_mode = off` is set in the `native` environment.
- The `esp32-s3-devkitm-1` environment enables PSRAM (`BOARD_HAS_PSRAM`, `CONFIG_SPIRAM_USE_MALLOC`, etc.) and depends on `AsyncMqttClient@^0.9.0`, `ArduinoJson@^6.21.4`, `ESP32Time@^2.0.0`, and the Telaire T6700 CO2 sensor library (`@^1.0.1`). `lib_ignore = ArduinoSTL` is set in all environments.
- `scripts/verbose_test.py` is registered as a pre-test script for the `native` environment.
- `lib_extra_dirs = ~/Documents/Arduino/libraries` is set for the `esp32-s3-devkitm-1` and `esp32s2dev` environments; avoid relying on libraries that are only present there.

## Continuous Integration and Deployment

The project uses Bitbucket Pipelines (see `bitbucket-pipelines.yml`):

- A CI base image is built from `Dockerfile.ci` (Python 3.11 + PlatformIO + gcc/g++) and pushed to a DigitalOcean registry per environment (`develop`, `stage`, `production`).
- The default pipeline runs tests and builds firmware using the latest `develop` CI image.
- Branch pipelines exist for `develop`, `release/*`, and `main`:
  1. Rebuild the CI image if `Dockerfile.ci`, `platformio.ini`, or `scripts/**` changed.
  2. Run `pio test -e native -vvv` and `pio run -e esp32-s3-devkitm-1`.
  3. Produce artifacts: `firmware-<version>-<branch>.bin`, `release-notes-<version>-<branch>.md`, and `build-info.json`.
  4. Upload the firmware via `POST /api/v1/Firmware/upload` (with `X-CI-Token` header, form fields `File`, `ReleaseNotes`, `Version`, `Chip=S3`, `Board=0`) to the environment-specific backend, only when the `VERSION` file changes:
     - `main` → `https://gen3.api.growdirector.com`
     - `develop` → `https://api-test.growdirector.com`
     - `release/*` → `https://api.stage.k8s.growdirector.com`

Do not modify deployment URLs, authentication tokens, or registry names without explicit project approval.

## Architecture Map

Dependency direction must remain inward-only:

```text
Infrastructure -> Application -> Core
```

### Core (`src/Core/`)

Contains domain logic and should stay free of PlatformIO, Arduino, networking, storage, and hardware-specific headers.

Key areas:

- `src/Core/Domain/Data/Entities/` — domain entities (Device, Sensor, Rule, Diagnostic, Module, etc.).
- `src/Core/Domain/Data/Models/` — runtime models (`ModuleStructure`, `DeviceModel`, `SensorValueModel`, etc.).
- `src/Core/Domain/Enums/` — shared enumerations (`ModuleModeEnum`, `DeviceStateEnum`, `RuleType`, `SensorStateEnum`, etc.).
- `src/Core/Domain/Interfaces/` — abstract boundaries implemented elsewhere (repositories, adapters, API services, MQTT, UDP, use-case interfaces).
- `src/Core/Domain/UseCases/` — business logic organized by feature (Buttons, Led, DeviceControl, Sensors, Rules, SensorTypes, ModuleMode, ModuleCommand, ModuleStructure, Diagnostics).
- `src/Core/Utils/` — small shared utilities (`HexCodec`, `Optional`, `WindArc`).

### Application (`src/Application/`)

Orchestrates use cases and runtime flows.

Key areas:

- `src/Application/Commands/` — command objects and factory logic.
- `src/Application/DTOs/` — transport-facing DTOs (`UpdateDto`, `DiagnosticDto`, `DeviceCommandDto`, `ModuleCommandDto`, etc.).
- `src/Application/Interfaces/` — application service contracts (including `LoopServices` and `WebServices` subdirectories).
- `src/Application/Services/` — orchestration services (`ModuleSetupService`, `CommandService`, `DiagnosticService`, `FirmwareUpdateService`, `ModuleDataSyncService`, `ClearModuleDataService`, `TimeService`) organized further into `Devices/`, `Rules/`, `Sensors/` and `LoopServices/` subdirectories.
- `src/Application/Services/LoopServices/` — state-driven runtime flow services (`LoopServiceCanConfigure`, `LoopServiceConfigureDone`, `LoopServiceTestMode`, `LoopServiceReConfigure`, selected by `LoopServiceFactory`).

### Infrastructure (`src/Infrastructure/`)

Owns all hardware, network, storage, and framework integration. This entire layer is excluded from the native test build.

Key areas:

- `src/Infrastructure/Adapters/` — hardware (LED, Button, external WDT, devices), sensors (`SensorReader`, `AnalogSensorReader`, `ModbusSensorReader`), communication (Wi-Fi, HTTP, RS-485), storage (SPIFFS), time (NTP), and JSON conversion.
- `src/Infrastructure/Configuration/` — `ModuleConfig` (runtime module state) and `Settings` (EEPROM-backed persistence).
- `src/Infrastructure/Controllers/` — HTTP (`RestApiService`, `FirmwareRemoteUpdate`), MQTT (`MqttApiService`, `MqttManager`, `MessageProcessor`, topic handlers, `MqttInboundSizeGuard`), UDP, serial, web entry points, and hybrid API routing (`HybridAPIService`).
- `src/Infrastructure/Repositories/` — SPIFFS-backed `RuleRepository`, `SensorRepository`, `SensorTypeRepository` with DAO layer.
- `src/Infrastructure/Logging/` — logger/debug integrations (`MqttLogger`, `DebugSystem`, `OtaUpdateLog`, `BootEventLog`).
- `src/Infrastructure/Scheduling/` and `src/Infrastructure/BoardTesting/` — supporting runtime concerns.

## Runtime Composition

The embedded composition root is split across:

- `src/globals.h` — global singleton wiring and object-graph construction.
- `src/main.cpp` — Arduino `setup()` / `loop()`, FreeRTOS task creation (`buttonUpdateTask`, pinned via `xTaskCreatePinnedToCore`, polling button states every 50 ms), and runtime startup.

Practical implications:

- Changes that affect object lifetime, dependency wiring, or boot order often require edits in `src/globals.h`.
- Changes that affect task scheduling, MQTT loop cadence, button polling, or startup initialization often require edits in `src/main.cpp`.
- Be careful with global initialization order; this repo relies heavily on static/global object construction.
- Default MQTT broker (`IPAddress(116, 203, 235, 144)`, port `1883`), user, and password are currently hardcoded in `src/globals.h` (around lines 114–120).
- Board-specific RS-485 port and sensor-power wiring is also defined in `src/globals.h`.

Module modes (defined in `src/Core/Domain/Enums/ModuleModeEnum.h`):

- `USER_CAN_CONFIGURE` (42) — initial configuration AP + web server.
- `USER_CAN_RE_CONFIGURE` (142) — reconfiguration AP + web server.
- `AP_TEST_MODE` (166) — manufacturing/test mode.
- `CONFIGURED_DONE` (106) — normal operational mode.
- `CONFIGURED_NEED_RESTART` (111) — configured, waiting for restart.

Subsystem hotspots:

- Module runtime state: `src/Application/Services/LoopServices/`.
- Module setup/init flow: `src/Application/Services/ModuleSetupService.*`.
- Hybrid backend communication: `src/Infrastructure/Controllers/Api/HybridAPIService.*`, `src/Infrastructure/Controllers/HTTP/RestApiService.*`, `src/Infrastructure/Controllers/MQTT/MqttApiService.*`.
- Rules persistence/evaluation: repositories under `src/Infrastructure/Repositories/` plus `src/Core/Domain/UseCases/Rules/`.
- Sensor persistence and sensor-type sync: SPIFFS DAOs plus `SensorRepository`, `SensorTypeRepository`, and sensor-related use cases.
- `src/main.cpp` contains a `createTestSensorTypes()` bootstrap helper whose call in `setup()` is commented out; treat it as development/bootstrap code, not the default production flow.

## Storage and State

Persistent and runtime state are split:

- `Settings` in `src/Infrastructure/Configuration/Settings.*` uses EEPROM-backed persistence.
- `ModuleConfig` in `src/Infrastructure/Configuration/ModuleConfig.*` holds runtime module state.
- Rules, sensors, and sensor types persist through SPIFFS DAOs in `src/Infrastructure/Adapters/SPIFFS/`.

When changing state shape or persistence rules:

- Inspect both the repository and DAO layers.
- Inspect JSON conversion code under `src/Infrastructure/Adapters/Json/`.
- Check whether setup, clear/reset, or sync services also depend on the same schema.

## Code Style Guidelines

The codebase does not use an automated formatter configuration (no `.clang-format`, `.clang-tidy`, or `.editorconfig` is present). Follow the existing conventions observed in the project:

- **Language standard**: C++17 (`-std=gnu++17` for native, Arduino/ESP32 toolchain for embedded).
- **Indentation**: 4 spaces (no tabs).
- **Braces**: K&R / Egyptian style with opening braces on the same line.
- **Include guards**: Use `#ifndef FILE_NAME_H` / `#define FILE_NAME_H` / `#endif // FILE_NAME_H`.
- **Class naming**: PascalCase (`EvaluateRulesUseCase`, `ModuleSetupService`).
- **Interface naming**: PascalCase with an `I` prefix (`ILogger`, `ISettings`, `IRuleRepository`).
- **Use-case naming**: `I<Name>UseCase` interface + `<Name>UseCase` implementation (e.g., `IEvaluateRulesUseCase` / `EvaluateRulesUseCase`).
- **Enums**: A mix of `enum class` (e.g., `RuleType`, `RuleDirection`, `ModuleCommandEnum`) and plain `enum` (e.g., `DeviceStateEnum`, `ModuleModeEnum`) is in use. Match the style of the file you are editing.
- **Enum values**: Plain enums use `SCREAMING_SNAKE_CASE` (e.g., `DEVICE_STATE_OFF`); `enum class` values use PascalCase (e.g., `RuleType::STEP`).
- **Member variables**: The codebase is mixed. Some classes prefix private members with `_` (e.g., `EvaluateRulesUseCase::_deviceModel`), while others use unprefixed names (e.g., `ModuleSetupService::logger`). Match the surrounding file.
- **Namespaces**: Very limited. Only `Utils` and `DebugFeatures` namespaces are used; most code is in the global namespace.
- **Header-only implementations**: Many classes implement methods inline in the header file (e.g., `ModuleSetupService.h`). This is acceptable when it matches the surrounding code.
- **Logging**: Use `ILogger` together with `IDebugSystem` feature flags (e.g., `DebugFeatures::RULE_DEBUG`, `DebugFeatures::SETUP_DEBUG`). Most services provide a private `log(const char* format, ...)` helper.
- **Null checks**: Infrastructure-heavy code typically guards every injected dependency before use.
- **Arduino API indirection**: Prefer the adapter interfaces in `Core/Domain/Interfaces/Adapters/` (e.g., `IArduinoAdapter`, `ILedAdapter`, `IButtonAdapter`) rather than calling Arduino APIs directly from Core/Application.

## Testing

Tests live under `test/` and run in the `native` PlatformIO environment using Unity.

Important details:

- `test/test_main.cpp` is the single entry point for the native test binary; it declares ~64 `extern "C" run_*_tests()` functions and calls each of them.
- Each test file exposes a `run_*_tests()` function that is called from `test/test_main.cpp`. When adding a new test file, register its runner there.
- Mocks live under `test/Mock/` and mirror the major interfaces (`ISettings`, `IModuleConfig`, `IApiService`, `ISpiffsAdapter`, etc.), organized into `Application`, `Core`, `Infrastructure`, and `UseCases` subdirectories, plus a shared `ArduinoMock.h` and a `native/` subdirectory with host stand-ins for device-only headers.
- Production Core/Application sources are compiled into the test binary automatically (`test_build_src = yes`); never `#include` a production `.cpp` from a test.
- There is no dedicated per-file runner today. To isolate one test group, temporarily comment unrelated `run_*_tests()` calls in `test/test_main.cpp`, run `just test`, then revert the harness change.
- Some tests write debug logs to files in the repository root (e.g., `button_use_cases_test_debug.log`, `led_use_cases_test_debug.log`, `module_setup_test_debug.log`, `mock_logger.log`). These are ignored by `.gitignore`.

## Security Considerations

- **Hardcoded credentials**: MQTT broker IP (`IPAddress(116, 203, 235, 144)`), username, and password are hardcoded in `src/globals.h`. Do not commit changes that expose real secrets more widely than they already are, and do not treat them as configurable without an explicit task.
- **Hardcoded AP password**: The configuration access-point password is hardcoded as `"123456789"` in `ModuleSetupService` (`setupAP`).
- **CI secrets**: `bitbucket-pipelines.yml` references `DOCKER_LOGIN`, `DOCKER_PASSWORD`, and `CI_AUTH_TOKEN`; these are injected by Bitbucket and must not be embedded in source files.
- **EEPROM/SPIFFS**: Sensitive data (Wi-Fi credentials, tokens) is stored in EEPROM and SPIFFS. Be cautious about adding new persisted fields without considering wear and data retention.
- **OTA**: Firmware updates are fetched over HTTPS and applied via the ESP32 `Update` API. Ensure update links and signatures are validated appropriately when touching this flow.
- **Debug features**: `DEBUG_LOGGING_CODE_ON` is defined at the top of `src/globals.h`. Debug logging can leak internal state; do not leave debug-only behavior enabled in production-facing commits unless required.

## Useful Entry Points

- `src/main.cpp` — Arduino `setup()` / `loop()` and task creation.
- `src/globals.h` — global singleton composition root.
- `src/constants.h` — remaining hardware constants (`MODBUS_ADDRESS_MIN`/`MAX`, `MAX_SENSOR_CHANNELS`, `WDT_TIMEOUT_S`, `HARD_RESET_SLEEP_MS`, etc.; much of the file is commented-out legacy).
- `src/Application/Services/ModuleSetupService.*` — module setup and mode handling.
- `src/Application/Services/LoopServices/LoopServiceFactory.*` — loop-service selection.
- `src/Core/Domain/UseCases/` — domain business logic.
- `src/Infrastructure/Controllers/` — REST, MQTT, UDP, and web controllers.
- `src/Infrastructure/Repositories/` — persistence-backed repositories.
- `test/test_main.cpp` — native test harness.
- `ReleaseNotes.md` — changelog and recent feature/fix context.
- `tickets/` — per-ticket notes (`GDV1-*.md`); historical context, prefer the specs repo for canonical decisions.

## Agent Tooling

- `.vscode/` contains generated PlatformIO IntelliSense/debug configuration. `c_cpp_properties.json` and `launch.json` are auto-generated; do not edit them manually.
- `.claude/skills/` contains project-specific Claude skills (e.g., `log-analysis`, `task-doc`, `task-arch`, `feature-arch`, `imp-doc`, `bug-imp-doc`, `ba-doc-review`, `dev-assistant`, `prepare_new_version`, `sensor-mqtt-debug`). These are intended for Claude Code workflows and are not part of the firmware build.
- `.github/prompts/` contains prompt templates for ticket drafting, feature solution design, ELK search, and version preparation.
- `old/` contains historical reference code (`aptestmode.h`, `server_communication.h`). It is not compiled and should not be imported into the main build.
- `test_scripts/` contains Python helper scripts for bench/log analysis (e.g., ELK log download, jig HTTP test); they are not part of the firmware build.

---

When in doubt, prefer the specs repo docs; fall back to the local docs in `doc/` only after the Specs Guardrail has been resolved. Keep changes minimal, scoped, and consistent with the conventions of the files you touch.
