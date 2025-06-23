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

void createTestSensorTypes() {
  // Check if sensor types already exist
  std::vector<SensorType> existingTypes = sensorTypeRepository.getAll();
  Serial.printf("Found %d existing sensor types\n", existingTypes.size());
  
  // Check if temp_humidity_modbus already exists
  bool tempHumidityExists = false;
  for (const auto& type : existingTypes) {
    if (type.name == "temp_humidity_modbus") {
      tempHumidityExists = true;
      Serial.printf("Temperature/Humidity sensor type already exists (ID: %d)\n", type.id);
      break;
    }
  }
  
  if (!tempHumidityExists) {
    // Create Temperature/Humidity Modbus sensor type
    SensorType tempHumiditySensorType;
    tempHumiditySensorType.id = 1;
    tempHumiditySensorType.name = "temp_humidity_modbus";
    tempHumiditySensorType.displayName = "Temperature & Humidity Sensor (Modbus)";
    tempHumiditySensorType.isAnalog = false;
    tempHumiditySensorType.canCalibrate = true;
    tempHumiditySensorType.validCalibrationPeriodDays = 365;
    tempHumiditySensorType.calibrationTimeInSeconds = 30;
    tempHumiditySensorType.minValue = -40.0f;
    tempHumiditySensorType.maxValue = 80.0f;
    tempHumiditySensorType.limitMinValue = -50.0f;
    tempHumiditySensorType.limitMaxValue = 100.0f;
    tempHumiditySensorType.unit = "°C/%RH";
    tempHumiditySensorType.step = 0.1f;
    tempHumiditySensorType.precisionPercent = 2.0f;
    tempHumiditySensorType.precisionValue = 0.5f;
    tempHumiditySensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    tempHumiditySensorType.modbusDeviceAddress = 1;
    tempHumiditySensorType.baudRate = 9600;

    // Create Modbus register configurations for temperature and humidity
    ModbusRegisterConfig tempConfig;
    tempConfig.functionCode = 3;
    tempConfig.startRegister = 0;
    tempConfig.numRegisters = 1;
    tempConfig.multiplier = 0.1f;
    tempConfig.offset = 0.0f;
    tempConfig.channelName = "temperature";
    tempConfig.channel = 0;
    tempHumiditySensorType.modbusRegisterConfig.push_back(tempConfig);

    ModbusRegisterConfig humidityConfig;
    humidityConfig.functionCode = 3;
    humidityConfig.startRegister = 1;
    humidityConfig.numRegisters = 1;
    humidityConfig.multiplier = 0.1f;
    humidityConfig.offset = 0.0f;
    humidityConfig.channelName = "humidity";
    humidityConfig.channel = 1;
    tempHumiditySensorType.modbusRegisterConfig.push_back(humidityConfig);

    // Save to repository
    bool saved = sensorTypeRepository.save(tempHumiditySensorType);
    Serial.printf("Temperature/Humidity sensor type created: %s\n", saved ? "SUCCESS" : "FAILED");
  }

  // Print final summary
  std::vector<SensorType> finalTypes = sensorTypeRepository.getAll();
  Serial.printf("Total sensor types in repository: %d\n", finalTypes.size());
  
  for (const auto& type : finalTypes) {
    Serial.printf("- ID: %d, Name: %s, Protocol: %s\n", 
                  type.id, type.name.c_str(), type.getProtocolString().c_str());
  }
}

void setup()
{
  initDependencies();
  
  Serial.begin(115200);
  Serial.println("System initialized.");
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());

  // settings.begin();
  // settings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);

  // Create and store test sensor types
  createTestSensorTypes();

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
