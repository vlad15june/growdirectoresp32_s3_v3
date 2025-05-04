#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <Wire.h>
#include <Update.h>
#include <PubSubClient.h>
#include <T67XX.h>
#include "globals.h"

// Global loop service instance
std::unique_ptr<ILoopService> loopService;

void setup()
{
  initDependencies();
  
  Serial.begin(115200);
  Serial.println("System initialized.");

  // settings.begin();
  // settings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);

  moduleSetupService.setupModule();
  // Create loop service according to module mode and module type
  loopService = moduleSetupService.getLoopService();
}

void loop()
{
  if (loopService) {
    loopService->loop();
  }

}
