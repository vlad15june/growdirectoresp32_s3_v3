#pragma once

#include <vector>
#include <cstdarg>
#include "ISensorService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Application/DTOs/UpdateDto.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Application/Interfaces/IOutbox.h"
#include "Application/Interfaces/ISensorAcquisition.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Udp/IUdpManager.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/UseCases/Sensors/SensorRecoveryUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IVirtualSensorChannelsUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ISensorPlausibilityUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Boot/IBootGates.h"
#include <map>

class SensorService : public ISensorService {
private:
    IModuleConfig* moduleConfig;
    ModuleStructure* moduleStructure;
    ISensorRepository* sensorRepository;
    ISensorReader* sensorReader;
    ISensorValueModel* sensorValueModel;
    INTPTimeAdapter* timeAdapter;
    IArduinoAdapter* arduinoAdapter;
    IHybridAPIService* hybridApiService;
    IOutbox* outbox = nullptr;
    ILogger* logger;
    IDebugSystem* debugSystem;
    IUdpManager* udpManager;
    IJsonConverter* jsonConverter;

    // Injected, not owned: the task that talks to the RS-485 line. Without one this service
    // falls back to reading Modbus sensors inline, on the control loop - a path kept only for
    // the tests that exercise the readers directly.
    ISensorAcquisition* acquisition = nullptr;
    // Injected, not known: which computed channels a sensor has and how they are calculated is
    // IVirtualSensorChannelsUseCase, created in the composition root
    IVirtualSensorChannelsUseCase* virtualChannelsUseCase;

    // Injected, not owned: the gate is an interchangeable decision behind an interface, and a test
    // gives the service its own verdict instead of a repository full of sensor types
    ISensorPlausibilityUseCase* plausibilityUseCase;


    const unsigned long SENSOR_READ_INTERVAL = 15000;
    const unsigned long SENSOR_UPDATE_INTERVAL = 15000;
    const unsigned long UDP_SEND_INTERVAL = 15000;
    unsigned long lastSensorReadTime = 0;
    unsigned long lastSensorUpdateTime = 0;
    unsigned long lastUdpSendTime = 0;

    // Injected, not owned: the boot sequence's gate in front of the sensor update. Without one the
    // three timers above run from millis 0 and decide everything.
    IBootGates* bootGates = nullptr;
    bool telemetrySent = false;
    bool udpSent = false;
    // A tick that fell while the gate was shut, and when: see loop().
    bool telemetryHeld = false;
    unsigned long telemetryHeldSinceMs = 0;

    // The first outcome of every ModBus sensor the reads started with: what the boot sequence holds
    // the first telemetry, and the discovery, for. The uids are crossed off as their outcomes are
    // applied; the vector is sized once, to the sensors the reads started with, and only shrinks.
    enum class FirstReadingsState : uint8_t { NOT_STARTED, AWAITED, SETTLED };
    FirstReadingsState firstReadingsState = FirstReadingsState::NOT_STARTED;
    std::vector<std::string> firstReadingsAwaited;
    uint32_t firstReadingsCount = 0;
    uint32_t firstReadingsChannels = 0;
    uint32_t firstReadingsLastAtMs = 0;

    void beginAwaitingFirstReadings();
    void noteFirstOutcome(const std::string& sensorUid);
    void forgetAwaitedSensorsNoLongerRead(const std::vector<SensorEntity>& modbusSensors);

    // Port recovery only belongs here on the inline path. With an acquisition task wired, the
    // recovery state lives with the polling, in SensorAcquisitionService.
    SensorRecoveryUseCase _recovery;

    std::string generateSensorUid(const SensorEntity& sensor, const std::string& channelName = "") const;
    UpdateDto createSensorUpdateDto();
    bool readAnalogSensors();
    bool readI2CSensors();
    bool readModbusSensors();
    void sendUpdateViaUdp(const UpdateDto& updateDto);

    /**
     * @brief Apply everything the acquisition task finished since the last iteration.
     *
     * The half of readSensor() that follows the bus transaction - the plausibility gate, the
     * value model, the computed channels, the sensor's error state - runs here, on the control
     * loop, and nowhere else. Not for tidiness: Repository<T> caches entities in an unordered_map
     * and writes through to SPIFFS with no lock of its own, so `updateSensorStatus()` from a
     * second task would be a data race against the loop that reads it.
     *
     * Costs no bus time at all. The only wait is a mutex held across a vector swap.
     */
    void applyAcquiredReadings();

    /** @brief Apply one finished acquisition; the post-transaction half of readSensor(). */
    void applyReading(const AcquiredReading& reading);

    // Computed (virtual) channels of a sensor - VPD today, more later. Which channels exist and
    // how they are calculated is the injected use case; these two only store and report.
    void applyVirtualChannels(const SensorEntity& sensor, const std::map<std::string, float>& rawChannelValues);
    void appendVirtualChannelDtos(const SensorEntity& sensor, UpdateDto& dto);

    /**
     * @brief Plausibility gate (GDV1-3810): drop a reading outside the sensor physical range.
     * @param sensor Sensor being read
     * @param subSensorTypeName Sub sensor type (channel) name the value belongs to
     * @param channelUid UID the value would be stored under (the sensor UID for single-channel)
     * @param value Reading to check
     * @return true if the value may be stored and published
     */
    bool acceptReading(const SensorEntity& sensor,
                       const std::string& subSensorTypeName,
                       const std::string& channelUid,
                       float value);

    /**
     * @brief Sub sensor type name of one channel of a multi-channel sensor.
     * @param sensor Multi-channel sensor
     * @param channelUid Channel UID as built by the reader (<sensor uid>.<channel>)
     * @return Sub sensor type name, empty when the channel is not in the register configs
     */
    std::string channelSensorTypeName(const SensorEntity& sensor, const std::string& channelUid) const;
    
    void log(const char* format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SENSOR_DATA_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
    
            IDebugFeature* feature = debugSystem->getFeature(DebugFeatures::SENSOR_DATA_DEBUG);
            if (feature)
            {
                logger->println(DebugFeatures::SENSOR_DATA_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                logger->println(DebugFeatures::SENSOR_DATA_DEBUG, buffer);
            }
        }
    }

    void logVerbose(const char* format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::SENSOR_VERBOSE_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);

            IDebugFeature* feature = debugSystem->getFeature(DebugFeatures::SENSOR_VERBOSE_DEBUG);
            if (feature)
            {
                logger->println(DebugFeatures::SENSOR_VERBOSE_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                logger->println(DebugFeatures::SENSOR_VERBOSE_DEBUG, buffer);
            }
        }
    }

public:
    SensorService(IModuleConfig* moduleConfig, 
                  ModuleStructure* moduleStructure,
                  ISensorRepository* sensorRepository,
                  ISensorReader* sensorReader,
                  ISensorValueModel* sensorValueModel,
                  INTPTimeAdapter* timeAdapter,
                  IArduinoAdapter* arduinoAdapter,
                  IHybridAPIService* hybridApiService = nullptr,
                  ILogger* logger = nullptr,
                  IDebugSystem* debugSystem = nullptr,
                  IUdpManager* udpManager = nullptr,
                  IJsonConverter* jsonConverter = nullptr,
                  ISensorPlausibilityUseCase* plausibilityUseCase = nullptr,
                  IVirtualSensorChannelsUseCase* virtualChannelsUseCase = nullptr);

    ~SensorService();

    /**
     * @brief Send sensor updates through the outbox instead of inline.
     *
     * Without one this service keeps calling the API directly, which is what the tests that
     * predate the outbox exercise.
     */
    void setOutbox(IOutbox* outboxToUse) { outbox = outboxToUse; }

    /**
     * @brief Read Modbus sensors on the acquisition task instead of on the control loop.
     *
     * This is the one switch that separates the two shapes. With it, loop() never makes a bus
     * transaction; without it, loop() polls every Modbus sensor inline and a silent one costs it
     * 1400 ms per channel.
     */
    void setAcquisition(ISensorAcquisition* acquisitionToUse) { acquisition = acquisitionToUse; }

    /**
     * @brief Put the sensor update behind the boot sequence.
     *
     * With it the reads start on the first iteration of the control loop, the first update goes
     * out on the iteration its gate opens and the UDP copy once every stored sensor has had its
     * first outcome; after that all three keep their 15 s intervals. Without it the three timers
     * run from millis 0.
     */
    void setBootGates(IBootGates* gates) { bootGates = gates; }

    FirstReadings firstReadings() const override;
    void giveUpOnTheFirstReadings() override;

    bool init() override;
    void loop() override;
    bool readAllSensors() override;
    bool readSensor(SensorEntity sensor) override;
};
