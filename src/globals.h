#ifndef GLOBALS_H
#define GLOBALS_H

#include <WiFi.h>
#include <PubSubClient.h>

#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"

#include "Infrastructure/Logging/MqttLogger.h"
#include "Infrastructure/Configuration/ModuleConfig.h"
#include "Infrastructure/Logging/DebugSystem.h"


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

ModuleConfig moduleConfig(&mqttLogger);

DebugSystem debugSystem();

// Variables for IP change example
IPAddress myIP;

#endif // GLOBALS_H
