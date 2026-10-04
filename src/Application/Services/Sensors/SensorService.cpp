#include "SensorService.h"
#include "Core/Domain/Enums/BootTypeEnum.h"
#include "Core/Domain/Enums/ModuleModeEnum.h"
#include "Core/Domain/Enums/SensorStateEnum.h"
#include <cstddef>
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
                             ISensorPlausibilityUseCase *plausibilityUseCase,
                             IVirtualSensorChannelsUseCase *virtualChannelsUseCase)
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
      virtualChannelsUseCase(virtualChannelsUseCase),
      plausibilityUseCase(plausibilityUseCase),
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

    // With a boot sequence the reads start on the first iteration and the update waits for its gate;
    // without one - or before it has ticked - the three timers below decide, from millis 0.
    const bool sequenced = (bootGates != nullptr) && bootGates->isSequencing();

    // A cached view by reference: on an iteration in which nothing changed, this line - once the
    // hottest allocation site in the firmware - costs a generation check and nothing else.
    const auto& activeSensors = sensorRepository->getActiveSensors();
    if (activeSensors.empty())
    {
        // A module with no sensor has no first reading to wait for.
        if (sequenced && firstReadingsState == FirstReadingsState::NOT_STARTED)
        {
            firstReadingsState = FirstReadingsState::SETTLED;
            firstReadingsLastAtMs = static_cast<uint32_t>(currentTime);
        }
        // Nor one whose last sensor went before its first outcome.
        forgetAwaitedSensorsNoLongerRead(std::vector<SensorEntity>());

        // Nothing configured any more. Say so, or the acquisition task keeps polling the last
        // snapshot it was given - for ever, since the line below is what would have updated it -
        // and its readings pile up against the cap with nobody draining them.
        if (acquisition != nullptr)
        {
            acquisition->setSensors(std::vector<SensorEntity>());
            applyAcquiredReadings();
        }
        return;
    }

    // Whatever the bus produced since the last iteration. Costs no bus time, so it runs every
    // iteration and not on the 15 s tick: a reading that waited for the tick would be up to
    // fifteen seconds older than the timestamp it carries.
    applyAcquiredReadings();

    const bool firstReadOfTheBoot = sequenced && firstReadingsState == FirstReadingsState::NOT_STARTED;
    if (firstReadOfTheBoot || (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL))
    {
        if (firstReadOfTheBoot)
        {
            beginAwaitingFirstReadings();
        }
        logVerbose("SensorService: Reading all sensors");
        readAllSensors();
        lastSensorReadTime = currentTime;
    }

    const bool updateIntervalOver = (currentTime - lastSensorUpdateTime >= SENSOR_UPDATE_INTERVAL);
    const bool telemetryAllowed = !sequenced || bootGates->allows(BootGate::TELEMETRY);
    if (sequenced && telemetrySent && updateIntervalOver && !telemetryAllowed && !telemetryHeld)
    {
        // The update this tick would have handed over - and the outbox would have kept until the link
        // was back - is held by the gate instead. It goes when the gate opens.
        telemetryHeld = true;
        telemetryHeldSinceMs = currentTime;
    }
    const bool updateDue = sequenced ? (telemetryAllowed && (!telemetrySent || updateIntervalOver))
                                     : updateIntervalOver;
    if ((outbox != nullptr || hybridApiService != nullptr) && updateDue)
    {
        logVerbose("SensorService: Sending sensor update to server");
        UpdateDto updateDto = createSensorUpdateDto();

        if (outbox != nullptr)
        {
            // A hand-off, not a send. The outcome is not consulted, and that is not an oversight
            // being carried forward: a sensor reading has no consequence to undo on this side.
            // Today's code ignores the result of the send for the same reason (the value simply
            // vanished if it did not arrive); the difference is that the loss is now counted, in
            // the outbox, instead of being invisible.
            outbox->submitSensorUpdate(updateDto);
        }
        else
        {
            hybridApiService->sendModuleUpdateData(updateDto);
        }

        // Restart the interval from the moment the call came back, not from the moment it was
        // made, as the diagnostic timer does. Through the outbox the two moments are
        // the same microsecond; without one, sendModuleUpdateData() is a synchronous HTTP call and
        // a stamp taken before it made a send outlasting the 15 s interval satisfy the condition
        // again on the very next iteration.
        lastSensorUpdateTime = arduinoAdapter->millis();
        if (telemetryHeld)
        {
            // Held, the interval keeps the phase it had: the next update is where it would have been
            // had the held tick handed over its update - so a return of the link sends as many
            // updates as an outage the gate did not see, the held one fresher.
            lastSensorUpdateTime -= (lastSensorUpdateTime - telemetryHeldSinceMs) % SENSOR_UPDATE_INTERVAL;
            telemetryHeld = false;
        }
        telemetrySent = true;
    }

    // The neighbours that listen to this copy write it into their own model and wait for nobody,
    // so it needs no server - only something to say, which the first outcomes are.
    const bool udpIntervalOver = (currentTime - lastUdpSendTime >= UDP_SEND_INTERVAL);
    const bool udpDue = sequenced
                            ? (firstReadingsState == FirstReadingsState::SETTLED && (!udpSent || udpIntervalOver))
                            : udpIntervalOver;
    if (udpManager && udpDue)
    {
        logVerbose("SensorService: Sending sensor update via UDP");
        UpdateDto updateDto = createSensorUpdateDto();

        sendUpdateViaUdp(updateDto);
        lastUdpSendTime = currentTime;
        udpSent = true;
    }
}

ISensorService::FirstReadings SensorService::firstReadings() const
{
    FirstReadings readings;
    readings.settled = (firstReadingsState == FirstReadingsState::SETTLED);
    readings.awaited = firstReadingsCount;
    readings.awaitedChannels = firstReadingsChannels;
    readings.outstanding = static_cast<uint32_t>(firstReadingsAwaited.size());
    readings.lastAtMs = firstReadingsLastAtMs;
    return readings;
}

void SensorService::giveUpOnTheFirstReadings()
{
    if (firstReadingsState == FirstReadingsState::SETTLED)
    {
        return;
    }
    firstReadingsAwaited.clear();
    firstReadingsState = FirstReadingsState::SETTLED;
    firstReadingsLastAtMs = static_cast<uint32_t>(arduinoAdapter->millis());
}

/**
 * @brief Note which ModBus sensors the first read starts with, so their first outcomes can be told.
 *
 * Active ones only: they are the ones the acquisition task polls. Nothing to wait for settles at
 * once.
 */
void SensorService::beginAwaitingFirstReadings()
{
    const std::vector<SensorEntity> modbusSensors = sensorRepository->getModbusSensors();

    size_t active = 0;
    for (size_t i = 0; i < modbusSensors.size(); i++)
    {
        if (modbusSensors[i].isActive)
        {
            active++;
        }
    }

    firstReadingsAwaited.reserve(active);
    for (size_t i = 0; i < modbusSensors.size(); i++)
    {
        if (modbusSensors[i].isActive)
        {
            firstReadingsAwaited.push_back(modbusSensors[i].uid);
            firstReadingsChannels += static_cast<uint32_t>(modbusSensors[i].modbusRegisterConfigs.size());
        }
    }

    firstReadingsCount = static_cast<uint32_t>(active);
    firstReadingsState = FirstReadingsState::AWAITED;
    if (firstReadingsAwaited.empty())
    {
        firstReadingsState = FirstReadingsState::SETTLED;
        firstReadingsLastAtMs = static_cast<uint32_t>(arduinoAdapter->millis());
    }
}

/**
 * @brief A sensor's outcome reached the control loop: if it was awaited, it is not any more.
 *
 * Any outcome counts - a value, a failure, a value the plausibility gate refused: what is waited
 * for is that the line has been asked about the sensor and has answered for it, one way or the
 * other.
 */
void SensorService::noteFirstOutcome(const std::string &sensorUid)
{
    if (firstReadingsState != FirstReadingsState::AWAITED)
    {
        return;
    }

    for (size_t i = 0; i < firstReadingsAwaited.size(); i++)
    {
        if (firstReadingsAwaited[i] == sensorUid)
        {
            firstReadingsAwaited.erase(firstReadingsAwaited.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }

    if (firstReadingsAwaited.empty())
    {
        firstReadingsState = FirstReadingsState::SETTLED;
        firstReadingsLastAtMs = static_cast<uint32_t>(arduinoAdapter->millis());
    }
}

/**
 * @brief Stop waiting for a sensor that is not read any more - deleted or switched off since the
 *        reads started. It will never have an outcome.
 */
void SensorService::forgetAwaitedSensorsNoLongerRead(const std::vector<SensorEntity> &modbusSensors)
{
    if (firstReadingsState != FirstReadingsState::AWAITED)
    {
        return;
    }

    size_t i = 0;
    while (i < firstReadingsAwaited.size())
    {
        bool stillRead = false;
        for (size_t j = 0; j < modbusSensors.size(); j++)
        {
            if (modbusSensors[j].isActive && modbusSensors[j].uid == firstReadingsAwaited[i])
            {
                stillRead = true;
                break;
            }
        }

        if (stillRead)
        {
            i++;
        }
        else
        {
            firstReadingsAwaited.erase(firstReadingsAwaited.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    if (firstReadingsAwaited.empty())
    {
        firstReadingsState = FirstReadingsState::SETTLED;
        firstReadingsLastAtMs = static_cast<uint32_t>(arduinoAdapter->millis());
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
            int acceptedChannels = 0;
            for (const auto &pair : channelValues)
            {
                if (!acceptReading(sensor, channelSensorTypeName(sensor, pair.first), pair.first, pair.second))
                {
                    continue;
                }

                sensorValueModel->setChannelValue(sensor.uid, pair.first, pair.second);
                acceptedChannels++;
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

            if (acceptedChannels == 0)
            {
                // Every channel of this read was implausible: the sensor is left without a fresh
                // value, exactly as if the read had failed (GDV1-3810)
                sensorRepository->updateSensorStatus(sensor.uid.c_str(), sensor.isActive, true);
                return false;
            }

            // Computed channels are calculated from the RAW readings of this read and then pass the
            // same gate as any other value (accepted order, 2026-08-13)
            applyVirtualChannels(sensor, channelValues);

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
            if (!acceptReading(sensor, sensor.sensorType, sensor.uid, value))
            {
                // Implausible reading: no value stored, no fresh read time, sensor left in the
                // error state so the rules fall back on sensor_unreachable (GDV1-3810)
                sensorRepository->updateSensorStatus(sensor.uid.c_str(), sensor.isActive, true);
                return false;
            }

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

void SensorService::applyAcquiredReadings()
{
    if (acquisition == nullptr)
    {
        return;
    }

    std::vector<AcquiredReading> readings;
    if (!acquisition->drain(readings))
    {
        return;
    }

    for (const auto &reading : readings)
    {
        applyReading(reading);
    }
}

void SensorService::applyReading(const AcquiredReading &reading)
{
    if (!sensorRepository || !sensorValueModel)
    {
        return;
    }

    auto sensorOpt = sensorRepository->get(reading.sensorUid.c_str());
    if (!sensorOpt.has_value())
    {
        // The sensor was removed between the poll being started and its answer being applied. Nothing
        // more is waited for from it: its answer ends the wait now rather than at the next read.
        noteFirstOutcome(reading.sensorUid);
        log("SensorService: dropped a reading for unknown sensor %s", reading.sensorUid.c_str());
        return;
    }

    const SensorEntity sensor = sensorOpt.value();

    if (reading.model != sensor.model)
    {
        // The uid was given another model (SetSensorModel, ScanSensors on an occupied port) after
        // the acquisition task was handed the list it polled; the next hand-over is up to one
        // read interval away. The answer is the old model's: its channels are not this sensor's,
        // and neither is its failure.
        log("SensorService: dropped a reading of %s polled as %s - the sensor is %s now",
            reading.sensorUid.c_str(), reading.model.c_str(), sensor.model.c_str());
        // Nor is it the sensor's first outcome: that is the answer of the model it has now.
        return;
    }

    if (reading.ok && reading.registerFingerprint != sensor.registerFingerprint())
    {
        // The catalog gave the model other registers (SyncSensorTypesUseCase) after the task was
        // handed the list it polled: the values were read and scaled by registers this sensor no
        // longer has, and can be of a channel it no longer has. A failed poll is applied as it
        // is - it carries no value, and the device that did not answer is the one the sensor has.
        log("SensorService: dropped a reading of %s polled by the registers it had before",
            reading.sensorUid.c_str());
        // Nor is it the sensor's first outcome: that is the answer of the registers it has now.
        return;
    }

    noteFirstOutcome(reading.sensorUid);

    if (!reading.ok)
    {
        sensorRepository->updateSensorStatus(sensor.uid.c_str(), sensor.isActive, true);
        log("SensorService: Failed to read sensor %s, marking as error", sensor.uid.c_str());
        return;
    }

    if (reading.multiChannel)
    {
        sensorValueModel->setSensorMultiChannel(sensor.uid, true);

        std::map<std::string, float> rawChannelValues;
        std::string values;
        int acceptedChannels = 0;

        for (const auto &channel : reading.channels)
        {
            rawChannelValues[channel.channelUid] = channel.value;

            if (!acceptReading(sensor, channelSensorTypeName(sensor, channel.channelUid),
                               channel.channelUid, channel.value))
            {
                continue;
            }

            sensorValueModel->setChannelValue(sensor.uid, channel.channelUid, channel.value);
            acceptedChannels++;

            std::string ch = channel.channelUid;
            size_t dot = ch.find_last_of('.');
            if (dot != std::string::npos)
            {
                ch = ch.substr(dot + 1);
            }
            char item[48];
            snprintf(item, sizeof(item), "%s=%.2f ", ch.c_str(), channel.value);
            values += item;
        }

        if (acceptedChannels == 0)
        {
            // Every channel of this read was implausible: same outcome as a failed read, and for
            // the same reason as on the inline path.
            sensorRepository->updateSensorStatus(sensor.uid.c_str(), sensor.isActive, true);
            return;
        }

        applyVirtualChannels(sensor, rawChannelValues);

        // The time the bus answered, carried from the acquisition task. Taking millis() here
        // instead would restart the freshness window at the moment of applying, and a rule would
        // read a value of any age as fresh.
        sensorValueModel->setLastReadTime(sensor.uid, reading.acquiredAtMs);
        sensorRepository->updateSensorStatus(sensor.uid.c_str(), true, false);
        log("SensorService: Read %s addr=%d %s: %s", sensor.model.c_str(),
            sensor.modbusDeviceAddress, sensor.uid.c_str(), values.c_str());
        return;
    }

    sensorValueModel->setSensorMultiChannel(sensor.uid, false);

    if (reading.channels.empty())
    {
        return;
    }

    const float value = reading.channels[0].value;
    if (!acceptReading(sensor, sensor.sensorType, sensor.uid, value))
    {
        sensorRepository->updateSensorStatus(sensor.uid.c_str(), sensor.isActive, true);
        return;
    }

    sensorValueModel->setSensorValue(sensor.uid, value);
    sensorValueModel->setLastReadTime(sensor.uid, reading.acquiredAtMs);
    sensorRepository->updateSensorStatus(sensor.uid.c_str(), true, false);
    log("SensorService: Read sensor %s, value: %.2f", sensor.uid.c_str(), value);
}

// Computed channels of one read: value from the RAW readings, stored like any other channel.
// The value is subject to the same checks as the rest of the read (accepted order, 2026-08-13).
void SensorService::applyVirtualChannels(const SensorEntity &sensor,
                                         const std::map<std::string, float> &rawChannelValues)
{
    if (!sensorValueModel || !virtualChannelsUseCase)
    {
        return;
    }

    std::vector<IVirtualSensorChannelsUseCase::Channel> partial;
    for (const auto &channel : virtualChannelsUseCase->values(sensor, rawChannelValues, partial))
    {
        if (!acceptReading(sensor, channel.type, channel.uid, channel.value))
        {
            continue;
        }

        sensorValueModel->setChannelValue(sensor.uid, channel.uid, channel.value);
        log("SensorService: VIRTUAL_CHANNEL uid=%s type=%s value=%.3f sources=%.2f,%.2f",
            channel.uid.c_str(), channel.type.c_str(), channel.value,
            channel.firstSource, channel.secondSource);
    }

    // Not all sources answered this time: the cached value stays as it is, exactly like for a
    // register channel that was not read (owner decision, 2026-08-12)
    for (const auto &channel : partial)
    {
        log("SensorService: VIRTUAL_CHANNEL_SKIP uid=%s type=%s reason=partial_read",
            channel.uid.c_str(), channel.type.c_str());
    }
}

// A computed channel has no register config, so without this it would silently disappear from the
// packet of a failing sensor instead of being reported as disconnected (INV-8, D-10).
void SensorService::appendVirtualChannelDtos(const SensorEntity &sensor, UpdateDto &dto)
{
    if (!virtualChannelsUseCase)
    {
        return;
    }

    for (const auto &channel : virtualChannelsUseCase->channels(sensor))
    {
        SensorDto sensorDto;
        sensorDto.uid = channel.uid;
        sensorDto.value = 0.0f;
        sensorDto.state = getSensorStateName(SENSOR_STATE_DISCONNECTED);
        dto.sensors.push_back(sensorDto);
        log("SensorService: VIRTUAL_CHANNEL_DISCONNECTED uid=%s type=%s",
            sensorDto.uid.c_str(), channel.type.c_str());
    }
}

bool SensorService::acceptReading(const SensorEntity &sensor,
                                  const std::string &subSensorTypeName,
                                  const std::string &channelUid,
                                  float value)
{
    if (!plausibilityUseCase)
    {
        // Nothing wired in: the gate is inert, behaviour is the one from before GDV1-3810
        return true;
    }

    ISensorPlausibilityUseCase::Limits limits;
    if (plausibilityUseCase->isPlausible(sensor, subSensorTypeName, value, limits))
    {
        return true;
    }

    log("SensorService: REJECTED implausible value uid=%s type=%s value=%.2f limits=[%.2f,%.2f]",
        channelUid.c_str(),
        subSensorTypeName.empty() ? "-" : subSensorTypeName.c_str(),
        value,
        limits.min,
        limits.max);

    return false;
}

std::string SensorService::channelSensorTypeName(const SensorEntity &sensor, const std::string &channelUid) const
{
    for (const auto &registerConfig : sensor.modbusRegisterConfigs)
    {
        std::string uid = sensor.uid + "." + (registerConfig.channel < 10 ? "0" : "") + std::to_string(registerConfig.channel);
        if (uid == channelUid)
        {
            return registerConfig.sensorType.empty() ? registerConfig.channelName : registerConfig.sensorType;
        }
    }

    for (const auto &registerConfig : sensor.i2cRegisterConfigs)
    {
        std::string uid = sensor.uid + "." + (registerConfig.channel < 10 ? "0" : "") + std::to_string(registerConfig.channel);
        if (uid == channelUid)
        {
            return registerConfig.sensorType.empty() ? registerConfig.channelName : registerConfig.sensorType;
        }
    }

    return "";
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
    forgetAwaitedSensorsNoLongerRead(modbusSensors);

    if (acquisition != nullptr)
    {
        // The bus belongs to the acquisition task. All this side does is say which sensors exist
        // and make sure the task is there to poll them; the answers come back through
        // applyAcquiredReadings(). Neither call makes a transaction, so neither can block here.
        acquisition->setSensors(modbusSensors);
        if (modbusSensors.empty())
        {
            // No Modbus sensors on this module: no reason to hold a 6 KB stack for a task that
            // would only wake ten times a second to find nothing to do.
            return true;
        }
        if (!acquisition->ensureRunning())
        {
            log("SensorService: acquisition task not running - Modbus sensors are not being polled");
            return false;
        }
        return true;
    }

    if (modbusSensors.empty())
    {
        return true; // No sensors to read, consider it successful
    }

    bool allSuccess = true;

    for (const auto &sensor : modbusSensors)
    {
        // No mqttManager->loop() in here, deliberately: a sensor service has no business
        // reaching into the network stack to paper over its own duration. The control loop
        // reaches mqttManager->loop() every iteration by itself.

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
        noteFirstOutcome(sensor.uid);
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

    const auto &sensors = sensorRepository->getActiveSensors();
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

            appendVirtualChannelDtos(sensor, dto);
        }
        else if (!sensor.isError &&
                 (sensor.isMultiChannel() || !sensorValueModel->hasSensorValue(sensor.uid)))
        {
            // Not read yet and not failing: there is nothing to report. The record below would say
            // "online" with a value of 0 for a reading that was never taken - for a multi-channel
            // sensor, under a uid its structure does not declare. The sensor enters the packet
            // with its first reading, or as disconnected with its first failure.
            continue;
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
