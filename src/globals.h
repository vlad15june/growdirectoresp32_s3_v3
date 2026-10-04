#ifndef GLOBALS_H
#define GLOBALS_H

#define DEBUG_LOGGING_CODE_ON

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFi.h>
#include <AsyncMqttClient.h>
#include <WebServer.h>
#include <WiFiMulti.h>
#include <mutex>

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IButtonAdapter.h"

#include "Infrastructure/Controllers/UDP/UdpController.h"
#include "Infrastructure/Controllers/UDP/UdpSender.h"
#include "Infrastructure/Controllers/UDP/UpdManager.h"
#include "Infrastructure/Controllers/UDP/UdpInboundDispatcher.h"
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"
#include "Infrastructure/Controllers/UDP/RemoteModuleCommandSender.h"
#include "Infrastructure/Controllers/UDP/RemoteModuleCommandReceiver.h"

#include "Core/Domain/UseCases/Led/LedUseCases.h"
#include "Core/Domain/UseCases/Buttons/ButtonUseCases.h"
#include "Core/Domain/UseCases/ModuleMode/OnChangeModuleModeUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/OnChangeLocalIpUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/OnConnectivityRestoredUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/CheckModuleDeletionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/FirmwareUpdateRecoveryUseCase.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCaseFactory.h"
#include "Core/Domain/UseCases/DeviceControl/DeviceCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"
#include "Core/Domain/UseCases/Sensors/SensorCommandUseCase.h"
#include "Core/Domain/UseCases/Sensors/SensorPlausibilityUseCase.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Core/Domain/UseCases/Rules/SyncRulesUseCase.h"
#include "Core/Domain/UseCases/Diagnostics/PreventiveRestartUseCase.h"
#include "Core/Domain/UseCases/Network/WifiLinkLogUseCase.h"
#include "Core/Domain/UseCases/Network/WifiRoamUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"

#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Data/Models/DeviceModel.h"
#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"

#include "Infrastructure/Adapters/eepromAdapter.h"
#include "Infrastructure/Logging/MqttLogger.h"
#include "Infrastructure/Logging/OtaUpdateLog.h"
#include "Infrastructure/Logging/BootEventLog.h"
#include "Infrastructure/Configuration/ModuleConfig.h"
#include "Infrastructure/Configuration/PendingFirmwareUpdateStore.h"
#include "Infrastructure/Configuration/Settings.h"
#include "Infrastructure/Configuration/StepCycleStore.h"
#include "Infrastructure/Logging/DebugSystem.h"
#include "Infrastructure/Adapters/Time/NTPTimeAdapter.h"
#include "Infrastructure/Adapters/Led/LedAdapter.h"
#include "Infrastructure/Adapters/ArduinoAdapter.h"
#include "Infrastructure/Adapters/Button/ButtonAdapter.h"
#include "Infrastructure/Adapters/WiFi/WifiAdapter.h"
#include "Infrastructure/Adapters/WiFi/WiFiManager.h"
#include "Infrastructure/Controllers/WebServices/ModuleConfigurationWebServer.h"
#include "Infrastructure/Controllers/WebServices/Launchers/WebServerLauncher.h"
#include "Infrastructure/Controllers/MQTT/MqttManager.h"
#include "Infrastructure/Controllers/MQTT/MessageProcessor.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlerFactory.h"
#include "Application/Services/ModuleSetupService.h"
#include "Application/Interfaces/LoopServices/ILoopServiceFactory.h"
#include "Application/Services/LoopServices/LoopServiceFactory.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"
#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Application/Services/DiagnosticService.h"
#include "Application/Services/Net/LinkSupervisor.h"
#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Infrastructure/Adapters/WiFi/LwipLinkProbe.h"
#include "Application/Services/CounterReport.h"
#include "Application/Services/MemoryCounters.h"
#include "Application/Services/JsonCounters.h"
#include "Application/Services/ScanCounters.h"
#include "Core/Domain/Diagnostics/LoopStepMonitor.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Boot/BootSequencer.h"
#include "Application/Services/Storage/SensorTypeCatalogWriter.h"
#include "Infrastructure/Adapters/Http/TlsMemoryPool.h"
#include "Infrastructure/Adapters/Memory/HeapFailureHook.h"
#include "Infrastructure/Scheduling/FreeRtosTaskScheduler.h"
#include "Infrastructure/Scheduling/FreeRtosMessageQueue.h"
#include "Infrastructure/Controllers/MQTT/MqttApiService.h"
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsAdapter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Repositories/RuleRepository.h"
#include "Infrastructure/Repositories/SensorRepository.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"
#include "Application/Services/Devices/IDeviceService.h"
#include "Application/Services/Devices/DeviceService.h"
#include "Application/Services/CommandService.h"
#include "Application/Services/Sensors/ISensorService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Infrastructure/Scheduling/TicketBusLock.h"
#include "Infrastructure/Scheduling/FreeRtosBusLockPlatform.h"
#include "Application/Services/Rules/RuleService.h"
#include "Application/Services/Rules/RuleStateReportingService.h"
#include "Application/Services/ClearModuleDataService.h"

#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Adapters/Sensors/RemoteSensors/RemoteSensorClient.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/AnalogSensors/AnalogSensorReader.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Rs485/ModbusRS485.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/Adapters/Rs485/Rs485Counters.h"
#include "Infrastructure/BoardTesting/BoardTest.h"
#include "Infrastructure/Controllers/HTTP/FirmwareRemoteUpdate.h"
#include "Infrastructure/Adapters/Http/HttpClientAdapter.h"
#include "Infrastructure/Adapters/Ota/UpdateFirmwareImageWriter.h"
#include "Infrastructure/Adapters/Ota/EspOtaPartitionControl.h"
#include "Core/Domain/UseCases/FirmwareUpdate/ConfirmRunningFirmwareUseCase.h"
#include "Infrastructure/Controllers/WebServices/TestModeWebServer.h"

// MQTT configuration constants
const char *mqttUser = "GrowDirectorMqtt";
const char *mqttPassword = "lasdfh329483457sfdjhs92_w";
const char *mqttClientIdPrefix = "GrowDirectorWifiClient";

// MQTT settings
IPAddress mqttBroker(116, 203, 235, 144);
const int mqttPort = 1883;
const char *mqttDefaultTopic = "hardware/log";

IPAddress myIP;

WebServer webServer(80);
WiFiMulti wifiMulti;

AsyncMqttClient mqttClient;

std::recursive_mutex mqttClientMutex;

MqttLogger mqttLogger(mqttClient, mqttDefaultTopic, MqttLoggerMode::SerialOnly, &mqttClientMutex);

DebugSystem debugSystem;

ArduinoJsonConverter jsonConverter;

EepromAdapter eeprom(1024, 0);
Settings settings(eeprom, &mqttLogger, &debugSystem);

// STEP rule cycle counters (GDV1-3811) live in the RTC memory of the built-in clock, not in the
// EEPROM: the counter is written once per completed cycle, and the EEPROM has a finite write
// endurance (owner, review of PR #20, 2026-08-13). RTC_NOINIT_ATTR means the bytes are not zeroed
// at boot, so the table survives every restart the module makes by itself - the nightly preventive
// one, the technical one after an offline period, an OTA.
// A power cut does clear it: the on-chip RTC memory is not battery backed. TO FINISH LATER: the
// board carries an EXTERNAL clock, RV-3028, whose memory is battery backed and survives a power
// cut too - move this table there as soon as the chip has a driver. Today there is none: only the
// jig stubs `#init_rtc3028` / `#read_rtc3028` in BoardTest.h. The real `Rtc3028Adapter`
// (clock source + battery-backed storage) is planned work, see the canon:
// docs/features/workflow/jira-tickets/firmware/phase-3-1-rtc-and-basic-runtime-snapshot.md.
// StepCycleStore does not care where the table is, so the move is a one-line change here.
RTC_NOINIT_ATTR StepCycleTable rtcStepCycleTable;
StepCycleStore stepCycleStore(rtcStepCycleTable, &mqttLogger, &debugSystem);

LedAdapter ledAdapter(&mqttLogger, &debugSystem);

ArduinoAdapter arduinoAdapter;

// One word per background task - what it is doing right now - written by that task on the
// boundaries of its acts and read by the control loop when the heap watermark makes a new low, so
// that the line about the low names the work it fell on.
BackgroundActBoard backgroundActBoard(&arduinoAdapter);

#ifdef GD_HEAP_FORENSICS
// Frames the radio hands to the network stack, unicast and group, counted on the radio's task and
// read by the control loop with its account of a heap low.
RxFrameClassCounter stationRxFrameClasses;
#endif

ButtonAdapter buttonAdapter(&arduinoAdapter);

WifiAdapter wifiAdapter(wifiMulti, &mqttLogger, &debugSystem);

// What the radio reports about the station link: armed by setup, written from the event task.
WifiLinkLogUseCase wifiLinkLogUseCase(&mqttLogger, &debugSystem, &arduinoAdapter);

// Which access point of that network the station stands on. Decided on the control loop, moved on
// the network task; the outbox and the act board are wired at the end of initDependencies().
WifiRoamUseCase wifiRoamUseCase(&wifiAdapter, nullptr, &arduinoAdapter, nullptr, &mqttLogger,
                                &debugSystem);

// MQTT components (created after wifiAdapter)
TopicHandlerFactory topicHandlerFactory(&mqttLogger, &debugSystem);
MessageProcessor messageProcessor(&mqttLogger, &debugSystem);
MqttManager mqttManager(
    mqttClient,
    &mqttLogger,
    &debugSystem,
    mqttClientIdPrefix,
    mqttBroker,
    mqttPort,
    mqttUser,
    mqttPassword,
    &messageProcessor,
    &topicHandlerFactory,
    &wifiAdapter,
    &mqttClientMutex);

// Using default NTP server with UTC offset of 0 and daylight offset of 0
NTPTimeAdapter ntpTimeAdapter(&mqttLogger, &debugSystem, &settings, &arduinoAdapter);

ModuleConfig moduleConfig(&mqttLogger, &debugSystem, &settings, &ntpTimeAdapter, &wifiAdapter);

ModuleStructure moduleStructure;

DeviceModel deviceModel(&moduleConfig, &moduleStructure);

DeviceCommandExecutionUseCase deviceCommandExecutionUseCase(
    &deviceModel,
    &arduinoAdapter,
    &moduleStructure,
    &mqttLogger,
    &debugSystem,
    &moduleConfig);

SpiffsAdapter spiffsAdapter(&mqttLogger, &debugSystem);

OtaUpdateLog otaUpdateLog(&spiffsAdapter, &mqttLogger, &ntpTimeAdapter, &arduinoAdapter, &wifiAdapter);

BootEventLog bootEventLog(&spiffsAdapter, &mqttLogger, &ntpTimeAdapter, &arduinoAdapter, &wifiAdapter, DebugFeatures::BUTTON_DEBUG);

BootEventLog restartEventLog(&spiffsAdapter, &mqttLogger, &ntpTimeAdapter, &arduinoAdapter, &wifiAdapter, "restart");

// The updater takes no ISettings: it runs on the command network task and only records the
// FIRMWARE_UPDATE_STATE_* value; ModuleCommandExecutionUseCase persists it on the control loop
// in its finish step, so the settings sector keeps a single writer.
// The transport and the flash slot are injected, like RestAPIService's adapter: the download
// session is logic and runs under host tests, the HTTP stack and Update stay device-only.
HttpClientAdapter firmwareHttpClient;
UpdateFirmwareImageWriter firmwareImageWriter;
FirmwareRemoteUpdate firmwareRemoteUpdate(&firmwareHttpClient, &firmwareImageWriter,
                                          &arduinoAdapter, &wifiAdapter,
                                          &mqttLogger, &debugSystem, &otaUpdateLog);

// With verifyRollbackLater() overridden in main.cpp a new image boots on probation; the
// CONFIGURED_DONE loop reports its completed iterations here, and this use case confirms the
// image with the bootloader once the module has demonstrably reached its working state.
EspOtaPartitionControl otaPartitionControl;
ConfirmRunningFirmwareUseCase confirmRunningFirmwareUseCase(
    &otaPartitionControl, &moduleStructure, &arduinoAdapter, &mqttLogger, &debugSystem,
    &otaUpdateLog);

PendingFirmwareUpdateStore pendingFirmwareUpdateStore(&spiffsAdapter, &jsonConverter);

WiFiManager wifiManager(&wifiAdapter, &settings, &mqttLogger, &debugSystem, &spiffsAdapter, &jsonConverter);

// The clock is what reads a file that did not come back again, a sweep later, without waiting for a
// write (Repository::FAILED_READ_RETRY_MS).
SpiffsRuleDao ruleDao(&spiffsAdapter, &jsonConverter, &debugSystem, &mqttLogger);
RuleRepository ruleRepository(&ruleDao, &ntpTimeAdapter, &debugSystem, &mqttLogger, &arduinoAdapter);

SpiffsSensorDao sensorDao(&spiffsAdapter, &jsonConverter, &debugSystem, &mqttLogger);
SensorRepository sensorRepository(&sensorDao, &debugSystem, &mqttLogger, &arduinoAdapter);
ModbusAddressAllocator modbusAddressAllocator(&sensorRepository);

SpiffsSensorTypeDao sensorTypeDao(&spiffsAdapter, &jsonConverter, &debugSystem, &mqttLogger);
SensorTypeRepository sensorTypeRepository(&sensorTypeDao, &debugSystem, &mqttLogger);

ClearModuleDataService clearModuleDataService(&ruleDao, &sensorDao, &sensorTypeDao, &mqttLogger, &debugSystem, &settings, &spiffsAdapter);

HardwareSerial RS485_1(1);
HardwareSerial RS485_2(2);
ModbusRS485 modbusRS485_pin1(RS485_1, 19, "ModbusRS485_1", arduinoAdapter, SERIAL_8N1, 18, 17, SENSOR_POWER_1);
ModbusRS485 modbusRS485_pin2(RS485_1, 19, "ModbusRS485_2", arduinoAdapter, SERIAL_8N1, 18, 17, SENSOR_POWER_3);
ModbusRS485 modbusRS485_pin3(RS485_2, 20, "ModbusRS485_1", arduinoAdapter, SERIAL_8N1, 15, 14, SENSOR_POWER_2);
ModbusRS485 modbusRS485_pin4(RS485_2, 20, "ModbusRS485_2", arduinoAdapter, SERIAL_8N1, 15, 14, SENSOR_POWER_4);

// std::vector<IModbusRS485*> modbusPorts = {&modbusRS485_pin1, &modbusRS485_pin2};
std::vector<IModbusRS485*> modbusPorts = {&modbusRS485_pin1, &modbusRS485_pin2, &modbusRS485_pin3, &modbusRS485_pin4};

AnalogSensorReader analogSensorReader(arduinoAdapter);
ModbusSensorReader modbusSensorReader(modbusPorts, arduinoAdapter, &mqttLogger, &debugSystem);
RawBusAccessAdapter rawBusAccessAdapter(modbusPorts, arduinoAdapter, &mqttLogger, &debugSystem);

SensorReader sensorReader(&analogSensorReader, &modbusSensorReader, &arduinoAdapter, &mqttLogger, &debugSystem);

// API Services - created early so they can be used by use cases.
//
// The transport borrows this adapter and does not own it, so it is declared here, ahead of the
// service, and outlives it. Naming the concrete class here, in the composition root, is what
// lets the transport be built and tested on the host with a stand-in adapter injected instead.
HttpClientAdapter httpClientAdapter;
RestAPIService restApiService(&httpClientAdapter, &moduleConfig, &settings, &mqttLogger, &debugSystem, &jsonConverter);

MqttApiService mqttApiService(
    &mqttManager,
    &moduleConfig,
    &jsonConverter,
    &mqttLogger,
    &debugSystem);

HybridAPIService hybridApiService(
    &restApiService,
    &mqttApiService,
    &moduleConfig,
    &mqttLogger,
    &debugSystem,
    ApiPriority::BOTH);

CheckModuleDeletionUseCase checkModuleDeletionUseCase(
    &hybridApiService,
    &mqttLogger,
    &debugSystem);

SensorCommandUseCase sensorCommandUseCase(
    &moduleConfig,
    &settings,
    &mqttLogger,
    &debugSystem,
    &sensorRepository);

OnChangeModuleModeUseCase moduleModeUseCase(
    arduinoAdapter,
    moduleConfig,
    mqttLogger,
    debugSystem,
    &ntpTimeAdapter);

ButtonUseCases buttonUseCases(&buttonAdapter, &mqttLogger, &debugSystem, 0, &settings, &moduleConfig, &clearModuleDataService, &arduinoAdapter, &bootEventLog);

ModuleConfigurationWebServer moduleConfigurationWebServer(
    webServer,
    wifiMulti,
    &mqttLogger,
    &debugSystem,
    &settings,
    &moduleConfig);

TestModeWebServer testModeWebServer(
    webServer,
    &mqttLogger,
    &debugSystem,
    &settings,
    &moduleConfig,
    &wifiAdapter,
    &arduinoAdapter);

WebServerLauncher webServerLauncher(
    &mqttLogger,
    &debugSystem,
    moduleConfigurationWebServer,
    testModeWebServer);

// API Services already defined earlier before ModuleCommandExecutionUseCase

SensorValueModel sensorValueModel;

RuleStateReportingService ruleStateReportingService(
    &hybridApiService,
    &jsonConverter,
    &settings,
    &moduleConfig,
    &ntpTimeAdapter,
    &mqttLogger,
    &debugSystem);

EvaluateRulesUseCase evaluateRulesUseCase(
    &deviceModel,
    &ruleRepository,
    &arduinoAdapter,
    &ntpTimeAdapter,
    &sensorValueModel,
    &mqttLogger,
    &debugSystem,
    &ruleStateReportingService,
    &stepCycleStore);

PreventiveRestartUseCase preventiveRestartUseCase(
    &moduleConfig,
    &deviceModel,
    &evaluateRulesUseCase,
    &firmwareRemoteUpdate,
    &ntpTimeAdapter,
    &mqttLogger,
    &debugSystem,
    &restartEventLog);

// What the module's parts report about themselves - a TLS handshake short of memory, a network task the
// heap refused, a radio that would not start, whether the server answered a request at all - for the
// link's supervision to read. Written by the HTTP and TLS clients, the outbox and the radio restart.
ModuleStatusBoard moduleStatusBoard;

// The looks the module takes at its own link when its services fail: ARP to the gateway (A), then an
// ICMP echo to a public address (B), on the network task as a job of the outbox. What is asked, and
// for how long, is boardLinkProbePlan() (LinkProbeRun.h).
LwipLinkProbe lwipLinkProbe;

// What the module does about its link - finding out which part of the path failed when its services
// fail, restarting the radio or the module only where that helps, and the offline decision - on a
// tick of the control loop. It is the module's offline decision for everything that reads one: the
// front panel, the rule and catalog syncs, the update recovery.
LinkSupervisor linkSupervisor(
    &restApiService,
    &moduleConfig,
    mqttLogger,
    debugSystem,
    &wifiAdapter,
    &ntpTimeAdapter,
    &arduinoAdapter,
    &mqttManager);

DiagnosticService diagnosticService(
    &restApiService,
    &moduleConfig,
    mqttLogger,
    debugSystem,
    &jsonConverter,
    &wifiAdapter,
    &ntpTimeAdapter,
    &arduinoAdapter,
    &mqttManager,
    &hybridApiService);

// The periodic counters log line; its probes are wired at the end of initDependencies(), after
// every service it reads from exists.
CounterReport counterReport;

// The one place periodic network traffic leaves the module. The producers - sensor updates,
// device updates, diagnostics, rule sync - put a request in the outbox and carry on within the
// same control-loop iteration; one task pinned to core 0 - the same core the ButtonUpdate task
// already lives on - makes the calls and pays for the server's latency (a REST call is allowed
// ~30 s per attempt), so the control loop never does.
//
// The completion queue is what carries the answers back, and it has to be at least as deep as the
// number of requests that can be waiting, or an outcome is lost. State collapses by kind, so at
// most one request per state slot waits at a time - the seven state requests and the radio
// restart - but events do not collapse, and the event ring holds eight. The outbox states that
// bound as MAX_IN_FLIGHT and sizes the depth from it, with room to spare at ~16 bytes per entry.
//
// Losing a completion is not uniformly harmless: a lost DEVICE_UPDATE outcome only keeps a
// device's rule context for one more update, but a lost RULE_LIST outcome leaves
// SyncRulesUseCase's _requestTicket set, and it would never ask for rules again. Outbox counts
// any refusal in lostCompletionCount().
FreeRtosTaskScheduler taskScheduler(0);
FreeRtosMessageQueue<OutboxCompletion> outboxCompletionQueue(Outbox::COMPLETION_QUEUE_DEPTH);
Outbox outbox(
    &hybridApiService,
    &taskScheduler,
    &outboxCompletionQueue,
    &mqttLogger,
    &debugSystem);

LedUseCases ledUseCases(&ledAdapter, &moduleConfig, &arduinoAdapter, &mqttLogger, &debugSystem, &deviceModel, &linkSupervisor);

UdpManager udpManager(&mqttLogger, &debugSystem);

UdpInboundDispatcher udpInboundDispatcher(&udpManager, &mqttLogger, &debugSystem);

RemoteModuleCommandSender remoteModuleCommandSender(&udpManager, &jsonConverter, &mqttLogger, &debugSystem);

RemoteModuleCommandReceiver remoteModuleCommandReceiver(&udpInboundDispatcher, &moduleConfig, &settings, &jsonConverter, &mqttLogger, &debugSystem);

FirmwareUpdateRecoveryUseCase firmwareUpdateRecoveryUseCase(&linkSupervisor, &moduleConfig, &settings, &pendingFirmwareUpdateStore, &mqttLogger, &debugSystem, &otaUpdateLog, &otaPartitionControl);

ModuleCommandExecutionUseCase moduleCommandExecutionUseCase(&mqttLogger, &debugSystem, &ntpTimeAdapter, &firmwareRemoteUpdate, &arduinoAdapter, &sensorRepository, &sensorTypeRepository, &sensorReader, &moduleConfig, &settings, &hybridApiService, &clearModuleDataService, &moduleStructure, &sensorValueModel, &wifiManager, &ledUseCases, &rawBusAccessAdapter, &checkModuleDeletionUseCase, &remoteModuleCommandSender, &firmwareUpdateRecoveryUseCase, &modbusAddressAllocator);

// The task the slow, network-bound module commands run on: the OTA download and the module
// deletion check. Created lazily on the first such command, like the outbox network task;
// subscribes to the task watchdog and feeds itself, so liveness is vouched for by the task
// actually making progress - the download answers for the downloader, the loop for itself.
NetworkCommandRunner networkCommandRunner(
    &taskScheduler,
    &mqttLogger,
    &debugSystem);

// The task the sensor type catalog is stored on. Every catalog the server sends after setup is
// written here - the synchronization's and the one a ScanSensors command fetches - so none of the
// ~30 SPIFFS operations of such a write runs on the control loop. The file is touched elsewhere
// only on the boot path, before the loop starts - the boot after a factory reset removes the
// file - by a factory reset, which empties it wherever it is asked for, and by a read that finds a
// replacement interrupted and finishes it; no read goes to storage while a write is in flight. Created for each catalog write and gone once the write is
// done, and subscribed to the task watchdog while it lives so that a task stuck inside a file
// operation is visible as itself.
SensorTypeCatalogWriter sensorTypeCatalogWriter(
    &sensorTypeDao,
    &taskScheduler,
    &mqttLogger,
    &debugSystem);

CommandService commandService(
    &deviceCommandExecutionUseCase,
    &moduleCommandExecutionUseCase,
    &sensorCommandUseCase,
    &moduleStructure,
    &mqttLogger,
    &debugSystem,
    &ntpTimeAdapter,
    &jsonConverter,
    &ruleRepository,
    &settings);

ProcessDeviceStateUseCaseFactory deviceStateUseCaseFactory(
    &mqttLogger,
    &debugSystem,
    &moduleConfig,
    &arduinoAdapter,
    &deviceModel);

DeviceService deviceService(
    &moduleConfig,
    &moduleStructure,
    &deviceModel,
    &ntpTimeAdapter,
    &arduinoAdapter,
    &deviceStateUseCaseFactory,
    &hybridApiService,
    &mqttLogger,
    &debugSystem);

SyncRulesUseCase syncRulesUseCase(
    &hybridApiService,
    &ruleRepository,
    &arduinoAdapter,
    &ntpTimeAdapter,
    &jsonConverter,
    &mqttLogger,
    &debugSystem);

SyncSensorTypesUseCase syncSensorTypesUseCase(
    &hybridApiService,
    &sensorTypeRepository,
    &arduinoAdapter,
    &ntpTimeAdapter,
    &jsonConverter,
    &mqttLogger,
    &debugSystem,
    &sensorRepository,
    &moduleConfig);

OnChangeLocalIpUseCase onChangeLocalIpUseCase(
    wifiAdapter,
    wifiManager,
    moduleStructure,
    moduleConfig,
    syncRulesUseCase,
    syncSensorTypesUseCase,
    mqttLogger,
    debugSystem);

OnConnectivityRestoredUseCase onConnectivityRestoredUseCase(
    &linkSupervisor,
    &syncRulesUseCase,
    &moduleConfig,
    &arduinoAdapter,
    &mqttLogger,
    &debugSystem);

RuleService ruleService(
    &evaluateRulesUseCase,
    &syncRulesUseCase,
    &deviceModel,
    &mqttLogger,
    &debugSystem);

BoardTest boardTest(
    &arduinoAdapter,
    &wifiAdapter);

// Computed (virtual) sensor channels (GDV1-3782). VPD is the first kind: the arithmetic lives in
// VpdChannelUseCase, the list of kinds in VirtualSensorChannelsUseCase, and everyone who publishes
// channels - the reading path, the update packet, the module structure, the catalog sync - gets
// them from here as interfaces. A new computed channel is a new use case plus a line in this block.
VpdChannelUseCase vpdChannelUseCase;
VirtualSensorChannelsUseCase virtualSensorChannelsUseCase(&vpdChannelUseCase);
SensorStructureUseCase sensorStructureUseCase(
    &virtualSensorChannelsUseCase,
    &mqttLogger,
    &debugSystem);

// Plausibility gate of the reading path (GDV1-3810): resolves the physical range of a channel from
// the sensor type catalog. Created here and handed to SensorService as ISensorPlausibilityUseCase.
SensorPlausibilityUseCase sensorPlausibilityUseCase(&sensorTypeRepository);

SensorService sensorService(
    &moduleConfig,
    &moduleStructure,
    &sensorRepository,
    &sensorReader,
    &sensorValueModel,
    &ntpTimeAdapter,
    &arduinoAdapter,
    &hybridApiService,
    &mqttLogger,
    &debugSystem,
    &udpManager,
    &jsonConverter,
    &sensorPlausibilityUseCase,
    &virtualSensorChannelsUseCase);

// The RS-485 line, and the one task allowed to walk it on a schedule.
//
// The lock is shared by the two ways onto the line - ModbusSensorReader (polling, scanSensor,
// address writes) and RawBusAccessAdapter (ModbusRead/ModbusWrite/SerialWrite from the backend) -
// because both end up writing frames to the same HardwareSerial. Note that "four ports" is four
// power pins, not four buses: ports 0 and 1 share RS485_1 and ports 2 and 3 share RS485_2 (see
// the ModbusRS485 instances above), so one lock for the module is not a simplification, it is the
// shape of the hardware.
//
// The acquisition task is pinned to core 0 by the same scheduler the outbox uses; the Arduino
// loop() runs on core 1.
FreeRtosBusLockPlatform rs485BusLockPlatform;
TicketBusLock rs485BusLock(&rs485BusLockPlatform);
SensorAcquisitionService sensorAcquisitionService(
    &sensorReader,
    &arduinoAdapter,
    &taskScheduler,
    &mqttLogger,
    &debugSystem);

RemoteSensorReceiver remoteSensorReceiver(
    &udpManager,
    &udpInboundDispatcher,
    &mqttManager,
    &messageProcessor,
    &sensorValueModel,
    &ruleRepository,
    &sensorRepository,
    &jsonConverter,
    &arduinoAdapter,
    &mqttLogger,
    &debugSystem);

LoopServiceFactory loopServiceFactory(
    &ledUseCases,
    &buttonUseCases,
    &moduleModeUseCase,
    &mqttLogger,
    &debugSystem,
    &moduleConfigurationWebServer,
    &moduleConfig,
    &arduinoAdapter,
    &onChangeLocalIpUseCase,
    &onConnectivityRestoredUseCase,
    &mqttManager,
    &diagnosticService,       // the snapshot is taken on the loop, the send goes to the outbox
    &deviceService,
    &sensorService,
    &ruleService,
    &testModeWebServer,
    &clearModuleDataService,
    &boardTest);

ModuleSetupService moduleSetupService(
    &mqttLogger,
    &debugSystem,
    &moduleConfig,
    &ledAdapter,
    &buttonAdapter,
    &ledUseCases,
    &buttonUseCases,
    &wifiAdapter,
    &wifiManager,
    &webServerLauncher,
    &loopServiceFactory,
    &mqttManager,
    &moduleStructure,
    &deviceService,
    &ruleService,
    &sensorRepository,
    &sensorTypeRepository,
    &sensorReader,
    &spiffsAdapter,
    &jsonConverter,
    &boardTest,
    &udpManager,
    &hybridApiService,
    &arduinoAdapter,
    &syncSensorTypesUseCase,
    &remoteSensorReceiver,
    &onConnectivityRestoredUseCase,
    &otaUpdateLog,
    &udpInboundDispatcher,
    &remoteModuleCommandReceiver);

RepublishModuleStructureUseCase republishModuleStructureUseCase(
    &moduleStructure,
    &sensorRepository,
    &hybridApiService,
    &sensorStructureUseCase,
    &mqttLogger,
    &debugSystem,
    &arduinoAdapter);

// What a restart finds out about the sensors, and where it finds it out: the ping of every stored
// entity, the scan of the free ports, the ModBus address collisions and the per-model register
// writes are minutes of line time, so they run as steps on the acquisition task, taken
// between the sweeps of the sensors the module already knows.
StartupSensorScanUseCase startupSensorScanUseCase(
    &mqttLogger,
    &debugSystem,
    &moduleConfig,
    &sensorRepository,
    &sensorTypeRepository,
    &sensorReader,
    &moduleStructure,
    &sensorStructureUseCase,
    &modbusAddressAllocator,
    &sensorValueModel,
    &virtualSensorChannelsUseCase,
    &arduinoAdapter);

// The order the module comes up in once setup is over: the link, the structure that reports the
// module, and only then what the boot sends after it - an order of events rather than of timers
// from millis 0. Ticked by the CONFIGURED_DONE loop; the producers ask it, through IBootGates,
// whether their moment has come.
BootSequencer bootSequencer(
    &arduinoAdapter,
    &mqttLogger,
    &debugSystem);

void initDependencies()
{
    // Armed at the start of the wiring: static construction has already happened by now, but
    // every allocation the running firmware makes is covered, and a refusal is counted instead
    // of surfacing only as a null pointer somewhere.
    HeapFailureHook::install();

    // After a power cut the RTC memory holds whatever bytes were there (GDV1-3811)
    stepCycleStore.dropInvalidSlots();
    topicHandlerFactory.setModuleConfig(&moduleConfig);
    topicHandlerFactory.setCommandService(&commandService);
    topicHandlerFactory.setJsonConverter(&jsonConverter);
    boardTest.setModbusPorts(modbusPorts);
    syncRulesUseCase.setRemoteSensorReceiver(&remoteSensorReceiver);
    syncRulesUseCase.setSettings(&settings);
    commandService.setRemoteSensorReceiver(&remoteSensorReceiver);
    moduleSetupService.setSensorStructureUseCase(&sensorStructureUseCase);
    moduleSetupService.setStartupSensorScan(&startupSensorScanUseCase);
    moduleSetupService.setWifiLinkLog(&wifiLinkLogUseCase);
    moduleCommandExecutionUseCase.setSensorStructureUseCase(&sensorStructureUseCase);
    commandService.setFirmwareUpdateRecovery(&firmwareUpdateRecoveryUseCase);
    syncSensorTypesUseCase.setVirtualSensorChannelsUseCase(&virtualSensorChannelsUseCase);
    syncSensorTypesUseCase.setRepublishModuleStructureUseCase(&republishModuleStructureUseCase);
    syncSensorTypesUseCase.setSensorValueModel(&sensorValueModel);
    // The same offline decision OnConnectivityRestoredUseCase watches, read by the catalog sync
    // to re-arm a fetch the outage cost it.
    syncSensorTypesUseCase.setNetworkStatusProvider(&linkSupervisor);

    // One owner for the network, one queue in front of it. Everything below submits to the outbox
    // instead of calling the REST or MQTT client from the control loop.
    //
    // The link is that task's as well: the radio restart the diagnostics decide on is a job in
    // this queue, served first, and the MQTT reconnect is beaten from the same task between the
    // requests it makes - so the handshake, the socket and the bodies are never in flight at once.
    outbox.setWifiAdapter(&wifiAdapter);
    outbox.setRoamRadio(&wifiAdapter);
    outbox.setMqttLink(&mqttManager);
    outbox.setLinkProbe(&lwipLinkProbe, boardLinkProbePlan());

    // The module's own reports for the link's supervision: the outbox's network task, the radio's
    // start, and the REST client's requests and handshakes.
    outbox.setStatusBoard(&moduleStatusBoard);
    wifiAdapter.setStatusBoard(&moduleStatusBoard);
    httpClientAdapter.setStatusBoard(&moduleStatusBoard);
    linkSupervisor.setStatusBoard(&moduleStatusBoard);
    republishModuleStructureUseCase.setOutbox(&outbox);
    loopServiceFactory.setRepublishModuleStructureUseCase(&republishModuleStructureUseCase);
    diagnosticService.setOutbox(&outbox);
    linkSupervisor.setOutbox(&outbox);
    diagnosticService.setLinkSupervisor(&linkSupervisor);
    loopServiceFactory.setLinkSupervisor(&linkSupervisor);
    loopServiceFactory.setPreventiveRestartUseCase(&preventiveRestartUseCase);
    sensorService.setOutbox(&outbox);
    deviceService.setOutbox(&outbox);
    syncRulesUseCase.setOutbox(&outbox);
    ruleStateReportingService.setOutbox(&outbox);
    loopServiceFactory.setOutbox(&outbox);
    commandService.setOutbox(&outbox);
    syncSensorTypesUseCase.setOutbox(&outbox);
    // One writer for the catalogs the server sends, and it is not the control loop: the
    // synchronization hands what it fetched to the storage task, and a ScanSensors command that
    // had to fetch a catalog hands it to the synchronization.
    syncSensorTypesUseCase.setCatalogWriter(&sensorTypeCatalogWriter);
    loopServiceFactory.setSensorTypeCatalogWriter(&sensorTypeCatalogWriter);
    moduleCommandExecutionUseCase.setSyncSensorTypesUseCase(&syncSensorTypesUseCase);
    onChangeLocalIpUseCase.setOutbox(&outbox);
    onChangeLocalIpUseCase.setClock(&arduinoAdapter);
    loopServiceFactory.setCommandService(&commandService);
    loopServiceFactory.setSyncSensorTypesUseCase(&syncSensorTypesUseCase);

    // The move to a better access point of the same network: decided here, made by the network
    // task as the radio restart is. The radio's own warning about a falling signal is armed by the
    // adapter when the station starts - there is no radio to ask at this point in the boot.
    wifiRoamUseCase.setOutbox(&outbox);
    loopServiceFactory.setWifiRoamUseCase(&wifiRoamUseCase);

    // One owner for the RS-485 schedule, one lock in front of the line. The control loop makes
    // no bus transactions at all: the periodic sweep belongs to the acquisition task, and the
    // raw commands the backend sends (ModbusRead/ModbusWrite/SerialWrite/RebootRemoteModule) are
    // queued to the same task rather than executed on the loop that dispatched the MQTT message.
    // The lock stays because the line is also reached from the setup and board-test paths, which
    // do not run under the loop.
    //
    // The task is created lazily, on the first polling tick, so that a refusal for want of heap
    // can be retried instead of costing the module its sensors until the next reboot.
    modbusSensorReader.setBusLock(&rs485BusLock);
    sensorReader.setBusLock(&rs485BusLock);
    rawBusAccessAdapter.setBusLock(&rs485BusLock);
    sensorService.setAcquisition(&sensorAcquisitionService);
    sensorAcquisitionService.setRawCommandExecutor(&moduleCommandExecutionUseCase);
    sensorAcquisitionService.setStartupSensorScan(&startupSensorScanUseCase);
    commandService.setBusCommandExecutor(&sensorAcquisitionService);
    // The type decides whether a command's bus work may start: only a sensor module has the line.
    commandService.setModuleConfig(&moduleConfig);
    commandService.setStartupSensorScan(&startupSensorScanUseCase);
    // The front panel: a task to draw the boot stages on while setup blocks, and the discovery to
    // ask whether a scan pass with an end to it is under way.
    ledUseCases.setTaskScheduler(&taskScheduler);
    ledUseCases.setScanEntryStatus(&startupSensorScanUseCase);
    startupSensorScanUseCase.setRepublishModuleStructureUseCase(&republishModuleStructureUseCase);
    // The presence rung asks a port as a whole, which no entry point of the sensor reader can do:
    // the same adapter a raw ModbusRead uses, carrying the wildcard query frames of the address discovery.
    startupSensorScanUseCase.setRawBusAccess(&rawBusAccessAdapter);
    // The port pairs above: modbusRS485_pin1/pin2 are one HardwareSerial and pin3/pin4 another, so
    // ports 0 and 1 are one electrical line and ports 2 and 3 are the other. That is read off the
    // ports themselves and nowhere written down twice: the reader isolates a model scan and an
    // address write by it, and the discovery its presence poll and its windows.
    startupSensorScanUseCase.setPortLines(modbusSensorReader.portLines());
    // One answer to "how is a stored sensor described", used by the boot and by every republish.
    republishModuleStructureUseCase.setStoredSensorDescriber(&startupSensorScanUseCase);
    networkCommandRunner.setExecutor(&moduleCommandExecutionUseCase);
    commandService.setNetworkCommandRunner(&networkCommandRunner);

    // The boot sequence reads its events off their owners, and the producers it orders ask it.
    bootSequencer.setWifiAdapter(&wifiAdapter);
    bootSequencer.setOnChangeLocalIpUseCase(&onChangeLocalIpUseCase);
    bootSequencer.setSensorService(&sensorService);
    bootSequencer.setSyncSensorTypesUseCase(&syncSensorTypesUseCase);
    bootSequencer.setStartupSensorScan(&startupSensorScanUseCase);
    bootSequencer.setMqttManager(&mqttManager);
    bootSequencer.setRemoteSensorReceiver(&remoteSensorReceiver);
    bootSequencer.setSyncRulesUseCase(&syncRulesUseCase);
    bootSequencer.setNetworkListeners(&moduleSetupService);
    bootSequencer.setBootAssociation(&linkSupervisor);
    bootSequencer.setCommandService(&commandService);
    bootSequencer.setClock(&ntpTimeAdapter);
    {
        // How long the boot waits for each event before it gives up on it - ceilings for a
        // completion that never comes, not timers that pace the boot.
        BootSequencer::CeilingInputs inputs;
        // One REST attempt at its longest: the server is named by a host name, and the resolver
        // waits up to 15 s for an answer (WiFiGenericClass::hostByName); HTTPClient gives the TCP
        // connect 5 s (HTTPCLIENT_DEFAULT_TCP_TIMEOUT); the TLS handshake is counted at
        // WiFiClientSecure's default of 120 s, although HttpClientAdapter bounds it at 20 s
        // (kHandshakeTimeoutS) - so these ceilings are wider than an attempt can now take, still
        // upper bounds; and RestAPIService lets the answer be silent for 30 s (setTimeout(30000)).
        inputs.restAttemptMs = 15000 + 5000 + 120000 + 30000;
        inputs.attemptsPerJob = Outbox::DEFAULT_MAX_ATTEMPTS;
        inputs.firstBackoffMs = Outbox::DEFAULT_BACKOFF_BASE_MS;
        inputs.backoffFactor = Outbox::BACKOFF_FACTOR;
        inputs.mqttActMs = MqttManager::CLOSE_GRACE_MS + MqttManager::CONNECT_TIMEOUT_MS;
        // Every other source the outbox rotates over - the sensor update, the device update, the
        // diagnostic, the rule list, the device and the module command fetches - can be served
        // ahead of the catalog once the catalog is asked for.
        inputs.jobsAheadOfTheCatalog = 6;
        inputs.portWakeUpMs = PortPower::WAKE_UP_MS;
        inputs.sweepIntervalMs = SensorAcquisitionService::SWEEP_INTERVAL_MS;
        // A channel of a stored sensor that does not answer: SensorReader asks it three times,
        // 100 ms apart, each within ModbusSensorReader's 300 ms read budget.
        inputs.channelReadMs = 3 * 300 + 2 * 100;
        // Structure 702 500 ms, catalog 4 917 500 ms; first readings 25 000 ms + 1 100 ms a channel.
        bootSequencer.setCeilings(BootSequencer::ceilingsFrom(inputs));
    }
    sensorService.setBootGates(&bootSequencer);
    ruleService.setBootGates(&bootSequencer);
    deviceService.setBootGates(&bootSequencer);
    startupSensorScanUseCase.setBootGates(&bootSequencer);
    republishModuleStructureUseCase.setBootGates(&bootSequencer);
    loopServiceFactory.setBootSequencer(&bootSequencer);

    loopServiceFactory.setOtaUpdateLog(&otaUpdateLog);
    loopServiceFactory.setBootEventLog(&bootEventLog);
    loopServiceFactory.setRestartEventLog(&restartEventLog);
    loopServiceFactory.setConfirmRunningFirmwareUseCase(&confirmRunningFirmwareUseCase);

    // The four background tasks write to the act board; every loop service reads it.
    outbox.setActBoard(&backgroundActBoard);
    // Read, not written: what the board says is idle is what makes a moment quiet enough to spend
    // an association in - and, for the link's supervision, what tells a link the module took down
    // itself from one that needs the radio restarted.
    wifiRoamUseCase.setActBoard(&backgroundActBoard);
    linkSupervisor.setActBoard(&backgroundActBoard);
    sensorAcquisitionService.setActBoard(&backgroundActBoard);
    networkCommandRunner.setActBoard(&backgroundActBoard);
    sensorTypeCatalogWriter.setActBoard(&backgroundActBoard);
    loopServiceFactory.setBackgroundActBoard(&backgroundActBoard);

#ifdef GD_HEAP_FORENSICS
    // The radio adapter counts what it hands to the network stack; every loop service reads it.
    wifiAdapter.classifyReceivedFramesInto(&stationRxFrameClasses);
    loopServiceFactory.setRxFrameClasses(&stationRxFrameClasses);
#endif

    // The counters that count refusals, drops and fallbacks are read by nothing on the device;
    // this line makes them greppable in the log once per diagnostic period. Log only - the
    // diagnostic DTO is a wire contract and gets nothing from here.
    counterReport.setTaskScheduler(&taskScheduler);
    // First of all: the table refuses whatever is offered past its cap, so a column registered last
    // is the first one it would refuse - and the count of what it refused is the one number that
    // must not go missing with the column it was counting.
    counterReport.addRefusalColumn("counters_refused");
    // The one column that is a measurement and not a tally: the longest control-loop step of the
    // period just ended. Read and reset by this probe, so each line describes its own interval -
    // a stall that happened once is visible in the line it happened in, instead of colouring
    // every line after it.
    counterReport.addCounter("max_loop_step_ms", [] { return (uint64_t)LoopStepMonitor::take(); });
    counterReport.addCounter("tls_fallbacks", [] { return (uint64_t)TlsMemoryPool::heapFallbacks(); });
    counterReport.addCounter("cmd_drop_signal", [] { return (uint64_t)commandService.droppedSignalCount(); });
    counterReport.addCounter("cmd_drop_report", [] { return (uint64_t)commandService.droppedReportCount(); });
    counterReport.addCounter("cmd_drop_batch", [] { return (uint64_t)commandService.droppedBatchCount(); });
    counterReport.addCounter("cmd_refuse_bus", [] { return (uint64_t)commandService.refusedBusCommandCount(); });
    counterReport.addCounter("net_refused", [] { return (uint64_t)networkCommandRunner.refusedCount(); });
    counterReport.addCounter("net_task_fail", [] { return (uint64_t)networkCommandRunner.taskStartFailureCount(); });
    counterReport.addCounter("acq_refuse_bus", [] { return (uint64_t)sensorAcquisitionService.refusedBusCommandCount(); });
    counterReport.addCounter("acq_task_fail", [] { return (uint64_t)sensorAcquisitionService.taskStartFailureCount(); });
    counterReport.addCounter("acq_drop_reading", [] { return sensorAcquisitionService.droppedReadingCount(); });
    counterReport.addCounter("acq_refuse_scan", [] { return (uint64_t)sensorAcquisitionService.refusedStartupStepCount(); });
    counterReport.addCounter("catalog_write_refused", [] { return (uint64_t)sensorTypeCatalogWriter.refusedCount(); });
    counterReport.addCounter("catalog_task_fail", [] { return (uint64_t)sensorTypeCatalogWriter.taskStartFailureCount(); });
    counterReport.addCounter("outbox_refused_events", [] { return (uint64_t)outbox.refusedEventCount(); });
    counterReport.addCounter("outbox_lost_completions", [] { return (uint64_t)outbox.lostCompletionCount(); });
    counterReport.addCounter("outbox_task_fail", [] { return (uint64_t)outbox.taskStartFailureCount(); });
    counterReport.addCounter("led_task_fail", [] { return (uint64_t)ledUseCases.taskStartFailureCount(); });
    counterReport.addCounter("sched_untracked_tasks", [] { return (uint64_t)taskScheduler.untrackedTaskCount(); });
    counterReport.addCounter("wifi_roams", [] { return (uint64_t)wifiRoamUseCase.roamCount(); });
    counterReport.addCounter("wifi_roam_fails", [] { return (uint64_t)wifiRoamUseCase.roamFailCount(); });
    registerMemoryCounters(counterReport, arduinoAdapter);
    registerJsonCounters(counterReport);
    registerScanCounters(counterReport, startupSensorScanUseCase);
    {
        ModbusRS485* const rs485Ports[] = {&modbusRS485_pin1, &modbusRS485_pin2,
                                           &modbusRS485_pin3, &modbusRS485_pin4};
        registerRs485Counters(counterReport,
                              std::vector<ModbusRS485*>(rs485Ports, rs485Ports + sizeof(rs485Ports) /
                                                                                 sizeof(rs485Ports[0])));
    }
    counterReport.addTaskStack("stack_cmd_net", NetworkCommandRunner::TASK_NAME);
    counterReport.addTaskStack("stack_sensor_acq", SensorAcquisitionService::TASK_NAME);
    counterReport.addTaskStack("stack_outbox_net", "outbox-net");
    counterReport.addTaskStack("stack_catalog_io", SensorTypeCatalogWriter::TASK_NAME);
    diagnosticService.setCounterReport(&counterReport);
}
#endif // GLOBALS_H
