#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <Wire.h>
#include <Update.h>
#include <PubSubClient.h>
#include <T67XX.h>
#include "globals.h"


// Create ModuleConfig instance


void setup() {
  Serial.begin(115200);
  
  moduleConfig.configureModuleType();

  Serial.println("System initialized.");
}

void loop() {
  Serial.printf("loop\n");
  delay(1000);
}
