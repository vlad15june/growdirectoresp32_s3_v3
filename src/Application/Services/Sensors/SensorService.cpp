#include "SensorService.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/SensorStateEnum.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
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
      lastUdpSendTime(0)
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

    auto activeSensors = sensorRepository->getActiveSensors();
    if (activeSensors.empty())
    {
        return;
    }

    if (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL)
    {
        logVerbose("SensorService: Reading all sensors");
        readAllSensors();
        lastSensorReadTime = currentTime;
    }

    if (hybridApiService && (currentTime - lastSensorUpdateTime >= SENSOR_UPDATE_INTERVAL))
    {
        logVerbose("SensorService: Sending sensor update to server");
        UpdateDto updateDto = createSensorUpdateDto();

        hybridApiService->sendModuleUpdateData(updateDto);
        lastSensorUpdateTime = currentTime;
    }

    if (udpManager && (currentTime - lastUdpSendTime >= UDP_SEND_INTERVAL))
    {
        logVerbose("SensorService: Sending sensor update via UDP");
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
            std::string values;
            for (const auto &pair : channelValues)
            {
                sensorValueModel->setChannelValue(sensor.uid, pair.first, pair.second);
                std::string ch = pair.first;
                size_t dot = ch.find_last_of('.');
                if (dot != std::string::npos)
                {
                    ch = ch.substr(dot + 1);
                }
                char item[48];
                snprintf(item, sizeof(item), "%s=%.2f ", ch.c_str(), pair.second);
                values += item;
            }

            // Computed VPD channel: needs both sources from THIS read, otherwise the cached
            // value is left as it is - same as any other channel that was not read this time
            uint8_t tempCh = 0;
            uint8_t humCh = 0;
            uint8_t vpdCh = 0;
            if (VpdChannelUseCase::findAirPair(sensor.modbusRegisterConfigs, tempCh, humCh) &&
                VpdChannelUseCase::vpdChannelNumber(sensor.modbusRegisterConfigs, vpdCh))
            {
                auto tempIt = channelValues.find(VpdChannelUseCase::channelUid(sensor.uid, tempCh));
                auto humIt = channelValues.find(VpdChannelUseCase::channelUid(sensor.uid, humCh));

                if (tempIt != channelValues.end() && humIt != channelValues.end())
                {
                    std::string vpdUid = VpdChannelUseCase::channelUid(sensor.uid, vpdCh);
                    float vpd = VpdChannelUseCase::compute(tempIt->second, humIt->second);
                    sensorValueModel->setChannelValue(sensor.uid, vpdUid, vpd);
                    log("SensorService: VPD uid=%s t=%.2f rh=%.2f vpd=%.3f",
                        vpdUid.c_str(), tempIt->second, humIt->second, vpd);
                }
                else
                {
                    log("SensorService: VPD_SKIP uid=%s reason=partial_read", sensor.uid.c_str());
                }
            }

            sensorValueModel->setLastReadTime(sensor.uid, currentTime);
            sensorRepository->updateSensorStatus(sensor.uid.c_str(), true, false);
            log("SensorService: Read %s addr=%d %s: %s", sensor.model.c_str(), sensor.modbusDeviceAddress, sensor.uid.c_str(), values.c_str());
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
            sensorValueModel->setLastReadTime(sensor.uid, currentTime);
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
    logVerbose("SensorService: Found %zu Modbus sensors", modbusSensors.size());
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

        unsigned long now = arduinoAdapter->millis();
        RecoveryPhase phaseBefore = _recovery.getPhase(sensor.pin);
        RecoveryAction action = _recovery.decide(sensor.pin, now, sensor.uid);
        RecoveryPhase phaseAfter = _recovery.getPhase(sensor.pin);

        if (action == RecoveryAction::POWER_OFF)
        {
            sensorReader->setModbusPortPower(sensor.pin, false);
            if (phaseBefore == RecoveryPhase::POLLING)
                log("SensorService: RECOVERY retry port=%d uid=%s", sensor.pin, sensor.uid.c_str());
            else
                log("SensorService: RECOVERY power_off port=%d uid=%s", sensor.pin, sensor.uid.c_str());
            _recovery.observe(sensor.uid, sensor.isError, now);
            continue;
        }

        if (action == RecoveryAction::POWER_ON)
        {
            sensorReader->setModbusPortPower(sensor.pin, true);
            log("SensorService: RECOVERY power_on port=%d uid=%s", sensor.pin, sensor.uid.c_str());
            _recovery.observe(sensor.uid, sensor.isError, now);
            continue;
        }

        if (phaseBefore == RecoveryPhase::POWER_ON_PHASE)
            log("SensorService: RECOVERY polling_start port=%d uid=%s", sensor.pin, sensor.uid.c_str());

        logVerbose("SensorService: Reading Modbus sensor model %s address: %d",
            sensor.model.c_str(), sensor.modbusDeviceAddress);

        bool readOk = readSensor(sensor);
        if (!readOk)
        {
            allSuccess = false;
        }
        _recovery.observe(sensor.uid, !readOk, now);

        if (phaseBefore == RecoveryPhase::POLLING && phaseAfter == RecoveryPhase::NORMAL)
            log("SensorService: RECOVERY recovered port=%d uid=%s", sensor.pin, sensor.uid.c_str());
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
        else if (sensor.isMultiChannel() && sensor.isError)
        {
            // No stored channel values (e.g., sensor disconnected before first successful read after reboot).
            // Reconstruct channel UIDs from register configs and report each sub-sensor as disconnected.
            for (const auto &regConfig : sensor.modbusRegisterConfigs)
            {
                SensorDto sensorDto;
                sensorDto.uid = sensor.uid + "." + (regConfig.channel < 10 ? "0" : "") + std::to_string(regConfig.channel);
                sensorDto.value = 0.0f;
                sensorDto.state = getSensorStateName(SENSOR_STATE_DISCONNECTED);
                dto.sensors.push_back(sensorDto);
            }
            for (const auto &regConfig : sensor.i2cRegisterConfigs)
            {
                SensorDto sensorDto;
                sensorDto.uid = sensor.uid + "." + (regConfig.channel < 10 ? "0" : "") + std::to_string(regConfig.channel);
                sensorDto.value = 0.0f;
                sensorDto.state = getSensorStateName(SENSOR_STATE_DISCONNECTED);
                dto.sensors.push_back(sensorDto);
            }

            // The computed VPD channel has no register config - without this it would silently
            // disappear from the packet instead of being reported as disconnected
            uint8_t vpdCh = 0;
            if (VpdChannelUseCase::vpdChannelNumber(sensor.modbusRegisterConfigs, vpdCh))
            {
                SensorDto sensorDto;
                sensorDto.uid = VpdChannelUseCase::channelUid(sensor.uid, vpdCh);
                sensorDto.value = 0.0f;
                sensorDto.state = getSensorStateName(SENSOR_STATE_DISCONNECTED);
                dto.sensors.push_back(sensorDto);
                log("SensorService: VPD_DISCONNECTED uid=%s", sensorDto.uid.c_str());
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
            logVerbose("SensorService: Successfully sent sensor data via UDP multicast to %s:%d", multicastIP, port);
        }
        else
        {
            log("SensorService: Failed to send sensor data via UDP multicast");

            // Fallback to broadcast
            if (sender->sendBroadcast(jsonString.c_str(), port))
            {
                logVerbose("SensorService: Successfully sent sensor data via UDP broadcast on port %d", port);
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
