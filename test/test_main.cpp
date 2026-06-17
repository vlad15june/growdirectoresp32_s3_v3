#include <unity.h>
#include <stdio.h>

// External test functions
extern "C" void run_mock_settings_tests(void);
extern "C" int run_module_config_tests(void);
extern "C" int run_module_setup_service_tests(void);
extern "C" int run_led_use_cases_tests(void);
extern "C" int run_button_use_cases_tests(void);
extern "C" int run_loop_service_factory_tests(void);
extern "C" int run_loop_services_tests(void);
extern "C" void run_module_structure_tests(void);
extern "C" void run_fill_module_structure_tests(void);
extern "C" int run_on_change_local_ip_use_case_tests(void);
extern "C" void run_on_connectivity_restored_tests(void);
extern "C" int run_on_change_module_mode_use_case_tests(void);
extern "C" int run_diagnostic_service_tests(void);
extern "C" int run_hybrid_api_service_tests(void);
extern "C" void run_rule_converter_tests(void);
extern "C" void run_rule_phase2_sync_tests(void);
extern "C" void run_rule_phase3_deletion_tests(void);
extern "C" void run_rule_phase4_hysteresis_tests(void);
extern "C" void run_rule_phase4_propagation_tests(void);
extern "C" void run_rule_phase4_deactivation_tests(void);
extern "C" void run_rule_phase5_delay_tests(void);
extern "C" void run_rule_phase7_tests(void);
extern "C" void run_rule_phase8_freshness_tests(void);
extern "C" void run_rule_protective_shutdown_tests(void);
extern "C" void run_update_dto_converter_tests(void);
extern "C" int run_sensor_type_repository_tests(void);
extern "C" int run_spiffs_sensor_type_dao_tests(void);
extern "C" int run_sync_sensor_types_use_case_tests(void);
extern "C" int run_wifi_manager_tests(void);
extern "C" int run_module_command_execution_use_case_tests(void);
extern "C" int run_modbus_address_registers_tests(void);
extern "C" int run_ff_wildcard_broadcast_fallback_tests(void);
extern "C" void run_remote_sensor_tests(void);
extern "C" void run_device_command_tests(void);
extern "C" void run_relay_output_tests(void);
extern "C" int run_xf700a_rainfall_window_tests(void);
extern "C" void run_rule_workflow_phase1_tests(void);

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

static void setUp_module_config(void) {
    // Setup for each test
}

static void tearDown_module_config(void) {
    // Clean up after each test
}

// Example of a test that doesn't require hardware
static void test_simple_math(void) {
    setUp_module_config();
    TEST_ASSERT_EQUAL(2 + 2, 4);
    TEST_ASSERT_NOT_EQUAL(2 + 2, 5);
    tearDown_module_config();
}

int main(void) {
    UNITY_BEGIN();
    
    // Run tests defined in this file
    RUN_TEST(test_simple_math);
    
    // Run tests from the MockSettings test file
    run_mock_settings_tests();
    
    // Run tests from the ModuleConfig test file
    run_module_config_tests();
    
    // Run tests from the ModuleSetupService test file
    run_module_setup_service_tests();
    
    // Run tests from the LedUseCases test file
    run_led_use_cases_tests();
    
    // Run tests from the ButtonUseCases test file
    run_button_use_cases_tests();
    
    // Run tests from the LoopServiceFactory test file
    run_loop_service_factory_tests();
    
    // Run tests from the LoopServices test file
    run_loop_services_tests();
    
    // Run tests for ModuleStructure
    run_module_structure_tests();
    
    // Run tests for fillModuleStructure method
    run_fill_module_structure_tests();
    
    // Run tests for OnChangeLocalIpUseCase
    run_on_change_local_ip_use_case_tests();

    // Run tests for OnConnectivityRestoredUseCase
    run_on_connectivity_restored_tests();

    // Run tests for OnChangeModuleModeUseCase
    run_on_change_module_mode_use_case_tests();
    
    // Run tests for DiagnosticService
    run_diagnostic_service_tests();
    
    // Run tests for HybridAPIService
    run_hybrid_api_service_tests();
    
    // Run tests for Rule JSON conversion
    run_rule_converter_tests();

    // Run tests for Phase 2 rule sync semantics (push apply, version, state vocabulary)
    run_rule_phase2_sync_tests();

    run_rule_phase3_deletion_tests();

    run_rule_phase4_hysteresis_tests();

    run_rule_phase4_propagation_tests();

    run_rule_phase4_deactivation_tests();

    run_rule_phase5_delay_tests();

    run_rule_phase7_tests();

    run_rule_phase8_freshness_tests();

    run_rule_protective_shutdown_tests();

    // Run tests for UpdateDto JSON conversion
    run_update_dto_converter_tests();
    
    // Run tests for SensorTypeRepository
    run_sensor_type_repository_tests();
    
    // Run tests for SpiffsSensorTypeDao  
    run_spiffs_sensor_type_dao_tests();
    
    // Run tests for SyncSensorTypesUseCase
    run_sync_sensor_types_use_case_tests();

    // Run tests for WiFiManager
    run_wifi_manager_tests();

    // Run tests for ModuleCommandExecutionUseCase
    run_module_command_execution_use_case_tests();

    // Run tests for configurable Modbus address-management registers
    run_modbus_address_registers_tests();

    // Run tests for FF-wildcard broadcast fallback (WS12S, CWYW)
    run_ff_wildcard_broadcast_fallback_tests();

    // Run tests for remote sensor receiver and live sensor values
    run_remote_sensor_tests();

    // Run tests for device command ack format + switch-back duration
    run_device_command_tests();

    // Run tests for physical relay output drive
    run_relay_output_tests();

    // Run tests for XF700A rainfall accumulation-window configuration
    run_xf700a_rainfall_window_tests();

    // Run tests for Workflow Phase 1 (workflow-aware rule sync attribution)
    run_rule_workflow_phase1_tests();

    return UNITY_END();
}