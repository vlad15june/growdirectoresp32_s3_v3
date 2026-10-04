// Readings leave with the registers they were read by.
//
// The catalog synchronization brings the module the sensor models of the account, and when a model
// it holds has changed, it stores the sensors of that model again with the registers the catalog now
// gives it (SyncSensorTypesUseCase::updateSensorsForChangedSensorTypes()). The value model keeps a
// sensor's readings under the uid of its registry row, channel by channel, and the update packet
// reports every channel kept there (SensorService::createSensorUpdateDto()). These tests take a model
// whose registers the catalog changed and check what the packet says afterwards:
//
//   - a channel the catalog took out of the model,
//   - a channel it rescaled,
//   - a computed channel it took away by changing the type of a channel the VPD is made of,
//   - a computed channel whose sources it swapped by changing the types of the channels it is made of,
//   - a poll the acquisition task makes by the registers the model had before: the control loop
//     hands the task a new list on its read interval (SensorService::readModbusSensors()), so a poll
//     by the old registers can come back after the catalog was applied,
//   - a sensor that stops answering then,
// and that nothing is taken away when the registers stayed as they were, when the flash refused to
// store the sensor again, on a module that had read nothing yet, or from a sensor of a model the
// catalog did not change; that the structure goes out again without a channel the catalog took
// out; and that the acquisition task starts its sweep again for a sensor handed over with other
// registers, and not for one stored again with the ones it had.
//
// The model is the XF700A weather station, and the change is the one the platform's catalog made to
// it: channel 6 (solar radiation) taken out, the rainfall and illuminance multipliers changed, the
// solar radiation sub-sensor taken off the model - which is what the module notices, the model's own
// timestamp staying as it was (SyncSensorTypesUseCase::shouldUpdateSensorType()). A change of a
// multiplier alone is the ry-gw light sensor's lux, multiplied by 10 instead of 1.
//
// What runs is production code: the real SensorAcquisitionService, stepped by the test as the
// acquisition tests do, polls through the real SensorReader and ModbusSensorReader on a simulated
// RS-485 line wired as the board is; SensorService applies the readings and builds the packet from
// the real SensorRepository (over an in-memory DAO) and SensorValueModel. The catalog arrives through
// the real SyncSensorTypesUseCase, wired as globals.h wires it: the fetch goes to the outbox, the
// answer is merged on the loop, the real SensorTypeCatalogWriter stores it on its own task through
// the real SpiffsSensorTypeDao, and the outcome of the write is applied on the loop - the steps
// LoopServiceConfigureDone::drainCatalogWrites() takes, taken here by the stand. Replaced are the
// line, the outbox's queue and the server behind it, the flash devices and time. The writer's task
// runs on a scheduler of its own, so that running it does not run the acquisition task the test
// steps by hand. The plausibility gate is not wired, as in the telemetry tests.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Application/Services/Storage/SensorTypeCatalogWriter.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Repositories/SensorRepository.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

#include "Mock/Application/MockOutbox.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
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

const char* const MODULE_UID = "t99-module";

const char* const XF700A_UID = "5ec10934-c2d9-4eff-a637-e7e320ef855c";

// The model's own timestamp, which the edit leaves as it was.
const char* const XF700A_UPDATED_AT = "2026-06-16T11:08:12.458594+00:00";

// SensorService's read and update intervals: one hand-over of the list and one packet per tick.
const unsigned long TICK_MS = 15000;

// The port the weather station is on.
const int PORT = 1;

// What the station's registers 9 to 16 hold.
const uint16_t AIR_TEMP_RAW = 6245;    // 62.45 - 40 = 22.45 degrees
const uint16_t HUMIDITY_RAW = 5510;    // 55.10 percent
const uint16_t PRESSURE_RAW = 10132;   // 1013.2
const uint16_t WIND_SPEED_RAW = 350;   // 3.50
const uint16_t WIND_DIR_RAW = 2700;    // 270.0
const uint16_t RAINFALL_RAW = 12;      // 1.2 at the multiplier of 0.1, 120 at the multiplier of 10
const uint16_t SOLAR_RAW = 480;        // 480
const uint16_t LUX_RAW = 3456;         // 34.56 at the multiplier of 0.01, 34560 at the multiplier of 10

ModbusRegisterConfig channelConfig(uint8_t channel, uint16_t startRegister, float multiplier,
                                   float offset, const char* type, const char* name, float maxValue)
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
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = maxValue;
    return config;
}

SubSensorType subSensor(const char* name)
{
    SubSensorType sub;
    sub.name = name;
    sub.displayName = name;
    sub.createdAt = "2026-06-16T08:12:26.35888+00:00";
    sub.updatedAt = "2026-06-16T08:12:26.35888+00:00";
    return sub;
}

SensorType xf700aWithout()
{
    SensorType type;
    type.uid = XF700A_UID;
    type.name = "XF700A";
    type.displayName = "Weather Station";
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.modbusDeviceAddress = 1;
    type.baudRate = 9600;
    type.canScan = true;
    type.isSupported = true;
    type.createdAt = "2026-06-16T08:12:26.35888+00:00";
    type.updatedAt = XF700A_UPDATED_AT;
    return type;
}

/** @brief The XF700A as the module stored it: eight channels, solar radiation on channel 6. */
SensorType xf700aBefore()
{
    SensorType type = xf700aWithout();
    type.modbusRegisterConfig.push_back(channelConfig(0, 9, 0.01f, -40.0f, "air_temp", "Ambient Temperature", 100.0f));
    type.modbusRegisterConfig.push_back(channelConfig(1, 10, 0.01f, 0.0f, "humidity", "Relative Humidity", 100.0f));
    type.modbusRegisterConfig.push_back(channelConfig(2, 11, 0.1f, 0.0f, "atmospheric_pressure_dig", "Atmospheric Pressure", 10000.0f));
    type.modbusRegisterConfig.push_back(channelConfig(3, 12, 0.01f, 0.0f, "wind_speed_dig", "Wind Speed", 100.0f));
    type.modbusRegisterConfig.push_back(channelConfig(4, 13, 0.1f, 0.0f, "wind_direction_dig", "Wind Direction", 360.0f));
    type.modbusRegisterConfig.push_back(channelConfig(5, 14, 0.1f, 0.0f, "rainfall_dig", "Rainfall", 1000.0f));
    type.modbusRegisterConfig.push_back(channelConfig(6, 15, 1.0f, 0.0f, "solar_radiation_dig", "Solar Radiation", 10000.0f));
    type.modbusRegisterConfig.push_back(channelConfig(7, 16, 0.01f, 0.0f, "lux", "Illuminance", 100000.0f));
    const char* const subs[] = {"air_temp", "humidity", "atmospheric_pressure_dig", "wind_speed_dig",
                                "wind_direction_dig", "rainfall_dig", "solar_radiation_dig", "lux"};
    for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); i++)
    {
        type.sensorTypes.push_back(subSensor(subs[i]));
    }
    return type;
}

/** @brief The XF700A as the catalog has it now: channel 6 gone, rainfall and illuminance x10. */
SensorType xf700aAfter()
{
    SensorType type = xf700aWithout();
    type.modbusRegisterConfig.push_back(channelConfig(0, 9, 0.01f, -40.0f, "air_temp", "Ambient Temperature", 100.0f));
    type.modbusRegisterConfig.push_back(channelConfig(1, 10, 0.01f, 0.0f, "humidity", "Relative Humidity", 100.0f));
    type.modbusRegisterConfig.push_back(channelConfig(2, 11, 0.1f, 0.0f, "atmospheric_pressure_dig", "Atmospheric Pressure", 10000.0f));
    type.modbusRegisterConfig.push_back(channelConfig(3, 12, 0.01f, 0.0f, "wind_speed_dig", "Wind Speed", 100.0f));
    type.modbusRegisterConfig.push_back(channelConfig(4, 13, 0.1f, 0.0f, "wind_direction_dig", "Wind Direction", 360.0f));
    type.modbusRegisterConfig.push_back(channelConfig(5, 14, 10.0f, 0.0f, "rainfall_dig", "Rainfall", 1000.0f));
    type.modbusRegisterConfig.push_back(channelConfig(7, 16, 10.0f, 0.0f, "lux", "Illuminance", 100000.0f));
    const char* const subs[] = {"air_temp", "humidity", "atmospheric_pressure_dig", "wind_speed_dig",
                                "wind_direction_dig", "rainfall_dig", "lux"};
    for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); i++)
    {
        type.sensorTypes.push_back(subSensor(subs[i]));
    }
    return type;
}

const char* const RY_GW_UID = "500f6788-fc70-44fd-8e0f-42c00ae32fd8";

// What the ry-gw light sensor's register 0 holds: 345 lux at the multiplier of 1, 3450 at 10.
const uint16_t RY_GW_LUX_RAW = 345;

/**
 * @brief The ry-gw light sensor: one channel, lux, on register 0. An edit made through the platform
 *        moves the model's timestamp, which is what the module notices it by.
 */
SensorType ryGw(float multiplier, const char* updatedAt)
{
    SensorType type;
    type.uid = RY_GW_UID;
    type.name = "ry-gw";
    type.displayName = "Illuminance Sensor";
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.modbusDeviceAddress = 255;
    type.baudRate = 9600;
    type.canScan = true;
    type.isSupported = true;
    type.createdAt = "2026-01-25T08:26:27.170983+00:00";
    type.updatedAt = updatedAt;
    type.modbusRegisterConfig.push_back(channelConfig(1, 0, multiplier, 0.0f, "lux", "lux", 4000.0f));
    type.sensorTypes.push_back(subSensor("lux"));
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

/** @brief The weather station at the port's own address, each register read where it is named. */
SimRs485Bus::Device weatherStationAt(int pin)
{
    SimRs485Bus::Device device;
    device.address = static_cast<uint8_t>(MODBUS_ADDRESS_MIN + pin);
    device.registerValues.assign(17, 0);
    device.registerValues[9] = AIR_TEMP_RAW;
    device.registerValues[10] = HUMIDITY_RAW;
    device.registerValues[11] = PRESSURE_RAW;
    device.registerValues[12] = WIND_SPEED_RAW;
    device.registerValues[13] = WIND_DIR_RAW;
    device.registerValues[14] = RAINFALL_RAW;
    device.registerValues[15] = SOLAR_RAW;
    device.registerValues[16] = LUX_RAW;
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
    char message[600];
    const SensorDto* record = find(records, uid);
    snprintf(message, sizeof(message), "no record for %s in the packet (%s)", uid.c_str(),
             describe(records).c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(record, message);
    snprintf(message, sizeof(message), "%s went out %s, not %s", uid.c_str(), record->state.c_str(),
             state);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(state, record->state.c_str(), message);
    snprintf(message, sizeof(message), "%s went out as %.2f, not %.2f", uid.c_str(), record->value,
             value);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, value, record->value, message);
}

/** @brief What the packet must not carry: a record the sensor's registers do not give. */
void assertNoRecord(const std::vector<SensorDto>& records, const std::string& uid,
                    const char* when)
{
    char message[600];
    const SensorDto* record = find(records, uid);
    snprintf(message, sizeof(message), "%s: the packet carries %s=%.2f/%s (%s)", when, uid.c_str(),
             record != nullptr ? record->value : 0.0f,
             record != nullptr ? record->state.c_str() : "", describe(records).c_str());
    TEST_ASSERT_NULL_MESSAGE(record, message);
}

/** @brief The sensor's records after it was read by the registers the catalog gives it now. */
void assertReadByTheRegistersItHasNow(const std::vector<SensorDto>& records)
{
    assertRecord(records, channelUid(PORT, 0), 22.45f, "online");
    assertRecord(records, channelUid(PORT, 1), 55.10f, "online");
    assertRecord(records, channelUid(PORT, 2), 1013.2f, "online");
    assertRecord(records, channelUid(PORT, 3), 3.50f, "online");
    assertRecord(records, channelUid(PORT, 4), 270.0f, "online");
    assertRecord(records, channelUid(PORT, 5), 120.0f, "online");
    assertRecord(records, channelUid(PORT, 7), 34560.0f, "online");
    TEST_ASSERT_NOT_NULL_MESSAGE(find(records, channelUid(PORT, 8)), "no VPD on channel 8");
    assertNoRecord(records, channelUid(PORT, 6), "after the sensor was read by the registers it has now");
    TEST_ASSERT_EQUAL_UINT(8, recordsOfPort(records, PORT));
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

    /** @brief Did the loop take a failed poll of the sensor? */
    bool tookAFailureOf(const std::string& sensorUid) const
    {
        for (size_t i = 0; i < drained.size(); i++)
        {
            if (drained[i].sensorUid == sensorUid && !drained[i].ok)
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
 * @brief The registry's flash, which can be told to refuse a write that puts a row in, and counts
 *        those writes.
 *
 * A row reaches it either on its own or inside the whole registry, depending on how the repository
 * stores one; both are writes of the row. A whole registry with fewer rows than the flash holds is
 * a removal, which is neither counted nor refused.
 */
class RegistryFlash : public MockSpiffsSensorDao {
public:
    bool refuseSave = false;
    uint32_t saves = 0;

    bool save(const SensorEntity& entity, const std::string& uid) override
    {
        saves++;
        return refuseSave ? false : MockSpiffsSensorDao::save(entity, uid);
    }

    bool saveAll(const std::vector<SensorEntity>& entities) override
    {
        if (entities.size() < getStorage().size())
        {
            return MockSpiffsSensorDao::saveAll(entities);
        }
        saves++;
        return refuseSave ? false : MockSpiffsSensorDao::saveAll(entities);
    }
};

/** @brief The module's sensor side and its catalog synchronization, wired the way globals.h wires them. */
struct Stand {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    // The storage task's scheduler: running it runs the catalog write and nothing else.
    HostTaskScheduler storageScheduler;
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
    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;

    MockSpiffsAdapter catalogFlash;
    ArduinoJsonConverter jsonConverter;
    SpiffsSensorTypeDao sensorTypeDao;
    SensorTypeRepository sensorTypeRepository;
    SensorTypeCatalogWriter catalogWriter;

    ModbusSensorReader modbusSensorReader;
    SensorReader sensorReader;
    SensorAcquisitionService acquisitionTask;
    WatchedAcquisition acquisition;

    MockHybridAPIService server;
    MockOutbox outbox;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    RepublishModuleStructureUseCase republish;
    SyncSensorTypesUseCase sync;
    SensorService sensorService;

    uint64_t lastTickMs = 0;

    Stand()
        : scheduler(clock),
          storageScheduler(clock),
          arduino(clock, &scheduler),
          busLock(scheduler, clock),
          port0(clock, "sim-port-0"),
          port1(clock, "sim-port-1"),
          port2(clock, "sim-port-2"),
          port3(clock, "sim-port-3"),
          ports{&port0, &port1, &port2, &port3},
          moduleConfig(&logger, &debugSystem, &settings),
          sensorRepository(&sensorDao),
          sensorTypeDao(&catalogFlash, &jsonConverter, &debugSystem, &logger),
          sensorTypeRepository(&sensorTypeDao, &debugSystem, &logger),
          catalogWriter(&sensorTypeDao, &storageScheduler, &logger, &debugSystem),
          modbusSensorReader(ports, arduino, &logger, &debugSystem),
          sensorReader(nullptr, &modbusSensorReader, &arduino, &logger, &debugSystem),
          acquisitionTask(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          acquisition(acquisitionTask),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          republish(&moduleStructure, &sensorRepository, &server, &sensorStructure, &logger,
                    &debugSystem, &arduino),
          sync(&server, &sensorTypeRepository, &arduino, &timeAdapter, &jsonConverter, &logger,
               &debugSystem, &sensorRepository, &moduleConfig),
          sensorService(&moduleConfig, &moduleStructure, &sensorRepository, &sensorReader,
                        &sensorValueModel, &timeAdapter, &arduino, &server, &logger,
                        &debugSystem, nullptr, nullptr, nullptr, &virtualChannels)
    {
        debugSystem.featureEnabled = false;

        timeAdapter.setEpochTime(1757066400);
        settings.setUser("t99@example.com");
        settings.setAccountUid("t99-account");
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

        sensorService.setAcquisition(&acquisition);

        // As initDependencies() wires the synchronization.
        sync.setVirtualSensorChannelsUseCase(&virtualChannels);
        sync.setRepublishModuleStructureUseCase(&republish);
        sync.setSensorValueModel(&sensorValueModel);
        sync.setOutbox(&outbox);
        sync.setCatalogWriter(&catalogWriter);
    }

    /** @brief The catalog the module holds, stored as a module that synchronized it before has it. */
    void holdCatalog(const SensorType& type)
    {
        holdCatalog(std::vector<SensorType>(1, type));
    }

    void holdCatalog(const std::vector<SensorType>& catalog)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)catalog.size(), sensorTypeRepository.saveAll(catalog),
                                      "the module's catalog could not be stored");
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
        printf("  t99: t=%llu ms - %s\n", (unsigned long long)at,
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

    /**
     * @brief The server answers a catalog fetch with this catalog, the way the device takes it: the
     *        fetch goes to the outbox, the answer is merged on the loop, the storage task writes it,
     *        and the outcome of the write is applied on the loop.
     */
    void catalogArrives(const SensorType& type)
    {
        catalogArrives(std::vector<SensorType>(1, type));
    }

    void catalogArrives(const std::vector<SensorType>& answer)
    {
        const uint32_t asked = outbox.sensorTypesRequests;
        TEST_ASSERT_TRUE(sync.forceSync());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(asked + 1, outbox.sensorTypesRequests,
                                         "the catalog was not asked for through the outbox");

        outbox.sensorTypesResponse = answer;
        outbox.sensorTypesResponseReady = true;
        OutboxCompletion fetched;
        fetched.kind = OutboxKind::SENSOR_TYPES_FETCH;
        fetched.ticket = outbox.lastIssuedTicket();
        fetched.delivered = true;
        fetched.attempts = 1;
        fetched.responseItemCount = static_cast<uint16_t>(answer.size());
        sync.onOutboxCompletion(fetched);
        TEST_ASSERT_TRUE_MESSAGE(sync.isWritePending(),
                                 "the merged catalog was not handed to the storage task");

        storageScheduler.runForMs(100);

        std::vector<CatalogWriteCompletion> written;
        TEST_ASSERT_TRUE_MESSAGE(catalogWriter.drainResults(written),
                                 "the storage task did not finish the write");
        for (size_t i = 0; i < written.size(); i++)
        {
            sync.onCatalogWriteCompletion(written[i]);
        }
        TEST_ASSERT_FALSE(sync.isWritePending());
        TEST_ASSERT_TRUE_MESSAGE(sync.getLastSyncStatus(), "the synchronization did not succeed");
    }

    size_t registersOfThePort(int pin)
    {
        Utils::Optional<SensorEntity> row = sensorRepository.get(uidOf(pin).c_str());
        return row.has_value() ? row.value().modbusRegisterConfigs.size() : 0;
    }
};

} // namespace

// (a) The catalog takes channel 6 out of the XF700A the module reads on port 1. The loop hands the
//     task the new list before it polls again, so what the packet says next comes from the value
//     model as the synchronization left it: nothing under channel 6 - then, or ever after.
static void test_a_channel_the_catalog_takes_out_of_a_model_is_not_reported_after_it(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    stand.tick();
    stand.pollSensors(1);
    const std::vector<SensorDto> before = stand.tick();
    // The premise: the station is read by its eight channels, and the VPD is computed.
    assertRecord(before, channelUid(PORT, 6), 480.0f, "online");
    TEST_ASSERT_EQUAL_UINT(9, recordsOfPort(before, PORT));
    const uint32_t readAtMs = stand.sensorValueModel.getLastReadTime(uidOf(PORT));
    TEST_ASSERT_TRUE(readAtMs > 0);

    stand.catalogArrives(xf700aAfter());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(7, stand.registersOfThePort(PORT),
                                   "the premise: the synchronization stored the sensor again with seven channels");
    // The values go; when the station last answered does not - the restart's scan reads it as
    // "the line answers for this sensor", and the device that answered is the same.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(readAtMs, stand.sensorValueModel.getLastReadTime(uidOf(PORT)),
                                     "the synchronization dropped when the station was last read");

    const std::vector<SensorDto> after = stand.tick();
    assertNoRecord(after, channelUid(PORT, 6), "the first packet after the catalog took channel 6 out");

    stand.pollSensors(1);
    assertReadByTheRegistersItHasNow(stand.tick());

    size_t carryingTheOldChannel = 0;
    for (int i = 0; i < 6; i++)
    {
        stand.pollSensors(1);
        if (find(stand.tick(), channelUid(PORT, 6)) != nullptr)
        {
            carryingTheOldChannel++;
        }
    }
    printf("  t99: %u of the 6 packets after it carry channel 6\n", (unsigned)carryingTheOldChannel);
    TEST_ASSERT_EQUAL_UINT(0, carryingTheOldChannel);
}

// (b) The same edit changed the multipliers of rainfall and illuminance. What the module holds for
//     them was read at the old scale - and only the scaled value is kept, so there is nothing to
//     convert. The sensor is left out of the packet until it is read by the registers it has now,
//     and from then on it is reported at the new scale.
static void test_a_channel_the_catalog_rescales_is_reported_only_at_the_new_scale(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    stand.tick();
    stand.pollSensors(1);
    const std::vector<SensorDto> before = stand.tick();
    assertRecord(before, channelUid(PORT, 5), 1.2f, "online");
    assertRecord(before, channelUid(PORT, 7), 34.56f, "online");

    stand.catalogArrives(xf700aAfter());

    const std::vector<SensorDto> after = stand.tick();
    assertNoRecord(after, channelUid(PORT, 5), "the first packet after the catalog rescaled rainfall");
    assertNoRecord(after, channelUid(PORT, 7), "the first packet after the catalog rescaled illuminance");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, recordsOfPort(after, PORT),
                                   "the sensor is reported before it was read by the registers it has now");

    stand.pollSensors(1);
    assertReadByTheRegistersItHasNow(stand.tick());
}

// (b) A catalog that changes nothing but a multiplier: the ry-gw's lux multiplied by 10 instead of
//     1, the channel set as it was. The reading kept is 345 at the old scale; the sensor is left out
//     of the packet until it is read again, and is reported as 3450 from then on.
static void test_a_channel_the_catalog_only_rescales_is_reported_only_at_the_new_scale(void)
{
    Stand stand;
    stand.holdCatalog(ryGw(1.0f, "2026-02-26T14:59:49.817582+00:00"));
    SimRs485Bus::Device lightSensor;
    lightSensor.address = static_cast<uint8_t>(MODBUS_ADDRESS_MIN + PORT);
    lightSensor.registerValues.push_back(RY_GW_LUX_RAW);
    lightSensor.registersAreAddressed = true;
    lightSensor.answersTheWildcard = false;
    stand.port1.addDevice(lightSensor);
    stand.sensorRepository.save(storedRow(ryGw(1.0f, "2026-02-26T14:59:49.817582+00:00"), PORT));

    stand.tick();
    stand.pollSensors(1);
    const std::vector<SensorDto> before = stand.tick();
    assertRecord(before, channelUid(PORT, 1), 345.0f, "online");
    TEST_ASSERT_EQUAL_UINT(1, recordsOfPort(before, PORT));

    stand.catalogArrives(ryGw(10.0f, "2026-07-23T14:04:41.000000+00:00"));
    Utils::Optional<SensorEntity> row = stand.sensorRepository.get(uidOf(PORT).c_str());
    TEST_ASSERT_TRUE(row.has_value());
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(10.0f, row.value().modbusRegisterConfigs[0].multiplier,
                                    "the premise: the synchronization stored the new multiplier");

    const std::vector<SensorDto> after = stand.tick();
    assertNoRecord(after, channelUid(PORT, 1), "the first packet after the catalog rescaled lux");

    stand.pollSensors(1);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(PORT, 1), 3450.0f, "online");
    TEST_ASSERT_EQUAL_UINT(1, recordsOfPort(read, PORT));
}

// (b) A catalog that changes nothing but the type of a channel: the XF700A's channel 1 is
//     humidity_dig instead of humidity, edited through the platform (the model's timestamp moves).
//     The registers are as they were, but without a humidity channel there is no VPD: the structure
//     goes out again without channel 8, and the readings no longer compute it - so nothing would
//     overwrite the VPD kept from before. It is not reported, then or ever after.
static void test_a_computed_channel_a_type_edit_takes_away_is_not_reported_after_it(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    stand.tick();
    stand.pollSensors(1);
    const std::vector<SensorDto> before = stand.tick();
    TEST_ASSERT_NOT_NULL_MESSAGE(find(before, channelUid(PORT, 8)), "the premise: the VPD is computed");

    SensorType retyped = xf700aBefore();
    retyped.modbusRegisterConfig[1].sensorType = "humidity_dig";
    retyped.updatedAt = "2026-08-01T09:00:00.000000+00:00";
    stand.server.sendToServerModuleStructureCalled = false;
    stand.catalogArrives(retyped);
    Utils::Optional<SensorEntity> row = stand.sensorRepository.get(uidOf(PORT).c_str());
    TEST_ASSERT_TRUE(row.has_value());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("humidity_dig", row.value().modbusRegisterConfigs[1].sensorType.c_str(),
                                     "the premise: the synchronization stored the new type");
    TEST_ASSERT_TRUE_MESSAGE(stand.server.sendToServerModuleStructureCalled,
                             "the premise: the structure went out again");
    for (size_t i = 0; i < stand.moduleStructure.nodes[0].sensors.size(); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(stand.moduleStructure.nodes[0].sensors[i].uid == channelUid(PORT, 8),
                                  "the premise: the structure that went out has no VPD");
    }

    const std::vector<SensorDto> after = stand.tick();
    assertNoRecord(after, channelUid(PORT, 8), "the first packet after the catalog took the VPD away");

    stand.pollSensors(1);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(PORT, 1), 55.10f, "online");
    assertRecord(read, channelUid(PORT, 6), 480.0f, "online");
    assertNoRecord(read, channelUid(PORT, 8), "after the sensor was read with the types it has now");
    TEST_ASSERT_EQUAL_UINT(8, recordsOfPort(read, PORT));

    size_t carryingTheVpd = 0;
    for (int i = 0; i < 6; i++)
    {
        stand.pollSensors(1);
        if (find(stand.tick(), channelUid(PORT, 8)) != nullptr)
        {
            carryingTheVpd++;
        }
    }
    printf("  t99: %u of the 6 packets after it carry channel 8\n", (unsigned)carryingTheVpd);
    TEST_ASSERT_EQUAL_UINT(0, carryingTheVpd);
}

// (c) The acquisition task polls the station once more by the registers it had before the catalog
//     was applied: the loop has not handed it the new list yet. The module had read nothing until
//     then - the order of a boot whose first list went to the task before the catalog came. That
//     answer is not the station read by the registers it has now: neither channel 6 nor the old
//     scale reaches the packet.
static void test_a_poll_by_the_registers_a_model_had_before_the_catalog_changed_them_is_not_reported(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    // The task is handed the list with the registers the station has before the catalog comes.
    TEST_ASSERT_EQUAL_UINT(0, recordsOfPort(stand.tick(), PORT));

    stand.catalogArrives(xf700aAfter());
    TEST_ASSERT_EQUAL_UINT(7, stand.registersOfThePort(PORT));

    stand.acquisition.drained.clear();
    stand.pollSensors(1);
    const std::vector<SensorDto> afterCatalog = stand.tick();
    TEST_ASSERT_TRUE_MESSAGE(stand.acquisition.tookAReadingWith(uidOf(PORT), channelUid(PORT, 6)),
                             "the premise: the loop took a poll by the old registers after the catalog");
    assertNoRecord(afterCatalog, channelUid(PORT, 6), "the first packet after the catalog");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, recordsOfPort(afterCatalog, PORT),
                                   "a poll by the old registers was reported");

    // That tick handed the new list over; from here the task polls by the registers of the catalog.
    stand.pollSensors(1);
    assertReadByTheRegistersItHasNow(stand.tick());
}

// (c) After a type edit the registers are the ones the task already polls by, so its next poll -
//     still by the list it had before the catalog - is a reading of the sensor and is taken. The
//     VPD is worked out from the sensor as it is when the reading is applied: that poll brings the
//     register channels back and the VPD does not come back with them.
static void test_a_poll_by_the_types_a_model_had_before_brings_no_computed_channel_back(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    stand.tick();
    stand.pollSensors(1);
    TEST_ASSERT_NOT_NULL_MESSAGE(find(stand.tick(), channelUid(PORT, 8)), "the premise: the VPD is computed");

    SensorType retyped = xf700aBefore();
    retyped.modbusRegisterConfig[1].sensorType = "humidity_dig";
    retyped.updatedAt = "2026-08-01T09:00:00.000000+00:00";
    stand.catalogArrives(retyped);

    stand.acquisition.drained.clear();
    stand.pollSensors(1);
    const std::vector<SensorDto> afterCatalog = stand.tick();
    TEST_ASSERT_TRUE_MESSAGE(stand.acquisition.tookAReadingWith(uidOf(PORT), channelUid(PORT, 1)),
                             "the premise: the loop took a poll by the list from before the catalog");
    assertRecord(afterCatalog, channelUid(PORT, 1), 55.10f, "online");
    assertRecord(afterCatalog, channelUid(PORT, 6), 480.0f, "online");
    assertNoRecord(afterCatalog, channelUid(PORT, 8), "the packet after a poll by the list from before the catalog");
    TEST_ASSERT_EQUAL_UINT(8, recordsOfPort(afterCatalog, PORT));
}

// (d) The station stops answering just after the catalog was applied, and the task's next poll -
//     still by the old registers - fails. A failed poll carries no value, and the device that did
//     not answer is the one the sensor still has: the sensor goes out disconnected at once, by the
//     channels it has now.
static void test_a_sensor_that_stops_answering_after_the_catalog_changed_its_registers_is_reported_by_them(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    stand.tick();
    stand.pollSensors(1);
    assertRecord(stand.tick(), channelUid(PORT, 6), 480.0f, "online");

    stand.catalogArrives(xf700aAfter());

    stand.port1.removeDevice(static_cast<uint8_t>(MODBUS_ADDRESS_MIN + PORT));
    stand.acquisition.drained.clear();
    stand.pollSensors(1);
    const std::vector<SensorDto> silent = stand.tick();
    TEST_ASSERT_TRUE_MESSAGE(stand.acquisition.tookAFailureOf(uidOf(PORT)),
                             "the premise: the loop took the failed poll");

    const int channels[] = {0, 1, 2, 3, 4, 5, 7, 8};
    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); i++)
    {
        assertRecord(silent, channelUid(PORT, channels[i]), 0.0f, "disconnected");
    }
    assertNoRecord(silent, channelUid(PORT, 6), "the first packet after the station went silent");
    TEST_ASSERT_EQUAL_UINT(8, recordsOfPort(silent, PORT));
}

// (e) A catalog edit that stores the sensor again with the very registers it had: the platform renamed
//     one of the station's channels. What it was read as stays what it is read as, and stays in the
//     packet.
static void test_a_catalog_edit_that_leaves_the_registers_as_they_were_keeps_the_readings(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    stand.tick();
    stand.pollSensors(1);
    const std::vector<SensorDto> before = stand.tick();
    TEST_ASSERT_EQUAL_UINT(9, recordsOfPort(before, PORT));

    SensorType renamed = xf700aBefore();
    renamed.modbusRegisterConfig[2].channelName = "Air Pressure";
    renamed.updatedAt = "2026-08-01T09:00:00.000000+00:00";
    const uint32_t savesBefore = stand.sensorDao.saves;
    stand.catalogArrives(renamed);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(savesBefore + 1, stand.sensorDao.saves,
                                     "the premise: the synchronization stored the sensor again");

    const std::vector<SensorDto> after = stand.tick();
    assertRecord(after, channelUid(PORT, 5), 1.2f, "online");
    assertRecord(after, channelUid(PORT, 6), 480.0f, "online");
    assertRecord(after, channelUid(PORT, 7), 34.56f, "online");
    TEST_ASSERT_EQUAL_UINT(9, recordsOfPort(after, PORT));

    // And a poll by those registers is still a reading of the sensor.
    stand.pollSensors(1);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(PORT, 6), 480.0f, "online");
    TEST_ASSERT_EQUAL_UINT(9, recordsOfPort(read, PORT));
}

// (f) The registry's flash refuses to store the sensor again. It keeps the registers it had, it is
//     read by them, and what it was read as stays in the packet.
static void test_a_sensor_whose_storing_again_the_flash_refused_keeps_its_readings(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    stand.tick();
    stand.pollSensors(1);
    assertRecord(stand.tick(), channelUid(PORT, 6), 480.0f, "online");

    stand.sensorDao.refuseSave = true;
    stand.catalogArrives(xf700aAfter());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(8, stand.registersOfThePort(PORT),
                                   "the premise: the registry kept the sensor with eight channels");

    const std::vector<SensorDto> after = stand.tick();
    assertRecord(after, channelUid(PORT, 5), 1.2f, "online");
    assertRecord(after, channelUid(PORT, 6), 480.0f, "online");
    assertRecord(after, channelUid(PORT, 7), 34.56f, "online");
    TEST_ASSERT_EQUAL_UINT(9, recordsOfPort(after, PORT));

    stand.pollSensors(1);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(PORT, 6), 480.0f, "online");
    TEST_ASSERT_EQUAL_UINT(9, recordsOfPort(read, PORT));
}

// (g) A fresh module: nothing read, no catalog stored - the one state in which setup synchronizes the
//     catalog itself, on its own task, before the control loop starts
//     (ModuleSetupService::fillSensorModuleStructure(), SyncSensorTypesUseCase::syncNow()). The stored
//     station is given the registers of the catalog, and is reported by them from its first reading.
static void test_a_catalog_stored_at_setup_on_a_fresh_module_reports_the_sensor_by_its_registers(void)
{
    Stand stand;
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));
    TEST_ASSERT_EQUAL_INT(0, stand.sensorTypeRepository.count());

    stand.server.sensorTypesListReturnValue.push_back(xf700aAfter());
    TEST_ASSERT_TRUE_MESSAGE(stand.sync.syncNow(), "the synchronization at setup did not succeed");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(7, stand.registersOfThePort(PORT),
                                   "the premise: setup stored the sensor again with seven channels");

    TEST_ASSERT_EQUAL_UINT(0, recordsOfPort(stand.tick(), PORT));
    stand.pollSensors(1);
    assertReadByTheRegistersItHasNow(stand.tick());
}

// (h) The catalog edits the XF700A on port 1. The ry-gw on port 2 is of another model, which the
//     answer carries as it was: what the ry-gw was read as stays, and goes on being reported.
static void test_a_sensor_of_a_model_the_catalog_left_as_it_was_keeps_its_readings(void)
{
    const char* const RY_GW_STAMP = "2026-02-26T14:59:49.817582+00:00";
    const int OTHER_PORT = 2;
    Stand stand;
    std::vector<SensorType> held;
    held.push_back(xf700aBefore());
    held.push_back(ryGw(1.0f, RY_GW_STAMP));
    stand.holdCatalog(held);
    stand.port1.addDevice(weatherStationAt(PORT));
    SimRs485Bus::Device lightSensor;
    lightSensor.address = static_cast<uint8_t>(MODBUS_ADDRESS_MIN + OTHER_PORT);
    lightSensor.registerValues.push_back(RY_GW_LUX_RAW);
    lightSensor.registersAreAddressed = true;
    lightSensor.answersTheWildcard = false;
    stand.port2.addDevice(lightSensor);
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));
    stand.sensorRepository.save(storedRow(ryGw(1.0f, RY_GW_STAMP), OTHER_PORT));

    stand.tick();
    stand.pollSensors(2);
    const std::vector<SensorDto> before = stand.tick();
    assertRecord(before, channelUid(OTHER_PORT, 1), 345.0f, "online");
    TEST_ASSERT_EQUAL_UINT(9, recordsOfPort(before, PORT));

    std::vector<SensorType> answer;
    answer.push_back(xf700aAfter());
    answer.push_back(ryGw(1.0f, RY_GW_STAMP));
    stand.catalogArrives(answer);

    const std::vector<SensorDto> after = stand.tick();
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, recordsOfPort(after, PORT),
                                   "the premise: the readings of the XF700A were dropped");
    assertRecord(after, channelUid(OTHER_PORT, 1), 345.0f, "online");
    TEST_ASSERT_EQUAL_UINT(1, recordsOfPort(after, OTHER_PORT));
}

// (b) A catalog that swaps the types of the XF700A's first two channels: air_temp on the humidity
//     register and humidity on the air temperature one. The registers are the ones they were and the
//     VPD is still channel 8, but it is now worked out from the other way round: the value kept
//     from the pair before is not reported, and the next reading reports the VPD of the pair now.
static void test_a_computed_channel_a_type_edit_gives_other_sources_is_not_reported_from_the_old_ones(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    stand.tick();
    stand.pollSensors(1);
    const std::vector<SensorDto> before = stand.tick();
    assertRecord(before, channelUid(PORT, 8), stand.vpdChannel.compute(22.45f, 55.10f), "online");

    SensorType swapped = xf700aBefore();
    swapped.modbusRegisterConfig[0].sensorType = "humidity";
    swapped.modbusRegisterConfig[1].sensorType = "air_temp";
    swapped.updatedAt = "2026-08-01T09:00:00.000000+00:00";
    stand.catalogArrives(swapped);
    Utils::Optional<SensorEntity> row = stand.sensorRepository.get(uidOf(PORT).c_str());
    TEST_ASSERT_TRUE(row.has_value());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("air_temp", row.value().modbusRegisterConfigs[1].sensorType.c_str(),
                                     "the premise: the synchronization stored the swapped types");

    const std::vector<SensorDto> after = stand.tick();
    assertNoRecord(after, channelUid(PORT, 8), "the first packet after the catalog gave the VPD the other pair");

    stand.pollSensors(1);
    const std::vector<SensorDto> read = stand.tick();
    assertRecord(read, channelUid(PORT, 8), stand.vpdChannel.compute(55.10f, 22.45f), "online");
    TEST_ASSERT_EQUAL_UINT(9, recordsOfPort(read, PORT));
}

/** @brief The records of the module structure on the port, sorted, as "uid:type". */
static std::string structureOfPort(const ModuleStructure& structure, int pin)
{
    const std::string prefix = uidOf(pin) + ".";
    std::vector<std::string> records;
    for (size_t i = 0; i < structure.nodes[0].sensors.size(); i++)
    {
        const Sensor& record = structure.nodes[0].sensors[i];
        if (record.uid.compare(0, prefix.size(), prefix) == 0)
        {
            records.push_back(record.uid + ":" + record.type);
        }
    }
    std::sort(records.begin(), records.end());
    std::string text;
    for (size_t i = 0; i < records.size(); i++)
    {
        text += (i == 0 ? "" : " ") + records[i];
    }
    return text;
}

// The structure the module sent describes the XF700A by its eight channels and the VPD on 8. The
// catalog takes channel 6 out, and the VPD stays on 8 - the highest register channel is still 7. The
// structure goes out again, once, without channel 6.
static void test_a_channel_the_catalog_takes_out_of_a_model_leaves_the_structure(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));
    TEST_ASSERT_TRUE(stand.republish.execute("boot"));
    TEST_ASSERT_TRUE_MESSAGE(structureOfPort(stand.moduleStructure, PORT).find(channelUid(PORT, 6)) != std::string::npos,
                             "the premise: the structure the module sent describes channel 6");
    stand.server.sendToServerModuleStructureCallCount = 0;

    stand.catalogArrives(xf700aAfter());

    printf("  t99: the structure after the catalog took channel 6 out: %s (sent %d time(s))\n",
           structureOfPort(stand.moduleStructure, PORT).c_str(), stand.server.sendToServerModuleStructureCallCount);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("t99-module.01.00:air_temp t99-module.01.01:humidity "
                                     "t99-module.01.02:atmospheric_pressure_dig t99-module.01.03:wind_speed_dig "
                                     "t99-module.01.04:wind_direction_dig t99-module.01.05:rainfall_dig "
                                     "t99-module.01.07:lux t99-module.01.08:vpd",
                                     structureOfPort(stand.moduleStructure, PORT).c_str(),
                                     "the structure after the catalog took channel 6 out");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stand.server.sendToServerModuleStructureCallCount,
                                  "the structure without channel 6 did not go out once");
}

// The acquisition task starts its sweep again when the list the loop hands it holds a sensor with
// other registers, and only then. A catalog edit that stores the station again with the registers it
// had hands the task those same registers: the sweep goes on at its own pace, and a step taken at
// once polls nothing. An edit of a multiplier hands it other ones: the step taken at once polls the
// station by them. The list is the one the loop hands over on its read tick
// (SensorService::readModbusSensors()), handed over here right after a poll, before the sweep is due.
static void test_a_sensor_stored_again_with_the_registers_it_had_does_not_restart_the_sweep(void)
{
    Stand stand;
    stand.holdCatalog(xf700aBefore());
    stand.port1.addDevice(weatherStationAt(PORT));
    stand.sensorRepository.save(storedRow(xf700aBefore(), PORT));

    stand.tick();
    stand.pollSensors(1);

    SensorType renamed = xf700aBefore();
    renamed.modbusRegisterConfig[2].channelName = "Air Pressure";
    renamed.updatedAt = "2026-08-01T09:00:00.000000+00:00";
    const uint32_t savesBefore = stand.sensorDao.saves;
    stand.catalogArrives(renamed);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(savesBefore + 1, stand.sensorDao.saves,
                                     "the premise: the synchronization stored the sensor again");

    stand.acquisitionTask.setSensors(stand.sensorRepository.getModbusSensors());
    uint64_t polls = stand.acquisitionTask.pollCount();
    stand.acquisitionTask.step();
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(polls, stand.acquisitionTask.pollCount(),
                                     "the sweep started again for a sensor handed over with the registers it had");

    SensorType rescaled = renamed;
    rescaled.modbusRegisterConfig[5].multiplier = 10.0f;
    rescaled.updatedAt = "2026-08-02T09:00:00.000000+00:00";
    stand.catalogArrives(rescaled);

    stand.acquisitionTask.setSensors(stand.sensorRepository.getModbusSensors());
    polls = stand.acquisitionTask.pollCount();
    stand.acquisitionTask.step();
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(polls + 1, stand.acquisitionTask.pollCount(),
                                     "the station handed over with a new multiplier was not polled by it at once");
}

extern "C" void run_readings_go_with_the_registers_tests(void)
{
    RUN_TEST(test_a_channel_the_catalog_takes_out_of_a_model_is_not_reported_after_it);
    RUN_TEST(test_a_channel_the_catalog_rescales_is_reported_only_at_the_new_scale);
    RUN_TEST(test_a_channel_the_catalog_only_rescales_is_reported_only_at_the_new_scale);
    RUN_TEST(test_a_computed_channel_a_type_edit_takes_away_is_not_reported_after_it);
    RUN_TEST(test_a_poll_by_the_registers_a_model_had_before_the_catalog_changed_them_is_not_reported);
    RUN_TEST(test_a_poll_by_the_types_a_model_had_before_brings_no_computed_channel_back);
    RUN_TEST(test_a_sensor_that_stops_answering_after_the_catalog_changed_its_registers_is_reported_by_them);
    RUN_TEST(test_a_catalog_edit_that_leaves_the_registers_as_they_were_keeps_the_readings);
    RUN_TEST(test_a_sensor_whose_storing_again_the_flash_refused_keeps_its_readings);
    RUN_TEST(test_a_catalog_stored_at_setup_on_a_fresh_module_reports_the_sensor_by_its_registers);
    RUN_TEST(test_a_sensor_of_a_model_the_catalog_left_as_it_was_keeps_its_readings);
    RUN_TEST(test_a_computed_channel_a_type_edit_gives_other_sources_is_not_reported_from_the_old_ones);
    RUN_TEST(test_a_channel_the_catalog_takes_out_of_a_model_leaves_the_structure);
    RUN_TEST(test_a_sensor_stored_again_with_the_registers_it_had_does_not_restart_the_sweep);
}
