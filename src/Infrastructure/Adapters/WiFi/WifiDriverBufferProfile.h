#pragma once

// How much memory the WiFi driver may hold at once is fixed at its first initialisation, by the
// wifi_init_config_t it is handed: how many receive and transmit buffers it may keep, and how many
// of them are allocated once and pinned for its lifetime. The precompiled driver copies those
// counts out of that structure and reads them from nowhere else afterwards.
//
// The Arduino core builds the structure from the build's configuration and then overwrites the
// buffer counts with its own - static RX 4, dynamic RX 32, dynamic TX 32, a TX cache of 4 -
// unless the sketch has asked it to keep the configured ones: WiFi.useStaticBuffers(true), which
// is read at that first initialisation and so has to precede every call that can bring the radio
// up.
//
// GD_WIFI_STATIC_BUFFERS is that request, and it is half of a decision. The other half is the
// sdkconfig overlay that shrinks the counts (sdkconfig.defaults.wifi-static). Asking for the
// configured counts without the overlay hands the driver the factory profile instead - eight
// static receive buffers of about 1.6 KB each, pinned for as long as the radio lives, and
// thirty-two dynamic ones - which is the opposite of what the request is for. The two halves are
// checked against each other below, so a build that makes the request without the overlay stops
// here instead of pinning tens of kilobytes of a heap that has about fifty free.

#ifdef GD_WIFI_STATIC_BUFFERS

#include "esp_wifi.h"

// The ceiling is only a ceiling in a build with no external RAM support compiled in. With that
// support the driver pins a cache of transmit buffers - Kconfig will not take it below sixteen,
// about 27 KB - and Kconfig offers no dynamic transmit buffers at all, so it pins a static
// transmit pool as well. Such an image would hold more memory than the receive ceiling saves,
// which is the opposite of the point, so it is not built.
#if defined(CONFIG_ESP32S3_SPIRAM_SUPPORT) || defined(CONFIG_ESP32S2_SPIRAM_SUPPORT) ||            \
    defined(CONFIG_ESP32_SPIRAM_SUPPORT) || defined(CONFIG_SPIRAM) || WIFI_CACHE_TX_BUFFER_NUM > 0
#error "GD_WIFI_STATIC_BUFFERS needs a build without external RAM support: with it the driver pins a transmit cache of at least 16 buffers (~27 KB) and a static transmit pool, neither of which Kconfig can shrink"
#endif

// Receive side. The static buffers are the driver's own receive ring, allocated at initialisation
// and never freed; the dynamic ones are the per-frame copies handed up to the network stack, and
// their count is a hard ceiling - the driver refuses the allocation past it and drops the frame
// rather than growing into the heap.
static_assert(CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM <= 4,
              "GD_WIFI_STATIC_BUFFERS without the wifi-static overlay: each static RX buffer is "
              "about 1.6 KB pinned for the lifetime of the driver");
static_assert(CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM > 0 &&
                  CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM <= 12,
              "GD_WIFI_STATIC_BUFFERS without the wifi-static overlay: the dynamic RX count is "
              "the ceiling of what one burst of received frames can hold at once (0 means the "
              "driver's own 64)");

// The block-ack reorder window has to fit into the buffers that hold it - the two relations
// ESP-IDF itself refuses to build without. Here they are checked against the counts this firmware
// hands the driver, which are the ones the radio runs on.
static_assert(CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM >= WIFI_DEFAULT_RX_BA_WIN,
              "the block-ack receive window does not fit into the dynamic RX buffers");
static_assert(2 * CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM >= WIFI_DEFAULT_RX_BA_WIN,
              "the block-ack receive window does not fit into twice the static RX buffers");

// A received frame is not copied on its way into the network stack: the pbuf points into the
// driver's buffer and releases it when it is freed, so a segment parked in a connection's
// out-of-order queue holds one dynamic RX buffer. Unlimited (0), that queue can hold every buffer
// the driver may have, leaving nothing to receive the retransmission that would empty it. The
// relation is the one ESP-IDF states: the per-connection limit times the number of connections
// plus one must fit into the dynamic buffers; this firmware holds two connections.
static_assert(CONFIG_LWIP_TCP_OOSEQ_MAX_PBUFS > 0 &&
                  CONFIG_LWIP_TCP_OOSEQ_MAX_PBUFS * 3 <= CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM,
              "GD_WIFI_STATIC_BUFFERS without the wifi-static overlay: out-of-order TCP queues may "
              "pin every dynamic RX buffer the driver is allowed to have");

// Transmit side. Which of the two counts is in force depends on the transmit buffer type: static
// buffers are allocated at initialisation and pinned, dynamic ones are allocated per frame up to
// their count, which is their ceiling. The overlay selects the dynamic type - the Kconfig choice
// offers it exactly when the build has no external RAM support, which is the only build this
// profile is for.
static_assert(WIFI_STATIC_TX_BUFFER_NUM == 0,
              "GD_WIFI_STATIC_BUFFERS without the wifi-static overlay: the transmit side is static "
              "and its pool - about 1.6 KB a buffer - is pinned for the lifetime of the driver");
static_assert(WIFI_DYNAMIC_TX_BUFFER_NUM > 0 && WIFI_DYNAMIC_TX_BUFFER_NUM <= 16,
              "GD_WIFI_STATIC_BUFFERS without the wifi-static overlay: the dynamic TX count is "
              "the ceiling of what the transmit path can hold at once");

const bool kWifiBufferCountsComeFromTheBuild = true;

#else

const bool kWifiBufferCountsComeFromTheBuild = false;

#endif // GD_WIFI_STATIC_BUFFERS

/**
 * @brief What has to be said to the radio before it is first brought up, in the order it has to be
 *        said in.
 *
 * The request to keep the configured buffer counts is read once, when the driver is initialised,
 * which is what the first call that puts the radio into a mode does; after that it is too late and
 * the core says so in a log line nothing reads. The sleep type is the same kind of thing from the
 * other side: before the first start the core only remembers it, and re-applies what it remembers
 * at every station start, so a station begun anywhere - by this adapter or past it - comes up with
 * modem sleep off. See WifiAdapter::disableModemSleep().
 *
 * Putting the radio into the off mode while it is already off does not initialise anything; it is
 * how the adapter states the mode it starts from.
 *
 * Templated on the radio and on its mode type so that this order has one definition: the adapter
 * passes the core's WiFi object, a test passes a radio that records what it was told.
 */
template <typename Radio, typename Mode>
inline void prepareRadioBeforeFirstStart(Radio& radio, Mode off, bool bufferCountsFromTheBuild)
{
    if (bufferCountsFromTheBuild)
    {
        radio.useStaticBuffers(true);
    }
    radio.mode(off);
    radio.setSleep(false);
}
