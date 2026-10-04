// The sensor update packet before a sensor's first reading.
//
// A sensor that has not been read yet has no value, and the packet has nothing true to say about
// it. Until it is read - or fails, which is reported as disconnected - it is left out: a record
// with a value of 0 and the state "online" would be read by the backend, by the modules that
// subscribe to this one and by the UDP listeners as a measurement.
//
// The state is not a corner case. The Modbus sensors are polled by the acquisition task, and the
// iteration of the control loop that hands it the sensor list is the one that builds the first
// packet (SensorService::loop()): every boot with a stored sensor, every sensor the discovery adds
// and every sensor a SetSensorModel command replaces starts in it.
//
// What runs here is production code: SensorService builds the packet from the real
// SensorRepository (over an in-memory DAO) and the real SensorValueModel, the real
// HybridAPIService hands it to REST and MQTT with the priority globals.h gives it, and the real
// ArduinoJsonConverter serialises the UDP copy. Replaced are the outside world - the REST and MQTT
// clients, the UDP socket - and the acquisition task, which here hands over the readings a test
// queues for it instead of reading a line.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Sensors/RemoteSensors/RemoteSensorClient.h"
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Infrastructure/Controllers/UDP/UdpInboundDispatcher.h"
#include "Infrastructure/Repositories/SensorRepository.h"

#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Mqtt/MockMessageProcessor.h"
#include "Mock/Infrastructure/Mqtt/MockMqttApiService.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Repositories/MockRuleRepository.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorDao.h"
#include "Mock/Infrastructure/Udp/MockUdpManager.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* const MODULE_UID = "test_module_uid";
const char* const RY_WS301_MODEL_UID = "4719a986-a300-48d5-bd1f-e34a1329798a";
const char* const MP_508C_MODEL_UID = "144f6c6d-d210-44bf-a9c3-23c5f26472fe";

// SensorService::SENSOR_UPDATE_INTERVAL; the first packet of a module whose SensorService::init()
// is not called - main.cpp does not call it - is built on the first iteration past it.
const unsigned long TICK_MS = 15000;

/** @brief One record of a packet, as it went out. */
struct Record {
    std::string uid;
    float value = 0.0f;
    std::string state;
};

std::vector<Record> recordsOf(const UpdateDto& update)
{
    std::vector<Record> records;
    for (size_t i = 0; i < update.sensors.size(); i++)
    {
        Record record;
        record.uid = update.sensors[i].uid;
        record.value = update.sensors[i].value;
        record.state = update.sensors[i].state;
        records.push_back(record);
    }
    return records;
}

/** @brief The records of the UDP copy, read back out of the JSON that went to the socket. */
std::vector<Record> recordsOf(const std::string& json)
{
    std::vector<Record> records;
    DynamicJsonDocument doc(8192);
    TEST_ASSERT_FALSE_MESSAGE(deserializeJson(doc, json), "the UDP copy is not JSON");
    TEST_ASSERT_TRUE_MESSAGE(doc["sensors"].is<JsonArray>(), "the UDP copy has no sensors array");
    for (JsonObject sensor : doc["sensors"].as<JsonArray>())
    {
        Record record;
        record.uid = sensor["uid"].as<std::string>();
        record.value = sensor["value"].as<float>();
        record.state = sensor["state"].as<std::string>();
        records.push_back(record);
    }
    return records;
}

std::string describe(const std::vector<Record>& records)
{
    if (records.empty())
    {
        return "no records";
    }
    std::string text;
    for (size_t i = 0; i < records.size(); i++)
    {
        char item[160];
        snprintf(item, sizeof(item), "%s%s=%.2f/%s", i == 0 ? "" : " ", records[i].uid.c_str(),
                 records[i].value, records[i].state.c_str());
        text += item;
    }
    return text;
}

const Record* find(const std::vector<Record>& records, const std::string& uid)
{
    for (size_t i = 0; i < records.size(); i++)
    {
        if (records[i].uid == uid)
        {
            return &records[i];
        }
    }
    return nullptr;
}

/** @brief The socket end of UDP: keeps every multicast payload. */
class RecordingUdpSender : public IUdpSender {
public:
    std::vector<std::string> multicasts;

    bool sendBroadcast(const char*, uint16_t) override { return true; }
    bool sendBroadcast(const char*, size_t, uint16_t) override { return true; }
    bool sendUnicast(const char*, const char*, uint16_t) override { return true; }
    bool sendUnicast(const char*, size_t, const char*, uint16_t) override { return true; }
    bool sendMulticast(const char* data, const char*, uint16_t) override
    {
        multicasts.push_back(data != nullptr ? std::string(data) : std::string());
        return true;
    }
    bool sendMulticast(const char* data, size_t length, const char*, uint16_t) override
    {
        multicasts.push_back(data != nullptr ? std::string(data, length) : std::string());
        return true;
    }
    bool isInitialized() const override { return true; }
};

class RecordingUdpManager : public IUdpManager {
public:
    RecordingUdpSender sender;

    bool initialize(uint16_t, const char*) override { return true; }
    void shutdown() override {}
    IUdpController* getController() override { return nullptr; }
    IUdpSender* getSender() override { return &sender; }
    void setLocalPort(uint16_t) override {}
    uint16_t getLocalPort() const override { return 1234; }
    void setMulticastIP(const char*) override {}
    const char* getMulticastIP() const override { return "239.1.2.3"; }
    bool isInitialized() const override { return true; }
};

/**
 * @brief The acquisition task, seen from the control loop: it is told the sensor list and hands
 *        back what it read. Here it reads nothing by itself - it hands back the readings the test
 *        queues, on the next drain, the way the real task's answers arrive after the iteration
 *        that asked for them.
 */
class QueuedAcquisition : public ISensorAcquisition {
public:
    std::vector<AcquiredReading> queued;
    std::vector<SensorEntity> lastSensors;
    int setSensorsCalls = 0;

    void setSensors(const std::vector<SensorEntity>& modbusSensors) override
    {
        lastSensors = modbusSensors;
        setSensorsCalls++;
    }
    bool ensureRunning() override { return true; }
    bool drain(std::vector<AcquiredReading>& out) override
    {
        if (queued.empty())
        {
            return false;
        }
        out = queued;
        queued.clear();
        return true;
    }
    uint32_t submitBusCommand(const std::string&, const std::string&) override { return kBusCommandRejected; }
    uint32_t submitComplexBusCommand(const ComplexCommandWork&) override { return kBusCommandRejected; }
    uint32_t submitStartupScanStep(const StartupScanStep&) override { return kBusCommandRejected; }
    bool drainBusCommandResults(std::vector<BusCommandCompletion>&) override { return false; }
};

ModbusRegisterConfig makeChannel(uint8_t channel, uint16_t startRegister, const char* sensorType)
{
    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = startRegister;
    config.numRegisters = 1;
    config.multiplier = 0.01f;
    config.offset = 0.0f;
    config.channelName = sensorType;
    config.channel = channel;
    config.sensorType = sensorType;
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    return config;
}

std::string uidOfPin(int pin)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "%s.%02d", MODULE_UID, pin);
    return std::string(uid);
}

std::string channelUid(int pin, int channel)
{
    char uid[56];
    snprintf(uid, sizeof(uid), "%s.%02d.%02d", MODULE_UID, pin, channel);
    return std::string(uid);
}

/** @brief A stored RY-WS301: air temperature and humidity, and the VPD computed from them. */
SensorEntity makeStoredRyWs301(int pin)
{
    SensorEntity row;
    row.uid = uidOfPin(pin);
    row.pin = pin;
    row.protocol = "ModBus";
    row.model = "ry_ws301";
    row.sensorType = "ry_ws301";
    row.sensorTypeUid = RY_WS301_MODEL_UID;
    row.isActive = true;
    row.isError = false;
    row.status = SENSOR_STATUS_ADDED;
    row.modbusDeviceAddress = MODBUS_ADDRESS_MIN + pin;
    row.modbusRegisterConfigs.push_back(makeChannel(1, 0, "air_temp"));
    row.modbusRegisterConfigs.push_back(makeChannel(2, 1, "humidity"));
    return row;
}

/**
 * @brief A stored sensor with no register configs, which makes it single-channel
 *        (SensorEntity::isMultiChannel()).
 */
SensorEntity makeStoredSingleChannel(int pin, const char* protocol)
{
    SensorEntity row;
    row.uid = uidOfPin(pin);
    row.pin = pin;
    row.protocol = protocol;
    row.model = "single";
    row.sensorType = "lux";
    row.isActive = true;
    row.isError = false;
    row.status = SENSOR_STATUS_ADDED;
    return row;
}

/**
 * @brief The two-channel sensor stored as `polledAs` answered: channel 1 and channel 2. The reading
 *        carries what the acquisition task polled it as - the model and the registers of the row it
 *        was handed (SensorAcquisitionService::pollSensor()).
 */
AcquiredReading answered(const SensorEntity& polledAs, float first, float second, uint32_t atMs)
{
    AcquiredReading reading;
    reading.sensorUid = polledAs.uid;
    reading.model = polledAs.model;
    reading.registerFingerprint = polledAs.registerFingerprint();
    reading.port = polledAs.pin;
    reading.multiChannel = true;
    reading.ok = true;
    reading.acquiredAtMs = atMs;
    AcquiredChannel one;
    one.channelUid = channelUid(polledAs.pin, 1);
    one.value = first;
    AcquiredChannel two;
    two.channelUid = channelUid(polledAs.pin, 2);
    two.value = second;
    reading.channels.push_back(one);
    reading.channels.push_back(two);
    return reading;
}

/** @brief The sensor on the given pin did not answer. */
AcquiredReading silent(int pin, uint32_t atMs, const char* model = "ry_ws301")
{
    AcquiredReading reading;
    reading.sensorUid = uidOfPin(pin);
    reading.model = model;
    reading.port = pin;
    reading.multiChannel = true;
    reading.ok = false;
    reading.acquiredAtMs = atMs;
    return reading;
}

/** @brief The module's sensor service, wired the way globals.h wires it. */
struct TelemetryStand {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    MockArduinoAdapter arduino;
    MockSpiffsSensorDao sensorDao;
    SensorRepository sensorRepository;
    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;
    MockSensorReader sensorReader;
    MockRestAPIService restApi;
    MockMqttApiService mqttApi;
    HybridAPIService hybridApi;
    RecordingUdpManager udp;
    ArduinoJsonConverter jsonConverter;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    QueuedAcquisition acquisition;
    SensorService sensorService;

    /** @brief What each way out carried, one entry per packet. */
    std::vector<std::vector<Record> > rest;
    std::vector<std::vector<Record> > mqtt;
    std::vector<std::vector<Record> > multicast;

    TelemetryStand()
        : sensorRepository(&sensorDao),
          hybridApi(&restApi, &mqttApi, &moduleConfig, &logger, &debugSystem, ApiPriority::BOTH),
          virtualChannels(&vpdChannel),
          sensorService(&moduleConfig, &moduleStructure, &sensorRepository, &sensorReader,
                        &sensorValueModel, &timeAdapter, &arduino, &hybridApi, &logger,
                        &debugSystem, &udp, &jsonConverter, nullptr, &virtualChannels)
    {
        debugSystem.featureEnabled = false;
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setSettings(&settings);
        settings.setUser("t91@example.com");
        settings.setAccountUid("t91-account");
        timeAdapter.setEpochTime(1757066400);
        sensorService.setAcquisition(&acquisition);
    }

    /**
     * @brief One iteration of the control loop at the given moment, and what it sent - by REST,
     *        by MQTT and by UDP, which must all say the same.
     */
    std::vector<Record> iterateAt(unsigned long ms)
    {
        restApi.sendModuleUpdateDataCalled = false;
        mqttApi.sendModuleUpdateDataCalled = false;
        const size_t multicastsBefore = udp.sender.multicasts.size();

        arduino.setMillis(ms);
        sensorService.loop();

        char message[160];
        snprintf(message, sizeof(message), "t=%lu ms: no packet went out by REST", ms);
        TEST_ASSERT_TRUE_MESSAGE(restApi.sendModuleUpdateDataCalled, message);
        snprintf(message, sizeof(message), "t=%lu ms: no packet went out by MQTT", ms);
        TEST_ASSERT_TRUE_MESSAGE(mqttApi.sendModuleUpdateDataCalled, message);
        snprintf(message, sizeof(message), "t=%lu ms: no packet went out by UDP", ms);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(multicastsBefore + 1, udp.sender.multicasts.size(), message);

        rest.push_back(recordsOf(restApi.lastUpdateData));
        mqtt.push_back(recordsOf(mqttApi.lastUpdateData));
        multicast.push_back(recordsOf(udp.sender.multicasts.back()));

        const std::string restText = describe(rest.back());
        const std::string mqttText = describe(mqtt.back());
        const std::string udpText = describe(multicast.back());
        printf("  t91: t=%lu ms - REST: %s\n", ms, restText.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(restText.c_str(), mqttText.c_str(),
                                         "the MQTT copy does not say what REST said");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(restText.c_str(), udpText.c_str(),
                                         "the UDP copy does not say what REST said");
        return rest.back();
    }
};

/**
 * @brief Another module that listens to this one - by the MQTT subscription a rule of its makes and
 *        by the UDP multicast - wired the way globals.h wires its receiver.
 */
struct ListeningModule {
    MockUdpManager udpManager;
    MockMqttManager mqttManager;
    MockMessageProcessor messageProcessor;
    SensorValueModel sensorValueModel;
    MockRuleRepository ruleRepository;
    MockSensorRepository sensorRepository;
    ArduinoJsonConverter jsonConverter;
    MockArduinoAdapter arduino;
    MockLogger logger;
    MockDebugSystem debugSystem;
    UdpInboundDispatcher dispatcher;
    RemoteSensorReceiver receiver;

    ListeningModule()
        : dispatcher(&udpManager, &logger, &debugSystem),
          receiver(&udpManager, &dispatcher, &mqttManager, &messageProcessor, &sensorValueModel,
                   &ruleRepository, &sensorRepository, &jsonConverter, &arduino, &logger,
                   &debugSystem)
    {
        debugSystem.featureEnabled = false;
        dispatcher.init();
        receiver.init();
    }

    /** @brief What the module last sent, as both copies arrive here: the multicast and the MQTT one. */
    void hear(const TelemetryStand& stand, unsigned long atMs)
    {
        arduino.setMillis(atMs);
        udpManager.controller.emitPacket(stand.udp.sender.multicasts.back());

        const std::string published = jsonConverter.updateToJson(stand.mqttApi.lastUpdateData);
        ITopicHandler* handler = messageProcessor.lastAddedHandler;
        TEST_ASSERT_NOT_NULL_MESSAGE(handler, "the listening module has no MQTT handler");
        handler->handleMessage("hardware/update/test_module_uid",
                               reinterpret_cast<const uint8_t*>(published.c_str()),
                               static_cast<unsigned int>(published.size()));
    }
};

void assertNotInThePacket(const std::vector<Record>& records, const std::string& sensorUid,
                          const char* when)
{
    char message[320];
    for (size_t i = 0; i < records.size(); i++)
    {
        const std::string& uid = records[i].uid;
        const bool ofTheSensor = uid == sensorUid ||
                                 (uid.size() > sensorUid.size() &&
                                  uid.compare(0, sensorUid.size(), sensorUid) == 0 &&
                                  uid[sensorUid.size()] == '.');
        snprintf(message, sizeof(message),
                 "%s: the packet carries %s=%.2f/%s for sensor %s, which has not been read yet",
                 when, uid.c_str(), records[i].value, records[i].state.c_str(), sensorUid.c_str());
        TEST_ASSERT_FALSE_MESSAGE(ofTheSensor, message);
    }
}

void assertRecord(const std::vector<Record>& records, const std::string& uid, float value,
                  const char* state)
{
    char message[320];
    const Record* record = find(records, uid);
    snprintf(message, sizeof(message), "no record for %s in the packet (%s)", uid.c_str(),
             describe(records).c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(record, message);
    snprintf(message, sizeof(message), "%s went out %s, not %s", uid.c_str(), record->state.c_str(),
             state);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(state, record->state.c_str(), message);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, value, record->value, uid.c_str());
}

} // namespace

// (a) A stored multi-channel sensor - the logged RY-WS301 - on the boot's first packet,
//     built in the iteration that handed it to the acquisition task: not in the packet.
static void test_a_stored_multi_channel_sensor_is_not_reported_before_its_first_reading(void)
{
    TelemetryStand stand;
    stand.sensorRepository.save(makeStoredRyWs301(0));

    const std::vector<Record> first = stand.iterateAt(TICK_MS);

    // The premise: the sensor was handed to the acquisition task in this very iteration, and the
    // value model has nothing for it.
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stand.acquisition.setSensorsCalls,
                                  "the first iteration did not hand the sensor list over");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, stand.acquisition.lastSensors.size(),
                                   "the stored sensor was not in the list handed over");
    TEST_ASSERT_FALSE(stand.sensorValueModel.hasChannelValues(uidOfPin(0)));

    assertNotInThePacket(first, uidOfPin(0), "the first packet of the boot");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, first.size(), "the first packet carries a record nothing read");
}

// (c) The first reading arrives after the first packet; the next packet carries the channels and
//     the computed VPD, and nothing under the sensor's own uid.
static void test_a_multi_channel_sensor_is_reported_channel_by_channel_from_its_first_reading(void)
{
    TelemetryStand stand;
    stand.sensorRepository.save(makeStoredRyWs301(0));
    stand.iterateAt(TICK_MS);

    stand.acquisition.queued.push_back(answered(makeStoredRyWs301(0), 24.10f, 45.94f, TICK_MS + 1900));
    const std::vector<Record> second = stand.iterateAt(2 * TICK_MS);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(3, second.size(), "the read sensor is not reported channel by channel");
    assertRecord(second, channelUid(0, 1), 24.10f, "online");
    assertRecord(second, channelUid(0, 2), 45.94f, "online");
    const Record* vpd = find(second, channelUid(0, 3));
    TEST_ASSERT_NOT_NULL_MESSAGE(vpd, "the computed VPD channel is missing after the first reading");
    TEST_ASSERT_EQUAL_STRING("online", vpd->state.c_str());
    TEST_ASSERT_TRUE_MESSAGE(vpd->value > 0.0f, "the VPD went out without a value");
    TEST_ASSERT_NULL_MESSAGE(find(second, uidOfPin(0)),
                             "a record under the sensor uid went out next to its channels");
}

// (a) Not only the first packet: as long as the bus has not answered, the sensor stays out of
//     every packet - a slow first pass of the acquisition task does not let a 0 through later.
static void test_a_sensor_the_bus_has_not_answered_yet_stays_out_of_every_packet(void)
{
    TelemetryStand stand;
    stand.sensorRepository.save(makeStoredRyWs301(0));

    for (int tick = 1; tick <= 4; tick++)
    {
        assertNotInThePacket(stand.iterateAt(tick * TICK_MS), uidOfPin(0), "before the first answer");
    }
}

// (b) A single-channel sensor that has never been read: not in the packet either. An analog row
//     is never read on this module (SensorService::readAnalogSensors() asks the repository for
//     protocol "ANALOG", the row carries "Analog"), so this is every packet, not just the first.
static void test_a_single_channel_sensor_is_not_reported_before_its_first_reading(void)
{
    TelemetryStand stand;
    stand.sensorRepository.save(makeStoredSingleChannel(1, "Analog"));

    const std::vector<Record> first = stand.iterateAt(TICK_MS);
    TEST_ASSERT_FALSE(stand.sensorValueModel.hasSensorValue(uidOfPin(1)));
    assertNotInThePacket(first, uidOfPin(1), "the first packet of the boot");
    TEST_ASSERT_EQUAL_UINT(0, first.size());

    assertNotInThePacket(stand.iterateAt(2 * TICK_MS), uidOfPin(1), "the second packet");
}

// (c) A single-channel sensor that has been read goes out under its uid with its value - and a
//     reading of exactly 0 is a reading: the sensor is in the packet, online, with 0.
static void test_a_single_channel_sensor_is_reported_with_its_reading_even_when_it_reads_zero(void)
{
    TelemetryStand stand;
    stand.sensorRepository.save(makeStoredSingleChannel(2, "I2C"));

    stand.sensorReader.readSensorReturnValue = true;
    stand.sensorReader.readSensorValue = 0.0f;
    const std::vector<Record> zero = stand.iterateAt(TICK_MS);
    TEST_ASSERT_TRUE_MESSAGE(stand.sensorValueModel.hasSensorValue(uidOfPin(2)),
                             "the reading of 0 was not stored as a value");
    TEST_ASSERT_EQUAL_UINT(1, zero.size());
    assertRecord(zero, uidOfPin(2), 0.0f, "online");

    stand.sensorReader.readSensorValue = 312.5f;
    const std::vector<Record> next = stand.iterateAt(2 * TICK_MS);
    TEST_ASSERT_EQUAL_UINT(1, next.size());
    assertRecord(next, uidOfPin(2), 312.5f, "online");
}

// (d) A multi-channel sensor whose first answer is a failure: reported disconnected - every channel
//     of its register configs and its computed channel, with 0.
static void test_a_multi_channel_sensor_failing_before_its_first_reading_is_reported_disconnected(void)
{
    TelemetryStand stand;
    stand.sensorRepository.save(makeStoredRyWs301(0));
    stand.iterateAt(TICK_MS);

    stand.acquisition.queued.push_back(silent(0, TICK_MS + 4200));
    const std::vector<Record> second = stand.iterateAt(2 * TICK_MS);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(3, second.size(), "a failing sensor is not reported channel by channel");
    assertRecord(second, channelUid(0, 1), 0.0f, "disconnected");
    assertRecord(second, channelUid(0, 2), 0.0f, "disconnected");
    assertRecord(second, channelUid(0, 3), 0.0f, "disconnected");
}

// (d) A single-channel sensor whose first read fails, and a stored row that comes up with the
//     error flag already set: both reported disconnected under the sensor uid.
static void test_a_single_channel_sensor_failing_before_its_first_reading_is_reported_disconnected(void)
{
    TelemetryStand stand;
    stand.sensorRepository.save(makeStoredSingleChannel(2, "I2C"));
    SensorEntity flagged = makeStoredSingleChannel(3, "Analog");
    flagged.isError = true;
    stand.sensorRepository.save(flagged);

    stand.sensorReader.readSensorReturnValue = false;
    const std::vector<Record> first = stand.iterateAt(TICK_MS);

    TEST_ASSERT_EQUAL_UINT(2, first.size());
    assertRecord(first, uidOfPin(2), 0.0f, "disconnected");
    assertRecord(first, uidOfPin(3), 0.0f, "disconnected");
}

// (e) SetSensorModel replaces the sensor of a port: the command drops the old sensor's values
//     (ModuleCommandExecutionUseCase::applySetSensorModel()) and saves a new row the bus has not
//     answered yet. The next packet leaves it out; its first reading brings it in.
static void test_a_sensor_set_by_a_command_is_not_reported_before_its_first_reading(void)
{
    TelemetryStand stand;
    MockSensorTypeRepository sensorTypeRepository;
    ModbusAddressAllocator addressAllocator(&stand.sensorRepository);
    ModuleCommandExecutionUseCase commands(
        &stand.logger, &stand.debugSystem, nullptr, nullptr, nullptr, &stand.sensorRepository,
        &sensorTypeRepository, &stand.sensorReader, &stand.moduleConfig, nullptr, nullptr, nullptr,
        &stand.moduleStructure, &stand.sensorValueModel, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, &addressAllocator);

    SensorType mp508c;
    mp508c.uid = MP_508C_MODEL_UID;
    mp508c.name = "mp-508c";
    mp508c.protocol = SensorProtocol::MODBUS_PROTOCOL;
    mp508c.modbusDeviceAddress = 1;
    mp508c.modbusRegisterConfig.push_back(makeChannel(1, 0, "soil_moisture"));
    mp508c.modbusRegisterConfig.push_back(makeChannel(2, 1, "soil_temp"));
    sensorTypeRepository.addSensorType(mp508c);

    // A working RY-WS301 on port 0, read and reported.
    stand.sensorRepository.save(makeStoredRyWs301(0));
    stand.iterateAt(TICK_MS);
    stand.acquisition.queued.push_back(answered(makeStoredRyWs301(0), 24.10f, 45.94f, TICK_MS + 1900));
    const std::vector<Record> before = stand.iterateAt(2 * TICK_MS);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(3, before.size(), "the premise: the RY-WS301 is reported before the command");

    // The app gives port 0 another model. Pin 4 of the payload is pin 0 of the module.
    ModuleCommandDto command;
    command.uid = "cmd-set-model";
    command.command = "SetSensorModel";
    command.options = std::string("{\"pin\":4,\"uid\":\"") + MP_508C_MODEL_UID + "\"}";
    command.createdAt = "";
    stand.sensorReader.scanSensorReturnValue = true;
    std::vector<ModuleCommandExecutionResult> results = commands.executeCommands({command});
    TEST_ASSERT_EQUAL_UINT(1, results.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", results[0].status.c_str(), results[0].errorMessage.c_str());
    Utils::Optional<SensorEntity> replaced = stand.sensorRepository.get(uidOfPin(0).c_str());
    TEST_ASSERT_TRUE(replaced.has_value());
    TEST_ASSERT_EQUAL_STRING("mp-508c", replaced.value().model.c_str());
    TEST_ASSERT_FALSE_MESSAGE(stand.sensorValueModel.hasChannelValues(uidOfPin(0)),
                              "the premise: the command dropped the old sensor's values");

    const std::vector<Record> after = stand.iterateAt(3 * TICK_MS);
    assertNotInThePacket(after, uidOfPin(0), "the first packet after SetSensorModel");
    TEST_ASSERT_EQUAL_UINT(0, after.size());

    stand.acquisition.queued.push_back(answered(replaced.value(), 31.5f, 22.25f, 3 * TICK_MS + 1500));
    const std::vector<Record> read = stand.iterateAt(4 * TICK_MS);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2, read.size(), "the new sensor is not reported channel by channel");
    assertRecord(read, channelUid(0, 1), 31.5f, "online");
    assertRecord(read, channelUid(0, 2), 22.25f, "online");
}

// A module that listens to this one writes every record it hears that is not disconnected into its
// own value model, with the moment it heard it as the read stamp (RemoteSensorReceiver), and its
// rules take that for a fresh reading. Before the first reading it learns nothing of the sensor;
// with the first reading it learns the channels.
static void test_a_listening_module_learns_nothing_of_a_sensor_before_its_first_reading(void)
{
    TelemetryStand stand;
    stand.sensorRepository.save(makeStoredRyWs301(0));
    ListeningModule peer;

    stand.iterateAt(TICK_MS);
    peer.hear(stand, TICK_MS + 50);

    const std::string uids[4] = {uidOfPin(0), channelUid(0, 1), channelUid(0, 2), channelUid(0, 3)};
    char message[200];
    for (int i = 0; i < 4; i++)
    {
        snprintf(message, sizeof(message),
                 "the listening module took %.2f for %s as a reading of t=%u - nothing had read it",
                 peer.sensorValueModel.getSensorValue(uids[i]), uids[i].c_str(),
                 (unsigned)peer.sensorValueModel.getLastReadTime(uids[i]));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, peer.sensorValueModel.getLastReadTime(uids[i]), message);
    }

    stand.acquisition.queued.push_back(answered(makeStoredRyWs301(0), 24.10f, 45.94f, TICK_MS + 1900));
    stand.iterateAt(2 * TICK_MS);
    peer.hear(stand, 2 * TICK_MS + 50);

    TEST_ASSERT_EQUAL_UINT32(2 * TICK_MS + 50, peer.sensorValueModel.getLastReadTime(channelUid(0, 1)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.10f, peer.sensorValueModel.getSensorValue(channelUid(0, 1)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 45.94f, peer.sensorValueModel.getSensorValue(channelUid(0, 2)));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, peer.sensorValueModel.getLastReadTime(uidOfPin(0)),
                                     "the listening module heard a record under the sensor's own uid");
}

// The value model's own answer: a stored value is a value whatever it is, and a uid nothing was
// stored for has none - getSensorValue() answers 0 for both.
static void test_the_value_model_tells_a_stored_zero_from_no_value(void)
{
    SensorValueModel model;
    TEST_ASSERT_FALSE(model.hasSensorValue("m.00"));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, model.getSensorValue("m.00"));

    model.setSensorMultiChannel("m.00", false);
    model.setLastReadTime("m.00", 1234);
    TEST_ASSERT_FALSE_MESSAGE(model.hasSensorValue("m.00"),
                              "an entry without a stored value counts as a value");

    model.setSensorValue("m.00", 0.0f);
    TEST_ASSERT_TRUE(model.hasSensorValue("m.00"));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, model.getSensorValue("m.00"));

    model.removeSensor("m.00");
    TEST_ASSERT_FALSE_MESSAGE(model.hasSensorValue("m.00"), "a removed sensor still has a value");

    // Channel values are not a single value.
    model.setSensorMultiChannel("m.01", true);
    model.setChannelValue("m.01", "m.01.01", 5.0f);
    TEST_ASSERT_FALSE(model.hasSensorValue("m.01"));

    // On an entry that is not multi-channel a channel write is the single value (SensorValue).
    model.setSensorMultiChannel("m.02", false);
    model.setChannelValue("m.02", "m.02", 0.0f);
    TEST_ASSERT_TRUE(model.hasSensorValue("m.02"));
}

extern "C" void run_telemetry_before_first_reading_tests(void)
{
    RUN_TEST(test_a_stored_multi_channel_sensor_is_not_reported_before_its_first_reading);
    RUN_TEST(test_a_multi_channel_sensor_is_reported_channel_by_channel_from_its_first_reading);
    RUN_TEST(test_a_sensor_the_bus_has_not_answered_yet_stays_out_of_every_packet);
    RUN_TEST(test_a_single_channel_sensor_is_not_reported_before_its_first_reading);
    RUN_TEST(test_a_single_channel_sensor_is_reported_with_its_reading_even_when_it_reads_zero);
    RUN_TEST(test_a_multi_channel_sensor_failing_before_its_first_reading_is_reported_disconnected);
    RUN_TEST(test_a_single_channel_sensor_failing_before_its_first_reading_is_reported_disconnected);
    RUN_TEST(test_a_sensor_set_by_a_command_is_not_reported_before_its_first_reading);
    RUN_TEST(test_a_listening_module_learns_nothing_of_a_sensor_before_its_first_reading);
    RUN_TEST(test_the_value_model_tells_a_stored_zero_from_no_value);
}
