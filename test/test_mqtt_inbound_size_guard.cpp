// GDV1-3813: an inbound MQTT message over the size limit used to be dropped by a bare
// `return` in MqttManager::onMqttMessage - no log, no counter, no trace for the server.
// MqttManager itself pulls in Arduino/AsyncMqttClient/FreeRTOS and cannot be built in
// env:native, so the size decision it now takes lives in MqttInboundSizeGuard and is
// covered here: exactly at the limit, one byte over, and a fragmented message whose
// excess only shows up in the reassembled total.

#include <unity.h>
#include <cstddef>

#include "Infrastructure/Controllers/MQTT/MqttInboundSizeGuard.h"

static const size_t LIMIT = MqttInboundSizeGuard::MAX_INBOUND_MESSAGE_SIZE;

static void test_limit_is_the_ceiling_mqtt_manager_assembles_up_to(void) {
    // The number is the contract with the backend; it must not drift silently.
    TEST_ASSERT_EQUAL_UINT32(8192, (uint32_t)MqttInboundSizeGuard::MAX_INBOUND_MESSAGE_SIZE);
}

static void test_message_exactly_at_limit_is_accepted(void) {
    MqttInboundSizeGuard guard;

    TEST_ASSERT_TRUE(MqttInboundSizeVerdict::Accept == guard.inspect(LIMIT, 0, LIMIT));
    TEST_ASSERT_EQUAL_UINT32(0, guard.drops);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)guard.lastDroppedTotal);
}

static void test_message_one_byte_over_limit_is_dropped_and_reported(void) {
    MqttInboundSizeGuard guard;

    TEST_ASSERT_TRUE(MqttInboundSizeVerdict::DropReport == guard.inspect(LIMIT + 1, 0, LIMIT));
    TEST_ASSERT_EQUAL_UINT32(1, guard.drops);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(LIMIT + 1), (uint32_t)guard.lastDroppedTotal);
}

static void test_fragmented_message_over_limit_is_reported_once(void) {
    MqttInboundSizeGuard guard;

    // 12000 bytes delivered in four 3000-byte fragments: every fragment on its own is
    // well under the limit, the excess only exists in the reassembled total.
    const size_t total = 12000;
    const size_t fragment = 3000;

    TEST_ASSERT_TRUE(MqttInboundSizeVerdict::DropReport == guard.inspect(total, 0, LIMIT));
    for (size_t index = fragment; index < total; index += fragment) {
        TEST_ASSERT_TRUE(MqttInboundSizeVerdict::DropQuiet == guard.inspect(total, index, LIMIT));
    }

    // One message lost, not four.
    TEST_ASSERT_EQUAL_UINT32(1, guard.drops);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)total, (uint32_t)guard.lastDroppedTotal);
}

static void test_fragmented_message_within_limit_is_accepted(void) {
    MqttInboundSizeGuard guard;

    const size_t total = 6000;
    const size_t fragment = 2000;

    for (size_t index = 0; index < total; index += fragment) {
        TEST_ASSERT_TRUE(MqttInboundSizeVerdict::Accept == guard.inspect(total, index, LIMIT));
    }

    TEST_ASSERT_EQUAL_UINT32(0, guard.drops);
}

static void test_each_oversized_message_is_counted(void) {
    MqttInboundSizeGuard guard;

    guard.inspect(LIMIT + 1, 0, LIMIT);
    guard.inspect(LIMIT, 0, LIMIT);        // accepted in between, must not count
    guard.inspect(LIMIT * 2, 0, LIMIT);

    TEST_ASSERT_EQUAL_UINT32(2, guard.drops);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(LIMIT * 2), (uint32_t)guard.lastDroppedTotal);

    guard.reset();
    TEST_ASSERT_EQUAL_UINT32(0, guard.drops);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)guard.lastDroppedTotal);
}

extern "C" void run_mqtt_inbound_size_guard_tests(void) {
    RUN_TEST(test_limit_is_the_ceiling_mqtt_manager_assembles_up_to);
    RUN_TEST(test_message_exactly_at_limit_is_accepted);
    RUN_TEST(test_message_one_byte_over_limit_is_dropped_and_reported);
    RUN_TEST(test_fragmented_message_over_limit_is_reported_once);
    RUN_TEST(test_fragmented_message_within_limit_is_accepted);
    RUN_TEST(test_each_oversized_message_is_counted);
}
