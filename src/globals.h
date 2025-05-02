#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ISettings.h"
#include "Core/Domain/Interfaces/Adapters/ILedAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IButtonAdapter.h"
#include "Core/Domain/UseCases/Led/LedUseCases.h"

#include "Infrastructure/Adapters/eepromAdapter.h"
#include "Infrastructure/Logging/MqttLogger.h"
#include "Infrastructure/Configuration/ModuleConfig.h"
#include "Infrastructure/Configuration/Settings.h"
#include "Infrastructure/Logging/DebugSystem.h"
#include "Infrastructure/Adapters/Time/NTPTimeAdapter.h"
#include "Infrastructure/Adapters/Led/LedAdapter.h"
#include "Infrastructure/Adapters/ArduinoAdapter.h"
#include "Infrastructure/Adapters/Button/ButtonAdapter.h"
#include "Application/Services/ModuleSetupService.h"

const char *mqttUser = "GrowDirectorMqtt";
const char *mqttPassword = "lasdfh329483457sfdjhs92_w";

// MQTT settings
IPAddress mqttBroker(116, 203, 235, 144);
const int mqttPort = 1883;
const char* mqttDefaultTopic = "hardware/log";

// Callback function for MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Handle incoming messages
  Serial.print("Message received on topic: ");
  Serial.println(topic);
}

// Initialize WiFi and MQTT clients
WiFiClient wifiClient;
PubSubClient mqttClient(mqttBroker, 1883, mqttCallback, wifiClient);
MqttLogger mqttLogger(mqttClient, mqttDefaultTopic, MqttLoggerMode::MqttAndSerial);

DebugSystem debugSystem;

EepromAdapter eeprom(1024, 0);
Settings settings(eeprom);

LedAdapter ledAdapter;

ArduinoAdapter arduinoAdapter;

// Create Button Adapter instance
ButtonAdapter buttonAdapter(&arduinoAdapter);

// Create NTP Time Adapter instance
// Using default NTP server with UTC offset of 0 and daylight offset of 0
NTPTimeAdapter ntpTimeAdapter(&mqttLogger, &debugSystem, &settings);

ModuleConfig moduleConfig(&mqttLogger, &debugSystem, &settings, &ntpTimeAdapter);

LedUseCases ledUseCases(&ledAdapter, &moduleConfig, &arduinoAdapter, &mqttLogger, &debugSystem);

ModuleSetupService moduleSetupService(&mqttLogger, &debugSystem, &moduleConfig, &ledAdapter, &ledUseCases);

IPAddress myIP;

#endif // GLOBALS_H
