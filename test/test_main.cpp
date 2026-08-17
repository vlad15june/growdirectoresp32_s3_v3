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
extern "C" int run_check_module_deletion_use_case_tests(void);
extern "C" int run_diagnostic_service_tests(void);
extern "C" int run_hybrid_api_service_tests(void);
extern "C" void run_rule_converter_tests(void);
extern "C" void run_rule_phase2_sync_tests(void);
extern "C" void run_rule_phase3_deletion_tests(void);
extern "C" void run_rule_phase4_hysteresis_tests(void);
extern "C" void run_rule_wind_between_tests(void);
extern "C" void run_rule_condition_between_tests(void);
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
extern "C" int run_scan_wdt_feed_tests(void);
extern "C" void run_rule_workflow_phase1_tests(void);
extern "C" void run_rule_schedule_periods_tests(void);
extern "C" void run_rule_completion_fields_tests(void);
extern "C" void run_ota_update_log_tests(void);
extern "C" int run_wifi_self_reconnect_tests(void);
extern "C" int run_check_firmware_update_on_startup_tests(void);
extern "C" int run_firmware_update_recovery_tests(void);
extern "C" int run_sensor_recovery_tests(void);
extern "C" int run_sensor_plausibility_limits_tests(void);
extern "C" void run_module_to_module_reboot_tests(void);
extern "C" void run_rule_dnf_combine_tests(void);
extern "C" void run_rule_condition_hold_tests(void);
extern "C" void run_rule_fired_condition_tests(void);
extern "C" int run_preventive_restart_use_case_tests(void);
extern "C" void run_rule_operation_time_tests(void);
extern "C" void run_rule_sensor_value_refresh_tests(void);
extern "C" void run_mqtt_inbound_size_guard_tests(void);
extern "C" void run_rule_step_cycle_persistence_tests(void);
extern "C" void run_vpd_channel_tests(void);
extern "C" void run_sensor_service_tests(void);
extern "C" int run_sensor_types_bulk_save_tests(void);

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

    run_check_module_deletion_use_case_tests();

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

    run_rule_wind_between_tests();

    run_rule_condition_between_tests();

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

    // Run tests for Task WDT feed during the ModBus sensor scan (GDV1-3618)
    run_scan_wdt_feed_tests();

    // Run tests for Workflow Phase 1 (workflow-aware rule sync attribution)
    run_rule_workflow_phase1_tests();

    run_rule_schedule_periods_tests();

    run_rule_completion_fields_tests();

    run_ota_update_log_tests();

    // Run tests for WiFi self-reconnect (GDV1-3706)
    run_wifi_self_reconnect_tests();

    run_check_firmware_update_on_startup_tests();

    run_firmware_update_recovery_tests();

    // Run tests for ModBus sensor power-cycle recovery (GDV1-3715)
    run_sensor_recovery_tests();

    // Run tests for the sensor plausibility limits gate (GDV1-3810)
    run_sensor_plausibility_limits_tests();

    // Run tests for module-to-module reboot over UDP (GDV1-3707)
    run_module_to_module_reboot_tests();

    // Run tests for DNF AND/OR condition combining (GDV1-3717)
    run_rule_dnf_combine_tests();

    // Run tests for per-condition hysteresis hold state (GDV1-3793)
    run_rule_condition_hold_tests();

    // Run tests for reporting the condition that actually fired (GDV1-3755)
    run_rule_fired_condition_tests();

    // Run tests for nightly preventive restart
    run_preventive_restart_use_case_tests();

    run_rule_operation_time_tests();

    run_rule_sensor_value_refresh_tests();

    // Run tests for the inbound MQTT size limit (GDV1-3813)
    run_mqtt_inbound_size_guard_tests();

    // Run tests for the STEP cycle counter surviving a reboot (GDV1-3811)
    run_rule_step_cycle_persistence_tests();

    // Run tests for the computed VPD sensor channel (GDV1-3782)
    run_vpd_channel_tests();

    run_sensor_service_tests();

    // Run tests for always storing the sensor type catalog (GDV1-3827)
    run_sensor_types_bulk_save_tests();

    return UNITY_END();
}