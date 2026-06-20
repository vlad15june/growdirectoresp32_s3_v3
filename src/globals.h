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
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"

#include "Core/Domain/UseCases/Led/LedUseCases.h"
#include "Core/Domain/UseCases/Buttons/ButtonUseCases.h"
#include "Core/Domain/UseCases/ModuleMode/OnChangeModuleModeUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/OnChangeLocalIpUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/OnConnectivityRestoredUseCase.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCaseFactory.h"
#include "Core/Domain/UseCases/DeviceControl/DeviceCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/Sensors/SensorCommandUseCase.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Core/Domain/UseCases/Rules/SyncRulesUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"

#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Data/Models/DeviceModel.h"
#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"

#include "Infrastructure/Adapters/eepromAdapter.h"
#include "Infrastructure/Logging/MqttLogger.h"
#include "Infrastructure/Logging/OtaUpdateLog.h"
#include "Infrastructure/Configuration/ModuleConfig.h"
#include "Infrastructure/Configuration/Settings.h"
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
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Application/Services/DiagnosticService.h"
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
#include "Application/Services/Rules/RuleService.h"
#include "Application/Services/Rules/RuleStateReportingService.h"
#include "Application/Services/ClearModuleDataService.h"

#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Adapters/Sensors/RemoteSensors/RemoteSensorClient.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/AnalogSensors/AnalogSensorReader.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Rs485/ModbusRS485.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/BoardTesting/BoardTest.h"
#include "Infrastructure/Controllers/HTTP/FirmwareRemoteUpdate.h"
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

LedAdapter ledAdapter(&mqttLogger, &debugSystem);

ArduinoAdapter arduinoAdapter;

ButtonAdapter buttonAdapter(&arduinoAdapter);

WifiAdapter wifiAdapter(wifiMulti, &mqttLogger, &debugSystem);

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
    true,
    &messageProcessor,
    &topicHandlerFactory,
    &wifiAdapter,
    &mqttClientMutex);

// Using default NTP server with UTC offset of 0 and daylight offset of 0
NTPTimeAdapter ntpTimeAdapter(&mqttLogger, &debugSystem, &settings);

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

OtaUpdateLog otaUpdateLog(&spiffsAdapter, &mqttLogger, &ntpTimeAdapter);

FirmwareRemoteUpdate firmwareRemoteUpdate(&mqttLogger, &debugSystem, &settings, &otaUpdateLog);

WiFiManager wifiManager(&wifiAdapter, &settings, &mqttLogger, &debugSystem, &spiffsAdapter, &jsonConverter);

SpiffsRuleDao ruleDao(&spiffsAdapter, &jsonConverter, &debugSystem, &mqttLogger);
RuleRepository ruleRepository(&ruleDao, &ntpTimeAdapter, &debugSystem, &mqttLogger);

SpiffsSensorDao sensorDao(&spiffsAdapter, &jsonConverter, &debugSystem, &mqttLogger);
SensorRepository sensorRepository(&sensorDao, &debugSystem, &mqttLogger);

SpiffsSensorTypeDao sensorTypeDao(&spiffsAdapter, &jsonConverter, &debugSystem, &mqttLogger);
SensorTypeRepository sensorTypeRepository(&sensorTypeDao, &debugSystem, &mqttLogger);

ClearModuleDataService clearModuleDataService(&ruleDao, &sensorDao, &sensorTypeDao, &mqttLogger, &debugSystem, &settings);

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
RawBusAccessAdapter rawBusAccessAdapter(modbusPorts, arduinoAdapter);

SensorReader sensorReader(&analogSensorReader, &modbusSensorReader, &arduinoAdapter, &mqttLogger, &debugSystem);

// API Services - created early so they can be used by use cases
RestAPIService restApiService(&moduleConfig, &settings, &mqttLogger, &debugSystem, &jsonConverter);

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
    debugSystem);

ButtonUseCases buttonUseCases(&buttonAdapter, &mqttLogger, &debugSystem, 0, &settings, &moduleConfig, &clearModuleDataService, &arduinoAdapter);

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

LedUseCases ledUseCases(&ledAdapter, &moduleConfig, &arduinoAdapter, &mqttLogger, &debugSystem, &deviceModel, &diagnosticService);

ModuleCommandExecutionUseCase moduleCommandExecutionUseCase(&mqttLogger, &debugSystem, &ntpTimeAdapter, &firmwareRemoteUpdate, &arduinoAdapter, &sensorRepository, &sensorTypeRepository, &sensorReader, &moduleConfig, &settings, &hybridApiService, &clearModuleDataService, &moduleStructure, &sensorValueModel, &wifiManager, &ledUseCases, &rawBusAccessAdapter);

CommandService commandService(
    &deviceCommandExecutionUseCase,
    &moduleCommandExecutionUseCase,
    &sensorCommandUseCase,
    &hybridApiService,
    &moduleStructure,
    &mqttLogger,
    &debugSystem,
    &ntpTimeAdapter,
    &jsonConverter,
    &ruleRepository);

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
    &ruleStateReportingService);

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
    &sensorRepository);

OnChangeLocalIpUseCase onChangeLocalIpUseCase(
    wifiAdapter,
    wifiManager,
    moduleStructure,
    hybridApiService,
    moduleConfig,
    syncRulesUseCase,
    syncSensorTypesUseCase,
    mqttLogger,
    debugSystem);

OnConnectivityRestoredUseCase onConnectivityRestoredUseCase(
    &diagnosticService,
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

UdpManager udpManager(&mqttLogger, &debugSystem);

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
    &mqttManager);

RemoteSensorReceiver remoteSensorReceiver(
    &udpManager,
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
    &diagnosticService,
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
    &clearModuleDataService,
    &arduinoAdapter,
    &syncSensorTypesUseCase,
    &remoteSensorReceiver,
    &onConnectivityRestoredUseCase,
    &otaUpdateLog);

void initDependencies()
{
    topicHandlerFactory.setModuleConfig(&moduleConfig);
    topicHandlerFactory.setCommandService(&commandService);
    topicHandlerFactory.setJsonConverter(&jsonConverter);
    boardTest.setModbusPorts(modbusPorts);
    syncRulesUseCase.setRemoteSensorReceiver(&remoteSensorReceiver);
    commandService.setRemoteSensorReceiver(&remoteSensorReceiver);
    loopServiceFactory.setOtaUpdateLog(&otaUpdateLog);
}
#endif // GLOBALS_H
