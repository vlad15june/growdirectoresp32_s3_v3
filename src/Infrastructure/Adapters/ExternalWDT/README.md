# External WDT

## Hardware
- WDI ← GPIO34 (single line, no INH), RESET → ESP32 EN
- Kick = toggle WDI level every 100ms
- Initial WDI level: HIGH

## Software inhibit
- Not possible: there is no INH line, so the WDT cannot be paused in software.
- During flash erase/write (SPIFFS/NVS/OTA) both cores stall while cache is off; ESP-IDF yields during long erases, so the high-priority kick task keeps WDI alive between chunks. Safety comes from the kick interval margin, not from disable().

## Usage
```cpp
ExternalWDT& wdt = ExternalWDT::getInstance();
wdt.init();
wdt.enable();
wdt.startTask();  // Auto-kick every 100ms

// Monitor
Serial.printf("Kicks: %llu\n", wdt.getKickCount());
```

## Config (ExternalWDT.h)
```cpp
#define EXT_WDT_WDI_PIN GPIO_NUM_34
#define WDT_KICK_INTERVAL_MS 100
```
