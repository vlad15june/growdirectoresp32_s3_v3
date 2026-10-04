#pragma once

// Host stand-in for the Arduino WiFiUDP header, reachable only through the include path of
// env:native (see platformio.ini). NTPTimeAdapter names it but uses nothing from it - the SNTP
// client owns the socket - so the only thing worth reproducing is what the real header brings
// along: Arduino.h, and with it String and the system clock functions.

#include <Arduino.h>
