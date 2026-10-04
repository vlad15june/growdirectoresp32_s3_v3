// MqttLogger and the cost of a log call that goes nowhere near MQTT.
//
// Every topic'd method builds "<defaultTopic>/<topic>[/<moduleUid>]" out of Arduino String
// concatenations. On the device each concatenation is a heap-allocating temporary, and logging
// is the most frequent call in the system - so the topic must be built only on the branch that
// publishes it. These tests pin that property down with the String concat counter from the host
// stand-in, and pin the published topics and payloads byte-for-byte so moving the construction
// cannot change what goes out.

#include <unity.h>

#include "Infrastructure/Logging/MqttLogger.h"

// Serial-side expectations per call: MqttLogger routes print/printf to Serial.print and
// println/log/logJson to Serial.println.
static void reset_counters()
{
    String::concatCount = 0;
    Serial.printCalls = 0;
    Serial.printlnCalls = 0;
    Serial.lastText.clear();
}

// --- Serial-only mode: every topic'd entry point, zero String work ---------------------------

static void test_serial_only_print_builds_no_topic_strings(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::SerialOnly);
    logger.setModuleUid("MODULE-1");
    reset_counters();

    logger.print("net", "hello");

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)String::concatCount);
    TEST_ASSERT_EQUAL_INT(0, (int)client.published.size());
    TEST_ASSERT_EQUAL_INT(1, Serial.printCalls);
    TEST_ASSERT_EQUAL_STRING("hello", Serial.lastText.c_str());
}

static void test_serial_only_println_builds_no_topic_strings(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::SerialOnly);
    logger.setModuleUid("MODULE-1");
    reset_counters();

    logger.println("net", "hello");

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)String::concatCount);
    TEST_ASSERT_EQUAL_INT(0, (int)client.published.size());
    TEST_ASSERT_EQUAL_INT(1, Serial.printlnCalls);
    TEST_ASSERT_EQUAL_STRING("hello", Serial.lastText.c_str());
}

static void test_serial_only_printf_builds_no_topic_strings(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::SerialOnly);
    logger.setModuleUid("MODULE-1");
    reset_counters();

    logger.printf("net", "value=%d", 42);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)String::concatCount);
    TEST_ASSERT_EQUAL_INT(0, (int)client.published.size());
    TEST_ASSERT_EQUAL_INT(1, Serial.printCalls);
    TEST_ASSERT_EQUAL_STRING("value=42", Serial.lastText.c_str());
}

static void test_serial_only_channel_override_builds_no_topic_strings(void)
{
    AsyncMqttClient client;
    // Logger mode says both channels; the per-call override narrows it to Serial - the override
    // is what must drive the decision.
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::MqttAndSerial);
    client.connectedFlag = true;
    logger.setModuleUid("MODULE-1");
    reset_counters();

    logger.print("net", "hello", MqttLoggerMode::SerialOnly);
    logger.println("net", "hello", MqttLoggerMode::SerialOnly);
    logger.printf("net", "value=%d", MqttLoggerMode::SerialOnly, 42);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)String::concatCount);
    TEST_ASSERT_EQUAL_INT(0, (int)client.published.size());
    TEST_ASSERT_EQUAL_INT(2, Serial.printCalls);
    TEST_ASSERT_EQUAL_INT(1, Serial.printlnCalls);
}

static void test_serial_only_custom_mode_builds_no_topic_strings(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::MqttAndSerial);
    client.connectedFlag = true;
    logger.setModuleUid("MODULE-1");
    reset_counters();

    logger.print("net", MqttLoggerMode::SerialOnly, "hello");
    logger.println("net", MqttLoggerMode::SerialOnly, "hello");
    logger.printf("net", MqttLoggerMode::SerialOnly, "value=%d", 42);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)String::concatCount);
    TEST_ASSERT_EQUAL_INT(0, (int)client.published.size());
    TEST_ASSERT_EQUAL_INT(2, Serial.printCalls);
    TEST_ASSERT_EQUAL_INT(1, Serial.printlnCalls);
}

static void test_serial_only_log_and_logjson_build_no_topic_strings(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::SerialOnly);
    logger.setModuleUid("MODULE-1");
    reset_counters();

    logger.log(LogLevel::INFO, "net", "hello");
    logger.logJson("net", "wifi", "connect", "{}");

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)String::concatCount);
    TEST_ASSERT_EQUAL_INT(0, (int)client.published.size());
    TEST_ASSERT_EQUAL_INT(2, Serial.printlnCalls);
}

// --- Disconnected client: MQTT is asked for but cannot happen - still zero String work --------

static void test_disconnected_client_builds_no_topic_strings(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::MqttAndSerial);
    client.connectedFlag = false;
    logger.setModuleUid("MODULE-1");
    reset_counters();

    logger.print("net", "hello");
    logger.printf("net", "value=%d", 42);
    logger.log(LogLevel::INFO, "net", "hello");
    logger.print("net", MqttLoggerMode::MqttAndSerial, "hello");

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)String::concatCount);
    TEST_ASSERT_EQUAL_INT(0, (int)client.published.size());
    // Serial still saw every message: the branch that was skipped is topic building, not output.
    TEST_ASSERT_EQUAL_INT(3, Serial.printCalls);
    TEST_ASSERT_EQUAL_INT(1, Serial.printlnCalls);
}

// --- Publishing enabled: the topics and payloads that go out are byte-for-byte the same -------

static void test_publish_topic_includes_module_uid(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::MqttAndSerial);
    client.connectedFlag = true;
    logger.setModuleUid("MODULE-1");
    reset_counters();

    logger.print("net", "hello");

    TEST_ASSERT_EQUAL_INT(1, (int)client.published.size());
    TEST_ASSERT_EQUAL_STRING("growdirector/logs/net/MODULE-1", client.published[0].topic.c_str());
    TEST_ASSERT_EQUAL_STRING("hello", client.published[0].payload.c_str());
    TEST_ASSERT_EQUAL_INT(1, Serial.printCalls);
}

static void test_publish_topic_without_module_uid(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::MqttOnly);
    client.connectedFlag = true;
    reset_counters();

    logger.println("net", "hello");

    TEST_ASSERT_EQUAL_INT(1, (int)client.published.size());
    TEST_ASSERT_EQUAL_STRING("growdirector/logs/net", client.published[0].topic.c_str());
    TEST_ASSERT_EQUAL_INT(0, Serial.printlnCalls);
}

static void test_publish_default_topic_and_formatted_payload(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::MqttAndSerial);
    client.connectedFlag = true;
    logger.setModuleUid("MODULE-1");
    reset_counters();

    logger.printf("value=%d", 42);
    logger.log(LogLevel::WARNING, "net", "careful");

    TEST_ASSERT_EQUAL_INT(2, (int)client.published.size());
    TEST_ASSERT_EQUAL_STRING("growdirector/logs/MODULE-1", client.published[0].topic.c_str());
    TEST_ASSERT_EQUAL_STRING("value=42", client.published[0].payload.c_str());
    TEST_ASSERT_EQUAL_STRING("growdirector/logs/net/MODULE-1", client.published[1].topic.c_str());
    TEST_ASSERT_EQUAL_STRING("[warning] careful", client.published[1].payload.c_str());
}

static void test_publish_custom_mode_topic_unchanged(void)
{
    AsyncMqttClient client;
    MqttLogger logger(client, "growdirector/logs", MqttLoggerMode::SerialOnly);
    client.connectedFlag = true;
    logger.setModuleUid("MODULE-1");
    reset_counters();

    // The per-call mode overrides the logger's SerialOnly - this branch must still build the
    // full topic.
    logger.printf("net", MqttLoggerMode::MqttOnly, "value=%d", 42);

    TEST_ASSERT_EQUAL_INT(1, (int)client.published.size());
    TEST_ASSERT_EQUAL_STRING("growdirector/logs/net/MODULE-1", client.published[0].topic.c_str());
    TEST_ASSERT_EQUAL_STRING("value=42", client.published[0].payload.c_str());
    TEST_ASSERT_EQUAL_INT(0, Serial.printCalls);
}

extern "C" void run_mqtt_logger_topic_alloc_tests(void)
{
    RUN_TEST(test_serial_only_print_builds_no_topic_strings);
    RUN_TEST(test_serial_only_println_builds_no_topic_strings);
    RUN_TEST(test_serial_only_printf_builds_no_topic_strings);
    RUN_TEST(test_serial_only_channel_override_builds_no_topic_strings);
    RUN_TEST(test_serial_only_custom_mode_builds_no_topic_strings);
    RUN_TEST(test_serial_only_log_and_logjson_build_no_topic_strings);
    RUN_TEST(test_disconnected_client_builds_no_topic_strings);
    RUN_TEST(test_publish_topic_includes_module_uid);
    RUN_TEST(test_publish_topic_without_module_uid);
    RUN_TEST(test_publish_default_topic_and_formatted_payload);
    RUN_TEST(test_publish_custom_mode_topic_unchanged);
}
