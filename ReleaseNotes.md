# Release Notes

## Version 2.1.25 (2026-02-21)

### Changed
- **Logging Enhancement**: GDV1-3293 Improved logging for sensor type synchronization with detailed error messages, response length checks, and JSON parsing diagnostics
- **Memory Optimization**: Increased JSON document buffer sizes for better handling of large sensor type responses (4KB→8KB for single types, 8KB→32KB for arrays)

### Added
- **Documentation**: Added comprehensive sensor scanning analysis and detailed sensor models documentation

---

## Version 2.1.24 (2026-02-16)

### Fixed
- **Build System**: Fixed compilation error in native test environment (missing `<cstdarg>` include for va_start/va_end macros)

---

## Version 2.1.23 (2026-02-16)

### Fixed
- **Sensor Type Synchronization**: GDV1-3261 Fixed critical bug where sensor types weren't being updated when server changed parameters

---

## Version 2.1.22
New firmware version

