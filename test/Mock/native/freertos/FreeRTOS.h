#pragma once

// FreeRTOS core stand-in for the native (host) test environment.
//
// It exists so that production classes written against the scheduler's primitives can be built
// and executed on the host - MqttManager first of all, which names <freertos/FreeRTOS.h>,
// <freertos/queue.h> and <freertos/task.h> and was therefore reachable by no test at all.
//
// Only the types and constants the firmware actually spells are declared, and each of them means
// on the host what it means on the device: pdTRUE/pdFALSE as the scheduler's answers, ticks as
// milliseconds (the device runs configTICK_RATE_HZ = 1000), and a tick type wide enough for the
// waits the code passes. What is NOT modelled is pre-emption: see freertos/task.h.
//
// Reachable only through the include path of env:native (see platformio.ini); on the device the
// real headers of the ESP-IDF come from the framework.

#include <cstddef>
#include <cstdint>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS pdTRUE
#define pdFAIL pdFALSE

// The device builds with a 1 kHz tick, so a tick is a millisecond and the conversion is identity.
#define portTICK_PERIOD_MS ((TickType_t)1)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#define portMAX_DELAY ((TickType_t)0xFFFFFFFFU)
