#include <unity.h>

// External test functions
extern "C" void run_mock_settings_tests(void);

void setUp(void) {
    // Setup for each test
}

void tearDown(void) {
    // Clean up after each test
}

// Example of a test that doesn't require hardware
void test_simple_math(void) {
    TEST_ASSERT_EQUAL(2 + 2, 4);
    TEST_ASSERT_NOT_EQUAL(2 + 2, 5);
}

int main(void) {
    UNITY_BEGIN();
    
    // Run tests defined in this file
    RUN_TEST(test_simple_math);
    
    // Run tests from the MockSettings test file
    run_mock_settings_tests();
    
    return UNITY_END();
}