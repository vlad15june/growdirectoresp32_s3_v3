// Readings leave with the sensor they were read from.
//
// The value model (SensorValueModel) keeps a sensor's last readings under the uid of its registry
// row, <module>.<port>, and the update packet reports every channel kept there
// (SensorService::createSensorUpdateDto()). The uid belongs to the port, not to the device: once the
// row under it is removed or given another sensor, whatever the model still holds under the uid was
// read from a sensor the module no longer has. These tests take the paths that do that to a row and
// check what the packet says afterwards:
//
//   - DeleteSensors removes the rows of a port, and a sensor is added on the port later.
//   - SetSensorModel gives the port another model while the acquisition task still polls the list
//     it was last handed: the control loop hands a new one on its read interval
//     (SensorService::readModbusSensors()), so a poll of the old model can come back after the
//     command.
//   - ScanSensors on an occupied port replaces the row it finds there.
//
// What runs is production code: the real SensorAcquisitionService, stepped by the test as the
// acquisition tests do, polls through the real SensorReader and ModbusSensorReader on a simulated
// RS-485 line wired as the board is; SensorService applies the readings and builds the packet from
// the real SensorRepository (over an in-memory DAO) and SensorValueModel; the commands run through
// the real ModuleCommandExecutionUseCase, given the value model as globals.h gives it. Replaced are
// the line, the server end - which records the packet - and time. The plausibility gate is not
// wired, as in the telemetry tests: it is not what decides here.
//
// The records of the module structure belong to the port the same way: a sensor's channels are
// described under <module>.<port>.<channel> with the port's pin, and the structure goes to the
// server as the module holds it once a sensor command is done (CommandService). The last tests
// check what ScanSensors leaves there - on an occupied port, on a port it finds nothing on, with the
// model the port already has, when the flash refuses the row, and on a fresh module.
//
// And what a sensor command answers, and leaves the port with, when the flash refuses its row: a
// SetSensorModel or a ScanSensors that could not store the sensor it found fails, and the port keeps
// the sensor it had. The last test pins which port a ScanSensors without a pin scans.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Repositories/SensorRepository.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorDao.h"

#include "Sim/HostBusLock.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/VirtualClock.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using hostsim::HostBusLock;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimRs485Bus;
using hostsim::VirtualClock;

namespace {

const char* const MODULE_UID = "t98-module";

// Uids, default addresses and register layouts of three models of the platform's catalog.
const char* const GENERIC_MODBUS_UID = "2065c111-b0a8-4183-9430-40ad08e60eaa";
const char* const RY_WS301_UID = "4719a986-a300-48d5-bd1f-e34a1329798a";
const char* const MP_508C_UID = "144f6c6d-d210-44bf-a9c3-23c5f26472fe";

// SensorService's read and update intervals: one hand-over of the list and one packet per tick.
const unsigned long TICK_MS = 15000;

// The raw registers of a logged RY-WS301: 62.45 - 40 = 22.45 degrees, 55.10 percent.
const uint16_t AIR_TEMP_RAW = 6245;
const uint16_t HUMIDITY_RAW = 5510;

ModbusRegisterConfig channelConfig(uint8_t channel, uint16_t startRegister, float multiplier,
                                   float offset, const char* type, const char* name,
                                   float minValue, float maxValue)
{
    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = startRegister;
    config.numRegisters = 1;
    config.multiplier = multiplier;
    config.offset = offset;
    config.channel = channel;
    config.channelName = name;
    config.sensorType = type;
    config.canConnectMinValue = minValue;
    config.canConnectMaxValue = maxValue;
    return config;
}

SensorType modbusType(const char* uid, const char* name, int defaultAddress)
{
    SensorType type;
    type.uid = uid;
    type.name = name;
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.modbusDeviceAddress = defaultAddress;
    type.baudRate = 9600;
    return type;
}

/** @brief The generic "ModBus" model: register 0 as it is, channel 0. */
SensorType genericModbusType()
{
    SensorType type = modbusType(GENERIC_MODBUS_UID, "ModBus", 255);
    type.canScan = true;
    type.modbusRegisterConfig.push_back(
        channelConfig(0, 0, 1.0f, 0.0f, "UNKNOWN", "Unknown", 0.0f, 100000.0f));
    return type;
}

/** @brief RY-WS301: air temperature and humidity; the module computes the VPD from the two. */
SensorType ryWs301Type()
{
    SensorType type = modbusType(RY_WS301_UID, "ry_ws301", 255);
    type.modbusRegisterConfig.push_back(
        channelConfig(1, 0, 0.01f, -40.0f, "air_temp", "air_temp", 0.0f, 105.0f));
    type.modbusRegisterConfig.push_back(
        channelConfig(2, 1, 0.01f, 0.0f, "humidity", "humidity", 0.0f, 100.0f));
    return type;
}

/** @brief MP-508C: soil moisture and soil temperature - channels 1 and 2 again, other meanings. */
SensorType mp508cType()
{
    SensorType type = modbusType(MP_508C_UID, "mp-508c", 1);
    type.modbusRegisterConfig.push_back(
        channelConfig(1, 0, 0.1f, 0.0f, "soil_moisture_dig", "soil_moisture_dig", 0.0f, 100.0f));
    type.modbusRegisterConfig.push_back(
        channelConfig(2, 1, 0.01f, -20.0f, "soil_temp", "soil_temp", 10.0f, 40.0f));
    return type;
}

std::string uidOf(int pin)
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

/** @brief A row of the registry for a model on a port, at the port's own address. */
SensorEntity storedRow(const SensorType& type, int pin)
{
    SensorEntity row;
    row.uid = uidOf(pin);
    row.pin = pin;
    row.sensorType = type.name;
    row.model = type.name;
    row.protocol = type.getProtocolString();
    row.sensorTypeUid = type.uid;
    row.modbusDeviceAddress = MODBUS_ADDRESS_MIN + pin;
    row.baudRate = type.baudRate;
    row.modbusRegisterConfigs = type.modbusRegisterConfig;
    row.isActive = true;
    row.isError = false;
    row.status = SENSOR_STATUS_ADDED;
    return row;
}

/**
 * @brief A device at the port's own address with registers 0 and 1, each read where it is named.
 *        It does not answer the wildcard frames, as an RY-WS301 does not.
 */
SimRs485Bus::Device deviceAt(int pin, uint16_t register0, uint16_t register1)
{
    SimRs485Bus::Device device;
    device.address = static_cast<uint8_t>(MODBUS_ADDRESS_MIN + pin);
    device.registerValues.push_back(register0);
    device.registerValues.push_back(register1);
    device.registersAreAddressed = true;
    device.answersTheWildcard = false;
    return device;
}

std::string describe(const std::vector<SensorDto>& records)
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

const SensorDto* find(const std::vector<SensorDto>& records, const std::string& uid)
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

/** @brief Records of the sensor on the port: its own uid and its channels. */
size_t recordsOfPort(const std::vector<SensorDto>& records, int pin)
{
    const std::string uid = uidOf(pin);
    size_t count = 0;
    for (size_t i = 0; i < records.size(); i++)
    {
        const std::string& recordUid = records[i].uid;
        if (recordUid == uid ||
            (recordUid.size() > uid.size() && recordUid.compare(0, uid.size(), uid) == 0 &&
             recordUid[uid.size()] == '.'))
        {
            count++;
        }
    }
    return count;
}

void assertRecord(const std::vector<SensorDto>& records, const std::string& uid, float value,
                  const char* state)
{
    char message[400];
    const SensorDto* record = find(records, uid);
    snprintf(message, sizeof(message), "no record for %s in the packet (%s)", uid.c_str(),
             describe(records).c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(record, message);
    snprintf(message, sizeof(message), "%s went out %s, not %s", uid.c_str(), record->state.c_str(),
             state);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(state, record->state.c_str(), message);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, value, record->value, uid.c_str());
}

/** @brief What the packet must not carry: a record read from a sensor the port no longer has. */
void assertNoRecord(const std::vector<SensorDto>& records, const std::string& uid,
                    const char* when)
{
    char message[400];
    const SensorDto* record = find(records, uid);
    snprintf(message, sizeof(message), "%s: the packet carries %s=%.2f/%s (%s)", when, uid.c_str(),
             record != nullptr ? record->value : 0.0f,
             record != nullptr ? record->state.c_str() : "", describe(records).c_str());
    TEST_ASSERT_NULL_MESSAGE(record, message);
}

/**
 * @brief The acquisition task as the control loop sees it, with every reading it hands over written
 *        down. All calls go through to the real task; nothing is answered here.
 */
class WatchedAcquisition : public ISensorAcquisition {
public:
    struct Drained {
        std::string sensorUid;
        bool ok = false;
        std::vector<std::string> channelUids;
    };

    explicit WatchedAcquisition(ISensorAcquisition& task) : _task(task) {}

    std::vector<Drained> drained;

    void setSensors(const std::vector<SensorEntity>& modbusSensors) override
    {
        _task.setSensors(modbusSensors);
    }
    bool ensureRunning() override { return _task.ensureRunning(); }
    bool drain(std::vector<AcquiredReading>& out) override
    {
        const bool any = _task.drain(out);
        if (any)
        {
            for (size_t i = 0; i < out.size(); i++)
            {
                Drained entry;
                entry.sensorUid = out[i].sensorUid;
                entry.ok = out[i].ok;
                for (size_t c = 0; c < out[i].channels.size(); c++)
                {
                    entry.channelUids.push_back(out[i].channels[c].channelUid);
                }
                drained.push_back(entry);
            }
        }
        return any;
    }
    uint32_t submitBusCommand(const std::string& command, const std::string& options) override
    {
        return _task.submitBusCommand(command, options);
    }
    uint32_t submitComplexBusCommand(const ComplexCommandWork& work) override
    {
        return _task.submitComplexBusCommand(work);
    }
    uint32_t submitStartupScanStep(const StartupScanStep& step) override
    {
        return _task.submitStartupScanStep(step);
    }
    bool drainBusCommandResults(std::vector<BusCommandCompletion>& out) override
    {
        return _task.drainBusCommandResults(out);
    }

    /** @brief Did the loop take a reading of the sensor that carried this channel? */
    bool tookAReadingWith(const std::string& sensorUid, const std::string& channel) const
    {
        for (size_t i = 0; i < drained.size(); i++)
        {
            if (drained[i].sensorUid != sensorUid)
            {
                continue;
            }
            if (std::find(drained[i].channelUids.begin(), drained[i].channelUids.end(), channel) !=
                drained[i].channelUids.end())
            {
                return true;
            }
        }
        return false;
    }

private:
    ISensorAcquisition& _task;
};

/**
 * @brief The registry's flash, which can be told to refuse a removal or a write of a row.
 *
 * The registry reaches it as one row or as the whole registry, and a refusal goes by what the write
 * does to the rows: a whole registry with fewer rows than the flash holds is a removal, any other
 * one puts a row in.
 */
class RegistryFlash : public MockSpiffsSensorDao {
public:
    bool refuseRemoval = false;
    bool refuseSave = false;

    bool remove(const std::string& uid) override
    {
        return refuseRemoval ? false : MockSpiffsSensorDao::remove(uid);
    }

    bool save(const SensorEntity& entity, const std::string& uid) override
    {
        return refuseSave ? false : MockSpiffsSensorDao::save(entity, uid);
    }

    bool saveAll(const std::vector<SensorEntity>& entities) override
    {
        const bool removal = entities.size() < getStorage().size();
        if (removal ? refuseRemoval : refuseSave)
        {
            return false;
        }
        return MockSpiffsSensorDao::saveAll(entities);
    }
};

/** @brief The module's sensor side, wired the way globals.h wires it. */
struct Stand {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;
    HostBusLock busLock;

    SimRs485Bus port0;
    SimRs485Bus port1;
    SimRs485Bus port2;
    SimRs485Bus port3;
    std::vector<IModbusRS485*> ports;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    RegistryFlash sensorDao;
    SensorRepository sensorRepository;
    MockSensorTypeRepository sensorTypeRepository;
    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;

    ModbusSensorReader modbusSensorReader;
    SensorReader sensorReader;
    SensorAcquisitionService acquisitionTask;
    WatchedAcquisition acquisition;

    MockHybridAPIService server;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    // Rebuilds the records of the node from the registry and sends the structure: what the module
    // holds after a restart for every stored sensor it describes as itself.
    RepublishModuleStructureUseCase structureFromTheRegistry;
    ModbusAddressAllocator addressAllocator;
    ModuleCommandExecutionUseCase commands;
    SensorService sensorService;

    uint64_t lastTickMs = 0;

    Stand()
        : scheduler(clock),
          arduino(clock, &scheduler),
          busLock(scheduler, clock),
          port0(clock, "sim-port-0"),
          port1(clock, "sim-port-1"),
          port2(clock, "sim-port-2"),
          port3(clock, "sim-port-3"),
          ports{&port0, &port1, &port2, &port3},
          moduleConfig(&logger, &debugSystem, &settings),
          sensorRepository(&sensorDao),
          modbusSensorReader(ports, arduino, &logger, &debugSystem),
          sensorReader(nullptr, &modbusSensorReader, &arduino, &logger, &debugSystem),
          acquisitionTask(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          acquisition(acquisitionTask),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          structureFromTheRegistry(&moduleStructure, &sensorRepository, &server, &sensorStructure,
                                   &logger, &debugSystem, &arduino),
          addressAllocator(&sensorRepository),
          commands(&logger, &debugSystem, &timeAdapter, nullptr, &arduino, &sensorRepository,
                   &sensorTypeRepository, &sensorReader, &moduleConfig, &settings, &server,
                   nullptr, &moduleStructure, &sensorValueModel, nullptr, nullptr, nullptr,
                   nullptr, nullptr, nullptr, &addressAllocator),
          sensorService(&moduleConfig, &moduleStructure, &sensorRepository, &sensorReader,
                        &sensorValueModel, &timeAdapter, &arduino, &server, &logger,
                        &debugSystem, nullptr, nullptr, nullptr, &virtualChannels)
    {
        debugSystem.featureEnabled = false;

        timeAdapter.setEpochTime(1757066400);
        settings.setUser("t98@example.com");
        settings.setAccountUid("t98-account");
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setSensorPinCount(4);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setSettings(&settings);
        moduleStructure.nodes.push_back(Node());

        // Ports 0 and 1 on one UART and 2 and 3 on the other, each port with its own power pin,
        // all of them down until something powers them - as the board has them.
        port0.shareTheLineWith(port1);
        port2.shareTheLineWith(port3);
        for (size_t i = 0; i < ports.size(); i++)
        {
            SimRs485Bus* port = static_cast<SimRs485Bus*>(ports[i]);
            port->setPortPowerLikeTheBoard(true);
            port->setMasterArduino(&arduino);
            port->startUnpowered();
        }

        modbusSensorReader.setBusLock(&busLock);
        sensorReader.setBusLock(&busLock);
        // The boot begins the UARTs before the first frame (ModuleSetupService::setupSensorBus()).
        sensorReader.init();

        commands.setSensorStructureUseCase(&sensorStructure);
        sensorService.setAcquisition(&acquisition);
    }

    /**
     * @brief One control-loop iteration on the next read tick: the loop applies what the
     *        acquisition task handed back, hands it the list again and sends the packet.
     */
    std::vector<SensorDto> tick()
    {
        const uint64_t at = std::max<uint64_t>(lastTickMs + TICK_MS, clock.nowMs());
        clock.advanceMs(at - clock.nowMs());
        lastTickMs = at;

        server.sendModuleUpdateDataCalled = false;
        sensorService.loop();

        char message[96];
        snprintf(message, sizeof(message), "t=%llu ms: no packet went out", (unsigned long long)at);
        TEST_ASSERT_TRUE_MESSAGE(server.sendModuleUpdateDataCalled, message);
        printf("  t98: t=%llu ms - %s\n", (unsigned long long)at,
               describe(server.lastUpdateData.sensors).c_str());
        return server.lastUpdateData.sensors;
    }

    /**
     * @brief Let the acquisition task go on until it has polled `count` sensors, sleeping as it
     *        asks. It polls the list it was last handed, whatever the registry holds by now.
     */
    void pollSensors(uint64_t count)
    {
        const uint64_t target = acquisitionTask.pollCount() + count;
        for (int i = 0; i < 4000 && acquisitionTask.pollCount() < target; i++)
        {
            const uint32_t sleepMs = acquisitionTask.step();
            if (sleepMs > 0)
            {
                clock.advanceMs(sleepMs);
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(acquisitionTask.pollCount() >= target,
                                 "the acquisition task did not get to poll");
    }

    /** @brief A command from the server, executed the way the control loop executes it. */
    ModuleCommandExecutionResult run(const char* command, const std::string& options)
    {
        ModuleCommandDto dto;
        dto.uid = std::string("t98-") + command;
        dto.command = command;
        dto.options = options;
        dto.createdAt = "";
        std::vector<ModuleCommandExecutionResult> results = commands.executeCommands({dto});
        TEST_ASSERT_EQUAL_UINT(1, results.size());
        return results[0];
    }

    std::string modelOfThePort(int pin)
    {
        Utils::Optional<SensorEntity> row = sensorRepository.get(uidOf(pin).c_str());
        return row.has_value() ? row.value().model : std::string("(no row)");
    }

    /** @brief The structure the module holds for its stored sensors: rebuilt and sent. */
    void describeTheRegistry()
    {
        TEST_ASSERT_TRUE_MESSAGE(structureFromTheRegistry.execute("stored_sensors"),
                                 "the structure built from the registry did not go out");
    }
};

/** @brief Options of a command naming a port; the server's pins 4/3/2/1 are ports 0/1/2/3. */
std::string pinOptions(int port, const char* modelUid = nullptr)
{
    char options[160];
    if (modelUid != nullptr)
    {
        snprintf(options, sizeof(options), "{\"pin\":%d,\"uid\":\"%s\"}", 4 - port, modelUid);
    }
    else
    {
        snprintf(options, sizeof(options), "{\"pin\":%d}", 4 - port);
    }
    return std::string(options);
}

/** @brief One record of the structure, as "uid:type". */
std::string structureRecord(int pin, int channel, const char* type)
{
    return channelUid(pin, channel) + ":" + type;
}

/** @brief The records the structure holds with a port's pin, as "uid:type", sorted. */
std::vector<std::string> structureOfPort(const ModuleStructure& structure, int pin)
{
    std::vector<std::string> records;
    for (size_t i = 0; i < structure.nodes[0].sensors.size(); i++)
    {
        const Sensor& record = structure.nodes[0].sensors[i];
        if (record.pin == pin)
        {
            records.push_back(record.uid + ":" + record.type);
        }
    }
    std::sort(records.begin(), records.end());
    return records;
}

std::string joinRecords(const std::vector<std::string>& records)
{
    if (records.empty())
    {
        return "no records";
    }
    std::string text;
    for (size_t i = 0; i < records.size(); i++)
    {
        text += (i == 0 ? "" : " ") + records[i];
    }
    return text;
}

/** @brief What the structure says about a port: these records, each of them once, and no other. */
void assertStructureOfPort(const ModuleStructure& structure, int pin,
                           std::vector<std::string> expected, const char* when)
{
    std::sort(expected.begin(), expected.end());
    const std::string held = joinRecords(structureOfPort(structure, pin));
    printf("  t98: %s - port %d: %s\n", when, pin, held.c_str());
    char message[512];
    snprintf(message, sizeof(message), "%s: the structure describes port %d as %s", when, pin,
             held.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(joinRecords(expected).c_str(), held.c_str(), message);
}

/**
 * @brief The acquisition task as the control loop sees it, handing back the readings a test
 *        queues - for the one case the line of these stands does not produce on its own.
 */
class QueuedAcquisition : public ISensorAcquisition {
public:
    std::vector<AcquiredReading> queued;

    void setSensors(const std::vector<SensorEntity>&) override {}
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

} // namespace

// (a) DeleteSensors removes the RY-WS301 of port 1; later, without a restart, ScanSensors adds the
//     MP-508C plugged in there. Until the new sensor is read the packet has nothing to say about
//     port 1, and once it is read it says what the MP-508C read - and nothing of the RY-WS301.
static void test_a_sensor_added_on_a_port_whose_sensors_were_deleted_starts_without_their_readings(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(mp508cType());
    stand.port0.addDevice(deviceAt(0, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(ryWs301Type(), 0));
    stand.sensorRepository.save(storedRow(ryWs301Type(), 1));

    stand.tick();
    stand.pollSensors(2);
    const std::vector<SensorDto> before = stand.tick();
    // The premise: both RY-WS301s are read and reported.
    assertRecord(before, channelUid(1, 1), 22.45f, "online");
    assertRecord(before, channelUid(1, 2), 55.10f, "online");
    TEST_ASSERT_NOT_NULL(find(before, channelUid(1, 3)));

    const ModuleCommandExecutionResult deleted = stand.run("DeleteSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", deleted.status.c_str(), deleted.errorMessage.c_str());
    TEST_ASSERT_FALSE_MESSAGE(stand.sensorRepository.get(uidOf(1).c_str()).has_value(),
                              "the premise: DeleteSensors removed the row of port 1");

    // What the model keeps under the port's uid once its row is gone.
    char message[200];
    snprintf(message, sizeof(message),
             "the readings of the deleted sensor stayed under %s (read at t=%u ms)",
             uidOf(1).c_str(), (unsigned)stand.sensorValueModel.getLastReadTime(uidOf(1)));
    TEST_ASSERT_FALSE_MESSAGE(stand.sensorValueModel.hasChannelValues(uidOf(1)), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stand.sensorValueModel.getLastReadTime(uidOf(1)), message);

    const std::vector<SensorDto> afterDelete = stand.tick();
    TEST_ASSERT_EQUAL_UINT(0, recordsOfPort(afterDelete, 1));

    // Another sensor on port 1, added by a scan, no restart in between.
    stand.port1.removeDevice(static_cast<uint8_t>(MODBUS_ADDRESS_MIN + 1));
    stand.port1.addDevice(deviceAt(1, 312, 4225));
    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("mp-508c", stand.modelOfThePort(1).c_str(),
                                     "the premise: the scan added the MP-508C on port 1");

    const std::vector<SensorDto> added = stand.tick();
    assertNoRecord(added, channelUid(1, 1), "the first packet after the MP-508C was added");
    assertNoRecord(added, channelUid(1, 2), "the first packet after the MP-508C was added");
    assertNoRecord(added, channelUid(1, 3), "the first packet after the MP-508C was added");
    TEST_ASSERT_EQUAL_UINT(0, recordsOfPort(added, 1));
    assertRecord(added, channelUid(0, 1), 22.45f, "online");

    stand.pollSensors(2);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(1, 1), 31.2f, "online");
    assertRecord(read, channelUid(1, 2), 22.25f, "online");
    // The MP-508C has no computed channel: a VPD on port 1 is the RY-WS301's.
    assertNoRecord(read, channelUid(1, 3), "after the MP-508C was read");
    TEST_ASSERT_EQUAL_UINT(2, recordsOfPort(read, 1));
}

// (a) A removal the flash refuses leaves the row where it was - DeleteSensors answers with an error
//     - and the sensor that is still there keeps its readings and goes on being reported.
static void test_a_sensor_whose_removal_the_flash_refused_keeps_its_readings(void)
{
    Stand stand;
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(ryWs301Type(), 1));

    stand.tick();
    stand.pollSensors(1);
    assertRecord(stand.tick(), channelUid(1, 1), 22.45f, "online");

    stand.sensorDao.refuseRemoval = true;
    const ModuleCommandExecutionResult refused = stand.run("DeleteSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING("Error", refused.status.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ry_ws301", stand.modelOfThePort(1).c_str(),
                                     "the premise: the row stayed where it was");

    // The next packet, before the task has polled again: what it says comes from the value model.
    const std::vector<SensorDto> after = stand.tick();
    assertRecord(after, channelUid(1, 1), 22.45f, "online");
    assertRecord(after, channelUid(1, 2), 55.10f, "online");
    TEST_ASSERT_EQUAL_UINT(3, recordsOfPort(after, 1));
}

// (b) SetSensorModel gives port 1 the RY-WS301 in place of the generic "ModBus" model it was added
//     as. The acquisition task polls the generic model once more before the loop hands it the new
//     list, and that answer comes back after the command. The packets from there on carry the
//     RY-WS301's channels and nothing under the generic model's channel 0.
static void test_a_poll_of_the_model_a_port_had_before_set_sensor_model_is_not_reported_after_it(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(genericModbusType());
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(genericModbusType(), 1));

    stand.tick();
    stand.pollSensors(1);
    const std::vector<SensorDto> before = stand.tick();
    // The premise: the generic model reads register 0 as it is.
    assertRecord(before, channelUid(1, 0), 6245.0f, "online");

    const ModuleCommandExecutionResult set =
        stand.run("SetSensorModel", pinOptions(1, RY_WS301_UID));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", set.status.c_str(), set.errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING("ry_ws301", stand.modelOfThePort(1).c_str());
    TEST_ASSERT_FALSE_MESSAGE(stand.sensorValueModel.hasChannelValues(uidOf(1)),
                              "the premise: the command dropped the generic model's readings");

    // The task polls before the loop's next hand-over: the list it holds is the one from before
    // the command.
    stand.acquisition.drained.clear();
    stand.pollSensors(1);
    const std::vector<SensorDto> afterCommand = stand.tick();
    TEST_ASSERT_TRUE_MESSAGE(stand.acquisition.tookAReadingWith(uidOf(1), channelUid(1, 0)),
                             "the premise: the loop took a poll of the generic model after the command");
    assertNoRecord(afterCommand, channelUid(1, 0), "the first packet after SetSensorModel");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, recordsOfPort(afterCommand, 1),
                                   "the RY-WS301 is reported before it was read");

    // That tick handed the new list over; from here the task polls the RY-WS301.
    stand.pollSensors(1);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(1, 1), 22.45f, "online");
    assertRecord(read, channelUid(1, 2), 55.10f, "online");
    TEST_ASSERT_NOT_NULL_MESSAGE(find(read, channelUid(1, 3)), "no VPD after the RY-WS301 was read");
    assertNoRecord(read, channelUid(1, 0), "after the RY-WS301 was read");
    TEST_ASSERT_EQUAL_UINT(3, recordsOfPort(read, 1));

    size_t carryingTheOldChannel = 0;
    for (int i = 0; i < 6; i++)
    {
        stand.pollSensors(1);
        if (find(stand.tick(), channelUid(1, 0)) != nullptr)
        {
            carryingTheOldChannel++;
        }
    }
    printf("  t98: generic -> RY-WS301: %u of the 6 packets after it carry channel 0\n",
           (unsigned)carryingTheOldChannel);
    TEST_ASSERT_EQUAL_UINT(0, carryingTheOldChannel);
}

// (b) The same between two models whose channels have the same numbers - RY-WS301 to MP-508C,
//     channels 1 and 2 in both: a poll of the RY-WS301 that comes back after the command is not
//     the MP-508C's soil moisture and soil temperature.
static void test_a_poll_of_the_old_model_is_not_taken_for_the_new_one_on_the_same_channel_numbers(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.sensorTypeRepository.addSensorType(mp508cType());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(ryWs301Type(), 1));

    stand.tick();
    stand.pollSensors(1);
    assertRecord(stand.tick(), channelUid(1, 1), 22.45f, "online");

    // The device on the port is an MP-508C now, and the app says so.
    stand.port1.removeDevice(static_cast<uint8_t>(MODBUS_ADDRESS_MIN + 1));
    stand.port1.addDevice(deviceAt(1, 312, 4225));
    const ModuleCommandExecutionResult set =
        stand.run("SetSensorModel", pinOptions(1, MP_508C_UID));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", set.status.c_str(), set.errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING("mp-508c", stand.modelOfThePort(1).c_str());

    stand.acquisition.drained.clear();
    stand.pollSensors(1);
    const std::vector<SensorDto> afterCommand = stand.tick();
    TEST_ASSERT_TRUE_MESSAGE(stand.acquisition.tookAReadingWith(uidOf(1), channelUid(1, 1)),
                             "the premise: the loop took a poll of the RY-WS301 after the command");
    // Polled as an RY-WS301, the MP-508C's registers read 3.12 - 40 and 42.25.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, recordsOfPort(afterCommand, 1),
                                   "the packet reports a poll of the RY-WS301 as the MP-508C");

    stand.pollSensors(1);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(1, 1), 31.2f, "online");
    assertRecord(read, channelUid(1, 2), 22.25f, "online");
    TEST_ASSERT_EQUAL_UINT(2, recordsOfPort(read, 1));
}

// (b) A poll of the old model that failed - the device no longer answers where the old row asked
//     it - is not a failure of the sensor the port has now: the new sensor is not reported
//     disconnected for it. The line of the stand above answers wherever the device is, so the
//     failed answer is handed over here directly, as the task hands it: polled as the old model.
static void test_a_failed_poll_of_the_model_a_port_had_before_does_not_fail_the_new_sensor(void)
{
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig(&logger, &debugSystem, &settings);
    MockNTPTimeAdapter timeAdapter;
    MockArduinoAdapter arduino;
    MockSpiffsSensorDao sensorDao;
    SensorRepository sensorRepository(&sensorDao);
    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;
    MockHybridAPIService server;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels(&vpdChannel);
    QueuedAcquisition acquisition;
    SensorService sensorService(&moduleConfig, &moduleStructure, &sensorRepository, nullptr,
                                &sensorValueModel, &timeAdapter, &arduino, &server, &logger,
                                &debugSystem, nullptr, nullptr, nullptr, &virtualChannels);
    debugSystem.featureEnabled = false;
    moduleConfig.setModuleUid(MODULE_UID);
    moduleConfig.setSettings(&settings);
    sensorService.setAcquisition(&acquisition);

    // The row SetSensorModel left: the RY-WS301, not read yet.
    sensorRepository.save(storedRow(ryWs301Type(), 1));

    AcquiredReading failed;
    failed.sensorUid = uidOf(1);
    failed.model = "ModBus";
    failed.port = 1;
    failed.multiChannel = true;
    failed.ok = false;
    failed.acquiredAtMs = 14000;
    acquisition.queued.push_back(failed);

    arduino.setMillis(TICK_MS);
    sensorService.loop();
    TEST_ASSERT_TRUE(server.sendModuleUpdateDataCalled);
    const std::vector<SensorDto> packet = server.lastUpdateData.sensors;
    printf("  t98: a failed poll of the old model - %s\n", describe(packet).c_str());

    Utils::Optional<SensorEntity> row = sensorRepository.get(uidOf(1).c_str());
    TEST_ASSERT_TRUE(row.has_value());
    TEST_ASSERT_FALSE_MESSAGE(row.value().isError,
                              "a failed poll of the old model put the RY-WS301 in error");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, recordsOfPort(packet, 1),
                                   "the RY-WS301 went out disconnected for a poll of the old model");
}

// (c) ScanSensors on a port that already has a sensor replaces the row: the generic "ModBus" row of
//     port 1 becomes the RY-WS301 the scan finds. Nothing of the generic model's reading stays.
static void test_a_scan_that_replaces_the_sensor_of_an_occupied_port_drops_the_old_readings(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(genericModbusType(), 1));

    stand.tick();
    stand.pollSensors(1);
    assertRecord(stand.tick(), channelUid(1, 0), 6245.0f, "online");

    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ry_ws301", stand.modelOfThePort(1).c_str(),
                                     "the premise: the scan replaced the generic row of port 1");

    // The loop hands the new list over before the task polls again: what is in the packet is what
    // the command left in the value model.
    const std::vector<SensorDto> afterScan = stand.tick();
    assertNoRecord(afterScan, channelUid(1, 0), "the first packet after the scan");
    TEST_ASSERT_EQUAL_UINT(0, recordsOfPort(afterScan, 1));

    stand.pollSensors(1);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(1, 1), 22.45f, "online");
    assertRecord(read, channelUid(1, 2), 55.10f, "online");
    assertNoRecord(read, channelUid(1, 0), "after the RY-WS301 was read");
    TEST_ASSERT_EQUAL_UINT(3, recordsOfPort(read, 1));
}

// (c) A fresh module - empty registry, empty value model: ScanSensors adds the RY-WS301, which is
//     left out of the packet until it is read and reported channel by channel from then on.
static void test_a_scan_on_a_fresh_module_reports_the_sensor_it_adds_from_its_first_reading(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));

    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING("ry_ws301", stand.modelOfThePort(1).c_str());

    TEST_ASSERT_EQUAL_UINT(0, recordsOfPort(stand.tick(), 1));
    stand.pollSensors(1);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(1, 1), 22.45f, "online");
    assertRecord(read, channelUid(1, 2), 55.10f, "online");
    TEST_ASSERT_EQUAL_UINT(3, recordsOfPort(read, 1));
}

// (c) A scan whose row the flash refused to write leaves the row of the port as it was - the generic
//     model, still read - and that sensor keeps its readings.
static void test_a_scan_whose_row_the_flash_refused_keeps_the_readings_of_the_sensor_still_there(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(genericModbusType(), 1));

    stand.tick();
    stand.pollSensors(1);
    assertRecord(stand.tick(), channelUid(1, 0), 6245.0f, "online");

    stand.sensorDao.refuseSave = true;
    stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ModBus", stand.modelOfThePort(1).c_str(),
                                     "the premise: the row of port 1 stayed the generic model");

    // The next packet, before the task has polled again: what it says comes from the value model.
    const std::vector<SensorDto> after = stand.tick();
    assertRecord(after, channelUid(1, 0), 6245.0f, "online");
    TEST_ASSERT_EQUAL_UINT(1, recordsOfPort(after, 1));
}

// (d) ScanSensors finds an RY-WS301 on port 1, which the module holds as the generic "ModBus" model.
//     The structure that goes to the server after the command describes port 1 by the RY-WS301's
//     channels alone: channel 0 belonged to the generic row the scan replaced. Port 0 is not touched.
static void test_a_scan_that_replaces_the_sensor_of_an_occupied_port_leaves_none_of_its_records(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port0.addDevice(deviceAt(0, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(ryWs301Type(), 0));
    stand.sensorRepository.save(storedRow(genericModbusType(), 1));
    stand.describeTheRegistry();

    const std::vector<std::string> portZero = {structureRecord(0, 1, "air_temp"),
                                               structureRecord(0, 2, "humidity"),
                                               structureRecord(0, 3, "vpd")};
    assertStructureOfPort(stand.moduleStructure, 0, portZero, "the premise, port 0");
    assertStructureOfPort(stand.moduleStructure, 1, {structureRecord(1, 0, "UNKNOWN")},
                          "the premise, port 1");

    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ry_ws301", stand.modelOfThePort(1).c_str(),
                                     "the premise: the scan replaced the generic row of port 1");

    assertStructureOfPort(stand.moduleStructure, 1,
                          {structureRecord(1, 1, "air_temp"), structureRecord(1, 2, "humidity"),
                           structureRecord(1, 3, "vpd")},
                          "after the scan");
    assertStructureOfPort(stand.moduleStructure, 0, portZero, "after the scan");
    TEST_ASSERT_EQUAL_UINT(6, stand.moduleStructure.nodes[0].sensors.size());
}

// (d) The same with a model that has no computed channel: an MP-508C found where an RY-WS301 was.
//     Channels 1 and 2 are in both models and are described anew; the VPD of channel 3 is the
//     RY-WS301's and goes with it.
static void test_a_scan_that_finds_a_model_without_the_computed_channel_takes_it_out(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(mp508cType());
    stand.port1.addDevice(deviceAt(1, 312, 4225));
    stand.sensorRepository.save(storedRow(ryWs301Type(), 1));
    stand.describeTheRegistry();
    assertStructureOfPort(stand.moduleStructure, 1,
                          {structureRecord(1, 1, "air_temp"), structureRecord(1, 2, "humidity"),
                           structureRecord(1, 3, "vpd")},
                          "the premise");

    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("mp-508c", stand.modelOfThePort(1).c_str(),
                                     "the premise: the scan replaced the RY-WS301 of port 1");

    assertStructureOfPort(stand.moduleStructure, 1,
                          {structureRecord(1, 1, "soil_moisture_dig"),
                           structureRecord(1, 2, "soil_temp")},
                          "after the scan");
}

// (d) A scan that finds nothing on an occupied port saves nothing: the row stays, and so do the
//     records that describe it.
static void test_a_scan_that_finds_nothing_on_an_occupied_port_leaves_its_records(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(mp508cType());
    stand.sensorRepository.save(storedRow(ryWs301Type(), 1));
    stand.describeTheRegistry();

    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, scanned.result.sensors.size(),
                                   "the premise: the scan found nothing on port 1");
    TEST_ASSERT_EQUAL_STRING("ry_ws301", stand.modelOfThePort(1).c_str());

    assertStructureOfPort(stand.moduleStructure, 1,
                          {structureRecord(1, 1, "air_temp"), structureRecord(1, 2, "humidity"),
                           structureRecord(1, 3, "vpd")},
                          "after a scan that found nothing");
}

// (d) A scan that finds the model the port already has describes it once, as it was.
static void test_a_scan_that_finds_the_model_the_port_has_describes_it_once(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(ryWs301Type(), 1));
    stand.describeTheRegistry();

    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, scanned.result.sensors.size(),
                                   "the premise: the scan found the RY-WS301 on port 1");

    assertStructureOfPort(stand.moduleStructure, 1,
                          {structureRecord(1, 1, "air_temp"), structureRecord(1, 2, "humidity"),
                           structureRecord(1, 3, "vpd")},
                          "after a scan that found the same model");
    TEST_ASSERT_EQUAL_UINT(3, stand.moduleStructure.nodes[0].sensors.size());
}

// (d) The flash refuses the row the scan found: the registry keeps the generic row of port 1, and the
//     structure goes on describing what the registry holds.
static void test_a_scan_whose_row_the_flash_refused_leaves_the_records_of_the_row_still_there(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(genericModbusType(), 1));
    stand.describeTheRegistry();

    stand.sensorDao.refuseSave = true;
    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    printf("  t98: a scan whose row the flash refused answers %s with %u sensor(s)\n",
           scanned.status.c_str(), (unsigned)scanned.result.sensors.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ModBus", stand.modelOfThePort(1).c_str(),
                                     "the premise: the row of port 1 stayed the generic model");

    assertStructureOfPort(stand.moduleStructure, 1, {structureRecord(1, 0, "UNKNOWN")},
                          "after a scan whose row the flash refused");
}

// (d) A fresh module - empty registry. The first boot that could not reach the server for the
//     catalog describes every port as empty (ModuleSetupService::createPlaceholderSensors(): one
//     record per port, uid <module>.<port>, type "none"); the records are written out here as that
//     function writes them. ScanSensors then adds an RY-WS301 on port 1: the port is no longer empty
//     and the structure stops saying it is. The other ports keep their records.
static void test_a_scan_on_a_fresh_module_describes_the_port_it_fills_by_its_sensor_alone(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    for (int pin = 0; pin < 4; pin++)
    {
        Sensor emptyPort;
        emptyPort.pin = pin;
        emptyPort.type = "none";
        emptyPort.uid = uidOf(pin);
        stand.moduleStructure.nodes[0].sensors.push_back(emptyPort);
    }

    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING("ry_ws301", stand.modelOfThePort(1).c_str());

    assertStructureOfPort(stand.moduleStructure, 1,
                          {structureRecord(1, 1, "air_temp"), structureRecord(1, 2, "humidity"),
                           structureRecord(1, 3, "vpd")},
                          "a fresh module after the scan");
    for (int pin = 0; pin < 4; pin++)
    {
        if (pin != 1)
        {
            assertStructureOfPort(stand.moduleStructure, pin, {uidOf(pin) + ":none"},
                                  "a fresh module after the scan");
        }
    }
}

// (d) A fresh module whose restart has described its registry - nothing in it, so a node without a
//     single record: the scan adds the RY-WS301's records and nothing else.
static void test_a_scan_on_a_fresh_module_with_an_empty_structure_adds_its_sensor(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.describeTheRegistry();
    TEST_ASSERT_EQUAL_UINT(0, stand.moduleStructure.nodes[0].sensors.size());

    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());

    assertStructureOfPort(stand.moduleStructure, 1,
                          {structureRecord(1, 1, "air_temp"), structureRecord(1, 2, "humidity"),
                           structureRecord(1, 3, "vpd")},
                          "an empty structure after the scan");
    TEST_ASSERT_EQUAL_UINT(3, stand.moduleStructure.nodes[0].sensors.size());
}

// (e) The flash takes the removal of a row and refuses the write of one. SetSensorModel fails, and
//     port 1 keeps the sensor it had: the row, its record in the structure and its reading.
static void test_a_set_sensor_model_whose_row_the_flash_refused_keeps_the_sensor_of_the_port(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(genericModbusType());
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(genericModbusType(), 1));
    stand.describeTheRegistry();

    stand.tick();
    stand.pollSensors(1);
    assertRecord(stand.tick(), channelUid(1, 0), 6245.0f, "online");

    stand.sensorDao.refuseSave = true;
    const ModuleCommandExecutionResult set =
        stand.run("SetSensorModel", pinOptions(1, RY_WS301_UID));
    printf("  t98: a SetSensorModel whose row the flash refused answers %s (%s)\n",
           set.status.c_str(), set.errorMessage.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Error", set.status.c_str(),
                                     "the premise: the flash refused the new row");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("ModBus", stand.modelOfThePort(1).c_str(),
                                     "port 1 lost its row to a write the flash refused");
    assertStructureOfPort(stand.moduleStructure, 1, {structureRecord(1, 0, "UNKNOWN")},
                          "after a SetSensorModel whose row the flash refused");

    // The next packet, before the task has polled again: what it says comes from the value model.
    const std::vector<SensorDto> after = stand.tick();
    assertRecord(after, channelUid(1, 0), 6245.0f, "online");
    TEST_ASSERT_EQUAL_UINT(1, recordsOfPort(after, 1));
}

// (e) A flash that refuses every write - full: the removal fails as well as the new row. Port 1 keeps
//     the records the structure had for it.
static void test_a_set_sensor_model_on_a_flash_that_refuses_every_write_leaves_the_port_as_it_was(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(genericModbusType());
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(genericModbusType(), 1));
    stand.describeTheRegistry();

    stand.sensorDao.refuseSave = true;
    stand.sensorDao.refuseRemoval = true;
    const ModuleCommandExecutionResult set =
        stand.run("SetSensorModel", pinOptions(1, RY_WS301_UID));
    TEST_ASSERT_EQUAL_STRING("Error", set.status.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ModBus", stand.modelOfThePort(1).c_str(),
                                     "the premise: the row of port 1 stayed the generic model");

    assertStructureOfPort(stand.moduleStructure, 1, {structureRecord(1, 0, "UNKNOWN")},
                          "after a SetSensorModel on a flash that refuses every write");
}

// (e) A fresh module - empty registry, every port described as empty, as the first boot without the
//     catalog leaves it (see the fresh-module scan test above). SetSensorModel on port 1 with the
//     flash refusing the row: no row, and port 1 is still described as empty.
static void test_a_set_sensor_model_on_a_fresh_module_whose_row_the_flash_refused_leaves_the_port_empty(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    for (int pin = 0; pin < 4; pin++)
    {
        Sensor emptyPort;
        emptyPort.pin = pin;
        emptyPort.type = "none";
        emptyPort.uid = uidOf(pin);
        stand.moduleStructure.nodes[0].sensors.push_back(emptyPort);
    }

    stand.sensorDao.refuseSave = true;
    const ModuleCommandExecutionResult set =
        stand.run("SetSensorModel", pinOptions(1, RY_WS301_UID));
    TEST_ASSERT_EQUAL_STRING("Error", set.status.c_str());
    TEST_ASSERT_EQUAL_STRING("(no row)", stand.modelOfThePort(1).c_str());

    for (int pin = 0; pin < 4; pin++)
    {
        assertStructureOfPort(stand.moduleStructure, pin, {uidOf(pin) + ":none"},
                              "a fresh module after a SetSensorModel whose row the flash refused");
    }
}

// (e) SetSensorModel on a port that also carries a row under another uid: the new row is stored under
//     the port's uid, and the other row leaves the port with the records it had.
static void test_a_set_sensor_model_takes_a_row_of_another_uid_off_its_port(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    SensorEntity other = storedRow(genericModbusType(), 1);
    other.uid = std::string(MODULE_UID) + ".other";
    // At an address nothing answers on, so that the port's own address is free for the device.
    other.modbusDeviceAddress = MODBUS_ADDRESS_MAX;
    stand.sensorRepository.save(other);
    stand.describeTheRegistry();
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, structureOfPort(stand.moduleStructure, 1).size(),
                                   "the premise: the other row is described on port 1");

    const ModuleCommandExecutionResult set =
        stand.run("SetSensorModel", pinOptions(1, RY_WS301_UID));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", set.status.c_str(), set.errorMessage.c_str());

    TEST_ASSERT_EQUAL_STRING("ry_ws301", stand.modelOfThePort(1).c_str());
    TEST_ASSERT_FALSE_MESSAGE(stand.sensorRepository.get(other.uid.c_str()).has_value(),
                              "the row of another uid stayed on the port");
    TEST_ASSERT_EQUAL_INT(1, stand.sensorRepository.count());
    assertStructureOfPort(stand.moduleStructure, 1,
                          {structureRecord(1, 1, "air_temp"), structureRecord(1, 2, "humidity"),
                           structureRecord(1, 3, "vpd")},
                          "after a SetSensorModel over a row of another uid");
}

// (e) ScanSensors whose row the flash refused answers the way SetSensorModel answers it: an error, and
//     no sensor in the result - the port holds the row it had, not the one the scan found.
static void test_a_scan_whose_row_the_flash_refused_answers_with_an_error(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port1.addDevice(deviceAt(1, AIR_TEMP_RAW, HUMIDITY_RAW));
    stand.sensorRepository.save(storedRow(genericModbusType(), 1));

    stand.sensorDao.refuseSave = true;
    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", pinOptions(1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ModBus", stand.modelOfThePort(1).c_str(),
                                     "the premise: the row of port 1 stayed the generic model");

    char message[200];
    snprintf(message, sizeof(message),
             "a scan whose row the flash refused answered %s with %u sensor(s)",
             scanned.status.c_str(), (unsigned)scanned.result.sensors.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Error", scanned.status.c_str(), message);
    TEST_ASSERT_EQUAL_STRING("Failed to save sensor", scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, scanned.result.sensors.size(), message);
}

// (f) What the options of a ScanSensors decide. Options that name no pin are read as pin 0 - port 0 -
//     and the scan runs there; no options at all are refused.
static void test_a_scan_whose_options_name_no_pin_scans_port_0(void)
{
    Stand stand;
    stand.sensorTypeRepository.addSensorType(ryWs301Type());
    stand.port0.addDevice(deviceAt(0, AIR_TEMP_RAW, HUMIDITY_RAW));

    const ModuleCommandExecutionResult scanned = stand.run("ScanSensors", "{}");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Done", scanned.status.c_str(), scanned.errorMessage.c_str());
    TEST_ASSERT_EQUAL_UINT(1, scanned.result.sensors.size());
    TEST_ASSERT_EQUAL_STRING(uidOf(0).c_str(), scanned.result.sensors[0].uid.c_str());
    TEST_ASSERT_EQUAL_STRING("ry_ws301", stand.modelOfThePort(0).c_str());

    const ModuleCommandExecutionResult refused = stand.run("ScanSensors", "");
    TEST_ASSERT_EQUAL_STRING("Error", refused.status.c_str());
    TEST_ASSERT_EQUAL_STRING("Missing options", refused.errorMessage.c_str());
}

extern "C" void run_sensor_values_go_with_the_sensor_tests(void)
{
    RUN_TEST(test_a_sensor_added_on_a_port_whose_sensors_were_deleted_starts_without_their_readings);
    RUN_TEST(test_a_sensor_whose_removal_the_flash_refused_keeps_its_readings);
    RUN_TEST(test_a_poll_of_the_model_a_port_had_before_set_sensor_model_is_not_reported_after_it);
    RUN_TEST(test_a_poll_of_the_old_model_is_not_taken_for_the_new_one_on_the_same_channel_numbers);
    RUN_TEST(test_a_failed_poll_of_the_model_a_port_had_before_does_not_fail_the_new_sensor);
    RUN_TEST(test_a_scan_that_replaces_the_sensor_of_an_occupied_port_drops_the_old_readings);
    RUN_TEST(test_a_scan_on_a_fresh_module_reports_the_sensor_it_adds_from_its_first_reading);
    RUN_TEST(test_a_scan_whose_row_the_flash_refused_keeps_the_readings_of_the_sensor_still_there);
    RUN_TEST(test_a_scan_that_replaces_the_sensor_of_an_occupied_port_leaves_none_of_its_records);
    RUN_TEST(test_a_scan_that_finds_a_model_without_the_computed_channel_takes_it_out);
    RUN_TEST(test_a_scan_that_finds_nothing_on_an_occupied_port_leaves_its_records);
    RUN_TEST(test_a_scan_that_finds_the_model_the_port_has_describes_it_once);
    RUN_TEST(test_a_scan_whose_row_the_flash_refused_leaves_the_records_of_the_row_still_there);
    RUN_TEST(test_a_scan_on_a_fresh_module_describes_the_port_it_fills_by_its_sensor_alone);
    RUN_TEST(test_a_scan_on_a_fresh_module_with_an_empty_structure_adds_its_sensor);
    RUN_TEST(test_a_set_sensor_model_whose_row_the_flash_refused_keeps_the_sensor_of_the_port);
    RUN_TEST(test_a_set_sensor_model_on_a_flash_that_refuses_every_write_leaves_the_port_as_it_was);
    RUN_TEST(test_a_set_sensor_model_on_a_fresh_module_whose_row_the_flash_refused_leaves_the_port_empty);
    RUN_TEST(test_a_set_sensor_model_takes_a_row_of_another_uid_off_its_port);
    RUN_TEST(test_a_scan_whose_row_the_flash_refused_answers_with_an_error);
    RUN_TEST(test_a_scan_whose_options_name_no_pin_scans_port_0);
}
