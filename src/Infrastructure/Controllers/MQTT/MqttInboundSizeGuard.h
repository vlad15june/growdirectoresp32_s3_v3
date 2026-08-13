#pragma once

#include <cstddef>
#include <cstdint>

// What has to happen to an inbound MQTT fragment that does not fit the reassembly limit.
enum class MqttInboundSizeVerdict {
    Accept,      // fits the limit, keep assembling
    DropReport,  // over the limit, first fragment of the message - drop it and make it visible
    DropQuiet    // over the limit, already reported on the first fragment of this message
};

// Size decision for inbound MQTT messages, kept free of Arduino/FreeRTOS so it can be
// covered by the native test suite (same split as OtaProgressThrottle next to OtaUpdateLog).
//
// The verdict is taken on `total` - the reassembled size the broker announces up front -
// so a message split into fragments is judged by its sum and not by the size of a single
// fragment. An oversized message is counted and reported once, on its first fragment,
// instead of once per fragment.
struct MqttInboundSizeGuard {
    // The ceiling MqttManager assembles up to. Single definition for the code and the tests.
    static constexpr size_t MAX_INBOUND_MESSAGE_SIZE = 8192;

    uint32_t drops = 0;            // inbound messages dropped for exceeding the limit
    size_t lastDroppedTotal = 0;   // announced size of the last dropped message

    MqttInboundSizeVerdict inspect(size_t total, size_t index, size_t maxSize) {
        if (total <= maxSize) {
            return MqttInboundSizeVerdict::Accept;
        }

        lastDroppedTotal = total;

        if (index != 0) {
            return MqttInboundSizeVerdict::DropQuiet;
        }

        if (drops != UINT32_MAX) {
            drops++;
        }
        return MqttInboundSizeVerdict::DropReport;
    }

    void reset() {
        drops = 0;
        lastDroppedTotal = 0;
    }
};
