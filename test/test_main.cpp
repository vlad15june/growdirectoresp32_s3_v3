#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// External test functions
extern "C" void run_mock_settings_tests(void);
extern "C" int run_module_config_tests(void);
extern "C" int run_module_setup_service_tests(void);
extern "C" int run_led_use_cases_tests(void);
extern "C" int run_led_boot_stage_tests(void);
extern "C" int run_button_use_cases_tests(void);
extern "C" int run_loop_service_factory_tests(void);
extern "C" int run_loop_services_tests(void);
extern "C" void run_module_structure_tests(void);
extern "C" void run_fill_module_structure_tests(void);
extern "C" int run_on_change_local_ip_use_case_tests(void);
extern "C" void run_on_connectivity_restored_tests(void);
extern "C" void run_on_change_module_mode_use_case_tests(void);
extern "C" void run_check_module_deletion_use_case_tests(void);
extern "C" void run_diagnostic_service_tests(void);
extern "C" void run_hybrid_api_service_tests(void);
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
extern "C" void run_sync_sensor_types_use_case_tests(void);
extern "C" int run_wifi_manager_tests(void);
extern "C" int run_module_command_execution_use_case_tests(void);
extern "C" int run_modbus_address_registers_tests(void);
extern "C" int run_modbus_address_allocator_tests(void);
extern "C" int run_ff_wildcard_broadcast_fallback_tests(void);
extern "C" void run_remote_sensor_tests(void);
extern "C" void run_device_command_tests(void);
extern "C" void run_relay_output_tests(void);
extern "C" int run_xf700a_rainfall_window_tests(void);
extern "C" int run_scan_wdt_feed_tests(void);
extern "C" int run_startup_sensor_scan_tests(void);
extern "C" void run_startup_live_sensor_fact_tests(void);
extern "C" void run_sensor_liveness_snapshot_tests(void);
extern "C" int run_s18_boot_without_scan_tests(void);
extern "C" void run_s19_boot_sequence_tests(void);
extern "C" void run_rule_workflow_phase1_tests(void);
extern "C" void run_rule_schedule_periods_tests(void);
extern "C" void run_rule_completion_fields_tests(void);
extern "C" void run_ota_update_log_tests(void);
extern "C" void run_wifi_self_reconnect_tests(void);
extern "C" void run_link_troubleshooting_tests(void);
extern "C" void run_link_status_reporters_tests(void);
extern "C" int run_wifi_link_log_tests(void);
extern "C" int run_wifi_static_buffers_tests(void);
extern "C" int run_firmware_update_recovery_tests(void);
extern "C" int run_sensor_recovery_tests(void);
extern "C" int run_sensor_plausibility_limits_tests(void);
extern "C" void run_module_to_module_reboot_tests(void);
extern "C" void run_rule_dnf_combine_tests(void);
extern "C" void run_rule_condition_hold_tests(void);
extern "C" void run_rule_fired_condition_tests(void);
extern "C" void run_rule_sensor_condition_average_tests(void);
extern "C" void run_preventive_restart_use_case_tests(void);
extern "C" void run_rule_operation_time_tests(void);
extern "C" void run_rule_sensor_value_refresh_tests(void);
extern "C" void run_mqtt_inbound_size_guard_tests(void);
extern "C" void run_mqtt_guaranteed_delivery_tests(void);
extern "C" void run_rule_step_cycle_persistence_tests(void);
extern "C" void run_rule_calendar_or_tests(void);
extern "C" void run_vpd_channel_tests(void);
extern "C" void run_sensor_service_tests(void);
extern "C" void run_telemetry_before_first_reading_tests(void);
extern "C" void run_sensor_values_go_with_the_sensor_tests(void);
extern "C" void run_readings_go_with_the_registers_tests(void);
extern "C" void run_register_fingerprint_tests(void);
extern "C" void run_catalog_changes_reach_the_module_tests(void);
extern "C" void run_sensor_type_content_tests(void);
extern "C" void run_host_harness_tests(void);
extern "C" void run_s1_control_loop_cadence_tests(void);
extern "C" void run_s5_rule_fires_on_time_tests(void);
extern "C" void run_outbox_tests(void);
extern "C" void run_s6_loop_with_pending_send_tests(void);
extern "C" void run_device_update_rule_context_tests(void);
extern "C" void run_rule_sync_through_outbox_tests(void);
extern "C" void run_sensor_update_interval_tests(void);
extern "C" void run_modbus_read_path_tests(void);
extern "C" void run_s7_loop_with_silent_bus_tests(void);
extern "C" void run_modbus_port_power_tests(void);
extern "C" int run_rs485_bus_init_tests(void);
extern "C" int run_modbus_rs485_frame_drop_tests(void);
extern "C" void run_sensor_acquisition_tests(void);
extern "C" void run_ticket_bus_lock_tests(void);
extern "C" void run_s8_loop_with_command_tests(void);
extern "C" void run_s12_complex_commands_tests(void);
extern "C" void run_s13_watchdog_tests(void);
extern "C" void run_rest_api_service_tests(void);
extern "C" void run_json_list_parse_tests(void);
extern "C" void run_spiffs_rule_dao_tests(void);
extern "C" void run_spiffs_dao_construction_tests(void);
extern "C" void run_s9_loop_with_ip_change_tests(void);
extern "C" void run_mqtt_logger_topic_alloc_tests(void);
extern "C" void run_tls_memory_pool_tests(void);
extern "C" void run_s17_recoverable_update_tests(void);
extern "C" void run_confirm_running_firmware_tests(void);
extern "C" void run_counter_report_tests(void);
extern "C" void run_task_watermark_table_tests(void);
extern "C" void run_rule_eval_steady_alloc_tests(void);
extern "C" void run_rule_snapshot_invalidation_tests(void);
extern "C" void run_rule_validator_reuse_tests(void);
extern "C" void run_rule_repository_revision_tests(void);
extern "C" void run_loop_idle_allocation_tests(void);
extern "C" void run_device_state_tracking_tests(void);
extern "C" void run_analog_device_output_tests(void);
extern "C" void run_ota_image_checksum_tests(void);
extern "C" void run_memory_visibility_tests(void);
extern "C" void run_json_pool_small_document_tests(void);
extern "C" void run_json_pool_event_tests(void);
extern "C" void run_tls_handshake_buffer_tests(void);
extern "C" void run_tls_churn_meter_tests(void);
extern "C" void run_host_backtrace_tests(void);
extern "C" void run_ram_census_tests(void);
extern "C" void run_endurance_bench_tests(void);
extern "C" int run_sensor_types_bulk_save_tests(void);
extern "C" void run_factory_reset_storage_tests(void);
extern "C" void run_catalog_write_off_loop_tests(void);
extern "C" void run_scan_command_catalog_write_tests(void);
extern "C" int run_sensor_types_catalog_pruning_tests(void);
extern "C" int run_sensor_types_catalog_parse_tests(void);
extern "C" int run_sensor_types_sync_module_type_gate_tests(void);
extern "C" int run_catalog_fetch_rearm_tests(void);
extern "C" int run_catalog_sync_memory_peak_tests(void);
extern "C" int run_catalog_stream_read_tests(void);
extern "C" int run_catalog_type_size_tests(void);
extern "C" int run_catalog_write_in_flight_tests(void);
extern "C" void run_catalog_reader_copies_tests(void);
extern "C" int run_http_body_sink_tests(void);
extern "C" void run_http_response_retention_tests(void);
extern "C" void run_http_client_adapter_log_tests(void);
extern "C" void run_structure_republish_off_loop_tests(void);
extern "C" void run_sensor_error_overlay_tests(void);
extern "C" void run_rule_state_overlay_tests(void);
extern "C" void run_rule_stream_read_tests(void);
extern "C" void run_sensor_registry_stream_read_tests(void);
extern "C" void run_registry_safe_write_tests(void);
extern "C" void run_registry_unread_at_boot_tests(void);
extern "C" void run_json_number_fault_tests(void);
extern "C" void run_rule_sync_reporting_tests(void);
extern "C" void run_rule_sync_memory_peak_tests(void);
extern "C" void run_rule_snapshot_gaps_tests(void);
extern "C" void run_eeprom_time_write_discipline_tests(void);
extern "C" void run_sntp_sync_pace_tests(void);
extern "C" void run_heap_watermark_tracer_tests(void);
extern "C" void run_network_offboard_tests(void);
extern "C" void run_wifi_roam_tests(void);
extern "C" void run_heap_low_culprit_tests(void);
#ifdef GD_HEAP_FORENSICS
extern "C" void run_heap_forensics_tests(void);
#endif

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

/**
 * @brief Put the host clock on the zone the device runs in
 *
 * Everything that reads a wall-clock field - the calendar pre-filter of the rule repository, the
 * schedule windows, the day-rollover checks - goes through localtime_r(), which answers in the
 * zone of the process. On the module that zone is UTC: the NTP adapter is constructed with
 * utcOffsetSeconds 0 and daylightOffsetSeconds 0, so configTime() leaves the runtime at UTC and
 * localtime_r() equals gmtime_r(). On a developer machine it is whatever the machine is set to,
 * and a test that fixes an epoch and names the weekday it falls on then passes or fails by
 * geography. Pinning the zone here substitutes the environment, not the logic: the host is put
 * where the device already is.
 */
static void pin_host_timezone_to_the_device_zone(void) {
    setenv("TZ", "UTC", 1);
    tzset();
}

int main(void) {
    pin_host_timezone_to_the_device_zone();

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

    // Run tests from the LED boot stage and scan indication test file
    run_led_boot_stage_tests();

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

    run_modbus_address_allocator_tests();

    // Run tests for FF-wildcard broadcast fallback (WS12S, CWYW)
    run_ff_wildcard_broadcast_fallback_tests();

    // Run tests for remote sensor receiver and live sensor values
    run_remote_sensor_tests();

    // Run tests for device command ack format + switch-back duration
    run_device_command_tests();

    // Run tests for physical relay output drive
    run_relay_output_tests();

    // Run tests for the analog output state machine: every analog channel in the list is driven,
    // and a channel with a run time stops when the time is up.
    run_analog_device_output_tests();

    // Run tests for XF700A rainfall accumulation-window configuration
    run_xf700a_rainfall_window_tests();

    // Run tests for Task WDT feed during the ModBus sensor scan (GDV1-3618)
    run_scan_wdt_feed_tests();
    run_startup_sensor_scan_tests();
    run_startup_live_sensor_fact_tests();
    run_sensor_liveness_snapshot_tests();
    run_s18_boot_without_scan_tests();
    run_s19_boot_sequence_tests();

    // Run tests for Workflow Phase 1 (workflow-aware rule sync attribution)
    run_rule_workflow_phase1_tests();

    run_rule_schedule_periods_tests();

    run_rule_completion_fields_tests();

    run_ota_update_log_tests();

    // Run tests for WiFi self-reconnect (GDV1-3706)
    run_wifi_self_reconnect_tests();
    run_link_troubleshooting_tests();
    run_link_status_reporters_tests();

    run_wifi_link_log_tests();

    run_wifi_static_buffers_tests();


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

    run_rule_sensor_condition_average_tests();

    // Run tests for nightly preventive restart
    run_preventive_restart_use_case_tests();

    run_rule_operation_time_tests();

    run_rule_sensor_value_refresh_tests();

    // Run tests for the inbound MQTT size limit (GDV1-3813)
    run_mqtt_inbound_size_guard_tests();

    // What the module asks the broker for, and what survives an outage because of it
    run_mqtt_guaranteed_delivery_tests();

    // Run tests for the STEP cycle counter surviving a reboot (GDV1-3811)
    run_rule_step_cycle_persistence_tests();

    run_rule_calendar_or_tests();

    // Run tests for the computed VPD sensor channel (GDV1-3782)
    run_vpd_channel_tests();

    run_sensor_service_tests();

    // A sensor that has not been read yet is not in the sensor update packet
    run_telemetry_before_first_reading_tests();

    // A sensor's readings leave with it: deleted, replaced or given another model
    run_sensor_values_go_with_the_sensor_tests();

    // A sensor's readings leave with the registers they were read by, when the catalog changes them
    run_readings_go_with_the_registers_tests();
    run_register_fingerprint_tests();

    // A change of the catalog reaches the module whole - registers, structure - and only a change does
    run_catalog_changes_reach_the_module_tests();
    run_sensor_type_content_tests();

    // Host simulation harness: virtual clock, cooperative scheduler, queues, simulated server
    run_host_harness_tests();

    // Scenario S1: the control loop keeps running while the server is slow, because the
    // diagnostic send is handed to a task instead of being made inline.
    run_s1_control_loop_cadence_tests();

    // Scenario S5: a rule due at moment T fires at moment T even with a slow send in flight.
    run_s5_rule_fires_on_time_tests();

    // The contract of the one queue every producer now sends through: collapse by kind, events
    // never dropped in silence, hand-off is not delivery, one retry policy, watched task.
    run_outbox_tests();

    // Scenario S6: the loop stays bounded while the network hangs AND the loop itself has
    // something to send - the combination S1 does not exercise.
    run_s6_loop_with_pending_send_tests();

    // A device update that was not delivered must not erase why a device was switched on.
    run_device_update_rule_context_tests();

    // A rule snapshot is authoritative: a failed fetch must not be applied as an empty one, and a
    // long one must arrive whole.
    run_rule_sync_through_outbox_tests();

    // The sensor update interval must count from the end of the send, like the diagnostic one.
    run_sensor_update_interval_tests();

    // The Modbus read path: wait for the answer instead of sleeping through it, check the CRC and
    // who answered, and do not let a late frame become the next sensor's reading.
    run_modbus_read_path_tests();
    run_s7_loop_with_silent_bus_tests();
    run_modbus_port_power_tests();
    run_rs485_bus_init_tests();
    run_modbus_rs485_frame_drop_tests();
    run_sensor_acquisition_tests();
    run_ticket_bus_lock_tests();

    // Scenario S8: a command arrives while the server hangs and a raw bus command waits for a
    // silent device - the last two paths on which the control loop could block.
    run_s8_loop_with_command_tests();
    run_s12_complex_commands_tests();
    run_s13_watchdog_tests();

    // The HTTP transport itself, which no test executed before: retry counts, httpFailed, and the
    // URLs the backend is given.
    run_rest_api_service_tests();

    // "Parsed, and empty" is a third outcome, told apart from an unreadable body.
    run_json_list_parse_tests();

    // The storage caller of the same converter, which must not notice the change.
    run_spiffs_rule_dao_tests();

    // What the stored-data DAOs ask of the file system, and when they ask it.
    run_spiffs_dao_construction_tests();

    // Scenario S9: the local IP changes while the server answers nothing - the last place where
    // the control loop went to the network and waited, and the largest.
    run_s9_loop_with_ip_change_tests();

    // Two hot-path costs: a log call must not build MQTT topic strings it will not publish, and
    // the TLS pool's per-handshake calloc/free must not print.
    run_mqtt_logger_topic_alloc_tests();
    run_tls_memory_pool_tests();

    // Scenario S17: the OTA download as a bounded operation - the production download session
    // against a slow stream, a frozen stream, a dead connection and a refusing flash.
    run_s17_recoverable_update_tests();

    // A new image confirms itself by reaching the working state, exactly once; anything short
    // of that leaves the bootloader's rollback armed.
    run_confirm_running_firmware_tests();

    // Orphan counters become one periodic greppable log line, with honest n/a for what the
    // host cannot measure.
    run_counter_report_tests();

    // The stack columns of that line are read by task name, and a task that ends and is created
    // again - the catalog's storage task - is never read through the handle it deleted.
    run_task_watermark_table_tests();

    // A steady second of rule evaluation - nothing changed since the previous tick - must not
    // touch the heap at all; the snapshot that makes it possible must never serve a dead list;
    // and the shared validators that replaced the per-call factory must stay stateless.
    run_rule_eval_steady_alloc_tests();
    run_rule_snapshot_invalidation_tests();
    run_rule_validator_reuse_tests();

    // The revision that guards the rule snapshot: every repository mutator moves it, no read
    // does.
    run_rule_repository_revision_tests();

    // A settled idle iteration of the control loop allocates nothing, and every change path
    // invalidates the caches that zero is bought with.
    run_loop_idle_allocation_tests();

    // The record switchedAt is stamped from survives the device list changing shape: reordering,
    // an insertion in the middle, a device leaving the list and coming back.
    run_device_state_tracking_tests();

    // The update command may carry the digest the image must have: an image that does not match
    // it is refused before it can be booted, and a command without one installs as before.
    run_ota_image_checksum_tests();

    // A refused allocation is counted where it happens, and the largest free block is reported
    // next to it: free bytes alone cannot tell a fragmented heap from a healthy one.
    run_memory_visibility_tests();

    // Every document capacity the converters ask for comes out of the JSON pool, including the
    // ones below 4 KB that would otherwise be plain malloc between the pool's own large blocks.
    run_json_pool_small_document_tests();

    // And the two requests that miss the pool - a tier that is all taken, a capacity no tier
    // covers - reported as one line each, because a heap block the size of a document is what the
    // pool exists to keep out of the heap.
    run_json_pool_event_tests();

    // The exchanges a TLS session makes, run against every record-buffer profile: each profile
    // has to answer both record buffers from its slots and leave the rest of mbedtls alone.
    run_tls_handshake_buffer_tests();

    // And what one handshake costs: the accounting itself, the allocator that feeds it, and the
    // client that opens a window around every connect and reports what the window measured.
    run_tls_churn_meter_tests();

    // Run tests for always storing the sensor type catalog (GDV1-3827)
    run_sensor_types_bulk_save_tests();

    run_sensor_types_catalog_pruning_tests();

    run_sensor_types_catalog_parse_tests();

    run_sensor_types_sync_module_type_gate_tests();

    // And the catalog fetch that failed, re-armed on the control loop when the link comes back.
    run_catalog_fetch_rearm_tests();

    // What that synchronization has to have free at once, measured at two catalog sizes: the cost
    // above what the module holds may not be a function of how many models the account owns.
    run_catalog_sync_memory_peak_tests();

    // And the answer read off the socket in pieces rather than out of one buffer: the same types
    // and the same verdicts, wherever the piece boundaries fall.
    run_catalog_stream_read_tests();

    // And one sensor model of that catalog: what it may weigh, and what one the module cannot read
    // does to the models around it.
    run_catalog_type_size_tests();

    // What the control loop reads while that catalog is being written, and what the module is left
    // holding when the write is refused.
    run_catalog_write_in_flight_tests();

    // And what reading that catalog costs the readers of boot and scanning: a reader pass may not
    // carry a copy of the account's catalog.
    run_catalog_reader_copies_tests();

    // And the device's own body reader, driven by a stand-in for the framework that hands a body
    // over the way the framework does - in blocks, with one retry after a short write.
    run_http_body_sink_tests();

    // And the other half of the same adapter: the answer read whole is kept in a member, so a
    // body big enough to matter to the heap says so when it is retained.
    run_http_response_retention_tests();

    // And the line that adapter writes by itself, on a connection it could not open: it goes to
    // the port with no debug feature in the way, and the URL it is given carries the token.
    run_http_client_adapter_log_tests();

    // A factory reset has to leave the module without a catalog, including the copies an
    // interrupted write leaves behind.
    run_factory_reset_storage_tests();

    // And storing that catalog is a stream of file operations, so it happens on its own task -
    // measured against a flash device that charges for every one of them.
    run_catalog_write_off_loop_tests();

    // The catalog a sensor scan has to fetch takes the same road: the storage task, one writer of
    // the file at a time, and the scan once it has landed.
    run_scan_command_catalog_write_tests();

    // And the last outgoing HTTP call the loop could still make - the structure republish after
    // the catalog moved a computed channel - measured against a server that takes two seconds.
    run_structure_republish_off_loop_tests();

    // The sensor's error flag is runtime health, not configuration: it is held in RAM, seen by
    // every reader through the same interfaces, and costs the flash nothing when a sensor comes
    // and goes.
    run_sensor_error_overlay_tests();

    // And the same question about a rule: how a rule is doing is runtime state, held in RAM,
    // seen by every reader through the same interfaces, and costing the flash nothing when the
    // sensor the rule watches comes and goes.
    run_rule_state_overlay_tests();

    // The rule list is read as it arrives, one rule at a time: what it costs to read an answer is
    // one rule rather than the length of the answer, and a rule the module cannot read is left out
    // of the list instead of taking the list with it.
    run_rule_stream_read_tests();

    // The same for the sensor registry on flash: it is read and written one sensor at a time, a
    // record the module cannot read costs that record only, and the registry has a capacity of its
    // own that a longer file is read up to rather than losing everything to.
    run_sensor_registry_stream_read_tests();

    // And written so that a write the flash stops part of the way, or a read it refuses before a
    // write, costs none of what the registry and the rule file held.
    run_registry_safe_write_tests();

    // And read again a sweep after a read that did not come back, rather than left empty until the
    // next write; nothing decides on the empty answer meanwhile.
    run_registry_unread_at_boot_tests();

    // And what a number in either of those files, or in a rule body from the backend, is worth
    // when it is not a number: the field's default and a count, never an exception on the path
    // that reads the file at boot.
    run_json_number_fault_tests();

    // What the module tells the server about its rules: the report is built by reading the
    // registry at the moment of sending, so a rule in error and a rule the registry had no room
    // for are described as they are rather than as the snapshot that arrived.
    run_rule_sync_reporting_tests();

    // And what one synchronization has to have free at once: reading the answer costs one rule,
    // whatever the account's rule set is.
    run_rule_sync_memory_peak_tests();

    // And what the module does with an answer that does not carry every rule of it: a rule it
    // could not read, could not take or would not keep is a gap in the answer, never a statement
    // that the server deleted the rule.
    run_rule_snapshot_gaps_tests();

    // The time kept in EEPROM is a seed for the next boot, not a clock: it is refreshed on a slow
    // schedule, when the clock actually steps, and on the way into a planned restart - counted in
    // commits of the settings sector, over the production storage path.
    run_eeprom_time_write_discipline_tests();

    // How often the module asks a time server: fast while it is catching up after the network
    // came back, and back to the normal interval as soon as a server has answered.
    run_sntp_sync_pace_tests();

    // When the heap floor broke, said on the control-loop step it broke on: the watermark the
    // diagnostic snapshot carries says how deep the module went and never when.
    run_heap_watermark_tracer_tests();

    // The network on one task with the link's jobs in the same queue: the radio restart off the
    // control loop, the MQTT reconnect beaten between requests, and no request spent on a link
    // that is down - the storm of a returning link, in the shape before and the shape after.
    run_network_offboard_tests();

    // Which access point of a mesh the station stands on: the rules that keep a module from moving,
    // the rationed looking that feeds them, and the move itself as a job of the same network task.
    run_wifi_roam_tests();

    // Who was at work when the heap floor broke: the line about a new low names what each
    // background task was doing at the reading that found it, and the fetches that bring a body
    // back say what they weighed.
    run_heap_low_culprit_tests();

#ifdef GD_HEAP_FORENSICS
    // Bench builds only (env:native-heap-forensics): which task holds the heap at a low, and the
    // dips a watermark that only ever falls cannot show. The code is behind the same flag.
    run_heap_forensics_tests();
#endif

    // Traces taken from a simulated task: every task runs on a fiber, and a trace that cannot
    // walk a fiber stack is either a crash or an empty report - both silent until something
    // needs the trace.
    run_host_backtrace_tests();

    // Attribution of heap allocations to the subsystem that asked for them: a synthetic owner
    // with an appetite the test decides, so the census table the bench prints rests on something
    // whose right answer is known.
    run_ram_census_tests();

    // Last, and the longest: two simulated weeks of the whole module against the stand-ins,
    // asserting all the way through that nothing restarts, the loop always comes back on time
    // and nothing accumulates. Minutes of wall time; everything above it runs first so that a
    // failure of a mechanism is reported before the endurance verdict about it.
    run_endurance_bench_tests();

    return UNITY_END();
}