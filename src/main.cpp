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

  // settings.begin();
  // settings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);

  moduleSetupService.setupModule();
}

void loop()
{
  // pinMode(15, OUTPUT);
  // digitalWrite(15, HIGH);

  // Serial.printf("loop\n");
  // delay(1000);

  ledUseCases.loop(); //need ledService.loop(), service put in use case device state and other things
  // buttonUseCases.loop(); //need buttonService.loop(), service put in use case device state and other things

  webServer.handleClient();
}
