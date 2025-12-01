# External WDT (BD87A28FVM-TR)

## Hardware
- CLK (pin 3) ← GPIO4, INH (pin 6) ← GPIO5, RESET (pin 8) → ESP32 EN
- Monitor: 10ms, Kick interval: 5ms, Pulse: 2µs
- INH=LOW enables (L-Active type)

## Usage
```cpp
ExternalWDT& wdt = ExternalWDT::getInstance();
wdt.init();
wdt.enable();
wdt.startTask();  // Auto-kick every 5ms

// Temporarily disable
wdt.disable();
longOperation();
wdt.enable();

// Monitor
Serial.printf("Kicks: %llu\n", wdt.getKickCount());
```

## Config (ExternalWDT.h)
```cpp
#define EXT_WDT_CLK_PIN GPIO_NUM_4
#define EXT_WDT_INH_PIN GPIO_NUM_5
#define WDT_KICK_INTERVAL_MS 5
```
