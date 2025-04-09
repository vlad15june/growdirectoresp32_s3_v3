#include <unity.h>

// Include your project headers to test (without hardware-specific code)
// For example:
// #include "utils.h"
// #include "data_processing.h"

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

// Add more tests for your non-hardware dependent code
// void test_your_function(void) {
//     TEST_ASSERT_EQUAL(expected_value, your_function(input));
// }

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_simple_math);
    // Add your other test functions here
    // RUN_TEST(test_your_function);
    
    return UNITY_END();
}