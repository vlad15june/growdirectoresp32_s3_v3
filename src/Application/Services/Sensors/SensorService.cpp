#include "SensorService.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/SensorStateEnum.h"
#include <cstring>

SensorService::SensorService(IModuleConfig *moduleConfig,
                             ModuleStructure *moduleStructure,
                             ISensorRepository *sensorRepository,
                             ISensorReader *sensorReader,
                             ISensorValueModel *sensorValueModel,
                             INTPTimeAdapter *timeAdapter,
                             IArduinoAdapter *arduinoAdapter,
                             IHybridAPIService *hybridApiService,
                             ILogger *logger,
                             IDebugSystem *debugSystem,
                             IUdpManager *udpManager,
                             IJsonConverter *jsonConverter,
                             IMqttManager *mqttManager)
    : moduleConfig(moduleConfig),
      moduleStructure(moduleStructure),
      sensorRepository(sensorRepository),
      sensorReader(sensorReader),
      sensorValueModel(sensorValueModel),
      timeAdapter(timeAdapter),
      arduinoAdapter(arduinoAdapter),
      hybridApiService(hybridApiService),
      logger(logger),
      debugSystem(debugSystem),
      udpManager(udpManager),
      jsonConverter(jsonConverter),
      mqttManager(mqttManager),
      lastSensorReadTime(0),
      lastSensorUpdateTime(0),
      lastUdpSendTime(0),
      rebootScheduledTime(0),
      rebootScheduled(false)
{
}

SensorService::~SensorService()
{
}

bool SensorService::init()
{
    log("SensorService: Initializing");

    if (!sensorRepository)
    {
        log("SensorService: Sensor repository is null");
        return false;
    }

    if (!sensorReader)
    {
        log("SensorService: Sensor reader is null");
        return false;
    }

    if (!sensorValueModel)
    {
        log("SensorService: Sensor value model is null");
        return false;
    }

    if (!sensorValueModel->init())
    {
        log("SensorService: Failed to initialize sensor value model");
        return false;
    }

    lastSensorReadTime = arduinoAdapter->millis();
    lastSensorUpdateTime = arduinoAdapter->millis();

    log("SensorService: Initialization successful");
    return true;
}

void SensorService::loop()
{
    unsigned long currentTime = arduinoAdapter->millis();

    // Check if sensor scan was never performed and schedule reboot if needed
    if (!rebootScheduled && moduleConfig)
    {
        ISettings *settings = moduleConfig->getSettings();
        if (settings && settings->getWasSensorScanPerformed() == 0)
        {
            log("SensorService: Sensor scan was never performed, scheduling reboot in 5 seconds");
            rebootScheduledTime = currentTime;
            rebootScheduled = true;

            // Enable sensor scanning on next boot
            settings->setNoScanSensors(0);
        }
    }

    // Execute reboot if scheduled and time has elapsed
    if (rebootScheduled && (currentTime - rebootScheduledTime >= REBOOT_DELAY_FOR_SCAN))
    {
        log("SensorService: Rebooting module to perform sensor scan");
        if (moduleConfig)
        {
            ISettings *settings = moduleConfig->getSettings();
            if (settings)
            {
                settings->setBootType(BOOT_TYPE_TECHNICAL);
            }
            moduleConfig->setRequestedModuleMode(CONFIGURED_NEED_RESTART);
        }
        return; // Module will restart via main loop
    }

    auto activeSensors = sensorRepository->getActiveSensors();
    if (activeSensors.empty())
    {
        return;
    }

    if (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL)
    {
        log("SensorService: Reading all sensors");
        readAllSensors();
        lastSensorReadTime = currentTime;
    }

    if (hybridApiService && (currentTime - lastSensorUpdateTime >= SENSOR_UPDATE_INTERVAL))
    {
        log("SensorService: Sending sensor update to server");
        UpdateDto updateDto = createSensorUpdateDto();

        hybridApiService->sendModuleUpdateData(updateDto);
        lastSensorUpdateTime = currentTime;
    }

    if (udpManager && (currentTime - lastUdpSendTime >= UDP_SEND_INTERVAL))
    {
        log("SensorService: Sending sensor update via UDP");
        UpdateDto updateDto = createSensorUpdateDto();

        sendUpdateViaUdp(updateDto);
        lastUdpSendTime = currentTime;
    }
}

bool SensorService::readAllSensors()
{
    if (!sensorRepository)
    {
        return false;
    }

    bool success = true;

    if (!readAnalogSensors())
    {
        success = false;
    }

    if (!readI2CSensors())
    {
        success = false;
    }

    if (!readModbusSensors())
    {
        success = false;
    }

    return success;
}

bool SensorService::readSensor(SensorEntity sensor)
{
    if (!sensorRepository || !sensorReader)
    {
        return false;
    }

    if (!sensor.isActive)
    {
        log("SensorService: Sensor %s is not active", sensor.uid.c_str());
        return false;
    }

    uint32_t currentTime = arduinoAdapter->millis();

    if (sensor.isMultiChannel())
    {
        // Mark sensor as multi-channel in the value model
        sensorValueModel->setSensorMultiChannel(sensor.uid, true);

        std::map<std::string, float> channelValues;
        if (sensorReader->readMultiChannelSensor(sensor, channelValues))
        {
            for (const auto &pair : channelValues)
            {
                sensorValueModel->setChannelValue(sensor.uid, pair.first, pair.second);
            }
            sensorRepository->updateSensorStatus(sensor.uid.c_str(), true, false);
            log("SensorService: Read multi-channel sensor %s", sensor.uid.c_str());
            return true;
        }
        else
        {
            // Keep sensor active but mark as error when read fails
            sensorRepository->updateSensorStatus(sensor.uid.c_str(), sensor.isActive, true);
            log("SensorService: Failed to read multi-channel sensor %s, marking as error", sensor.uid.c_str());
            return false;
        }
    }
    else
    {
        // Mark sensor as single-channel in the value model
        sensorValueModel->setSensorMultiChannel(sensor.uid, false);

        float value;
        if (sensorReader->readSensor(sensor, value))
        {
            // For single-channel sensors, use setSensorValue
            sensorValueModel->setSensorValue(sensor.uid, value);
            sensorRepository->updateSensorStatus(sensor.uid.c_str(), true, false);
            log("SensorService: Read sensor %s, value: %.2f", sensor.uid.c_str(), value);
            return true;
        }
        else
        {
            // Keep sensor active but mark as error when read fails
            sensorRepository->updateSensorStatus(sensor.uid.c_str(), sensor.isActive, true);
            log("SensorService: Failed to read sensor %s, marking as error", sensor.uid.c_str());
            return false;
        }
    }
}

bool SensorService::readAnalogSensors()
{
    auto analogSensors = sensorRepository->getSensorsByProtocol("ANALOG");
    if (analogSensors.empty())
    {
        return true; // No sensors to read, consider it successful
    }

    bool allSuccess = true;

    for (const auto &sensor : analogSensors)
    {
        if (!sensor.isActive)
            continue;

        if (!readSensor(sensor))
        {
            allSuccess = false;
        }
    }

    return allSuccess;
}

bool SensorService::readI2CSensors()
{
    auto i2cSensors = sensorRepository->getI2CSensors();
    if (i2cSensors.empty())
    {
        return true; // No sensors to read, consider it successful
    }

    bool allSuccess = true;

    for (const auto &sensor : i2cSensors)
    {
        if (!sensor.isActive)
            continue;

        if (!readSensor(sensor))
        {
            allSuccess = false;
        }
    }

    return allSuccess;
}

bool SensorService::readModbusSensors()
{
    auto modbusSensors = sensorRepository->getModbusSensors();
    log("SensorService: Found %zu Modbus sensors", modbusSensors.size());
    if (modbusSensors.empty())
    {
        return true; // No sensors to read, consider it successful
    }

    bool allSuccess = true;

    for (const auto &sensor : modbusSensors)
    {
        mqttManager->loop(); // NOTE: not good solution - Keep MQTT responsive during potentially long operations

        if (!sensor.isActive)
            continue;

        log("SensorService: Reading Modbus sensor model %s address: %d",
            sensor.model.c_str(), sensor.modbusDeviceAddress);

        if (!readSensor(sensor))
        {
            allSuccess = false;
        }
    }

    return allSuccess;
}

UpdateDto SensorService::createSensorUpdateDto()
{
    UpdateDto dto;

    dto.growdirectorUid = moduleConfig->getModuleUid();
    dto.time = timeAdapter->getFormattedTime();
    dto.millis = arduinoAdapter->millis();

    ISettings *settings = moduleConfig->getSettings();
    if (settings)
    {
        char accountUidBuffer[256];
        settings->getAccountUid(accountUidBuffer);
        dto.accountUid = accountUidBuffer;

        char userEmailBuffer[256];
        settings->getUser(userEmailBuffer);
        dto.email = userEmailBuffer;
    }

    auto sensors = sensorRepository->getActiveSensors();
    for (const auto &sensor : sensors)
    {
        if (sensor.isMultiChannel() && sensorValueModel->hasChannelValues(sensor.uid))
        {
            auto channelValues = sensorValueModel->getChannelValues(sensor.uid);
            for (const auto &channelValue : channelValues)
            {
                SensorDto sensorDto;
                sensorDto.uid = channelValue.first;
                sensorDto.value = sensor.isError ? 0.0f : channelValue.second;
                sensorDto.state = sensor.isError ? getSensorStateName(SENSOR_STATE_DISCONNECTED) : getSensorStateName(SENSOR_STATE_ONLINE);

                dto.sensors.push_back(sensorDto);
            }
        }
        else
        {
            SensorDto sensorDto;
            sensorDto.uid = sensor.uid;
            sensorDto.value = sensor.isError ? 0.0f : sensorValueModel->getSensorValue(sensor.uid);
            sensorDto.state = sensor.isError ? getSensorStateName(SENSOR_STATE_DISCONNECTED) : getSensorStateName(SENSOR_STATE_ONLINE);

            dto.sensors.push_back(sensorDto);
        }
    }

    return dto;
}

void SensorService::sendUpdateViaUdp(const UpdateDto &updateDto)
{
    if (!udpManager || !jsonConverter)
    {
        log("SensorService: UDP manager or JSON converter not available");
        return;
    }

    if (!udpManager->isInitialized())
    {
        log("SensorService: UDP manager not initialized");
        return;
    }

    // Convert UpdateDto to JSON string
    std::string jsonString = jsonConverter->updateToJson(updateDto);
    if (jsonString.empty())
    {
        log("SensorService: Failed to convert UpdateDto to JSON");
        return;
    }

    // Send via UDP broadcast
    IUdpSender *sender = udpManager->getSender();
    if (sender)
    {
        // Use multicast for sensor data broadcast (port 1234 as default)
        const char *multicastIP = udpManager->getMulticastIP();
        uint16_t port = udpManager->getLocalPort();

        if (sender->sendMulticast(jsonString.c_str(), multicastIP, port))
        {
            log("SensorService: Successfully sent sensor data via UDP multicast to %s:%d", multicastIP, port);
        }
        else
        {
            log("SensorService: Failed to send sensor data via UDP multicast");

            // Fallback to broadcast
            if (sender->sendBroadcast(jsonString.c_str(), port))
            {
                log("SensorService: Successfully sent sensor data via UDP broadcast on port %d", port);
            }
            else
            {
                log("SensorService: Failed to send sensor data via UDP broadcast");
            }
        }
    }
    else
    {
        log("SensorService: UDP sender not available");
    }
}
