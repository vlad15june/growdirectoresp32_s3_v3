#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <Wire.h>
#include <Update.h>
#include <PubSubClient.h>
#include <T67XX.h>
#include "globals.h"


void setup()
{
  Serial.begin(115200);
  Serial.println("System initialized.");

  moduleSetupService.setupModule();
}

void loop()
{
  // pinMode(15, OUTPUT);
  // digitalWrite(15, HIGH);

  Serial.printf("loop\n");
  delay(1000);
}
