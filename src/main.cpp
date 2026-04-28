#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <Wire.h>
#include <Update.h>
#include <PubSubClient.h>
#include <T67XX.h>
#include "globals.h"
#include "Infrastructure/Adapters/ExternalWDT/ExternalWDT.h"

// Global loop service instance
std::unique_ptr<ILoopService> loopService;

// MQTT background task handle
TaskHandle_t mqttTaskHandle = nullptr;
// Button update task handle
TaskHandle_t buttonTaskHandle = nullptr;

void mqttTask(void *param)
{
  PubSubClient *mqttClient = (PubSubClient *)param;
  
  while (1)
  {
    if (WiFi.isConnected())
    {
      if (mqttClient->connected())
      {
        mqttClient->loop();
      }
    }
    delay(50);
  }
}

void buttonUpdateTask(void *param)
{
  ButtonAdapter *buttonAdapter = (ButtonAdapter *)param;
  
  while (1)
  {
    buttonAdapter->updateButtonStates();
    delay(50); // Update button states every 50ms
  }
}

void createTestSensorTypes()
{
  // Check if sensor types already exist
  std::vector<SensorType> existingTypes = sensorTypeRepository.getAll();
  Serial.printf("Found %d existing sensor types\n", existingTypes.size());

  // Check if RY-WS301 already exists
  bool ryWs301Exists = false;
  for (const auto &type : existingTypes)
  {
    if (type.name == "ry_ws301")
    {
      ryWs301Exists = true;
      Serial.printf("RY-WS301 sensor type already exists (UID: %s)\n", type.uid.c_str());
      break;
    }
  }

  if (!ryWs301Exists)
  {
    // Create RY-WS301 sensor type according to new structure
    SensorType ryWs301SensorType;
    ryWs301SensorType.uid = "4719a986-a300-48d5-bd1f-e34a1329798a";
    ryWs301SensorType.name = "ry_ws301";
    ryWs301SensorType.displayName = "RY-WS301 Environmental Sensor";
    ryWs301SensorType.canScan = true;
    ryWs301SensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    ryWs301SensorType.isSupported = true;
    ryWs301SensorType.modbusDeviceAddress = 255;
    ryWs301SensorType.baudRate = 9600;
    ryWs301SensorType.id = 1;

    SubSensorType airTempType;
    airTempType.name = "air_temp";
    airTempType.displayName = "Air temperature";
    airTempType.isAnalog = false;
    airTempType.canCalibrate = false;
    airTempType.validCalibrationPeriodDays = 0;
    airTempType.calibrationTimeInSeconds = 0;
    airTempType.minValue = 15.0f;
    airTempType.maxValue = 35.0f;
    airTempType.limitMinValue = -40.0f;
    airTempType.limitMaxValue = 75.0f;
    airTempType.unit = "°C";
    airTempType.step = 1.0f;
    airTempType.precisionPercent = 5.0f;
    airTempType.precisionValue = 5.8f;

    SubSensorType humidityType;
    humidityType.name = "humidity";
    humidityType.displayName = "Air humidity";
    humidityType.isAnalog = false;
    humidityType.canCalibrate = false;
    humidityType.validCalibrationPeriodDays = 0;
    humidityType.calibrationTimeInSeconds = 0;
    humidityType.minValue = 45.0f;
    humidityType.maxValue = 60.0f;
    humidityType.limitMinValue = 0.0f;
    humidityType.limitMaxValue = 100.0f;
    humidityType.unit = "%";
    humidityType.step = 1.0f;
    humidityType.precisionPercent = 5.0f;
    humidityType.precisionValue = 5.0f;

    ryWs301SensorType.sensorTypes.push_back(airTempType);
    ryWs301SensorType.sensorTypes.push_back(humidityType);

    ModbusRegisterConfig tempConfig;
    tempConfig.functionCode = 3;
    tempConfig.startRegister = 0;
    tempConfig.numRegisters = 1;
    tempConfig.multiplier = 0.01f;
    tempConfig.offset = -40.0f;
    tempConfig.channelName = "air_temp";
    tempConfig.channel = 1;
    tempConfig.sensorType = "air_temp";
    tempConfig.canConnectMinValue = 10.0f;
    tempConfig.canConnectMaxValue = 40.0f;
    ryWs301SensorType.modbusRegisterConfig.push_back(tempConfig);

    ModbusRegisterConfig humidityConfig;
    humidityConfig.functionCode = 3;
    humidityConfig.startRegister = 1;
    humidityConfig.numRegisters = 1;
    humidityConfig.multiplier = 0.01f;
    humidityConfig.offset = 0.0f;
    humidityConfig.channelName = "humidity";
    humidityConfig.channel = 2;
    humidityConfig.sensorType = "humidity";
    humidityConfig.canConnectMinValue = 30.0f;
    humidityConfig.canConnectMaxValue = 70.0f;
    ryWs301SensorType.modbusRegisterConfig.push_back(humidityConfig);

    // ModbusRegisterConfig pressureConfig;
    // pressureConfig.functionCode = 3;
    // pressureConfig.startRegister = 2;
    // pressureConfig.numRegisters = 1;
    // pressureConfig.multiplier = 0.1f;
    // pressureConfig.offset = 0.0f;
    // pressureConfig.channelName = "pressure";
    // pressureConfig.channel = 3;
    // pressureConfig.sensorType = "pressure";
    // pressureConfig.canConnectMinValue = 300.0f;
    // pressureConfig.canConnectMaxValue = 1100.0f;
    // ryWs301SensorType.modbusRegisterConfig.push_back(pressureConfig);

    bool saved = sensorTypeRepository.save(ryWs301SensorType);
    Serial.printf("RY-WS301 sensor type created: %s\n", saved ? "SUCCESS" : "FAILED");
  }

  bool mp508cExists = false;
  for (const auto &type : existingTypes)
  {
    if (type.name == "mp-508c")
    {
      mp508cExists = true;
      Serial.printf("MP-508C sensor type already exists (UID: %s)\n", type.uid.c_str());
      break;
    }
  }

  if (!mp508cExists)
  {
    SensorType mp508cSensorType;
    mp508cSensorType.uid = "144f6c6d-d210-44bf-a9c3-23c5f26472fe";
    mp508cSensorType.name = "mp-508c";
    mp508cSensorType.displayName = "MP-508C Soil Temperature and Humidity";
    mp508cSensorType.canScan = true;
    mp508cSensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    mp508cSensorType.isSupported = true;
    mp508cSensorType.modbusDeviceAddress = 1;
    mp508cSensorType.baudRate = 9600;
    mp508cSensorType.id = 2;

    SubSensorType soilMoistureType;
    soilMoistureType.name = "soil_moisture";
    soilMoistureType.displayName = "Soil Moisture";
    soilMoistureType.isAnalog = true;
    soilMoistureType.canCalibrate = true;
    soilMoistureType.validCalibrationPeriodDays = 90;
    soilMoistureType.calibrationTimeInSeconds = 120;
    soilMoistureType.minValue = 45.0f;
    soilMoistureType.maxValue = 65.0f;
    soilMoistureType.limitMinValue = 0.0f;
    soilMoistureType.limitMaxValue = 100.0f;
    soilMoistureType.unit = "%";
    soilMoistureType.step = 1.0f;
    soilMoistureType.precisionPercent = 10.0f;
    soilMoistureType.precisionValue = 10.0f;

    mp508cSensorType.sensorTypes.push_back(soilMoistureType);

    ModbusRegisterConfig soilMoistureConfig;
    soilMoistureConfig.functionCode = 3;
    soilMoistureConfig.startRegister = 0;
    soilMoistureConfig.numRegisters = 1;
    soilMoistureConfig.multiplier = 0.1f;
    soilMoistureConfig.offset = 0.0f;
    soilMoistureConfig.channelName = "soil_moisture";
    soilMoistureConfig.channel = 1;
    soilMoistureConfig.sensorType = "soil_moisture";
    soilMoistureConfig.canConnectMinValue = 0.0f;
    soilMoistureConfig.canConnectMaxValue = 50.0f;
    mp508cSensorType.modbusRegisterConfig.push_back(soilMoistureConfig);

    ModbusRegisterConfig soilTempConfig;
    soilTempConfig.functionCode = 3;
    soilTempConfig.startRegister = 1;
    soilTempConfig.numRegisters = 1;
    soilTempConfig.multiplier = 0.01f;
    soilTempConfig.offset = -20.0f;
    soilTempConfig.channelName = "soil_temp";
    soilTempConfig.channel = 2;
    soilTempConfig.sensorType = "soil_temp";
    soilTempConfig.canConnectMinValue = 10.0f;
    soilTempConfig.canConnectMaxValue = 30.0f;
    mp508cSensorType.modbusRegisterConfig.push_back(soilTempConfig);

    bool saved = sensorTypeRepository.save(mp508cSensorType);
    Serial.printf("MP-508C sensor type created: %s\n", saved ? "SUCCESS" : "FAILED");
  }

  std::vector<SensorType> finalTypes = sensorTypeRepository.getAll();
  Serial.printf("Total sensor types in repository: %d\n", finalTypes.size());

  for (const auto &type : finalTypes)
  {
    Serial.printf("Sensor Type Details:\n");
    Serial.printf("  UID: %s\n", type.uid.c_str());
    Serial.printf("  Name: %s\n", type.name.c_str());
    Serial.printf("  Display Name: %s\n", type.displayName.c_str());
    Serial.printf("  Can Scan: %s\n", type.canScan ? "true" : "false");
    Serial.printf("  Protocol: %s\n", type.getProtocolString().c_str());
    Serial.printf("  Is Supported: %s\n", type.isSupported ? "true" : "false");
    Serial.printf("  I2C Address: %s\n", type.i2cAddress.c_str());
    Serial.printf("  Chip Type: %s\n", type.chipType.c_str());
    Serial.printf("  Modbus Device Address: %d\n", type.modbusDeviceAddress);
    Serial.printf("  Baud Rate: %d\n", type.baudRate);
    Serial.printf("  Sub-Sensor Types: %d\n", type.sensorTypes.size());
    
    for (size_t i = 0; i < type.sensorTypes.size(); i++)
    {
      const auto &subType = type.sensorTypes[i];
      Serial.printf("    SubSensor %d:\n", (int)i + 1);
      Serial.printf("      Name: %s\n", subType.name.c_str());
      Serial.printf("      Display Name: %s\n", subType.displayName.c_str());
      Serial.printf("      Is Analog: %s\n", subType.isAnalog ? "true" : "false");
      Serial.printf("      Can Calibrate: %s\n", subType.canCalibrate ? "true" : "false");
      Serial.printf("      Min/Max Value: %.1f/%.1f\n", subType.minValue, subType.maxValue);
      Serial.printf("      Unit: %s\n", subType.unit.c_str());
    }
    
    Serial.printf("  Modbus Register Configs: %d\n", type.modbusRegisterConfig.size());
    for (size_t i = 0; i < type.modbusRegisterConfig.size(); i++)
    {
      const auto &config = type.modbusRegisterConfig[i];
      Serial.printf("    Config %d: Function=%d, Start=%d, Num=%d, Multiplier=%.3f, Offset=%.3f, Channel=%s, ChannelNum=%d, CanConnectMin=%.1f, CanConnectMax=%.1f\n",
                    i, config.functionCode, config.startRegister, config.numRegisters,
                    config.multiplier, config.offset, config.channelName.c_str(), config.channel,
                    config.canConnectMinValue, config.canConnectMaxValue);
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

  // Initialize and start External WDT (BD87A28FVM-TR)
  ExternalWDT& wdt = ExternalWDT::getInstance();
  if (wdt.init() == ESP_OK && wdt.enable() == ESP_OK && wdt.startTask() == ESP_OK)
  {
    Serial.println("External WDT started");
  }
  else
  {
    Serial.println("External WDT failed to start");
  }

  // settings.begin();
  // settings.setConfigured(ModuleModeEnum::USER_CAN_CONFIGURE);

  // settings.begin();
  // settings.setConfigured(ModuleModeEnum::AP_TEST_MODE);

  // settings.begin();
  // settings.setSsid("VPN_net");

  settings.begin();
  uint8_t bootMode = settings.getIfConfigured();
  bool needsSpiffs = (bootMode == ModuleModeEnum::USER_CAN_CONFIGURE ||
                      bootMode == ModuleModeEnum::USER_CAN_RE_CONFIGURE ||
                      bootMode == ModuleModeEnum::CONFIGURED_DONE ||
                      bootMode == ModuleModeEnum::CONFIGURED_NEED_RESTART);
  spiffsAdapter.initSPIFFS(!needsSpiffs);
  // spiffsAdapter.RemoveFile("/data/sensorTypes.json");
  // spiffsAdapter.RemoveFile("/data/sensors.json");
  // spiffsAdapter.RemoveFile("/sensors/sensors.json");
  // spiffsAdapter.RemoveFile("/wifiNetworks.json");

  if (settings.getNeedClearSpiffs() == NEED_CLEAR_SPIFFS_MARKER) {
    Serial.println("needClearSpiffs flag set — removing SPIFFS files");
    spiffsAdapter.RemoveFile("/data/sensorTypes.json");
    spiffsAdapter.RemoveFile("/data/sensors.json");
    spiffsAdapter.RemoveFile("/sensors/sensors.json");
    spiffsAdapter.RemoveFile("/wifiNetworks.json");
    settings.setNeedClearSpiffs(0);
    Serial.println("SPIFFS files removed, needClearSpiffs flag cleared");
  }

  // Create and store test sensor types
  // createTestSensorTypes();

  moduleSetupService.setupModule();
  
  // Start MQTT background task on Core 1
  xTaskCreatePinnedToCore(
    mqttTask,
    "MQTT",
    4096,
    &mqttClient,
    2,
    &mqttTaskHandle,
    1
  );

  //TODO: setup background task for led on core 0

  // Start button update task on Core 0
  xTaskCreatePinnedToCore(
    buttonUpdateTask,
    "ButtonUpdate",
    2048,
    &buttonAdapter,
    1,
    &buttonTaskHandle,
    0
  );

  // Create loop service according to module mode and module type
  loopService = moduleSetupService.getLoopService();
}

void loop()
{
  if (loopService)
  {
    loopService->loop();
  }
}
