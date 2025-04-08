#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <Wire.h>
#include <Update.h>
#include <PubSubClient.h>
#include <T67XX.h>
#include "globals.h"

void setup() {
  Serial.begin(115200);

  Serial.println("System initialized.");
}

void loop() {
  Serial.printf("loop\n");
  delay(1000);
}
