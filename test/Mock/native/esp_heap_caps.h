#pragma once

// esp_heap_caps.h stand-in for the native (host) test environment.
//
// The device header belongs to the ESP-IDF heap component; two of its entry points are needed
// here. The stand-in captures the registered allocation-failure callback, so a test can invoke
// exactly the entry point the heap calls on the device when it turns a request down; and it
// answers how large a block is, which the host allocators can be asked just as the device heap
// can.
//
// Reachable only through the include path of env:native (see platformio.ini); the device build
// uses the real header from the SDK.

#include <cstddef>
#include <cstdint>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;

typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps, const char* function_name);

inline esp_alloc_failed_hook_t espInstalledAllocFailedHook = nullptr;

inline esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback)
{
    espInstalledAllocFailedHook = callback;
    return ESP_OK;
}

/**
 * @brief How large the block behind this pointer is, asked of the allocator that made it.
 *
 * The device answers from the heap's own block header; both host libraries answer the same
 * question about their own heap under a different name. What matters to the caller is only that
 * the number is the same at allocation and at release, which every one of the three guarantees.
 */
inline size_t heap_caps_get_allocated_size(void *ptr)
{
#if defined(__APPLE__)
    return malloc_size(ptr);
#else
    return malloc_usable_size(ptr);
#endif
}
