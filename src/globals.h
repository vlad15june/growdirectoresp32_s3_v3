#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFiMulti.h>

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IButtonAdapter.h"
#include "Core/Domain/UseCases/Led/LedUseCases.h"
#include "Core/Domain/UseCases/Buttons/ButtonUseCases.h"
#include "Core/Domain/UseCases/ModuleMode/OnChangeModuleModeUseCase.h"

#include "Infrastructure/Adapters/eepromAdapter.h"
#include "Infrastructure/Logging/MqttLogger.h"
#include "Infrastructure/Configuration/ModuleConfig.h"
#include "Infrastructure/Configuration/Settings.h"
#include "Infrastructure/Logging/DebugSystem.h"
#include "Infrastructure/Adapters/Time/NTPTimeAdapter.h"
#include "Infrastructure/Adapters/Led/LedAdapter.h"
#include "Infrastructure/Adapters/ArduinoAdapter.h"
#include "Infrastructure/Adapters/Button/ButtonAdapter.h"
#include "Infrastructure/Adapters/WiFi/WifiAdapter.h"
#include "Infrastructure/Controllers/WebServices/ModuleConfigurationWebServer.h"
#include "Infrastructure/Controllers/WebServices/Launchers/WebServerLauncher.h"
#include "Infrastructure/Controllers/MQTT/MqttManager.h"
#include "Infrastructure/Controllers/MQTT/MessageProcessor.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlerFactory.h"
#include "Application/Services/ModuleSetupService.h"
#include "Application/Interfaces/LoopServices/ILoopServiceFactory.h"
#include "Application/Services/LoopServices/LoopServiceFactory.h"

const char *mqttUser = "GrowDirectorMqtt";
const char *mqttPassword = "lasdfh329483457sfdjhs92_w";

// MQTT settings
IPAddress mqttBroker(116, 203, 235, 144);
const int mqttPort = 1883;
const char* mqttDefaultTopic = "hardware/log";

IPAddress myIP;

WebServer webServer(80);
WiFiMulti wifiMulti;

// Callback function for MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // This will be replaced by the MqttManager callback
  Serial.print("Message received on topic: ");
  Serial.println(topic);
}

// Initialize WiFi and MQTT clients
WiFiClient wifiClient;
PubSubClient mqttClient(mqttBroker, 1883, mqttCallback, wifiClient);

MqttLogger mqttLogger(mqttClient, mqttDefaultTopic, MqttLoggerMode::MqttAndSerial);

DebugSystem debugSystem;

// MQTT components
TopicHandlerFactory topicHandlerFactory(&mqttLogger, &debugSystem);
MessageProcessor messageProcessor(&mqttLogger, &debugSystem);
MqttManager mqttManager(
    mqttClient, 
    &mqttLogger, 
    &debugSystem, 
    "GrowDirectorClient", 
    mqttUser, 
    mqttPassword, 
    true, 
    &messageProcessor,
    &topicHandlerFactory
);

EepromAdapter eeprom(1024, 0);
Settings settings(eeprom, &mqttLogger, &debugSystem);

LedAdapter ledAdapter(&mqttLogger, &debugSystem);

ArduinoAdapter arduinoAdapter;

ButtonAdapter buttonAdapter(&arduinoAdapter);

WifiAdapter wifiAdapter(wifiMulti);

// Create NTP Time Adapter instance
// Using default NTP server with UTC offset of 0 and daylight offset of 0
NTPTimeAdapter ntpTimeAdapter(&mqttLogger, &debugSystem, &settings);

ModuleConfig moduleConfig(&mqttLogger, &debugSystem, &settings, &ntpTimeAdapter);

// Create OnChangeModuleModeUseCase instance
OnChangeModuleModeUseCase moduleModeUseCase(arduinoAdapter, moduleConfig, &mqttLogger, &debugSystem);

LedUseCases ledUseCases(&ledAdapter, &moduleConfig, &arduinoAdapter, &mqttLogger, &debugSystem);

ButtonUseCases buttonUseCases(&buttonAdapter, &mqttLogger, &debugSystem, 0);

ModuleConfigurationWebServer moduleConfigurationWebServer(
    webServer, 
    wifiMulti,
    &mqttLogger, 
    &debugSystem, 
    &settings,
    &moduleConfig
);

WebServerLauncher webServerLauncher(
    &mqttLogger,
    &debugSystem,
    moduleConfigurationWebServer
);

// Create LoopServiceFactory instance with proper dependencies
LoopServiceFactory loopServiceFactory(
  &ledUseCases,
  &buttonUseCases,
  &moduleModeUseCase,
  &mqttLogger,
  &debugSystem,
  &moduleConfigurationWebServer,
  &moduleConfig,
  &arduinoAdapter
);

ModuleSetupService moduleSetupService(
    &mqttLogger, 
    &debugSystem, 
    &moduleConfig, 
    &ledAdapter, 
    &ledUseCases, 
    &buttonUseCases, 
    &wifiAdapter, 
    &webServerLauncher,
    &loopServiceFactory);

#endif // GLOBALS_H
