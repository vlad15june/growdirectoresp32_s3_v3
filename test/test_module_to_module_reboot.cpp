#include <unity.h>
#include <string>

#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Infrastructure/Controllers/UDP/UdpInboundDispatcher.h"
#include "Infrastructure/Controllers/UDP/RemoteModuleCommandReceiver.h"
#include "Application/Services/CommandService.h"
#include "Application/DTOs/CommandDto.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"

#include "Mock/ArduinoMock.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Udp/MockUdpManager.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Core/Domain/MockRemoteModuleCommandSender.h"

static void test_sender_valid_target(void) {
    MockLogger logger;
    MockDebugSystem debug;
    MockRemoteModuleCommandSender sender;

    ModuleCommandExecutionUseCase uc(&logger, &debug);
    uc.setRemoteModuleCommandSender(&sender);

    RawBusCommandResult result = uc.executeRaw("RebootRemoteModule", "{\"targetUid\":\"B\"}");

    TEST_ASSERT_EQUAL_INT(1, sender.sendRebootCallCount);
    TEST_ASSERT_EQUAL_STRING("B", sender.lastTargetUid.c_str());
    TEST_ASSERT_TRUE(result.status == ModuleCommandStatus::DONE);
}

static void test_sender_empty_target(void) {
    MockLogger logger;
    MockDebugSystem debug;
    MockRemoteModuleCommandSender sender;

    ModuleCommandExecutionUseCase uc(&logger, &debug);
    uc.setRemoteModuleCommandSender(&sender);

    RawBusCommandResult result = uc.executeRaw("RebootRemoteModule", "{}");

    TEST_ASSERT_EQUAL_INT(0, sender.sendRebootCallCount);
    TEST_ASSERT_TRUE(result.status == ModuleCommandStatus::ERROR);
}

static void test_sender_send_failure(void) {
    MockLogger logger;
    MockDebugSystem debug;
    MockRemoteModuleCommandSender sender;
    sender.sendRebootReturnValue = false;

    ModuleCommandExecutionUseCase uc(&logger, &debug);
    uc.setRemoteModuleCommandSender(&sender);

    RawBusCommandResult result = uc.executeRaw("RebootRemoteModule", "{\"targetUid\":\"B\"}");

    TEST_ASSERT_EQUAL_INT(1, sender.sendRebootCallCount);
    TEST_ASSERT_TRUE(result.status == ModuleCommandStatus::ERROR);
}

static void test_sender_missing(void) {
    MockLogger logger;
    MockDebugSystem debug;

    ModuleCommandExecutionUseCase uc(&logger, &debug);

    RawBusCommandResult result = uc.executeRaw("RebootRemoteModule", "{\"targetUid\":\"B\"}");

    TEST_ASSERT_TRUE(result.status == ModuleCommandStatus::ERROR);
}

static void test_command_service_route_inline_ack(void) {
    MockLogger logger;
    MockDebugSystem debug;
    MockRemoteModuleCommandSender sender;
    MockHybridAPIService hybrid;

    ModuleCommandExecutionUseCase uc(&logger, &debug);
    uc.setRemoteModuleCommandSender(&sender);

    CommandService commandService(nullptr, &uc, nullptr, &hybrid, nullptr, &logger, &debug);

    CommandDto command;
    command.type = "ModuleCommandNotification";
    command.command = "RebootRemoteModule";
    command.options = "{\"targetUid\":\"B\"}";

    bool ok = commandService.process(command);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(hybrid.sendCommandResultCalled);
    TEST_ASSERT_EQUAL_STRING("accepted", hybrid.lastCommandResult.status.c_str());
    TEST_ASSERT_EQUAL_INT(1, sender.sendRebootCallCount);
}

static void test_dispatcher_fans_out(void) {
    MockLogger logger;
    MockDebugSystem debug;
    MockUdpManager udpManager;
    UdpInboundDispatcher dispatcher(&udpManager, &logger, &debug);
    dispatcher.init();

    int a = 0;
    int b = 0;
    dispatcher.addHandler([&a](const UdpPacketData&) { a++; });
    dispatcher.addHandler([&b](const UdpPacketData&) { b++; });

    udpManager.controller.emitPacket("{\"type\":\"module_command\"}");

    TEST_ASSERT_EQUAL_INT(1, a);
    TEST_ASSERT_EQUAL_INT(1, b);
}

static void test_receiver_reboots_on_self(void) {
    MockLogger logger;
    MockDebugSystem debug;
    MockUdpManager udpManager;
    MockModuleConfig moduleConfig;
    MockSettings settings;
    JsonConverter jsonConverter;

    moduleConfig.setModuleUid("48:27:E2:00:00:0B");

    UdpInboundDispatcher dispatcher(&udpManager, &logger, &debug);
    dispatcher.init();

    RemoteModuleCommandReceiver receiver(&dispatcher, &moduleConfig, &settings, &jsonConverter, &logger, &debug);
    receiver.init();

    udpManager.controller.emitPacket(
        "{\"type\":\"module_command\",\"command\":\"Reboot\",\"targetUid\":\"48:27:E2:00:00:0B\"}");

    TEST_ASSERT_EQUAL_UINT8(1, settings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(BOOT_TYPE_TECHNICAL, settings.getBootType());
    TEST_ASSERT_TRUE(moduleConfig.getRequestedModuleMode() == ModuleModeEnum::CONFIGURED_NEED_RESTART);
}

static void test_receiver_ignores_foreign_target(void) {
    MockLogger logger;
    MockDebugSystem debug;
    MockUdpManager udpManager;
    MockModuleConfig moduleConfig;
    MockSettings settings;
    JsonConverter jsonConverter;

    moduleConfig.setModuleUid("48:27:E2:00:00:0B");

    UdpInboundDispatcher dispatcher(&udpManager, &logger, &debug);
    dispatcher.init();

    RemoteModuleCommandReceiver receiver(&dispatcher, &moduleConfig, &settings, &jsonConverter, &logger, &debug);
    receiver.init();

    udpManager.controller.emitPacket(
        "{\"type\":\"module_command\",\"command\":\"Reboot\",\"targetUid\":\"AA:BB:CC:DD:EE:FF\"}");

    TEST_ASSERT_EQUAL_UINT8(0, settings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(0, settings.getBootType());
    TEST_ASSERT_TRUE(moduleConfig.getRequestedModuleMode() == ModuleModeEnum::CONFIGURED_DONE);
}

static void test_receiver_drops_non_command_payload(void) {
    MockLogger logger;
    MockDebugSystem debug;
    MockUdpManager udpManager;
    MockModuleConfig moduleConfig;
    MockSettings settings;
    JsonConverter jsonConverter;

    moduleConfig.setModuleUid("48:27:E2:00:00:0B");

    UdpInboundDispatcher dispatcher(&udpManager, &logger, &debug);
    dispatcher.init();

    RemoteModuleCommandReceiver receiver(&dispatcher, &moduleConfig, &settings, &jsonConverter, &logger, &debug);
    receiver.init();

    udpManager.controller.emitPacket(
        "{\"sensors\":[{\"uid\":\"48:27:E2:00:00:0B.00.01\",\"value\":25}]}");

    TEST_ASSERT_EQUAL_UINT8(0, settings.getNoScanSensors());
    TEST_ASSERT_EQUAL_UINT8(0, settings.getBootType());
    TEST_ASSERT_TRUE(moduleConfig.getRequestedModuleMode() == ModuleModeEnum::CONFIGURED_DONE);
}

static void test_converter_round_trip(void) {
    JsonConverter converterImpl;
    IJsonConverter& converter = converterImpl;

    std::string json = converter.remoteModuleCommandToJson("Reboot", "48:27:E2:00:00:0B");

    std::string command;
    std::string targetUid;
    bool ok = converter.remoteModuleCommandFromJson(json, command, targetUid);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("Reboot", command.c_str());
    TEST_ASSERT_EQUAL_STRING("48:27:E2:00:00:0B", targetUid.c_str());
}

static void test_converter_rejects_sensor_payload(void) {
    JsonConverter converterImpl;
    IJsonConverter& converter = converterImpl;

    std::string command;
    std::string targetUid;
    bool ok = converter.remoteModuleCommandFromJson(
        "{\"sensors\":[{\"uid\":\"a\",\"value\":1}]}", command, targetUid);

    TEST_ASSERT_FALSE(ok);
}

extern "C" void run_module_to_module_reboot_tests(void) {
    RUN_TEST(test_sender_valid_target);
    RUN_TEST(test_sender_empty_target);
    RUN_TEST(test_sender_send_failure);
    RUN_TEST(test_sender_missing);
    RUN_TEST(test_command_service_route_inline_ack);
    RUN_TEST(test_dispatcher_fans_out);
    RUN_TEST(test_receiver_reboots_on_self);
    RUN_TEST(test_receiver_ignores_foreign_target);
    RUN_TEST(test_receiver_drops_non_command_payload);
    RUN_TEST(test_converter_round_trip);
    RUN_TEST(test_converter_rejects_sensor_payload);
}
