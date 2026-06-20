#include <unity.h>
#include <string>
#include "Infrastructure/Logging/OtaProgressThrottle.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

static void test_throttle_limits_emissions_over_full_download(void) {
    OtaProgressThrottle throttle;
    throttle.reset();

    int emits = 0;
    unsigned long now = 0;
    for (int pct = 0; pct <= 100; pct++) {
        now += 1000;
        int outPct = 0;
        if (throttle.shouldEmit((size_t)pct, 100, now, 10, 3000, outPct)) {
            emits++;
        }
    }

    TEST_ASSERT_LESS_OR_EQUAL_INT(11, emits);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(8, emits);
}

static void test_throttle_blocks_when_pct_step_not_reached(void) {
    OtaProgressThrottle throttle;
    throttle.reset();

    int outPct = 0;
    TEST_ASSERT_TRUE(throttle.shouldEmit(10, 100, 100000, 10, 3000, outPct));
    TEST_ASSERT_EQUAL_INT(10, outPct);

    TEST_ASSERT_FALSE(throttle.shouldEmit(15, 100, 200000, 10, 3000, outPct));
}

static void test_throttle_blocks_when_ms_step_not_reached(void) {
    OtaProgressThrottle throttle;
    throttle.reset();

    int outPct = 0;
    TEST_ASSERT_TRUE(throttle.shouldEmit(10, 100, 5000, 10, 3000, outPct));
    TEST_ASSERT_FALSE(throttle.shouldEmit(40, 100, 6000, 10, 3000, outPct));
    TEST_ASSERT_TRUE(throttle.shouldEmit(40, 100, 9000, 10, 3000, outPct));
}

static void test_throttle_handles_zero_total(void) {
    OtaProgressThrottle throttle;
    throttle.reset();

    int outPct = 0;
    throttle.shouldEmit(0, 0, 10000, 10, 3000, outPct);
    TEST_ASSERT_EQUAL_INT(0, outPct);
}

static void test_spiffs_append_accumulates_lines_with_newline(void) {
    MockSpiffsAdapter spiffs;

    TEST_ASSERT_TRUE(spiffs.appendToFile("/ota.log", "line one"));
    TEST_ASSERT_TRUE(spiffs.appendToFile("/ota.log", "line two"));

    std::string content = spiffs.readSPIFFSstr("/ota.log");
    TEST_ASSERT_EQUAL_STRING("line one\nline two\n", content.c_str());
}

static void test_spiffs_get_file_size_tracks_content(void) {
    MockSpiffsAdapter spiffs;
    TEST_ASSERT_EQUAL_UINT(0, spiffs.getFileSize("/ota.log"));

    spiffs.appendToFile("/ota.log", "abc");
    TEST_ASSERT_EQUAL_UINT(4, spiffs.getFileSize("/ota.log"));
}

static void test_spiffs_append_fails_when_backend_fails(void) {
    MockSpiffsAdapter spiffs;
    spiffs.setShouldFail(true);
    TEST_ASSERT_FALSE(spiffs.appendToFile("/ota.log", "x"));
}

extern "C" void run_ota_update_log_tests(void) {
    RUN_TEST(test_throttle_limits_emissions_over_full_download);
    RUN_TEST(test_throttle_blocks_when_pct_step_not_reached);
    RUN_TEST(test_throttle_blocks_when_ms_step_not_reached);
    RUN_TEST(test_throttle_handles_zero_total);
    RUN_TEST(test_spiffs_append_accumulates_lines_with_newline);
    RUN_TEST(test_spiffs_get_file_size_tracks_content);
    RUN_TEST(test_spiffs_append_fails_when_backend_fails);
}
