#ifndef ARDUINO_ADAPTER_H
#define ARDUINO_ADAPTER_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "constants.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"

#ifdef GD_HEAP_FORENSICS
// Both options come from the sdkconfig overlay of the heap-forensics environment. Without the
// first the allocator stamps no owner on a block and the call below does not exist; without the
// second the kernel cannot list its tasks. The flag on any other environment stops here rather
// than at a missing symbol.
#include "sdkconfig.h"
#if !defined(CONFIG_HEAP_TASK_TRACKING) || !defined(CONFIG_FREERTOS_USE_TRACE_FACILITY)
#error "GD_HEAP_FORENSICS needs CONFIG_HEAP_TASK_TRACKING and CONFIG_FREERTOS_USE_TRACE_FACILITY: build the -heap-forensics environment"
#endif
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_heap_task_info.h"
#endif

class ArduinoAdapter : public IArduinoAdapter {
public:
    // Constants
    uint8_t getPinModeInput() const override {
        return INPUT;
    }
    
    uint8_t getPinModeOutput() const override {
        return OUTPUT;
    }
    
    uint8_t getPinModeInputPullup() const override {
        return INPUT_PULLUP;
    }
    
    uint8_t getDigitalLow() const override {
        return LOW;
    }
    
    uint8_t getDigitalHigh() const override {
        return HIGH;
    }

    // Time functions
    void delay(unsigned long ms) override {
        ::delay(ms);
    }
    
    void delayMicroseconds(unsigned int us) override {
        ::delayMicroseconds(us);
    }
    
    unsigned long millis() override {
        return ::millis();
    }
    
    unsigned long micros() override {
        return ::micros();
    }

    // Digital I/O
    void pinMode(uint8_t pin, uint8_t mode) override {
        ::pinMode(pin, mode);
    }
    
    void digitalWrite(uint8_t pin, uint8_t val) override {
        ::digitalWrite(pin, val);
    }
    
    int digitalRead(uint8_t pin) override {
        return ::digitalRead(pin);
    }

    // Analog I/O
    int analogRead(uint8_t pin) override {
        return ::analogRead(pin);
    }
    
    void analogWrite(uint8_t pin, int val) override {
        ::analogWrite(pin, val);
    }

    // LEDC PWM (ESP32 hardware PWM for dimmer control)
    void ledcSetupChannel(uint8_t channel, double freq, uint8_t resolution) override {
        ::ledcSetup(channel, freq, resolution);
    }

    void ledcAttach(uint8_t pin, uint8_t channel) override {
        ::ledcAttachPin(pin, channel);
    }

    void ledcDetach(uint8_t pin) override {
        ::ledcDetachPin(pin);
    }

    void ledcWriteChannel(uint8_t channel, uint32_t duty) override {
        ::ledcWrite(channel, duty);
    }

    uint32_t ledcReadChannel(uint8_t channel) override {
        return ::ledcRead(channel);
    }
    
    // Serial communication
    void serialBegin(unsigned long baud) override {
        Serial.begin(baud);
    }
    
    size_t serialPrint(const char* str) override {
        return Serial.print(str);
    }
    
    size_t serialPrintln(const char* str) override {
        return Serial.println(str);
    }
    
    int serialAvailable() override {
        return Serial.available();
    }
    
    int serialRead() override {
        return Serial.read();
    }

    // System control
    void restart() override {
        // For ESP32
        ESP.restart();
    }

    void hardReset() override {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        ::delay(100);
        esp_sleep_enable_timer_wakeup((uint64_t)HARD_RESET_SLEEP_MS * 1000ULL);
        esp_deep_sleep_start();
        ESP.restart();
    }

    int getResetReason() override {
        return (int)esp_reset_reason();
    }

    void feedTaskWatchdog() override {
        esp_task_wdt_reset();
    }

    // Memory information
    uint32_t getFreeHeap() override {
        return ESP.getFreeHeap();
    }
    
    uint32_t getMinFreeHeap() override {
        return ESP.getMinFreeHeap();
    }
    
    uint32_t getHeapSize() override {
        return ESP.getHeapSize();
    }
    
    uint32_t getMaxAllocHeap() override {
        return ESP.getMaxAllocHeap();
    }

    uint32_t getPsramSize() override {
        return ESP.getPsramSize();
    }

    uint32_t getFreePsram() override {
        return ESP.getFreePsram();
    }

    uint32_t getMaxAllocPsram() override {
        return ESP.getMaxAllocPsram();
    }

#ifdef GD_HEAP_FORENSICS
    // One platform call and one copy loop each, and nothing to decide: what the figures mean
    // is HeapHolders' business, which the host tests execute. These two run on the device only.
    //
    // heap_caps_get_per_task_info() walks every allocated block of the internal heaps inside
    // the heap's critical section; uxTaskGetSystemState() lists the tasks inside the kernel's,
    // measuring every stack on the way. Neither asks for memory, and the arrays they fill are
    // static so that the calling task's stack does not pay for them either.
    bool readHeapOwners(HeapTaskLedger& ledger) override {
        static heap_task_totals_t totals[HeapTaskLedger::MAX_OWNERS];

        // One partition: the internal heaps, which is what getFreeHeap() speaks of. The other
        // three slots are given a capability no region has, so nothing else is counted anywhere.
        size_t ownerCount = 0;
        heap_task_info_params_t params;
        memset(&params, 0, sizeof(params));
        params.caps[0] = MALLOC_CAP_INTERNAL;
        params.mask[0] = MALLOC_CAP_INTERNAL;
        for (size_t i = 1; i < NUM_HEAP_TASK_CAPS; i++) {
            params.caps[i] = MALLOC_CAP_INVALID;
            params.mask[i] = MALLOC_CAP_INVALID;
        }
        params.totals = totals;
        params.num_totals = &ownerCount;
        params.max_totals = HeapTaskLedger::MAX_OWNERS;
        heap_caps_get_per_task_info(&params);

        ledger.ownerCount = ownerCount;
        for (size_t i = 0; i < ownerCount; i++) {
            ledger.owners[i].task = (uintptr_t)totals[i].task;
            ledger.owners[i].bytes = (uint32_t)totals[i].size[0];
            ledger.owners[i].blocks = (uint32_t)totals[i].count[0];
        }
        return true;
    }

    void readLiveTasks(HeapTaskLedger& ledger) override {
        static TaskStatus_t tasks[HeapTaskLedger::MAX_TASKS];

        // Answers 0 when more tasks are alive than the array holds.
        const UBaseType_t taskCount = uxTaskGetSystemState(tasks, HeapTaskLedger::MAX_TASKS, NULL);

        ledger.tasksListed = taskCount > 0;
        ledger.taskCount = taskCount;
        for (size_t i = 0; i < taskCount; i++) {
            ledger.tasks[i].task = (uintptr_t)tasks[i].xHandle;
            strncpy(ledger.tasks[i].name, tasks[i].pcTaskName, HeapTaskLedger::NAME_BYTES - 1);
            ledger.tasks[i].name[HeapTaskLedger::NAME_BYTES - 1] = '\0';
        }
    }
#endif
};

#endif // ARDUINO_ADAPTER_H
