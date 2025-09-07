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
    tempHumiditySensorType.canScan = true;
    tempHumiditySensorType.isSupported = true;

    tempHumiditySensorType.name = "temp_humidity_modbus";
    tempHumiditySensorType.displayName = "Temperature & Humidity Sensor (Modbus)";
    tempHumiditySensorType.isAnalog = false;
    tempHumiditySensorType.canCalibrate = false;
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

    // Set protocol and Modbus parameters
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
    tempConfig.channelName = "air_temp";
    tempConfig.sensorType = "air_temp";
    tempConfig.channel = 1;
    tempConfig.canConnectMinValue = 10.0f;
    tempConfig.canConnectMaxValue = 40.0f;
    tempHumiditySensorType.modbusRegisterConfig.push_back(tempConfig);

    ModbusRegisterConfig humidityConfig;
    humidityConfig.functionCode = 3;
    humidityConfig.startRegister = 1;
    humidityConfig.numRegisters = 1;
    humidityConfig.multiplier = 0.1f;
    humidityConfig.offset = 0.0f;
    humidityConfig.channelName = "humidity";
    humidityConfig.sensorType = "humidity";
    humidityConfig.channel = 2;
    humidityConfig.canConnectMinValue = 30.0f;
    humidityConfig.canConnectMaxValue = 70.0f;
    tempHumiditySensorType.modbusRegisterConfig.push_back(humidityConfig);

    // Save to repository
    bool saved = sensorTypeRepository.save(tempHumiditySensorType);
    Serial.printf("Temperature/Humidity sensor type created: %s\n", saved ? "SUCCESS" : "FAILED");
  }

  // Check if RY-WS301 already exists
  bool ryWs301Exists = false;
  for (const auto& type : existingTypes) {
    if (type.name == "ry_ws301") {
      ryWs301Exists = true;
      Serial.printf("RY-WS301 sensor type already exists (ID: %d)\n", type.id);
      break;
    }
  }
  
  if (!ryWs301Exists) {
    // Create RY-WS301 sensor type
    SensorType ryWs301SensorType;
    ryWs301SensorType.id = 2;
    ryWs301SensorType.canScan = true;
    ryWs301SensorType.isSupported = true;

    ryWs301SensorType.name = "ry_ws301";
    ryWs301SensorType.displayName = "RY-WS301 Environmental Sensor";
    ryWs301SensorType.isAnalog = false;
    ryWs301SensorType.canCalibrate = true;
    ryWs301SensorType.validCalibrationPeriodDays = 365;
    ryWs301SensorType.calibrationTimeInSeconds = 30;
    ryWs301SensorType.minValue = -40.0f;
    ryWs301SensorType.maxValue = 80.0f;
    ryWs301SensorType.limitMinValue = -50.0f;
    ryWs301SensorType.limitMaxValue = 100.0f;
    ryWs301SensorType.unit = "°C/%RH/hPa";
    ryWs301SensorType.step = 0.1f;
    ryWs301SensorType.precisionPercent = 2.0f;
    ryWs301SensorType.precisionValue = 0.5f;

    // Set protocol and Modbus parameters
    ryWs301SensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    ryWs301SensorType.modbusDeviceAddress = 255; // 0xFF
    ryWs301SensorType.baudRate = 9600;

    // Temperature register config (register 0, formula: value/100-40)
    ModbusRegisterConfig tempConfig;
    tempConfig.functionCode = 3;
    tempConfig.startRegister = 0;
    tempConfig.numRegisters = 1;
    tempConfig.multiplier = 0.01f;
    tempConfig.offset = -40.0f;
    tempConfig.channelName = "air_temp";
    tempConfig.sensorType = "air_temp";
    tempConfig.channel = 1;
    tempConfig.canConnectMinValue = 10.0f;
    tempConfig.canConnectMaxValue = 40.0f;
    ryWs301SensorType.modbusRegisterConfig.push_back(tempConfig);

    // Humidity register config (register 1, formula: value/100)
    ModbusRegisterConfig humidityConfig;
    humidityConfig.functionCode = 3;
    humidityConfig.startRegister = 1;
    humidityConfig.numRegisters = 1;
    humidityConfig.multiplier = 0.01f;
    humidityConfig.offset = 0.0f;
    humidityConfig.channelName = "humidity";
    humidityConfig.sensorType = "humidity";
    humidityConfig.channel = 2;
    humidityConfig.canConnectMinValue = 30.0f;
    humidityConfig.canConnectMaxValue = 70.0f;
    ryWs301SensorType.modbusRegisterConfig.push_back(humidityConfig);

    // Pressure register config (register 2, formula: value/10)
    ModbusRegisterConfig pressureConfig;
    pressureConfig.functionCode = 3;
    pressureConfig.startRegister = 2;
    pressureConfig.numRegisters = 1;
    pressureConfig.multiplier = 0.1f;
    pressureConfig.offset = 0.0f;
    pressureConfig.channelName = "pressure";
    pressureConfig.sensorType = "pressure";
    pressureConfig.channel = 3;
    pressureConfig.canConnectMinValue = 300.0f;
    pressureConfig.canConnectMaxValue = 1100.0f;
    ryWs301SensorType.modbusRegisterConfig.push_back(pressureConfig);

    // Save to repository
    bool saved = sensorTypeRepository.save(ryWs301SensorType);
    Serial.printf("RY-WS301 sensor type created: %s\n", saved ? "SUCCESS" : "FAILED");
  }

  // Print final summary
  std::vector<SensorType> finalTypes = sensorTypeRepository.getAll();
  Serial.printf("Total sensor types in repository: %d\n", finalTypes.size());
  
  for (const auto& type : finalTypes) {
    Serial.printf("Sensor Type Details:\n");
    Serial.printf("  ID: %d\n", type.id);
    Serial.printf("  Name: %s\n", type.name.c_str());
    Serial.printf("  Display Name: %s\n", type.displayName.c_str());
    Serial.printf("  Is Analog: %s\n", type.isAnalog ? "true" : "false");
    Serial.printf("  Can Calibrate: %s\n", type.canCalibrate ? "true" : "false");
    Serial.printf("  Valid Calibration Period (days): %d\n", type.validCalibrationPeriodDays);
    Serial.printf("  Calibration Time (seconds): %d\n", type.calibrationTimeInSeconds);
    Serial.printf("  Min Value: %.2f\n", type.minValue);
    Serial.printf("  Max Value: %.2f\n", type.maxValue);
    Serial.printf("  Limit Min Value: %.2f\n", type.limitMinValue);
    Serial.printf("  Limit Max Value: %.2f\n", type.limitMaxValue);
    Serial.printf("  Unit: %s\n", type.unit.c_str());
    Serial.printf("  Step: %.3f\n", type.step);
    Serial.printf("  Precision Percent: %.2f\n", type.precisionPercent);
    Serial.printf("  Precision Value: %.3f\n", type.precisionValue);
    Serial.printf("  Protocol: %s\n", type.getProtocolString().c_str());
    Serial.printf("  Modbus Device Address: %d\n", type.modbusDeviceAddress);
    Serial.printf("  Baud Rate: %d\n", type.baudRate);
    Serial.printf("  Modbus Register Configs: %d\n", type.modbusRegisterConfig.size());
    for (size_t i = 0; i < type.modbusRegisterConfig.size(); i++) {
      const auto& config = type.modbusRegisterConfig[i];
      Serial.printf("    Config %d: Function=%d, Start=%d, Num=%d, Multiplier=%.3f, Offset=%.3f, Channel=%s, ChannelNum=%d\n",
                    i, config.functionCode, config.startRegister, config.numRegisters,
                    config.multiplier, config.offset, config.channelName.c_str(), config.channel);
    }
    Serial.println("---");
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

  // settings.begin();
  // settings.setConfigured(ModuleModeEnum::AP_TEST_MODE);

  spiffsAdapter.initSPIFFS();
  // spiffsAdapter.RemoveFile("/data/sensorTypes.json");
  // spiffsAdapter.RemoveFile("/data/sensors.json");
  // spiffsAdapter.RemoveFile("/sensors/sensors.json");
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
