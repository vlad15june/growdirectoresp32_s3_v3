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
  // will create loop service according to module mode and module type
  // loopService = moduleSetupService.getLoopService();
}

void loop()
{


  //will be one service for execute loop
  // loopService.loop();
  ledUseCases.loop(); //need loopService.loop(), service put in use case device state and other things
  // buttonUseCases.loop(); //need loopService.loop(), service put in use case device state and other things

  moduleConfigurationWebServer.loop();
}
