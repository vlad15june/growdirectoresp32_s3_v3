#ifndef I_ARDUINO_ADAPTER_H
#define I_ARDUINO_ADAPTER_H

#include <cstddef> // For size_t

#ifdef GD_HEAP_FORENSICS
#include "Core/Domain/Diagnostics/HeapTaskLedger.h"
#endif

class IArduinoAdapter {
public:
    virtual ~IArduinoAdapter() = default;

    // Constants
    virtual uint8_t getPinModeInput() const = 0;
    virtual uint8_t getPinModeOutput() const = 0;
    virtual uint8_t getPinModeInputPullup() const = 0;
    virtual uint8_t getDigitalLow() const = 0;
    virtual uint8_t getDigitalHigh() const = 0;

    // Time functions
    virtual void delay(unsigned long ms) = 0;
    virtual void delayMicroseconds(unsigned int us) = 0;
    virtual unsigned long millis() = 0;
    virtual unsigned long micros() = 0;

    // Digital I/O
    virtual void pinMode(uint8_t pin, uint8_t mode) = 0;
    virtual void digitalWrite(uint8_t pin, uint8_t val) = 0;
    virtual int digitalRead(uint8_t pin) = 0;

    // Analog I/O
    virtual int analogRead(uint8_t pin) = 0;
    virtual void analogWrite(uint8_t pin, int val) = 0;

    // LEDC PWM (ESP32 hardware PWM for dimmer control)
    virtual void ledcSetupChannel(uint8_t channel, double freq, uint8_t resolution) = 0;
    virtual void ledcAttach(uint8_t pin, uint8_t channel) = 0;
    virtual void ledcDetach(uint8_t pin) = 0;
    virtual void ledcWriteChannel(uint8_t channel, uint32_t duty) = 0;
    virtual uint32_t ledcReadChannel(uint8_t channel) = 0;
    
    // Serial communication
    virtual void serialBegin(unsigned long baud) = 0;
    virtual size_t serialPrint(const char* str) = 0;
    virtual size_t serialPrintln(const char* str) = 0;
    virtual int serialAvailable() = 0;
    virtual int serialRead() = 0;
    
    // System control
    virtual void restart() = 0;
    virtual void hardReset() = 0;
    virtual int getResetReason() = 0;
    virtual void feedTaskWatchdog() = 0;

    // Memory information
    virtual uint32_t getFreeHeap() = 0;
    virtual uint32_t getMinFreeHeap() = 0;
    virtual uint32_t getHeapSize() = 0;
    virtual uint32_t getMaxAllocHeap() = 0;
    virtual uint32_t getPsramSize() = 0;
    virtual uint32_t getFreePsram() = 0;
    // Largest block a single allocation can still get out of PSRAM: free bytes say how much is
    // left, this says whether any of it is usable in one piece.
    virtual uint32_t getMaxAllocPsram() = 0;

#ifdef GD_HEAP_FORENSICS
    // Heap-forensics builds only. Who holds the heap, task by task: one walk over every allocated block
    // with the heap locked, so it is for the rare moment worth that and never for a steady path.
    // Fills the owners of the caller's ledger and asks for no memory. A platform that keeps no
    // such account answers false - which is what every implementation does unless it says
    // otherwise.
    virtual bool readHeapOwners(HeapTaskLedger& ledger) {
        (void)ledger;
        return false;
    }

    // The other half of the ledger: the tasks alive now, by handle and name - what turns an
    // owner into a word. A call of its own because it locks something else, the kernel, and its
    // price is worth knowing apart from the walk's. A platform that cannot list them says so.
    virtual void readLiveTasks(HeapTaskLedger& ledger) {
        ledger.taskCount = 0;
        ledger.tasksListed = false;
    }
#endif
};

#endif // I_ARDUINO_ADAPTER_H
