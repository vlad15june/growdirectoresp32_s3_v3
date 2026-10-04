#pragma once

// mbedtls/platform.h stand-in for the native (host) test environment.
//
// Exists so that host tests can include Infrastructure/Adapters/Http/TlsMemoryPool.h, whose only
// contact with mbedtls is registering its calloc/free hooks. The stand-in captures the two
// function pointers, so a test can drive the pool through exactly the entry points mbedtls would
// use on the device.
//
// Reachable only through the include path of env:native (see platformio.ini); on the device the
// real mbedtls header from the ESP-IDF SDK is used.

#include <cstddef>

// Tells TlsBufferProfile.h that mbedtls is not really here, so it must not try to read the record
// buffer sizes out of mbedtls/ssl_internal.h - those come from the sdkconfig of a device build.
#define GD_TLS_MBEDTLS_HOST_STANDIN 1

using MbedtlsCallocFn = void *(*)(size_t, size_t);
using MbedtlsFreeFn = void (*)(void *);

inline MbedtlsCallocFn mbedtlsInstalledCalloc = nullptr;
inline MbedtlsFreeFn mbedtlsInstalledFree = nullptr;

inline int mbedtls_platform_set_calloc_free(MbedtlsCallocFn callocFn, MbedtlsFreeFn freeFn)
{
    mbedtlsInstalledCalloc = callocFn;
    mbedtlsInstalledFree = freeFn;
    return 0;
}
