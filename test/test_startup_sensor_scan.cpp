// The sensor discovery a restart performs, off the restart path.
//
// What a restart does about its sensors - ping every stored entity, try the catalog on the free
// ports, move the devices that collide off each other's ModBus address, apply the per-model
// register writes - is a sequence of steps handed to the task that owns the RS-485 line. This file
// exercises the sequence itself: what is planned, in what order, what each step does on the line,
// and what its answer does to the registry and the module structure.
//
// The two halves are driven here the way the module drives them - the control loop plans a step,
// the bus task executes it, the loop applies the answer - with the queue and the tasks left out;
// the queue and the turn-taking are covered by the scenario file.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/CounterReport.h"
#include "Application/Services/ModuleSetupService.h"
#include "Application/Services/ScanCounters.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"

#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorTypeDao.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"

#include "Sim/HostAllocationCounter.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include <string>
#include <vector>

/** @brief Reaches the structure fill, which is private to the setup service. */
class StartupScanTestHelper {
public:
    static void fillStructure(ModuleSetupService& service) { service.fillModuleStructure(); }
};

namespace {

const char* const MODULE_UID = "AA:BB:CC:DD:EE:FF";

/**
 * @brief The window the ladder holds its first step for, after the boot arms it.
 *
 * Taken from the ladder itself rather than written out here: a test that carried its own copy of
 * the number would go on passing if the number changed.
 */
const unsigned long BOOT_SETTLE_MS = StartupSensorScanUseCase::BOOT_SETTLE_MS;
const int MODULE_PIN_COUNT = 4;

/** @brief A scannable ModBus model of the catalog. */
SensorType makeModbusType(const char* uid, const char* name, int defaultAddress)
{
    SensorType type;
    type.uid = uid;
    type.name = name;
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = true;
    type.isSupported = true;
    type.baudRate = 9600;
    type.modbusDeviceAddress = defaultAddress;
    type.modbusPingAddress = 0x0000;

    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = 0x0000;
    config.numRegisters = 1;
    config.channel = 1;
    config.channelName = "value";
    config.sensorType = "temperature";
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    type.modbusRegisterConfig.push_back(config);

    return type;
}

/** @brief An entity of the registry, as the module stored it before the restart. */
SensorEntity makeStoredEntity(const char* uid, int pin, int address, SensorStatusEnum status)
{
    SensorEntity entity;
    entity.uid = uid;
    entity.pin = pin;
    entity.protocol = "ModBus";
    entity.modbusDeviceAddress = address;
    entity.status = status;
    entity.isActive = true;
    entity.model = "stored_model";
    entity.sensorType = "temperature";
    return entity;
}

/** @brief Records the structure republishes the discovery asks for. */
class RecordingRepublish : public IRepublishModuleStructureUseCase
{
public:
    int calls = 0;
    std::string lastReason;
    std::vector<std::string> reasons;
    /** The rows of the registry when each structure was asked for - what it would be built from. */
    std::vector<size_t> rowsAtCall;
    /** Whether the restart's pass was still running when each structure was asked for. */
    std::vector<bool> passRunningAtCall;
    ISensorRepository* registry = nullptr;
    IStartupSensorScan* scan = nullptr;
    /** A structure handed over earlier is still on its way to the server. */
    bool pending = false;

    bool execute(const char* reason) override
    {
        calls++;
        lastReason = reason != nullptr ? reason : "";
        reasons.push_back(lastReason);
        rowsAtCall.push_back(registry != nullptr ? registry->getAll().size() : 0);
        passRunningAtCall.push_back(scan != nullptr && scan->isRunning());
        return true;
    }

    bool isSendPending() const override { return pending; }
};

/**
 * @brief The line as a whole, for the one question no address can answer: is anything on this port?
 *
 * The presence rung goes through IRawBusAccess, which is the adapter a raw ModbusRead from the
 * backend uses; this stands in for it and records what was asked.
 */
class StubRawBusAccess : public IRawBusAccess
{
public:
    struct Request
    {
        int channel = -1;
        uint8_t slave = 0;
        uint8_t functionCode = 0;
        uint16_t startRegister = 0;
        unsigned long timeoutMs = 0;
    };

    std::vector<Request> requests;
    /** Ports that have something on them; everything else is silence. */
    std::vector<int> answeringChannels;

    /**
     * @brief The reader whose port power the poll arranges, so a request can say what it went out
     *        into.
     *
     * The claim the presence poll rests on is not "the ports were arranged at some point" but "the
     * paired port had no power at the moment the wildcard frame left" - which is only visible here.
     */
    MockSensorReader* reader = nullptr;
    std::vector<std::vector<int> > poweredPortsAtRequest;

    bool modbusRequest(int channel, const uint8_t* pduNoCrc, size_t len, bool expectResponse,
                       unsigned long timeoutMs, std::vector<uint8_t>& out) override
    {
        if (reader != nullptr)
        {
            std::vector<int> powered;
            for (std::map<int, bool>::const_iterator it = reader->portPowered.begin();
                 it != reader->portPowered.end(); ++it)
            {
                if (it->second)
                {
                    powered.push_back(it->first);
                }
            }
            poweredPortsAtRequest.push_back(powered);
        }

        Request request;
        request.channel = channel;
        if (len >= 6)
        {
            request.slave = pduNoCrc[0];
            request.functionCode = pduNoCrc[1];
            request.startRegister = (uint16_t)((pduNoCrc[2] << 8) | pduNoCrc[3]);
        }
        request.timeoutMs = timeoutMs;
        requests.push_back(request);

        if (!expectResponse)
        {
            return true;
        }

        for (size_t i = 0; i < answeringChannels.size(); i++)
        {
            if (answeringChannels[i] == channel)
            {
                // A well-formed short frame from a device answering under its own address, which
                // is what a wildcard query gets back on this line.
                const uint8_t frame[7] = {0x07, request.functionCode, 0x02, 0x00, 0xFA, 0x00, 0x00};
                out.assign(frame, frame + sizeof(frame));
                return true;
            }
        }
        return false;
    }

    bool serialWrite(int, const uint8_t*, size_t, bool, unsigned long, std::vector<uint8_t>&) override
    {
        return false;
    }

    size_t requestsOnChannel(int channel) const
    {
        size_t count = 0;
        for (size_t i = 0; i < requests.size(); i++)
        {
            if (requests[i].channel == channel)
            {
                count++;
            }
        }
        return count;
    }

    /** @brief Did this port have its power when request number `index` went out? */
    bool portWasPoweredAtRequest(size_t index, int port) const
    {
        if (index >= poweredPortsAtRequest.size())
        {
            return false;
        }
        for (size_t i = 0; i < poweredPortsAtRequest[index].size(); i++)
        {
            if (poweredPortsAtRequest[index][i] == port)
            {
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief The module the discovery runs against, wired the way globals.h wires it.
 */
struct ModuleUnderTest
{
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockWifiAdapter wifiAdapter;
    MockMqttManager mqttManager;
    MockSensorReader sensorReader;
    MockSensorRepository sensorRepository;
    MockSensorTypeRepository sensorTypeRepository;
    // Wired the way globals.h wires it. No sweep runs on this stand, so a row is one the sweep has
    // not read yet unless a test writes that fact here itself.
    MockSensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;

    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    ModbusAddressAllocator modbusAddressAllocator;
    RecordingRepublish republish;
    MockArduinoAdapter arduino;
    StubRawBusAccess rawBus;
    StartupSensorScanUseCase startupScan;

    ModuleSetupService setupService;

    /**
     * @param withClock Wire the clock, which is what makes the cheap rung periodic. Off by
     *        default: a stand without one asks the line exactly once, which is what the tests
     *        about a single pass want to see.
     */
    explicit ModuleUnderTest(bool withClock = false)
        : moduleConfig(&logger, &debugSystem, &settings),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels),
          modbusAddressAllocator(&sensorRepository),
          startupScan(&logger, &debugSystem, &moduleConfig, &sensorRepository,
                      &sensorTypeRepository, &sensorReader, &moduleStructure, &sensorStructure,
                      &modbusAddressAllocator, &sensorValueModel, &virtualChannels,
                      withClock ? &arduino : nullptr),
          setupService(&logger, &debugSystem, &moduleConfig,
                       nullptr, nullptr, nullptr, nullptr,
                       &wifiAdapter, nullptr, nullptr, nullptr, &mqttManager,
                       &moduleStructure, nullptr, nullptr,
                       &sensorRepository, &sensorTypeRepository, &sensorReader)
    {
        _hasClock = withClock;
        debugSystem.featureEnabled = false;

        settings.setUser("scan@example.com");
        settings.setAccountUid("scan-account");
        settings.setNoScanSensors(false);
        moduleConfig.setSettings(&settings);
        moduleConfig.setWifiAdapter(&wifiAdapter);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
        moduleConfig.setSensorPinCount(MODULE_PIN_COUNT);
        wifiAdapter.setMACAddress(MODULE_UID);
        wifiAdapter.setLocalIP("192.168.1.100");

        setupService.setSensorStructureUseCase(&sensorStructure);
        setupService.setStartupSensorScan(&startupScan);
        startupScan.setRepublishModuleStructureUseCase(&republish);
        republish.registry = &sensorRepository;
        republish.scan = &startupScan;
    }

    /** @brief The boot, without the discovery it arms. */
    void boot() { StartupScanTestHelper::fillStructure(setupService); }

    /**
     * @brief Loop plans, bus executes, loop applies - until there is nothing left to do.
     *
     * The restart's own pass gives the free ports it switches on the seconds they need to come up
     * and has nothing to hand out while that lasts, so the clock moves whenever it runs dry and
     * the walk carries on until the pass is over. A periodic pass is left where it is - runFor()
     * is what lets those happen.
     */
    int runDiscovery()
    {
        skipTheBootSettleWindow();

        int steps = 0;
        for (int round = 0; round < 8; round++)
        {
            StartupScanStep step;
            while (startupScan.takeNextStep(step))
            {
                startupScan.applyStepOutcome(startupScan.runStepOnBus(step));
                steps++;
                if (steps > 5000)
                {
                    TEST_FAIL_MESSAGE("the discovery did not terminate");
                }
            }
            if (!startupScan.isRunning())
            {
                break;
            }
            _nowMs += StartupSensorScanUseCase::PORT_WAKE_UP_MS;
            arduino.setMillis(_nowMs);
        }
        return steps;
    }

    /**
     * @brief The same, with time passing: the clock moves in ticks and the discovery gets its turn
     *        after each of them.
     *
     * A pass is not a single burst of steps any more - it gives the ports it switched on the
     * seconds they need to come up, and gets nothing to do while that lasts - so a test that wants
     * whole passes has to let the clock run rather than jump from one interval to the next.
     */
    int runFor(unsigned long ms, unsigned long tickMs = 5000)
    {
        int steps = 0;
        for (unsigned long spent = 0; spent < ms; spent += tickMs)
        {
            _nowMs += tickMs;
            arduino.setMillis(_nowMs);
            steps += runDiscovery();
        }
        return steps;
    }

    unsigned long nowMs() const { return _nowMs; }

    /**
     * @brief The control loop asking the ladder for a step, tick by tick, without the clock ever
     *        jumping.
     *
     * runFor() moves the clock on whenever the ladder runs dry, so that a pass gets the seconds
     * its ports need inside a handful of ticks. This one never does: what it is for is watching
     * *when* the ladder puts its first step on the line, and a driver that jumps the clock to
     * find work would answer that question itself.
     */
    int pumpUntil(unsigned long untilMs, unsigned long tickMs = 1000)
    {
        int steps = 0;
        for (int tick = 0; _nowMs < untilMs; tick++)
        {
            if (tick > 100000)
            {
                TEST_FAIL_MESSAGE("the window asked for is longer than this driver can walk");
            }
            _nowMs += tickMs;
            arduino.setMillis(_nowMs);

            StartupScanStep step;
            while (startupScan.takeNextStep(step))
            {
                startupScan.applyStepOutcome(startupScan.runStepOnBus(step));
                steps++;
                if (steps > 5000)
                {
                    TEST_FAIL_MESSAGE("the discovery did not terminate");
                }
            }
        }
        return steps;
    }

    /**
     * @brief Step over the window the ladder holds its first step for.
     *
     * The boot arms the discovery and the ladder then leaves the line alone while the boot's own
     * peaks pass. A test about what a pass does, rather than about when it starts, is not about
     * that window - so it is stepped over here, once, and the pass runs as it always did. A stand
     * assembled without a clock has no window at all, exactly as the ladder has none.
     */
    void skipTheBootSettleWindow()
    {
        if (!_hasClock || _nowMs >= BOOT_SETTLE_MS)
        {
            return;
        }
        _nowMs = BOOT_SETTLE_MS;
        arduino.setMillis(_nowMs);
    }

    /** @brief Let the pass in progress, if there is one, get to its end. */
    void settle()
    {
        for (int round = 0; round < 8 && startupScan.aPassIsInFlight(); round++)
        {
            _nowMs += StartupSensorScanUseCase::PORT_WAKE_UP_MS;
            arduino.setMillis(_nowMs);
            runDiscovery();
        }
    }

private:
    unsigned long _nowMs = 0;
    bool _hasClock = false;
};

/** @brief How many address writes went out on one port. */
size_t countWritesOnPin(const MockSensorReader& reader, int pin)
{
    size_t count = 0;
    for (size_t i = 0; i < reader.addressWritePins.size(); i++)
    {
        if (reader.addressWritePins[i] == pin)
        {
            count++;
        }
    }
    return count;
}

} // namespace

// --- the boot no longer touches the line -------------------------------------------------------

/**
 * The claim the whole change rests on: filling the module structure at boot makes no transaction
 * on the RS-485 line at all - no ping of a stored entity, no scan of a free port, no address
 * write, no per-model register write.
 */
void test_startup_the_structure_fill_makes_no_bus_transaction(void)
{
    ModuleUnderTest module;
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));

    module.boot();

    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.scanSensorCallCount,
                              "the structure fill probed the line");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressCallCount,
                              "the structure fill wrote a ModBus address");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.prepareSensorsForReadingCallCount,
                              "the structure fill prepared the sensors on the line");
}

/**
 * And the sensors the module already knows are in the structure from the first second, described
 * as themselves - not as empty ports waiting for a scan to confirm them.
 */
void test_startup_the_stored_sensors_are_in_the_structure_before_any_scan(void)
{
    ModuleUnderTest module;
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));

    module.boot();

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.moduleStructure.nodes.size(),
                              "the boot did not build a node");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.moduleStructure.nodes[0].sensors.size(),
                              "the stored sensor is not in the structure the boot produced");
    TEST_ASSERT_EQUAL_STRING("temperature",
                             module.moduleStructure.nodes[0].sensors[0].type.c_str());
}

// --- noScanSensors ------------------------------------------------------------------------------

/** The flag decides whether the discovery is armed at all - and it is cleared, as before. */
void test_startup_no_scan_sensors_leaves_the_discovery_unarmed(void)
{
    ModuleUnderTest module;
    module.settings.setNoScanSensors(1);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));

    module.boot();

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, module.settings.getNoScanSensors(),
                                    "the flag was not consumed by the boot that used it");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.armCount(),
                              "the discovery was armed although noScanSensors was set");

    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.scanSensorCallCount,
                              "a port was probed although noScanSensors was set");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressCallCount,
                              "an address was written although noScanSensors was set");
}

/**
 * The per-model register writes, on the boot the flag skips. They are not what
 * noScanSensors is about: a boot after an OTA, a factory reset or a button press still owes them
 * to the sensors the module already holds, and the flag only skips the discovery.
 */
void test_startup_no_scan_sensors_still_prepares_the_stored_sensors(void)
{
    ModuleUnderTest module;
    module.settings.setNoScanSensors(1);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));

    module.boot();

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.preparationArmCount(),
                              "the preparation was not armed on a boot that skips the discovery");

    StartupScanStep::Kind onlyKind = StartupScanStep::NONE;
    int steps = 0;
    StartupScanStep step;
    while (module.startupScan.takeNextStep(step))
    {
        onlyKind = step.kind;
        steps++;
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, steps, "a boot that skips the discovery produced more than the preparation");
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::PREPARE_KNOWN, onlyKind,
                              "the one step of a no-scan boot was not the preparation");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.prepareSensorsForReadingCallCount,
                              "the stored sensors were never prepared on a no-scan boot");
}

/**
 * The flag on a module that holds no sensor: there is nothing to prepare, and the flag is used up on
 * this boot - the discovery is armed, as a catalog that arrives late arms it.
 */
void test_startup_no_scan_sensors_on_a_module_with_no_sensor_arms_the_discovery(void)
{
    ModuleUnderTest module;
    module.settings.setNoScanSensors(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));

    module.boot();

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.armCount(),
                              "a module that holds no sensor was not armed for the discovery");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.preparationArmCount(),
                              "a module that holds no sensor was armed for a preparation of nothing");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.settings.getNoScanSensors(), "the boot did not consume the flag");
}

/**
 * What the front panel may call a scan: not a boot, whichever kind of boot it is.
 *
 * A boot arms a pass and the pass outlives the boot - it is still walking the free ports half a
 * minute into the control loop, on a module that is by then working normally. Indicating it would
 * put a blink on the panel of every module for the first minute of its life, and nobody asked for
 * it: an installer looks at the LED to see whether the module is up, and it is. The pass a person
 * asks for through the app is the one that is answered on the panel, and only it.
 *
 * The boot that skips the discovery is the same answer for a second reason: it writes the
 * registers its stored sensors are owed and asks the line no question at all.
 */
void test_startup_no_boot_is_a_scan_entry(void)
{
    ModuleUnderTest module;
    module.settings.setNoScanSensors(1);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));

    module.boot();

    // The premise: a pass really is under way, so there is a window in which the wrong answer
    // would be given.
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.preparationArmCount(),
                              "the preparation was not armed - there is nothing to have asked "
                              "about");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.isRunning(),
                             "the preparation pass was not running after the boot armed it");

    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isScanEntryRunning(),
                              "a boot told not to scan called its register writes a scan entry - "
                              "the module would say it was looking for sensors while it was not");

    // And through the step itself, which is where the pass spends its time.
    int steps = 0;
    int stepsReportedAsAnEntry = 0;
    StartupScanStep step;
    while (module.startupScan.takeNextStep(step))
    {
        steps++;
        if (module.startupScan.isScanEntryRunning())
        {
            stepsReportedAsAnEntry++;
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, steps, "the preparation did not produce its one step");
    TEST_ASSERT_EQUAL_MESSAGE(0, stepsReportedAsAnEntry,
                              "the preparation step was reported as a scan entry");

    // The boot that does look: armed, running, walking the line - and still not an entry.
    ModuleUnderTest scanning(true);
    scanning.moduleConfig.setSensorPinCount(2);
    scanning.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    scanning.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    scanning.startupScan.setRawBusAccess(&scanning.rawBus);
    scanning.sensorReader.readSensorReturnValue = false;
    scanning.sensorReader.scanSensorReturnValue = false;

    scanning.boot();

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)scanning.startupScan.armCount(),
                              "a boot without the flag did not arm the discovery");
    TEST_ASSERT_FALSE_MESSAGE(scanning.startupScan.isScanEntryRunning(),
                              "the pass a restart armed was reported as an entry");

    scanning.skipTheBootSettleWindow();
    unsigned long now = scanning.nowMs();
    int scanningSteps = 0;
    int scanningStepsReportedAsAnEntry = 0;
    for (int round = 0; round < 8 && scanning.startupScan.isRunning(); round++)
    {
        StartupScanStep armedStep;
        while (scanning.startupScan.takeNextStep(armedStep))
        {
            scanningSteps++;
            if (scanning.startupScan.isScanEntryRunning())
            {
                scanningStepsReportedAsAnEntry++;
            }
            scanning.startupScan.applyStepOutcome(
                scanning.startupScan.runStepOnBus(armedStep));
        }
        now += StartupSensorScanUseCase::PORT_WAKE_UP_MS;
        scanning.arduino.setMillis(now);
    }

    TEST_ASSERT_TRUE_MESSAGE(scanningSteps > 0,
                             "the pass a restart armed put no step on the line - there is nothing "
                             "to have been silent about");
    TEST_ASSERT_EQUAL_MESSAGE(0, scanningStepsReportedAsAnEntry,
                              "a step of the pass a restart armed was reported as a scan entry");
}

void test_startup_a_normal_boot_arms_the_discovery(void)
{
    ModuleUnderTest module;
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));

    module.boot();

    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.isRunning(),
                             "a normal boot left the discovery unarmed");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, module.runDiscovery(),
                                     "the armed discovery produced no work");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, module.sensorReader.scanSensorCallCount,
                                     "the discovery never reached the line");
}

// --- one step at a time --------------------------------------------------------------------------

/**
 * One step is in flight at a time: until its answer is applied, the discovery offers nothing else.
 * That is what bounds the queue in front of the line to a single entry of discovery work.
 */
void test_startup_only_one_step_is_offered_until_its_answer_comes_back(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.boot();

    StartupScanStep first;
    TEST_ASSERT_TRUE(module.startupScan.takeNextStep(first));

    StartupScanStep second;
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.takeNextStep(second),
                              "a second step was offered while the first was still on the line");

    module.startupScan.returnStep();
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.takeNextStep(second),
                             "a step given back was not offered again");
    TEST_ASSERT_EQUAL_MESSAGE(first.kind, second.kind,
                              "the step given back was replaced by a different one");
    TEST_ASSERT_EQUAL_STRING(first.entity.uid.c_str(), second.entity.uid.c_str());
}

// --- what a scan of a free port does -------------------------------------------------------------

/**
 * The transactions of one candidate step are the ones the boot scan made for one model on one
 * port: the broadcast address write, the per-port address, the model's own address, then the
 * module's whole ModBus range.
 */
void test_startup_a_candidate_step_probes_the_addresses_the_boot_scan_probed(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();

    StartupScanStep step;
    while (module.startupScan.takeNextStep(step) && step.kind != StartupScanStep::SCAN_CANDIDATE)
    {
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::SCAN_CANDIDATE, step.kind,
                              "no candidate step was produced for an empty port");
    TEST_ASSERT_EQUAL_MESSAGE(0, step.pin, "the first free port scanned was not port 0");

    module.sensorReader.reset();
    module.sensorReader.scanSensorReturnValue = false;
    module.startupScan.runStepOnBus(step);

    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.writeSensorAddressCallCount,
                              "the broadcast address write of the scan did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 0, module.sensorReader.lastWriteToAddress,
                              "the scan did not offer the port its own address");

    const int expectedProbes = 2 + (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1);
    TEST_ASSERT_EQUAL_MESSAGE(expectedProbes, module.sensorReader.scanSensorCallCount,
                              "a candidate step did not probe the addresses the boot scan probed");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 0, module.sensorReader.probedAddresses[0],
                              "the first probe was not at the per-port address");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.probedAddresses[1],
                              "the second probe was not at the model's own address");
}

/**
 * A model that answers is written into the registry as NEW straight away - not at the end of the
 * discovery - and the rest of the catalog is not tried on that port.
 */
void test_startup_a_detected_sensor_is_adopted_on_the_spot(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-2", "HUMID", 2));
    // The device on port 0 answers at the per-port address the scan hands it, and no read reaches it
    // before that: it holds an address nobody asks.
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 0);
    module.sensorReader.readSensorReturnValue = false;
    module.boot();

    StartupScanStep step;
    while (module.startupScan.takeNextStep(step) && step.kind != StartupScanStep::SCAN_CANDIDATE)
    {
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }
    TEST_ASSERT_EQUAL(StartupScanStep::SCAN_CANDIDATE, step.kind);

    const int adoptedBefore = module.sensorRepository.createOrUpdateSensorCallCount;
    module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));

    TEST_ASSERT_EQUAL_MESSAGE(adoptedBefore + 1, module.sensorRepository.createOrUpdateSensorCallCount,
                              "the detected sensor was not written to the registry when it was found");
    TEST_ASSERT_EQUAL_MESSAGE(SENSOR_STATUS_NEW,
                              module.sensorRepository.lastCreatedOrUpdatedSensor.status,
                              "the detected sensor was not adopted with NEW status");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("AA:BB:CC:DD:EE:FF.00",
                                     module.sensorRepository.lastCreatedOrUpdatedSensor.uid.c_str(),
                                     "the adopted sensor does not carry the uid of its port");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.adoptedCount(),
                              "the discovery did not count the sensor it adopted");

    // The next step belongs to another port: the answered one is done with.
    StartupScanStep next;
    TEST_ASSERT_TRUE(module.startupScan.takeNextStep(next));
    TEST_ASSERT_TRUE_MESSAGE(next.pin != 0,
                             "the catalog was still being tried on a port that answered");
}

/** A port on which nothing answered is described as an empty port, as the boot scan described it. */
void test_startup_a_port_that_answered_nothing_gets_an_empty_record(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    int emptyRecords = 0;
    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        if (module.moduleStructure.nodes[0].sensors[i].type == "none")
        {
            emptyRecords++;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(MODULE_PIN_COUNT, emptyRecords,
                              "the ports on which nothing answered were not recorded as empty");
}

// --- what a ping of a stored entity does ---------------------------------------------------------

/**
 * A sensor the person had accepted, silent at this moment, keeps its place in the structure: the
 * module says it exists and is in error, not that its port is empty.
 */
void test_startup_a_silent_added_sensor_stays_in_the_structure(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.sensorRepository.sensors.size(),
                              "an accepted sensor was removed from the registry for being silent");

    bool describedAsItself = false;
    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        const Sensor& record = module.moduleStructure.nodes[0].sensors[i];
        if (record.pin == 0 && record.type != "none")
        {
            describedAsItself = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(describedAsItself,
                             "the accepted sensor was described as an empty port");
}

/**
 * And a sensor a scan found but nobody has accepted yet keeps its place in the *registry*: the
 * model and the ModBus address already found for it are not thrown away because the device
 * happened to be silent during one restart. The step from NEW to ADDED is the person's, made
 * through SetSensorModel; nothing here makes it, and nothing here unmakes it.
 */
void test_startup_a_silent_new_sensor_survives_the_discovery(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.sensorRepository.sensors.size(),
                              "the NEW sensor was deleted from the registry for being silent");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("AA:BB:CC:DD:EE:FF.01",
                                     module.sensorRepository.sensors[0].uid.c_str(),
                                     "the surviving row is not the NEW sensor");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1,
                              module.sensorRepository.sensors[0].modbusDeviceAddress,
                              "the address already found for the NEW sensor was lost");
    TEST_ASSERT_EQUAL_MESSAGE(SENSOR_STATUS_NEW, module.sensorRepository.sensors[0].status,
                              "the status of the NEW sensor was changed by the discovery");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorRepository.removeCallCount,
                              "the discovery removed something from the registry");
}

/**
 * The structure the boot built already describes every stored entity. When one of them turns out
 * silent, its record is replaced by the empty-port one - not joined by it: two records under one
 * channel uid would leave the backend with a structure saying two things about the same channel.
 */
void test_startup_a_silent_stored_sensor_is_described_once(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    const std::string channelUid = "AA:BB:CC:DD:EE:FF.01.01";
    int records = 0;
    std::string typeSeen;
    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        if (module.moduleStructure.nodes[0].sensors[i].uid == channelUid)
        {
            records++;
            typeSeen = module.moduleStructure.nodes[0].sensors[i].type;
        }
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, records,
                              "the structure carries more than one record for the same channel");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", typeSeen.c_str(),
                                     "the silent sensor is not described as an empty port");
}

/**
 * A sensor a restart adopts has to reach the backend without waiting for the next one, and it
 * reaches it through the structure the discovery settles on.
 */
void test_startup_an_adopted_sensor_is_republished(void)
{
    ModuleUnderTest adopting;
    adopting.moduleConfig.setSensorPinCount(1);
    adopting.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    adopting.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 0);
    adopting.boot();
    adopting.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)adopting.startupScan.adoptedCount(),
                              "the scenario did not adopt anything");
    // Once as it was adopted, and once more in the structure the pass ends with.
    TEST_ASSERT_EQUAL_MESSAGE(2, adopting.republish.calls,
                              "the adopted sensor was not announced as it was adopted and again at "
                              "the end of the pass");
    TEST_ASSERT_EQUAL_STRING("startup_scan_found", adopting.republish.reasons[0].c_str());
    TEST_ASSERT_EQUAL_STRING("startup_scan", adopting.republish.lastReason.c_str());
}

// --- what the discovery lets go of when it is over ---------------------------------------------

/**
 * This use case is constructed in globals.h and lives as long as the module does, while its work
 * takes minutes once. Anything it still holds afterwards it holds for ever - and the largest such
 * thing is the sensor type catalog, handed out by the repository as one shared list precisely so
 * that readers do not each carry a copy of 1.4-2 KB per type.
 *
 * Measured the way the repository itself measures a released catalog: with the real
 * SensorTypeRepository over an in-memory DAO, the use count of the shared list must be back to
 * "the repository and this local" once the discovery is over.
 */
void test_startup_the_finished_discovery_lets_the_catalog_go(void)
{
    // The whole module, twice: one that runs the discovery and one that does not. What is
    // compared is what the repository can still give back afterwards.
    struct CatalogStand
    {
        MockLogger logger;
        MockDebugSystem debugSystem;
        MockSettings settings;
        MockModuleConfig moduleConfig;
        MockSensorReader sensorReader;
        MockSensorRepository sensorRepository;
        MockSpiffsSensorTypeDao sensorTypeDao;
        SensorTypeRepository sensorTypeRepository;
        ModuleStructure moduleStructure;
        VpdChannelUseCase vpdChannel;
        VirtualSensorChannelsUseCase virtualChannels;
        SensorStructureUseCase sensorStructure;
        ModbusAddressAllocator allocator;
        StartupSensorScanUseCase scan;

        CatalogStand()
            : moduleConfig(&logger, &debugSystem, &settings),
              sensorTypeRepository(&sensorTypeDao),
              virtualChannels(&vpdChannel),
              sensorStructure(&virtualChannels),
              allocator(&sensorRepository),
              scan(&logger, &debugSystem, &moduleConfig, &sensorRepository, &sensorTypeRepository,
                   &sensorReader, &moduleStructure, &sensorStructure, &allocator)
        {
            debugSystem.featureEnabled = false;
            moduleConfig.setSettings(&settings);
            moduleConfig.setModuleUid(MODULE_UID);
            moduleConfig.setSensorPinCount(MODULE_PIN_COUNT);
            sensorReader.scanSensorReturnValue = false;
            moduleStructure.nodes.push_back(Node());
            for (int i = 0; i < 12; i++)
            {
                char uid[24];
                char name[24];
                snprintf(uid, sizeof(uid), "type-%d", i);
                snprintf(name, sizeof(name), "MODEL_%d", i);
                sensorTypeDao.addToStorage(makeModbusType(uid, name, 1));
            }
            sensorTypeRepository.count(); // read from storage once, before anything is measured
        }

        void runDiscovery()
        {
            scan.arm();
            StartupScanStep step;
            int guard = 0;
            while (scan.takeNextStep(step))
            {
                scan.applyStepOutcome(scan.runStepOnBus(step));
                if (++guard > 5000)
                {
                    TEST_FAIL_MESSAGE("the discovery did not terminate");
                }
            }
        }

        /** @brief Bytes the repository gives back when it drops the catalog it holds. */
        long long bytesFreedByDroppingTheCatalog()
        {
            const long long before = HostAllocationCounter::liveBytes();
            sensorTypeRepository.invalidateCache();
            return before - HostAllocationCounter::liveBytes();
        }
    };

    long long freedWithout = 0;
    long long freedAfter = 0;
    long controlUseCount = 0;
    long afterUseCount = 0;

    {
        CatalogStand control;
        controlUseCount = control.sensorTypeRepository.catalogSnapshot().use_count();
        freedWithout = control.bytesFreedByDroppingTheCatalog();
    }

    int retainedAfter = 0;
    bool finished = false;

    {
        CatalogStand scanned;
        scanned.runDiscovery();
        finished = !scanned.scan.isRunning();
        afterUseCount = scanned.sensorTypeRepository.catalogSnapshot().use_count();
        retainedAfter = (int)scanned.scan.retainedEntryCount();
        freedAfter = scanned.bytesFreedByDroppingTheCatalog();
    }

    TEST_ASSERT_TRUE_MESSAGE(finished, "the discovery did not finish");

    printf("  startup-scan: the catalog list has %ld owners after a discovery and %ld without one; "
           "dropping it frees %lld B after a discovery and %lld B without one\n",
           afterUseCount, controlUseCount, freedAfter, freedWithout);

    char message[256];
    snprintf(message, sizeof(message),
             "the catalog list is held by %ld owners after the discovery and by %ld without one - "
             "the finished discovery is still pinning the catalog it scanned",
             afterUseCount, controlUseCount);
    TEST_ASSERT_EQUAL_MESSAGE(controlUseCount, afterUseCount, message);

    TEST_ASSERT_GREATER_THAN_MESSAGE(1000, freedWithout,
                                     "the stand's catalog is too light for the measurement to mean anything");

    snprintf(message, sizeof(message),
             "dropping the catalog gives back %lld B on a module that ran the discovery and %lld B "
             "on one that did not - the discovery is still pinning the difference",
             freedAfter, freedWithout);
    TEST_ASSERT_EQUAL_MESSAGE(freedWithout, freedAfter, message);

    TEST_ASSERT_EQUAL_MESSAGE(0, retainedAfter,
                              "the finished discovery is still holding entries of the registry "
                              "and of its own plan");
}

/** The same for the boot the flag sends down the short path: it holds nothing either. */
void test_startup_a_preparation_only_boot_holds_nothing_afterwards(void)
{
    ModuleUnderTest module;
    module.settings.setNoScanSensors(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));

    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.retainedEntryCount(),
                              "the preparation step is still holding a copy of the registry");
}

// --- what the discovery must not touch ----------------------------------------------------------

/**
 * The discovery now takes minutes, and it shares them with the control loop. A person adding a
 * sensor through the app - or a ScanSensors command doing the same - claims a port while the walk
 * is still on it, and the walk must leave that port alone: no further model tried on it, no
 * broadcast re-address of a device somebody has just configured, and above all no registry row
 * replaced by a scan's guess.
 *
 * This is the rule the branch states in applyVerify() and must obey everywhere: the step from NEW
 * to ADDED is the person's, and nothing here makes it or unmakes it. There are three moments at
 * which the claim can land - while the step is on the line, between two steps, and while a step
 * that could not be queued waits - and each is its own test below, because each is caught by its
 * own check.
 */

/** @brief Walk the discovery until it offers a candidate step for `pin`, and hold that step. */
bool takeCandidateStepFor(ModuleUnderTest& module, int pin, StartupScanStep& step)
{
    int guard = 0;
    while (module.startupScan.takeNextStep(step))
    {
        // Either rung that tries a model on a free port: the cheap probe of its own address and
        // the full identification procedure both write the port's row if they find something.
        if ((step.kind == StartupScanStep::SCAN_CANDIDATE ||
             step.kind == StartupScanStep::PROBE_CANDIDATE) && step.pin == pin)
        {
            return true;
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        if (++guard > 500)
        {
            return false;
        }
    }
    return false;
}

/** @brief What SetSensorModel leaves in the registry when a person accepts a sensor on a port. */
SensorEntity acceptedByPerson(int pin)
{
    char uid[48];
    snprintf(uid, sizeof(uid), "%s.%02d", MODULE_UID, pin);
    SensorEntity accepted = makeStoredEntity(uid, pin, MODBUS_ADDRESS_MIN + pin, SENSOR_STATUS_ADDED);
    accepted.model = "accepted_by_person";
    accepted.sensorType = "accepted_by_person";
    return accepted;
}

/** @brief The row of a port, as the registry holds it now. */
void assertPortOneUntouched(ModuleUnderTest& module, const char* what)
{
    Utils::Optional<SensorEntity> row = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01");
    char message[224];

    TEST_ASSERT_TRUE_MESSAGE((bool)row, "the accepted sensor vanished from the registry");

    snprintf(message, sizeof(message), "%s: the model somebody accepted was overwritten", what);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("accepted_by_person", row.value().model.c_str(), message);

    snprintf(message, sizeof(message), "%s: an accepted sensor was pushed back to NEW", what);
    TEST_ASSERT_EQUAL_MESSAGE(SENSOR_STATUS_ADDED, row.value().status, message);

    snprintf(message, sizeof(message), "%s: the address of an accepted sensor was moved", what);
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1, row.value().modbusDeviceAddress, message);
}

/**
 * 1. The claim lands while the step is on the line. Nothing re-plans in between - the answer comes
 *    straight back to applyStepOutcome() - so only the check before the write can save the row.
 */
void test_startup_a_port_claimed_while_its_step_is_on_the_line_keeps_its_row(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-2", "HUMID", 2));
    module.sensorReader.scanSensorReturnValue = true; // everything answers, so a scan would adopt
    module.boot();

    StartupScanStep step;
    TEST_ASSERT_TRUE_MESSAGE(takeCandidateStepFor(module, 1, step),
                             "the discovery never offered a step for port 1");

    // The bus task is busy with the step; the control loop writes the registry meanwhile.
    module.sensorRepository.createOrUpdateSensor(acceptedByPerson(1));

    const int adoptedBefore = (int)module.startupScan.adoptedCount();
    module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));

    assertPortOneUntouched(module, "an answer that arrived after the claim");
    TEST_ASSERT_EQUAL_MESSAGE(adoptedBefore, (int)module.startupScan.adoptedCount(),
                              "the discovery adopted a port somebody had already claimed");
}

/**
 * 2. The claim lands between two steps. The next step for that port is planned after it, so the
 *    check before every model is what has to notice.
 */
void test_startup_a_port_claimed_between_steps_is_not_walked_further(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-2", "HUMID", 2));
    module.sensorReader.scanSensorReturnValue = false; // nothing answers, so the walk goes on
    module.boot();

    StartupScanStep step;
    TEST_ASSERT_TRUE_MESSAGE(takeCandidateStepFor(module, 1, step),
                             "the discovery never offered a step for port 1");
    // That step finishes normally; nothing is pending when the claim lands.
    module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));

    module.sensorRepository.createOrUpdateSensor(acceptedByPerson(1));
    const size_t writesBefore = countWritesOnPin(module.sensorReader, 1);

    int furtherCandidates = 0;
    int guard = 0;
    while (module.startupScan.takeNextStep(step))
    {
        if (step.kind == StartupScanStep::SCAN_CANDIDATE && step.pin == 1)
        {
            furtherCandidates++;
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        if (++guard > 500)
        {
            TEST_FAIL_MESSAGE("the discovery did not terminate");
        }
    }

    TEST_ASSERT_EQUAL_MESSAGE(0, furtherCandidates,
                              "the discovery kept trying models on a port somebody had claimed");
    TEST_ASSERT_EQUAL_MESSAGE(writesBefore, countWritesOnPin(module.sensorReader, 1),
                              "the discovery wrote a ModBus address on a port somebody had claimed");
    assertPortOneUntouched(module, "a claim that landed between two steps");
}

/**
 * And what such a claim leaves in the log, which is the whole reason the line exists: somebody
 * took this port while the walk was on it, so the walk left it alone.
 *
 * The line is only true of a port claimed by somebody else. A pass that adopts a sensor makes the
 * port it adopted it on an occupied one too, and there the same words tell a person on site that
 * the module did *not* take the sensor they just plugged in - see the tape at the end of this
 * file, which is that case and has no such line in it.
 */
void test_startup_a_port_a_person_claimed_mid_pass_is_said_to_have_been_left_alone(void)
{
    ModuleUnderTest module;
    // The debug feature the discovery logs under is on, as it is on a module in the field.
    module.debugSystem.featureEnabled = true;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-2", "HUMID", 2));
    module.sensorReader.scanSensorReturnValue = false; // nothing answers, so the walk goes on
    module.boot();

    StartupScanStep step;
    TEST_ASSERT_TRUE_MESSAGE(takeCandidateStepFor(module, 1, step),
                             "the discovery never offered a step for port 1");
    module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));

    module.sensorRepository.createOrUpdateSensor(acceptedByPerson(1));

    int guard = 0;
    while (module.startupScan.takeNextStep(step))
    {
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        if (++guard > 500)
        {
            TEST_FAIL_MESSAGE("the discovery did not terminate");
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(
        module.logger.hasMessageContaining(
            "pin 1 was claimed while the discovery walked it - leaving it alone"),
        "a port somebody claimed under the walk was passed over without a word about why");
}

/**
 * 3. The claim lands while a step that could not be queued waits to be offered again. A step given
 *    back is re-offered as it was, so it has to be re-checked before it goes out a second time.
 */
void test_startup_a_step_that_waited_is_dropped_when_its_port_is_claimed(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-2", "HUMID", 2));
    module.sensorReader.scanSensorReturnValue = true;
    module.boot();

    StartupScanStep step;
    TEST_ASSERT_TRUE_MESSAGE(takeCandidateStepFor(module, 1, step),
                             "the discovery never offered a step for port 1");
    // The acquisition task had no room: the step comes back unexecuted.
    module.startupScan.returnStep();

    module.sensorRepository.createOrUpdateSensor(acceptedByPerson(1));
    const size_t writesBefore = countWritesOnPin(module.sensorReader, 1);

    int furtherCandidates = 0;
    int guard = 0;
    while (module.startupScan.takeNextStep(step))
    {
        if (step.kind == StartupScanStep::SCAN_CANDIDATE && step.pin == 1)
        {
            furtherCandidates++;
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        if (++guard > 500)
        {
            TEST_FAIL_MESSAGE("the discovery did not terminate");
        }
    }

    TEST_ASSERT_EQUAL_MESSAGE(0, furtherCandidates,
                              "a step that waited went out on a port somebody had claimed");
    TEST_ASSERT_EQUAL_MESSAGE(writesBefore, countWritesOnPin(module.sensorReader, 1),
                              "a step that waited wrote a ModBus address on a claimed port");
    assertPortOneUntouched(module, "a claim that landed while a step waited");
}

/** 4. And the port nobody scanned is not reported as empty either - somebody is on it. */
void test_startup_a_claimed_port_is_not_reported_as_empty(void)
{
    ModuleUnderTest module;
    // One model, so the walk of port 1 is over the moment its only candidate has been tried and
    // nothing re-checks the port before the empty-port record is written.
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();

    StartupScanStep step;
    TEST_ASSERT_TRUE_MESSAGE(takeCandidateStepFor(module, 1, step),
                             "the discovery never offered a step for port 1");
    module.sensorRepository.createOrUpdateSensor(acceptedByPerson(1));
    module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));

    module.runDiscovery();

    for (size_t i = 0; i < module.moduleStructure.nodes[0].sensors.size(); i++)
    {
        const Sensor& record = module.moduleStructure.nodes[0].sensors[i];
        if (record.uid == std::string(MODULE_UID) + ".01" && record.type == "none")
        {
            TEST_FAIL_MESSAGE("a port somebody had claimed was reported as an empty port");
        }
    }
}

// --- what the boot may honestly say ---------------------------------------------------------------

/**
 * What the backend is told about a stored sensor turns on one thing: whether the line answered for
 * it on this boot. Not on whether somebody is going to ask - a boot cannot know the answer before
 * it has asked, and guessing produced the wrong report on whichever guess was made. Both arms
 * below are what the boot scan reported for the same sensor.
 */

/** @brief The type the structure carries for a channel, or "" when it carries no record for it. */
std::string typeOfRecord(ModuleStructure& structure, const std::string& channelUid)
{
    if (structure.nodes.empty())
    {
        return "";
    }
    for (size_t i = 0; i < structure.nodes[0].sensors.size(); i++)
    {
        if (structure.nodes[0].sensors[i].uid == channelUid)
        {
            return structure.nodes[0].sensors[i].type;
        }
    }
    return "";
}

const char* const STORED_CHANNEL = "AA:BB:CC:DD:EE:FF.01.01";

/**
 * Arm one, and the commonest boot of this product: the sensor was added, the module was restarted
 * as the app instructs, the person has not accepted it yet - and it is there and answering. The
 * base reported its type. So does this: the boot has nothing against it, and the ping confirms it.
 */
void test_startup_a_stored_sensor_that_answers_is_reported_by_its_type(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);

    module.boot();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("temperature",
                                     typeOfRecord(module.moduleStructure, STORED_CHANNEL).c_str(),
                                     "the boot reported a stored sensor as an empty port");

    module.runDiscovery();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("temperature",
                                     typeOfRecord(module.moduleStructure, STORED_CHANNEL).c_str(),
                                     "a sensor the line answered for stopped being reported by its type");
}

/**
 * Arm two: the same sensor, and nothing on the line. The base reported an empty port - once its
 * ping had failed, which is also when this reports it. Before the ping the module has been told
 * nothing and says what it was told.
 */
void test_startup_a_stored_sensor_that_stays_silent_becomes_an_empty_port(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.scanSensorReturnValue = false;

    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOfRecord(module.moduleStructure, STORED_CHANNEL).c_str(),
                                     "a sensor that answered nothing is still reported by its type");
}

/**
 * And what the backend actually receives. The structure that left at boot described the registry
 * before a single question had been put to the line; the one that matters is the settled one, and
 * it is sent whether or not anything was adopted - the commonest boot adopts nothing and still
 * learns which of its stored sensors are really there.
 */
void test_startup_the_settled_structure_is_sent_even_when_nothing_was_adopted(void)
{
    ModuleUnderTest module;
    // A catalog the module holds but cannot scan for: the free ports produce no candidate step,
    // so this boot adopts nothing and its only finding is that the stored sensor is there.
    SensorType unscannable = makeModbusType("type-1", "TEMP", 1);
    unscannable.canScan = false;
    module.sensorTypeRepository.addSensorType(unscannable);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);

    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.adoptedCount(),
                              "the scenario adopted something - it is meant not to");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.republish.calls,
                              "the module never sent the structure it settled on");
    TEST_ASSERT_EQUAL_STRING("startup_scan", module.republish.lastReason.c_str());
}

/** The boot that skips the discovery asks nothing, so it settles nothing and resends nothing. */
void test_startup_a_no_scan_boot_sends_no_second_structure(void)
{
    ModuleUnderTest module;
    module.settings.setNoScanSensors(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));

    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(0, module.republish.calls,
                              "a boot that asked the line nothing resent the structure anyway");
}

/**
 * The boot that skips the discovery keeps trusting the registry whole, as it always has: nothing
 * is going to check, so describing a stored sensor as an empty port would be a claim of its own.
 */
void test_startup_a_no_scan_boot_reports_the_registry_as_it_stands(void)
{
    ModuleUnderTest module;
    module.settings.setNoScanSensors(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));

    module.boot();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("temperature",
                                     typeOfRecord(module.moduleStructure, STORED_CHANNEL).c_str(),
                                     "the no-scan boot stopped reporting the registry as it stands");
}

/**
 * The person accepts the sensor while its ping is still on the line. The answer comes back a
 * failure, but the row is no longer the row the step carried: it is ADDED now, and an accepted
 * sensor is described as itself whether it answers or not - which is what the boot scan did.
 */
void test_startup_a_sensor_accepted_while_its_ping_ran_is_described_as_itself(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.scanSensorReturnValue = false;

    module.boot();

    StartupScanStep step;
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.takeNextStep(step),
                             "the discovery offered no step at all");
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::VERIFY_KNOWN, step.kind,
                              "the first step of the discovery was not the ping of the stored sensor");

    // SetSensorModel, on the control loop, while the bus task holds the ping.
    SensorEntity accepted = makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1,
                                             SENSOR_STATUS_ADDED);
    module.sensorRepository.createOrUpdateSensor(accepted);

    module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));

    TEST_ASSERT_EQUAL_STRING_MESSAGE("temperature",
                                     typeOfRecord(module.moduleStructure, STORED_CHANNEL).c_str(),
                                     "a sensor accepted while its ping ran was described as an empty port");

    module.runDiscovery();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("temperature",
                                     typeOfRecord(module.moduleStructure, STORED_CHANNEL).c_str(),
                                     "the rest of the discovery took the accepted sensor back");
}

// --- the ModBus addresses ------------------------------------------------------------------------

/**
 * Two sensors on one address: .02 on port 2 and .03 on port 3 both at 0x72. The entity kept
 * is the one sitting on the address of its own port, whatever order the registry lists them in.
 */
void test_startup_the_entity_on_its_own_port_address_keeps_it(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorReader.scanSensorReturnValue = false;
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("module.03", 3, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_ADDED));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("module.02", 2, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_ADDED));
    module.logger.reset();
    module.boot();

    StartupScanStep step;
    bool sawWrite = false;
    while (module.startupScan.takeNextStep(step))
    {
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            sawWrite = true;
            TEST_ASSERT_EQUAL_STRING_MESSAGE("module.03", step.entity.uid.c_str(),
                                             "the wrong entity of the collision was moved");
            TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 3, step.targetAddress,
                                      "the moved entity was not offered the address of its own port");
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }

    TEST_ASSERT_TRUE_MESSAGE(sawWrite, "the address collision produced no address write");
    TEST_ASSERT_TRUE(module.logger.jsonDetailsFor("duplicate_owner_chosen").find("per_port_match") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(module.logger.jsonDetailsFor("duplicate_owner_chosen").find("module.02") !=
                     std::string::npos);
}

/** Neither entity sits on the address of its own port: the survivor is the first one. */
void test_startup_a_collision_with_no_owner_keeps_the_first_entity(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorReader.scanSensorReturnValue = false;
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("module.00", 0, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_ADDED));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("module.01", 1, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_ADDED));
    module.logger.reset();
    module.boot();

    std::string movedUid;
    StartupScanStep step;
    while (module.startupScan.takeNextStep(step))
    {
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            movedUid = step.entity.uid;
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }

    TEST_ASSERT_EQUAL_STRING_MESSAGE("module.01", movedUid.c_str(),
                                     "the entity kept was not the first one the registry listed");
    TEST_ASSERT_TRUE(module.logger.jsonDetailsFor("duplicate_owner_chosen").find("first_in_repository") !=
                     std::string::npos);
}

/** The registry row follows the device: a refused write leaves the stored address untouched. */
void test_startup_a_refused_address_write_leaves_the_registry_alone(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorReader.scanSensorReturnValue = false;
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("module.03", 3, 0x10, SENSOR_STATUS_ADDED));
    module.sensorReader.writeSensorAddressReturnValue = false;
    module.boot();

    bool sawWrite = false;
    StartupScanStep step;
    while (module.startupScan.takeNextStep(step))
    {
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            sawWrite = true;
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }

    TEST_ASSERT_TRUE_MESSAGE(sawWrite, "the out-of-range address produced no write attempt");
    TEST_ASSERT_EQUAL_MESSAGE(0x10, module.sensorRepository.sensors[0].modbusDeviceAddress,
                              "the registry was updated although the device refused the write");
}

// --- the ModBus addresses the discovery hands out ------------------------------------------------

/**
 * The address phase resolves collisions, so it is the one phase that must never create one. The
 * allocator answers only from the occupancy it is handed, and between two address steps the
 * control loop can have given a port to somebody: the occupancy has to be read from the registry
 * at the moment the address is chosen, not from a map built when the phase was planned.
 */
void test_startup_an_address_taken_mid_phase_is_not_handed_out_again(void)
{
    ModuleUnderTest module;
    // Nothing scannable: the free ports produce no candidate steps, so what runs is the address
    // phase and nothing else.
    SensorType unscannable = makeModbusType("type-1", "TEMP", 1);
    unscannable.canScan = false;
    module.sensorTypeRepository.addSensorType(unscannable);

    // Two stored sensors holding addresses outside the module's range: both have to be moved, and
    // each prefers the address of its own port - 0x70 for port 0, 0x71 for port 1.
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, 0x10, SENSOR_STATUS_ADDED));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, 0x11, SENSOR_STATUS_ADDED));

    module.boot();

    int addressSteps = 0;
    int secondTarget = -1;
    int guard = 0;
    bool claimed = false;
    StartupScanStep step;
    while (module.startupScan.takeNextStep(step))
    {
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            addressSteps++;
            if (addressSteps == 1)
            {
                TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 0, step.targetAddress,
                                          "the first move did not take the address of its own port");
            }
            else if (addressSteps == 2)
            {
                secondTarget = step.targetAddress;
            }
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));

        // Between the two address steps a person's sensor takes 0x71 - the address the second one
        // is about to prefer.
        if (addressSteps == 1 && !claimed)
        {
            claimed = true;
            SensorEntity personsSensor = makeStoredEntity("AA:BB:CC:DD:EE:FF.02", 2,
                                                          MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_ADDED);
            personsSensor.model = "accepted_by_person";
            module.sensorRepository.createOrUpdateSensor(personsSensor);
        }

        if (++guard > 500)
        {
            TEST_FAIL_MESSAGE("the discovery did not terminate");
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(secondTarget >= 0, "the second sensor was never given a new address");
    TEST_ASSERT_TRUE_MESSAGE(secondTarget != MODBUS_ADDRESS_MIN + 1,
                             "the discovery handed out an address a live row already held");

    // And the registry is free of the collision this phase exists to resolve.
    const std::vector<SensorEntity> rows = module.sensorRepository.getAll();
    for (size_t i = 0; i < rows.size(); i++)
    {
        for (size_t j = i + 1; j < rows.size(); j++)
        {
            if (rows[i].modbusDeviceAddress == rows[j].modbusDeviceAddress)
            {
                char message[224];
                snprintf(message, sizeof(message),
                         "%s and %s both hold 0x%02X after the discovery",
                         rows[i].uid.c_str(), rows[j].uid.c_str(), rows[i].modbusDeviceAddress);
                TEST_FAIL_MESSAGE(message);
            }
        }
    }
}

// --- the one channel by which a settled structure reaches the backend ---------------------------

/**
 * The republish is the only way a structure the module has settled on gets sent again, and it
 * rebuilds the sensor records from scratch. Rebuilding them its own way would make it the second
 * answer to "how is a stored row described" and it would erase the first: a stored sensor the
 * module asked the line about and heard nothing from would go out under its own type again.
 */
void test_startup_a_republish_describes_stored_sensors_the_same_way(void)
{
    ModuleUnderTest module;
    MockHybridAPIService api;
    RepublishModuleStructureUseCase republish(&module.moduleStructure, &module.sensorRepository,
                                              &api, &module.sensorStructure, &module.logger,
                                              &module.debugSystem, nullptr);
    republish.setStoredSensorDescriber(&module.startupScan);

    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.scanSensorReturnValue = false;

    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOfRecord(module.moduleStructure, STORED_CHANNEL).c_str(),
                                     "the discovery did not settle on an empty port - "
                                     "the scenario did not happen");

    TEST_ASSERT_TRUE_MESSAGE(republish.execute("test"), "the republish was refused");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("none",
                                     typeOfRecord(module.moduleStructure, STORED_CHANNEL).c_str(),
                                     "the republish put a stored sensor back under its own type - "
                                     "the structure the backend gets disagrees with what the module found");
}

/** And a sensor the line answered for is still described by its type after a republish. */
void test_startup_a_republish_keeps_the_type_of_a_sensor_that_answered(void)
{
    ModuleUnderTest module;
    MockHybridAPIService api;
    RepublishModuleStructureUseCase republish(&module.moduleStructure, &module.sensorRepository,
                                              &api, &module.sensorStructure, &module.logger,
                                              &module.debugSystem, nullptr);
    republish.setStoredSensorDescriber(&module.startupScan);

    SensorType unscannable = makeModbusType("type-1", "TEMP", 1);
    unscannable.canScan = false;
    module.sensorTypeRepository.addSensorType(unscannable);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);

    module.boot();
    module.runDiscovery();
    TEST_ASSERT_TRUE_MESSAGE(republish.execute("test"), "the republish was refused");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("temperature",
                                     typeOfRecord(module.moduleStructure, STORED_CHANNEL).c_str(),
                                     "the republish reported a sensor that answers as an empty port");
}

// --- the preparation ------------------------------------------------------------------------------

/**
 * The per-model register writes happen on every boot that scans, and they
 * happen last - after the scan and the address resolution, so they reach the addresses the
 * entities ended up on.
 */
void test_startup_the_preparation_is_the_last_step_and_runs_once(void)
{
    ModuleUnderTest module;
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();

    StartupScanStep::Kind lastKind = StartupScanStep::NONE;
    int prepareSteps = 0;
    StartupScanStep step;
    while (module.startupScan.takeNextStep(step))
    {
        lastKind = step.kind;
        if (step.kind == StartupScanStep::PREPARE_KNOWN)
        {
            prepareSteps++;
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }

    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::PREPARE_KNOWN, lastKind,
                              "the preparation was not the last thing the discovery did");
    TEST_ASSERT_EQUAL_MESSAGE(1, prepareSteps, "the preparation ran more than once");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.prepareSensorsForReadingCallCount,
                              "the preparation never reached the reader");
}

// --- the ladder: what a free port is asked, in what order, and how often ------------------------

/**
 * A free port is asked three questions, and every one of them is only asked because the cheaper
 * one before it was answered with silence. This is the order, seen as the sequence of steps the
 * discovery produces for one port. On the restart's pass the cheap rung reads the module's range
 * first - address by address, every model at each - and then each model at its own factory address.
 */
void test_startup_a_free_port_is_asked_cheaply_before_it_is_identified(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-2", "HUMID", 2));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.rawBus.answeringChannels.push_back(0); // something is on the port, but no model claims it
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();

    std::vector<StartupScanStep::Kind> order;
    std::vector<int> probedAddressesInOrder;
    StartupScanStep step;
    int guard = 0;
    while (module.startupScan.takeNextStep(step))
    {
        if (step.kind == StartupScanStep::PROBE_CANDIDATE ||
            step.kind == StartupScanStep::PROBE_PRESENCE ||
            step.kind == StartupScanStep::SCAN_CANDIDATE)
        {
            order.push_back(step.kind);
            probedAddressesInOrder.push_back(step.entity.modbusDeviceAddress);
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        if (++guard > 500)
        {
            TEST_FAIL_MESSAGE("the discovery did not terminate");
        }
    }

    const int RANGE = MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1;
    const int CHEAP = 2 * RANGE + 2;
    TEST_ASSERT_EQUAL_MESSAGE(CHEAP + 3, (int)order.size(),
                              "the port was not asked the cheap questions (the range and the two "
                              "factory addresses), the presence poll and then the two identifications");
    char message[160];
    for (int i = 0; i < CHEAP; i++)
    {
        const int address = (i < 2 * RANGE) ? MODBUS_ADDRESS_MIN + i / 2 : 1 + (i - 2 * RANGE);
        snprintf(message, sizeof(message), "cheap question %d: not a cheap probe of address 0x%02X", i,
                 address);
        TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::PROBE_CANDIDATE, order[i], message);
        TEST_ASSERT_EQUAL_MESSAGE(address, probedAddressesInOrder[i], message);
    }
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::PROBE_PRESENCE, order[CHEAP],
                              "the port was not asked whether anything is on it before the "
                              "identification procedure was spent on it");
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::SCAN_CANDIDATE, order[CHEAP + 1],
                              "the identification procedure did not follow the presence answer");
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::SCAN_CANDIDATE, order[CHEAP + 2],
                              "the second model was not identified either");

    // The presence poll is the address discovery's wildcard query, and it stops at the first answer.
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.rawBus.requestsOnChannel(0),
                              "the presence poll went on asking after it had its answer");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, module.rawBus.requests[0].slave,
                                   "the presence poll was not addressed to the wildcard slave");
    TEST_ASSERT_EQUAL_HEX8(0x03, module.rawBus.requests[0].functionCode);
    TEST_ASSERT_EQUAL_HEX16(0x0100, module.rawBus.requests[0].startRegister);
    TEST_ASSERT_EQUAL_MESSAGE(StartupSensorScanUseCase::PRESENCE_POLL_TIMEOUT_MS,
                              (uint32_t)module.rawBus.requests[0].timeoutMs,
                              "the presence poll waited for something other than the read budget");
}

/**
 * And the case the ladder exists for: a port with nothing on it is answered by the cheap probes -
 * on the restart's pass the two models at the four addresses of the range and at their own - and
 * one presence poll, and the identification procedure is never spent on it at all.
 */
void test_startup_a_port_that_answers_nothing_never_reaches_the_identification(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-2", "HUMID", 2));
    module.startupScan.setRawBusAccess(&module.rawBus); // nothing answers on any channel
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(2 * (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1 + 1),
                              (int)module.startupScan.cheapProbeCount(),
                              "the two models were not probed cheaply on the free port, at the "
                              "range and at their own address");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.presencePollCount(),
                              "the port was not asked once whether anything is on it");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.identifyStepCount(),
                              "the identification procedure was spent on a port that answered "
                              "nothing at all");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressCallCount,
                              "the identification procedure's address write went out on a port "
                              "with nothing on it");
    // What the restart's window does write on such a port: the port's address, once per model, the
    // way the identification procedure wrote it into every free port.
    TEST_ASSERT_EQUAL_MESSAGE(StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS ? 2 : 0,
                              module.sensorReader.writeSensorAddressByBroadcastCallCount,
                              "the restart's window did not write the port's address once per "
                              "model of the catalog");

    // Both wildcard query frames were tried before the port was called empty.
    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.rawBus.requestsOnChannel(0),
                              "the presence poll is not the two wildcard queries");
    TEST_ASSERT_EQUAL_HEX8(0x03, module.rawBus.requests[0].functionCode);
    TEST_ASSERT_EQUAL_HEX16(0x0100, module.rawBus.requests[0].startRegister);
    TEST_ASSERT_EQUAL_HEX8(0x6E, module.rawBus.requests[1].functionCode);
    TEST_ASSERT_EQUAL_HEX16(0x0012, module.rawBus.requests[1].startRegister);

    // And the port is described exactly as the boot scan described it: an empty port.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "none", typeOfRecord(module.moduleStructure, std::string(MODULE_UID) + ".00").c_str(),
        "the port nothing answered on is not described as an empty port");
}

/**
 * A device holding the address it left the factory on is found by the cheap rung - and adopted at
 * that address, which is not one the module hands out. Moving it into the module's own range is a
 * separate act with its own step, and it is the same address write it has always been.
 */
void test_startup_a_device_at_its_factory_address_is_adopted_and_then_moved(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 7));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.answeringAddresses.push_back(7);
    module.boot();

    int addressWrites = 0;
    int addressWriteTarget = -1;
    int identifySteps = 0;
    StartupScanStep step;
    int guard = 0;
    while (module.startupScan.takeNextStep(step))
    {
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            addressWrites++;
            addressWriteTarget = step.targetAddress;
        }
        if (step.kind == StartupScanStep::SCAN_CANDIDATE)
        {
            identifySteps++;
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        if (++guard > 500)
        {
            TEST_FAIL_MESSAGE("the discovery did not terminate");
        }
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.adoptedCount(),
                              "the device sitting at its factory address was not adopted");
    TEST_ASSERT_EQUAL_MESSAGE(0, identifySteps,
                              "the identification procedure was spent on a device that answered "
                              "its own factory address");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.rawBus.requestsOnChannel(0),
                              "a port that answered a model was still asked whether anything is on it");
    TEST_ASSERT_EQUAL_MESSAGE(1, addressWrites,
                              "the adopted device was not moved off its factory address");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 0, addressWriteTarget,
                              "the device was not offered the address of its own port");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 0,
                              module.sensorRepository.sensors[0].modbusDeviceAddress,
                              "the registry did not follow the device to its new address");
}

/**
 * The cheap probe is one request, and what it hears is not enough to adopt on: the frame may have
 * come from the paired port of the same UART, and the value in it has not been checked against
 * the range the model declares. The model scan is what decides, exactly as it always did.
 */
void test_startup_a_cheap_probe_alone_adopts_nothing(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    // The single read answers; the model scan that follows it does not.
    module.sensorReader.readSensorReturnValue = true;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.adoptedCount(),
                              "a sensor was adopted on a single read nobody confirmed");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.sensorRepository.sensors.size(),
                              "the registry gained a row from an unconfirmed read");
}

/**
 * `noScanSensors` is about the line, not about the boot: a module that was told not to scan does
 * not scan periodically either, however long it runs. A module that holds a sensor - one that
 * holds none is discovered whatever the flag says.
 */
void test_startup_no_scan_sensors_stops_the_periodic_probing_too(void)
{
    ModuleUnderTest module(true);
    module.settings.setNoScanSensors(1);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.readSensorCallCount,
                              "a restart told not to scan asked the line something");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressByBroadcastCallCount,
                              "a restart told not to scan wrote an address into a free port");

    module.sensorReader.reset();
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;

    // An hour of a module that was told not to look.
    module.runFor(3600000);

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.freePortPassCount(),
                              "a module told not to scan walked the free ports anyway");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.readSensorCallCount,
                              "a module told not to scan put a cheap probe on the line");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.scanSensorCallCount,
                              "a module told not to scan probed the line");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.rawBus.requests.size(),
                              "a module told not to scan broadcast a presence poll");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.sensorReader.portPowerCalls.size(),
                              "a module told not to scan raised the power of a free port");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressByBroadcastCallCount,
                              "a module told not to scan wrote an address into a free port");
}

/**
 * The periodic pass is the cheap rung and nothing else: whatever it hears, it never spends the
 * identification procedure, and it never describes a port it heard nothing from - the structure
 * already says what the full pass concluded.
 */
void test_startup_the_periodic_pass_is_only_the_cheap_rung(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    const size_t recordsAfterTheFirstPass = module.moduleStructure.nodes[0].sensors.size();
    const int cheapAfterTheFirstPass = (int)module.startupScan.cheapProbeCount();
    const int republishesAfterTheFirstPass = module.republish.calls;

    // Ten minutes of an ordinary module, with the clock running rather than jumping.
    module.runFor(600000);

    const int passes = (int)module.startupScan.freePortPassCount();
    TEST_ASSERT_TRUE_MESSAGE(passes >= 10,
                             "the cheap rung stopped running - ten minutes should hold a pass per "
                             "interval, and the interval is half a minute");
    // Per free port, the one model at each address of the module's range and at its own.
    const int probesPerPort = (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1) + 1;
    TEST_ASSERT_EQUAL_MESSAGE(cheapAfterTheFirstPass + passes * 2 * probesPerPort,
                              (int)module.startupScan.cheapProbeCount(),
                              "a periodic pass did not probe both free ports for the one model at the "
                              "range and at its own address");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.identifyStepCount(),
                              "a periodic pass ran the identification procedure");
    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.startupScan.presencePollCount(),
                              "a periodic pass broadcast a presence poll on a port it had already "
                              "described as empty - the two polls of the restart's own pass are "
                              "all a module with two empty ports owes");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.prepareSensorsForReadingCallCount - 1,
                              "a periodic pass that found nothing still prepared the registry");
    TEST_ASSERT_EQUAL_MESSAGE(recordsAfterTheFirstPass,
                              module.moduleStructure.nodes[0].sensors.size(),
                              "a periodic pass added records to the structure - the empty ports "
                              "are described once, by the pass that established them");
    TEST_ASSERT_EQUAL_MESSAGE(republishesAfterTheFirstPass, module.republish.calls,
                              "a periodic pass that found nothing sent the structure again");
}

/**
 * What the front panel is allowed to show: the pass a person asked for, and nothing else.
 *
 * The periodic cheap walk never ends, so a module that blinked for it would blink for the rest of
 * its life; the pass a restart arms does end, but it runs on every boot and nobody asked for it.
 * A ScanSensors command is somebody standing in front of the module having pressed a button, and
 * what they get is the answer that the module is doing what they asked.
 */
void test_startup_only_a_hand_asked_pass_is_a_scan_entry(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;

    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isScanEntryRunning(),
                              "a discovery nobody armed reported an entry in flight");

    module.boot();
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isScanEntryRunning(),
                              "the pass a restart armed was reported as an entry");

    module.runDiscovery();
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isScanEntryRunning(),
                              "something was reported as an entry after the restart's pass ended");

    // Ten minutes of the periodic walk, sampled at every step it hands to the line.
    unsigned long now = module.nowMs();
    int steps = 0;
    int stepsReportedAsAnEntry = 0;
    for (int tick = 0; tick < 120; tick++)
    {
        now += 5000;
        module.arduino.setMillis(now);
        StartupScanStep step;
        while (module.startupScan.takeNextStep(step))
        {
            steps++;
            if (module.startupScan.isScanEntryRunning())
            {
                stepsReportedAsAnEntry++;
            }
            module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() >= 10,
                             "the periodic walk did not run - there is nothing to have asked about");
    TEST_ASSERT_TRUE_MESSAGE(steps > 0, "the periodic walk put no step on the line");
    TEST_ASSERT_EQUAL_MESSAGE(0, stepsReportedAsAnEntry,
                              "the periodic walk over the free ports was reported as a scan entry "
                              "- the module would blink for as long as it runs");

    // And a pass a person asked for is one, from the moment they ask until it is over.
    module.startupScan.rescan(0);
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.isScanEntryRunning(),
                             "a pass a ScanSensors command asked for was not an entry - it is owed "
                             "from the moment the command asks, while the line is still serving "
                             "the command's own procedure");

    for (int tick = 0; tick < 40 && module.startupScan.isScanEntryRunning(); tick++)
    {
        now += 5000;
        module.arduino.setMillis(now);
        StartupScanStep step;
        while (module.startupScan.takeNextStep(step))
        {
            module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        }
    }

    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isScanEntryRunning(),
                              "the pass a command asked for never stopped being an entry");
}

/**
 * A port that answers the line and matches no model gets one identification pass, and the
 * ScanSensors command is the way back to another one.
 */
void test_startup_an_unidentifiable_port_is_given_up_on_until_a_rescan(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.rawBus.answeringChannels.push_back(0);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.identifyStepCount(),
                              "the one model of the catalog was not tried on the port that answered");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.unidentifiedOccupantCount(),
                              "the module did not record that the port answers and matches nothing");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.unidentifiedPinCount(),
                              "the port was not held out of the identification rung");

    const int cheapAfterTheFullPass = (int)module.startupScan.cheapProbeCount();

    // An hour of passes with the thing still answering: not one more identification.
    module.runFor(3600000);

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.identifyStepCount(),
                              "the identification procedure ran again on a port already given up on");
    // What is rationed is the expensive rung, and only it. The cheap one names an address, costs
    // the ports sharing the wire nothing, and is the only thing that would notice a sensor put in
    // the place of the device nobody could name - so it goes on being asked.
    TEST_ASSERT_TRUE_MESSAGE((int)module.startupScan.cheapProbeCount() > cheapAfterTheFullPass,
                             "a port the module gave up identifying is never asked the cheap "
                             "questions again - a sensor plugged into it would never be found");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.unidentifiedPinCount(),
                              "the port stopped being counted while its device was still answering");

    // The command a person sends forgets all of it: the note goes, the port is asked from the top
    // of the ladder again, and the answer it gives is recorded afresh.
    const int cheapBeforeTheRescan = (int)module.startupScan.cheapProbeCount();
    module.startupScan.rescan(0);
    module.runFor(60000);

    TEST_ASSERT_TRUE_MESSAGE((int)module.startupScan.cheapProbeCount() > cheapBeforeTheRescan,
                             "a rescan did not ask the port anything");
    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.startupScan.unidentifiedOccupantCount(),
                              "a rescan did not look at the port again - the note the last pass "
                              "left was never dropped and never established a second time");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.unidentifiedPinCount(),
                              "the port is held out of the identification rung more than once, or "
                              "not at all, after a rescan");
    // And the procedure itself is not the ladder's to run twice: the ScanSensors command that
    // calls rescan() walks the whole catalog on this port on its own, and the line serves it
    // before this pass. A second walk here would isolate the rest of the port's wire again.
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.identifyStepCount(),
                              "the pass a rescan asked for ran the identification procedure a "
                              "second time on the same port");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.armCount(),
                              "a rescan armed the whole-module pass - the stored sensors are "
                              "verified and every free port walked because one port was named");
}

/**
 * A pass a person asked for asks a port that answered nothing at all the same two cheap questions
 * every other pass asks it, and comes back saying the port is empty. What it must not do is call
 * it a port with something unnameable on it: that note is what rations the expensive rung, and it
 * belongs to ports the line was heard on.
 */
void test_startup_a_hand_asked_pass_calls_a_silent_port_empty_not_unnameable(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // nothing answers on any channel
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.identifyStepCount(),
                              "the restart's own pass spent the procedure on a port that answered "
                              "nothing - the ladder is not what is under test then");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.unidentifiedOccupantCount(),
                              "an empty port was recorded as unnameable before anybody asked");

    // The person asks about that port.
    const int pollsBeforeTheRescan = (int)module.startupScan.presencePollCount();
    module.startupScan.rescan(0);
    module.runFor(120000);

    TEST_ASSERT_TRUE_MESSAGE((int)module.startupScan.presencePollCount() > pollsBeforeTheRescan,
                             "a pass asked for by hand never asked the port whether anything is "
                             "on it - then there is nothing for it to conclude");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.unidentifiedOccupantCount(),
                              "a port that answered nothing at all was recorded as having "
                              "something on it that matches no model");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.unidentifiedPinCount(),
                              "an empty port was held out of the identification rung as if "
                              "something unnameable were on it");
}

/**
 * And the pass is about one port: the one the command named. Every question of the ladder past the
 * cheap rung costs the ports it is not asked about - the presence poll takes the power off the
 * ports sharing the wire, the identification procedure takes it off all of them - so a pass over
 * ports nobody asked about is a working sensor taken off the line for the length of it.
 */
void test_startup_a_hand_asked_pass_asks_the_port_it_names_and_no_other(void)
{
    ModuleUnderTest module;
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    // Something on the port the person is about to ask about, answering nothing the catalog knows.
    module.rawBus.answeringChannels.push_back(2);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    const int cheapAfterTheBoot = (int)module.startupScan.cheapProbeCount();
    const int pollsAfterTheBoot = (int)module.startupScan.presencePollCount();
    const int identifyAfterTheBoot = (int)module.startupScan.identifyStepCount();
    const size_t requestsOnTheOtherPorts = module.rawBus.requestsOnChannel(0) +
                                           module.rawBus.requestsOnChannel(1) +
                                           module.rawBus.requestsOnChannel(3);

    TEST_ASSERT_EQUAL_MESSAGE(4, pollsAfterTheBoot,
                              "the restart's own pass did not ask all four free ports - the "
                              "comparison below is against the wrong number");

    module.startupScan.rescan(2);
    module.runDiscovery();

    // One model on one port, and one poll of that port: the whole of what the pass costs.
    TEST_ASSERT_EQUAL_MESSAGE(cheapAfterTheBoot + 1, (int)module.startupScan.cheapProbeCount(),
                              "a pass asked for by hand probed ports the command did not name");
    TEST_ASSERT_EQUAL_MESSAGE(pollsAfterTheBoot + 1, (int)module.startupScan.presencePollCount(),
                              "a pass asked for by hand broadcast a presence poll on a port the "
                              "command did not name - and that poll cuts the power of every port "
                              "sharing its wire");
    TEST_ASSERT_EQUAL_MESSAGE(requestsOnTheOtherPorts,
                              (int)(module.rawBus.requestsOnChannel(0) +
                                    module.rawBus.requestsOnChannel(1) +
                                    module.rawBus.requestsOnChannel(3)),
                              "a pass asked for by hand put a frame on a port nobody named");
    TEST_ASSERT_EQUAL_MESSAGE(identifyAfterTheBoot, (int)module.startupScan.identifyStepCount(),
                              "the pass a rescan asked for spent the identification procedure - "
                              "the command that asked for it has just spent it on that port");
}

/**
 * And the ladder does not remember a ghost: a port whose occupant has gone quiet is an ordinary
 * free port again, cheap probes and all, without anybody asking.
 */
void test_startup_a_port_whose_occupant_left_returns_to_the_cheap_rung(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.rawBus.answeringChannels.push_back(0);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.unidentifiedPinCount(),
                              "the scenario did not produce a port given up on");

    // Whatever it was is unplugged.
    module.rawBus.answeringChannels.clear();

    module.runFor(60000);
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.unidentifiedPinCount(),
                              "the port stayed given up on after its occupant went quiet");

    const int cheapBefore = (int)module.startupScan.cheapProbeCount();
    module.runFor(60000);
    TEST_ASSERT_TRUE_MESSAGE((int)module.startupScan.cheapProbeCount() > cheapBefore,
                             "the port is not asked cheaply again now that it is quiet");
}

// --- the wire two ports share, and the power that is the only thing separating them --------------

/** @brief Ports 0 and 1 on one UART, the way globals.h wires the first pair of the board. */
std::vector<int> twoPortsOnOneLine()
{
    std::vector<int> lines;
    lines.push_back(0);
    lines.push_back(0);
    return lines;
}

/**
 * The presence poll names no address, so every device that can hear it answers it - including the
 * sensor working on the port that shares the wire. The identification procedure never puts these frames on a line it
 * has not isolated first, and neither does this: the paired port loses its power for the length of
 * the poll and has it back before anything else runs.
 */
void test_startup_the_presence_poll_takes_the_paired_port_down_and_gives_it_back(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.rawBus.reader = &module.sensorReader;
    // Port 0 is powered, the way the preparation leaves a port with a sensor on it.
    module.sensorReader.portPowered[0] = true;
    module.sensorReader.readSensorReturnValue = false;
    module.boot();
    module.runFor(60000);

    TEST_ASSERT_TRUE_MESSAGE(module.rawBus.requestsOnChannel(1) > 0,
                             "the free port was never asked whether anything is on it");

    for (size_t i = 0; i < module.rawBus.requests.size(); i++)
    {
        if (module.rawBus.requests[i].channel != 1)
        {
            continue;
        }
        TEST_ASSERT_FALSE_MESSAGE(module.rawBus.portWasPoweredAtRequest(i, 0),
                                  "a wildcard query went out on the free port while the sensor "
                                  "sharing its wire still had power - the answer could be its own");
    }

    TEST_ASSERT_TRUE_MESSAGE(module.sensorReader.isPortPowered(0),
                             "the port the poll took down was left without its power");
    TEST_ASSERT_EQUAL_MESSAGE(module.sensorReader.portPowerCallCount(0, false),
                              module.sensorReader.portPowerCallCount(0, true),
                              "the power taken off the working port was not given back as often "
                              "as it was taken");
}

/**
 * A poll outside a window - the one a pass asked for by hand puts on the port it names - gives the
 * paired port its power back at once, and what is plugged into it has then had that power for no
 * time at all. So the sweep is kept off it for as long as after the close of a window: the time
 * what is on it needs to come up. A read before that is answered by nobody and counted as a miss.
 */
void test_startup_a_poll_outside_a_window_keeps_the_sweep_off_the_port_it_gave_back(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.rawBus.reader = &module.sensorReader;
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();
    module.settle();
    // The working sensor's port has its power, as the sweep's reads leave it.
    module.sensorReader.portPowered[1] = true;

    module.startupScan.rescan(0);
    unsigned long nowMs = module.nowMs();
    bool polled = false;
    bool poweredAfterThePoll = false;
    bool keptOffAfterThePoll = false;
    unsigned long polledAtMs = 0;
    for (int tick = 0; tick < 120 && !polled; tick++)
    {
        nowMs += 1000;
        module.arduino.setMillis(nowMs);
        StartupScanStep step;
        while (!polled && module.startupScan.takeNextStep(step))
        {
            const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
            if (step.kind == StartupScanStep::PROBE_PRESENCE && step.pin == 0)
            {
                polled = true;
                polledAtMs = nowMs;
                TEST_ASSERT_FALSE_MESSAGE(module.startupScan.holdsAWindowOpen(),
                                          "a pass asked for by hand opened a window");
                poweredAfterThePoll = module.sensorReader.isPortPowered(1);
                keptOffAfterThePoll = module.startupScan.keepsThePortDark(1);
            }
            module.startupScan.applyStepOutcome(outcome);
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(polled, "the pass asked for by hand never polled the port it names");
    TEST_ASSERT_TRUE_MESSAGE(module.sensorReader.portPowerCallCount(1, false) > 0,
                             "the poll did not take the paired port down - nothing to test");
    TEST_ASSERT_TRUE_MESSAGE(poweredAfterThePoll,
                             "the poll did not give the paired port its power back");
    TEST_ASSERT_TRUE_MESSAGE(keptOffAfterThePoll,
                             "the sweep was let at the paired port the moment the poll gave it its "
                             "power back - what is on it has not come up yet");

    module.arduino.setMillis(polledAtMs + StartupSensorScanUseCase::PORT_WAKE_UP_MS);
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.keepsThePortDark(1),
                              "the paired port is still held off the sweep after its warm-up");
}

/**
 * A free port is dark between two passes: the pass raises its power to ask it something and puts
 * it back down, so whatever is plugged into a port the module could not name is off the wire its
 * paired port works on for all the time the module is not asking.
 */
void test_startup_a_free_port_is_powered_for_the_pass_and_dark_between_two(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runFor(600000);
    // Between two passes, not in the middle of one: a pass that is still walking has its ports
    // powered on purpose, and that is not what this is about.
    module.settle();

    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() >= 5,
                             "the cheap rung stopped running - there is nothing to measure");
    TEST_ASSERT_TRUE_MESSAGE(module.sensorReader.portPowerCallCount(0, true) > 0,
                             "the free ports were never given their power - nothing could have "
                             "answered a cheap probe");

    TEST_ASSERT_FALSE_MESSAGE(module.sensorReader.isPortPowered(0),
                              "a free port was left under power between two passes");
    TEST_ASSERT_FALSE_MESSAGE(module.sensorReader.isPortPowered(1),
                              "a free port was left under power between two passes");
    TEST_ASSERT_EQUAL_MESSAGE(module.sensorReader.portPowerCallCount(0, true),
                              module.sensorReader.portPowerCallCount(0, false),
                              "the passes raised the power of a free port more often than they put "
                              "it back down");
}

/**
 * And the port is not asked the moment its power arrives: a sensor needs seconds of it before it
 * answers anything, which is why every other power transition in this firmware is followed by a
 * ten-second wait. The wait costs no line time - the pass simply has nothing to offer while it
 * lasts - and it is what makes the answer "this port is empty" worth anything.
 */
void test_startup_the_ports_are_given_time_to_come_up_before_they_are_asked(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    // This is about what the pass does with the ports, not about when it starts.
    module.skipTheBootSettleWindow();

    StartupScanStep step;
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.takeNextStep(step),
                             "the discovery offered no step at all");
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::POWER_FREE_PORTS, step.kind,
                              "the pass did not open by giving the free ports their power");
    module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    TEST_ASSERT_TRUE_MESSAGE(module.sensorReader.isPortPowered(0),
                             "the power step left the port without power");

    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.takeNextStep(step),
                              "the port was asked something in the same instant its power arrived");

    module.arduino.setMillis(BOOT_SETTLE_MS + StartupSensorScanUseCase::PORT_WAKE_UP_MS - 1);
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.takeNextStep(step),
                              "the port was asked before it had had its power for as long as this "
                              "firmware waits after every other power transition");

    module.arduino.setMillis(BOOT_SETTLE_MS + StartupSensorScanUseCase::PORT_WAKE_UP_MS + 1);
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.takeNextStep(step),
                             "the port was never asked anything after it had come up");
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::PROBE_CANDIDATE, step.kind,
                              "the first thing asked of a port that has come up was not a cheap probe");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.readSensorCallCount,
                              "a cheap probe went out while the port was still coming up");
}

/**
 * The broadcast poll is what costs the paired port a power cycle, and it is the only thing a
 * periodic pass gives up on such a wire: a sensor the module reads is not something to switch off
 * twice a minute for the sake of a note about a device nobody can name. The cheap rung names an
 * address, so it costs that sensor nothing at all - and it goes on being asked, which is how the
 * port is still a port a person can plug something into.
 */
void test_startup_a_port_given_up_on_is_not_polled_while_its_wire_carries_a_sensor(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.rawBus.reader = &module.sensorReader;
    module.sensorReader.portPowered[0] = true;
    // Something on port 1 answers the line and no model claims it.
    module.rawBus.answeringChannels.push_back(1);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runFor(60000);

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.unidentifiedPinCount(),
                              "the scenario did not produce a port given up on");

    const size_t pollsAfterTheFullPass = module.rawBus.requestsOnChannel(1);
    const size_t cyclesAfterTheFullPass = module.sensorReader.portPowerCallCount(0, false);
    const int cheapAfterTheFullPass = (int)module.startupScan.cheapProbeCount();

    // Ten minutes of passes with the thing still there.
    module.runFor(600000);

    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() >= 5,
                             "the cheap rung stopped running - there is nothing to measure");
    TEST_ASSERT_EQUAL_MESSAGE((int)pollsAfterTheFullPass, (int)module.rawBus.requestsOnChannel(1),
                              "a periodic pass broadcast a presence poll on the wire of a working "
                              "sensor");
    TEST_ASSERT_EQUAL_MESSAGE((int)cyclesAfterTheFullPass,
                              (int)module.sensorReader.portPowerCallCount(0, false),
                              "the working sensor was power-cycled by a periodic pass over the "
                              "port sharing its wire");
    // And the port itself is not frozen: what it loses is the one question that costs its
    // neighbour, not the ones that cost nobody anything.
    TEST_ASSERT_TRUE_MESSAGE((int)module.startupScan.cheapProbeCount() > cheapAfterTheFullPass,
                             "the port on the working sensor's wire is asked nothing at all - a "
                             "sensor plugged into it would never be found");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.unidentifiedPinCount(),
                              "the port stopped being counted as unidentifiable without anybody "
                              "asking the line about it");
}

/**
 * And what that buys: the port on the working sensor's wire is still a port. Somebody takes out
 * the device nobody could name and puts an ordinary sensor there, and the module finds it at its
 * interval - without a restart, without a command, and without the sensor on the other end of the
 * wire noticing anything.
 */
void test_startup_a_sensor_put_where_the_module_gave_up_is_found_by_a_cheap_pass(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.rawBus.reader = &module.sensorReader;
    module.sensorReader.portPowered[0] = true;
    // Something on port 1 answers the line and no model claims it.
    module.rawBus.answeringChannels.push_back(1);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runFor(60000);

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.unidentifiedPinCount(),
                              "the scenario did not produce a port given up on");
    const uint32_t identifyStepsBefore = module.startupScan.identifyStepCount();

    // The thing nobody could name comes out; an ordinary sensor holding its factory address goes in.
    module.rawBus.answeringChannels.clear();
    module.sensorReader.readSensorReturnValue = true;
    module.sensorReader.answeringAddresses.push_back(1);

    module.runFor(120000);

    Utils::Optional<SensorEntity> adopted =
        module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01");
    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "a sensor plugged into a port the module had given up identifying was "
                             "never found - the port was frozen by what an earlier pass concluded");
    TEST_ASSERT_EQUAL_MESSAGE(1, adopted.value().pin,
                              "the sensor was adopted on a port it was not plugged into");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(identifyStepsBefore,
                                     (uint32_t)module.startupScan.identifyStepCount(),
                                     "the identification procedure was spent on a sensor sitting "
                                     "at its own factory address");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.unidentifiedPinCount(),
                              "the port is still counted as unidentifiable after the module named "
                              "what is on it");
}

// --- no row is left on the address its model leaves the factory with ---------------------------

/**
 * @brief A row of the registry as the app leaves it: the model's own address, on a port of its own.
 *
 * The address resolution of SetSensorModel now moves such a device on the spot, but a module in
 * the field has rows this shape all the same - written by a firmware that did not, or by a command
 * whose address write the device refused, or restored from a configuration.
 */
SensorEntity makeRowOnItsFactoryAddress(const char* uid, int pin, const char* typeUid, int address)
{
    SensorEntity row = makeStoredEntity(uid, pin, address, SENSOR_STATUS_ADDED);
    row.sensorTypeUid = typeUid;
    return row;
}

/**
 * The invariant: a factory address is a state a row passes through. The module reads such a row,
 * and the first pass after that hands it the address of its own port - no free port involved, no
 * command, no restart.
 */
void test_startup_a_row_still_on_its_factory_address_is_moved_once_the_sweep_reads_it(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    // Both ports of the wire are taken, so there is no free port to hear anything for anybody:
    // whatever moves the row, it is not the answer a free port of its wire keeps getting.
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    // What the restart itself asked the line, so that what follows can be compared against it.
    const size_t pollsAfterTheRestart = module.rawBus.requests.size();

    // The person adds a sensor from the app after the restart. It answers, and the sweep reads it:
    // that read is the module's own evidence that there is something at the other end to write to.
    module.sensorRepository.sensors.push_back(
        makeRowOnItsFactoryAddress("AA:BB:CC:DD:EE:FF.01", 1, "type-1", 1));
    module.sensorValueModel.lastReadTimes["AA:BB:CC:DD:EE:FF.01"] = module.nowMs() + 1;
    // The counters start from the row, not from the restart; the ports stay silent.
    module.sensorReader.reset();
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;

    module.runFor(120000);

    Utils::Optional<SensorEntity> moved = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01");
    TEST_ASSERT_TRUE_MESSAGE((bool)moved, "the row left the registry");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1, moved.value().modbusDeviceAddress,
                              "the row is still on the address its model leaves the factory on - "
                              "a second device of that model would answer for it");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)countWritesOnPin(module.sensorReader, 1),
                              "the address of the module was not written into the device exactly once");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.factoryAddressMoveCount(),
                              "the move was not counted");
    TEST_ASSERT_EQUAL_MESSAGE((int)pollsAfterTheRestart, (int)module.rawBus.requests.size(),
                              "the move needed a broadcast poll of a free port - it must need "
                              "nothing but the registry");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.scanSensorCallCount,
                              "the move needed a scan of the line - it must need nothing but the "
                              "registry and the write itself");
}

/**
 * The write costs the rest of its wire its power, so it is spent on evidence and not on
 * hope: a row nothing has ever answered for is left where it is until the sweep reads it.
 */
void test_startup_a_row_the_sweep_has_not_read_is_not_written_to(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    // Nothing is plugged into the free ports of this stand: the row under test is the whole
    // scenario, and a reader that answers every question would have the restart adopt sensors too.
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    // The row exists - the person added the sensor - and nothing has answered for it: the device
    // is not plugged in yet.
    module.sensorRepository.sensors.push_back(
        makeRowOnItsFactoryAddress("AA:BB:CC:DD:EE:FF.01", 1, "type-1", 1));
    // The counters start from the row, not from the restart; the ports stay silent.
    module.sensorReader.reset();
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;

    module.runFor(300000);

    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressCallCount,
                              "the module wrote an address into a row nothing had answered for");

    // It is plugged in, the sweep reads it, and the next pass moves it.
    module.sensorValueModel.lastReadTimes["AA:BB:CC:DD:EE:FF.01"] = module.nowMs() + 1;
    module.runFor(120000);

    Utils::Optional<SensorEntity> moved = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01");
    TEST_ASSERT_TRUE_MESSAGE((bool)moved, "the row left the registry");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1, moved.value().modbusDeviceAddress,
                              "the row was not moved after the sweep started reading it");
}

/**
 * A row that has fallen silent is not written to either: the flag the sweep keeps is the same fact
 * read the other way round, and an address write ends in a ping the silent device would not answer.
 */
void test_startup_a_row_the_sweep_has_lost_is_not_written_to(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    // Nothing is plugged into the free ports of this stand: the row under test is the whole
    // scenario, and a reader that answers every question would have the restart adopt sensors too.
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    SensorEntity row = makeRowOnItsFactoryAddress("AA:BB:CC:DD:EE:FF.01", 1, "type-1", 1);
    row.isError = true;
    module.sensorRepository.sensors.push_back(row);
    module.sensorValueModel.lastReadTimes["AA:BB:CC:DD:EE:FF.01"] = module.nowMs() + 1;
    // The counters start from the row, not from the restart; the ports stay silent.
    module.sensorReader.reset();
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;

    module.runFor(300000);

    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressCallCount,
                              "the module wrote an address into a row the sweep had lost");
}

/**
 * A device whose address no write of the module's takes: one attempt, a note, and nothing after
 * it. The alternative is the module taking every port's power down twice a minute for as long as
 * such a sensor is plugged in.
 *
 * The row is in the registry before the restart - the way a module that has been running with such
 * a sensor comes up - and nothing is reset afterwards, so what is counted is every address write
 * that device gets, the restart's own included. One attempt means one for the boot and not one per
 * phase: the restart's address phase moves this row too, because the address its model leaves the
 * factory with is outside the range of the module, and the refusal it is given is the same refusal
 * a pass would spend every port's power to be given again.
 *
 * The device answers - that is what makes the refusal a refusal. It is on the port, the ping of the
 * restart is answered from it, and the address write is the one thing it does not honour.
 */
void test_startup_a_device_that_will_not_move_is_written_to_once(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorRepository.sensors.push_back(
        makeRowOnItsFactoryAddress("AA:BB:CC:DD:EE:FF.01", 1, "type-1", 1));
    // Its own address is the only one anything answers at: the free ports of this stand have
    // nothing on them - the row under test is the whole scenario - and a reader that answered for
    // them would have the restart adopt sensors as well.
    module.sensorReader.answeringAddresses.push_back(1);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.writeSensorAddressReturnValue = false;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.writeSensorAddressCallCount,
                              "the restart did not write to a row holding an address outside the "
                              "range of the module - the scenario is not the one being measured");

    // The device is on the line and the sweep is reading it, which is the fact the registry rule
    // of a cheap pass waits for. What it must not do is buy the answer the restart already has.
    module.sensorValueModel.lastReadTimes["AA:BB:CC:DD:EE:FF.01"] = module.nowMs() + 1;

    // An hour of passes, twice a minute.
    module.runFor(3600000);

    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.writeSensorAddressCallCount,
                              "the module kept writing an address into a device that would not take one");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.factoryAddressMoveRefusedCount(),
                              "the refusal was not counted");
    Utils::Optional<SensorEntity> row = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01");
    TEST_ASSERT_TRUE_MESSAGE((bool)row, "the row left the registry");
    TEST_ASSERT_EQUAL_MESSAGE(1, row.value().modbusDeviceAddress,
                              "the row moved off an address the device never left");

    // The way back is the person's: a ScanSensors command on that port makes the module try again.
    module.startupScan.rescan(1);
    module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(2, module.sensorReader.writeSensorAddressCallCount,
                              "a hand-asked pass did not try the device again");
}

/**
 * The same write into a port with nothing on it. It is refused in exactly the same way - the ping
 * that ends an address write is answered by nobody either way - and it must not leave the note: the
 * person adding a sensor in the app before plugging it in is the commonest way a row of this shape
 * comes to exist, and a note left here would hold off every pass that would otherwise move the row
 * once the device is there.
 */
void test_startup_a_write_into_a_port_with_nothing_on_it_is_not_a_refusal(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    // The row is in the registry when the module comes up, on the factory address of its model, and
    // there is nothing on the port: the sensor was added from the app and is not plugged in yet.
    module.sensorRepository.sensors.push_back(
        makeRowOnItsFactoryAddress("AA:BB:CC:DD:EE:FF.01", 1, "type-1", 1));
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.sensorReader.writeSensorAddressReturnValue = false;
    module.boot();
    module.runDiscovery();

    // The address phase of the restart writes into the row all the same - its address is outside the
    // range of the module - and nothing answers.
    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.writeSensorAddressCallCount,
                              "the restart did not write to a row holding an address outside the "
                              "range of the module - the scenario is not the one being measured");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.factoryAddressMoveRefusedCount(),
                              "the silence of a port with nothing on it was counted as a device "
                              "refusing the address the module wrote it");

    // The person comes back and plugs it in: it answers, and the sweep reads it.
    module.sensorReader.writeSensorAddressReturnValue = true;
    module.sensorValueModel.lastReadTimes["AA:BB:CC:DD:EE:FF.01"] = module.nowMs() + 1;

    module.runFor(120000);

    Utils::Optional<SensorEntity> moved = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01");
    TEST_ASSERT_TRUE_MESSAGE((bool)moved, "the row left the registry");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1, moved.value().modbusDeviceAddress,
                              "the row was never moved off the factory address - the write the "
                              "restart spent on an empty port is holding every pass off it");
    TEST_ASSERT_EQUAL_MESSAGE(2, module.sensorReader.writeSensorAddressCallCount,
                              "the module wrote the address more than once after the device "
                              "appeared");
}

/**
 * The same row, added between two restarts, so that nothing of the module's has ever put a question
 * to that port: the ping belongs to the restart's own pass and that pass is over. A pass that
 * adopted a sensor somewhere else resolves the whole registry for its address phase and writes into
 * this row too - and that write, into a port nothing has ever answered for, is not a refusal either.
 */
void test_startup_a_row_added_after_the_restart_is_not_noted_by_a_write_it_never_answered(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    // Nothing is on the ports while the restart's pass runs.
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    // The person adds their sensor in the app and goes to fetch it.
    module.sensorRepository.sensors.push_back(
        makeRowOnItsFactoryAddress("AA:BB:CC:DD:EE:FF.01", 1, "type-1", 1));
    module.sensorReader.writeSensorAddressReturnValue = false;

    // Somebody else plugs an ordinary sensor into the free port and asks for a scan of it. That
    // pass adopts, and a pass that adopted resolves the whole registry - the row waiting for its
    // device is in that registry like any other.
    module.sensorReader.readSensorReturnValue = true;
    module.sensorReader.scanSensorReturnValue = true;
    module.startupScan.rescan(0);
    module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.sensorRepository.sensors.size(),
                              "the scan adopted nothing, so nothing made the pass resolve the whole "
                              "registry - the scenario is not the one being measured");
    TEST_ASSERT_TRUE_MESSAGE(module.sensorReader.writeSensorAddressCallCount > 0,
                             "no address was written at all - the scenario is not the one being "
                             "measured");
    // One refusal is counted and it is the adopted sensor's: that device answered the model scan of
    // this very pass and then kept its address. The row still waiting for its sensor is not one -
    // nothing has ever answered for that port.
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.factoryAddressMoveRefusedCount(),
                              "more refusals were counted than the one device that answered and "
                              "refused - the write into a port nothing has ever answered for was "
                              "counted as a refusal too");

    // And then they plug theirs in: it answers, the sweep reads it, and the next pass moves it.
    module.sensorReader.writeSensorAddressReturnValue = true;
    module.sensorValueModel.lastReadTimes["AA:BB:CC:DD:EE:FF.01"] = module.nowMs() + 1;

    module.runFor(120000);

    Utils::Optional<SensorEntity> moved = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01");
    TEST_ASSERT_TRUE_MESSAGE((bool)moved, "the row left the registry");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1, moved.value().modbusDeviceAddress,
                              "the row was never moved off the factory address - a write it never "
                              "answered is holding every pass off it");
}

/**
 * The rule is about the model's own address and not about any address outside the module's range:
 * a row the catalog knows nothing about is left where it is, because there is nothing to compare
 * its address with.
 */
void test_startup_a_row_of_an_unknown_model_is_left_where_it_is(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    // Nothing is plugged into the free ports of this stand: the row under test is the whole
    // scenario, and a reader that answers every question would have the restart adopt sensors too.
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    // Same address as the catalog model's, and a type uid the catalog does not carry.
    module.sensorRepository.sensors.push_back(
        makeRowOnItsFactoryAddress("AA:BB:CC:DD:EE:FF.01", 1, "type-nobody-knows", 1));
    module.sensorValueModel.lastReadTimes["AA:BB:CC:DD:EE:FF.01"] = module.nowMs() + 1;
    // The counters start from the row, not from the restart; the ports stay silent.
    module.sensorReader.reset();
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;

    module.runFor(300000);

    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressCallCount,
                              "a row whose model the module does not know was moved on a guess");
}

/**
 * @brief A stand where one row is owed the same move by both halves of a pass.
 *
 * Two ports on one wire: the row sits on port 0 holding the address its model leaves the factory
 * with, and port 1 is free. The registry finds the row at the start of every cheap pass, and the
 * cheap probe of port 1 is answered from that same row during the pass - the probe is heard with
 * the wire live and the confirming scan, with the rest of the wire dark, hears nothing.
 */
void aRowOnAFactoryAddressWithAFreePortOnItsWire(ModuleUnderTest& module)
{
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.rawBus.reader = &module.sensorReader;
    // Port 0 is powered, the way the preparation leaves a port with a sensor on it.
    module.sensorReader.portPowered[0] = true;
    module.sensorReader.readSensorReturnValue = true;
    // The row's device answers a read at its factory address and at no address of the module's
    // range, which the restart's window asks.
    module.sensorReader.readAnsweringAddresses.push_back(1);
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    // The row goes in after the restart, so the one attempt this stand measures is a pass's and
    // not the restart's address phase.
    module.sensorRepository.sensors.push_back(
        makeRowOnItsFactoryAddress("AA:BB:CC:DD:EE:FF.00", 0, "type-1", 1));
    module.sensorValueModel.lastReadTimes["AA:BB:CC:DD:EE:FF.00"] = module.nowMs() + 1;
}

/**
 * One row, one write, however many reasons a pass has to move it. A second write would be aimed at
 * the address the first one has already taken the device off, and finding that out costs every
 * port of the module its power a second time.
 */
void test_startup_a_row_both_halves_of_a_pass_owe_a_move_is_written_to_once(void)
{
    ModuleUnderTest module(true);
    aRowOnAFactoryAddressWithAFreePortOnItsWire(module);

    module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)countWritesOnPin(module.sensorReader, 0),
                              "the row was written to more than once for the single move it owed");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.factoryAddressMoveCount(),
                              "more moves were counted than the row needed");
    Utils::Optional<SensorEntity> moved = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00");
    TEST_ASSERT_TRUE_MESSAGE((bool)moved, "the row left the registry");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN, moved.value().modbusDeviceAddress,
                              "the row is not on the address of its own port");
}

/**
 * And when that one write is refused: the note holds off both halves. The free port of the wire
 * goes on being answered by the row, and a pass a person asks for on that free port does not spend
 * every port's power to be told the same thing again - the port it names is not where the answer
 * comes from. The way back is a pass on the port the row itself is on.
 */
void test_startup_a_refused_row_is_not_queued_again_by_the_port_it_answers_for(void)
{
    ModuleUnderTest module(true);
    aRowOnAFactoryAddressWithAFreePortOnItsWire(module);
    module.sensorReader.writeSensorAddressReturnValue = false;

    // An hour of passes, twice a minute.
    module.runFor(3600000);

    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.writeSensorAddressCallCount,
                              "the module kept writing an address into a device that would not "
                              "take one");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.factoryAddressMoveRefusedCount(),
                              "the refusal was not counted");

    // The port a person would scan first: the free one, where nothing of theirs appears. What
    // answers it is the row on the other port of the wire, and that row's answer is known.
    module.startupScan.rescan(1);
    module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.writeSensorAddressCallCount,
                              "a pass asked for on the free port of the wire wrote to the device "
                              "on the other port again, which the module has already been refused "
                              "by");
    Utils::Optional<SensorEntity> stayed = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00");
    TEST_ASSERT_TRUE_MESSAGE((bool)stayed, "the row left the registry");
    TEST_ASSERT_EQUAL_MESSAGE(1, stayed.value().modbusDeviceAddress,
                              "the row moved off an address the device never left");

    // And the way back, on the port the row is on: the note is dropped and the module tries again.
    module.startupScan.rescan(0);
    module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(2, module.sensorReader.writeSensorAddressCallCount,
                              "a pass asked for on the port the row is on did not try the device "
                              "again - there is no way back at all");
}

/**
 * @brief A stand where the only fact about a refused write is the sweep's own.
 *
 * The row holds an address that is neither the module's own nor its model's: a catalog that moved
 * under it - a new factory address for the model - or a configuration written by an older firmware
 * leaves rows this shape. Nothing a pass finds out puts such a row in a plan of its own: the
 * registry rule is about the model's address and the free port of a wire hears a model's address,
 * so neither list carries it. What writes into it is the full resolution of the registry, which a
 * restart makes and so does any pass that adopted a sensor - and the pass making that write
 * adopted somebody else, on another port, minutes after the restart's ping expired.
 *
 * So the module has exactly one fact left about that port, and it is the sweep's: the read behind
 * the row and the flag standing over it. That is what the stand turns on and off.
 *
 * The device on the row's port is the one that will not take an address; the sensor plugged into
 * the free port takes its own, so every refusal the module counts here belongs to the row.
 */
void aRowOnAnAddressOfNobodys(ModuleUnderTest& module, bool theSweepIsReadingItThrough)
{
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    // Nothing is on the ports while the restart's pass runs: what this stand measures happens
    // after it, and the ping that pass leaves says nothing about a row that was not there.
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    // 0x11 is outside the range of the module, so the full resolution of the registry owes this
    // row a write; and it is not what the catalog says the model leaves the factory on, so the
    // registry rule of a cheap pass passes it by.
    SensorEntity row = makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, 0x11, SENSOR_STATUS_ADDED);
    row.sensorTypeUid = "type-1";
    row.isError = !theSweepIsReadingItThrough;
    module.sensorRepository.sensors.push_back(row);
    module.sensorValueModel.lastReadTimes["AA:BB:CC:DD:EE:FF.01"] = module.nowMs() + 1;
    module.sensorReader.writeSensorAddressAnswerByUid["AA:BB:CC:DD:EE:FF.01"] = false;

    // And somebody plugs an ordinary sensor into the free port, at its model's factory address. The
    // next cheap pass adopts it - and a device that stays where it is leaves the address phase of
    // every pass that adopts it a move to make.
    module.sensorReader.readAnsweringAddresses.push_back(1);
    module.sensorReader.answeringAddresses.push_back(1);
    module.sensorReader.answersAtNewAddressAfterWrite = false;
}

/**
 * The sweep is reading the row through, so there is a device at the other end and the refusal it
 * gave is the answer: one attempt, a note, and no pass writes into it again. Without the sweep's
 * fact the pass would have nothing to tell a refusing device from an empty port, and this row is
 * where that question is asked - the pass adopted somebody else and the restart's ping is spent.
 */
void test_startup_a_row_the_sweep_is_reading_is_noted_by_a_write_no_pass_chose(void)
{
    ModuleUnderTest module(true);
    aRowOnAnAddressOfNobodys(module, true);

    module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.sensorRepository.sensors.size(),
                              "the cheap pass adopted nothing, so nothing made it resolve the "
                              "whole registry - the scenario is not the one being measured");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)countWritesOnPin(module.sensorReader, 1),
                              "the row was not written to once - the scenario is not the one "
                              "being measured");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.factoryAddressMoveRefusedCount(),
                              "the refusal of a device the sweep is reading was not counted");

    // The person deletes the adopted sensor in the app and the module finds it again: another
    // pass that adopted, another full resolution of the registry, and the row is in it again.
    module.sensorRepository.remove("AA:BB:CC:DD:EE:FF.00");
    module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(3, (int)module.sensorReader.writeSensorAddressCallCount,
                              "the second pass did not adopt and move the sensor on the free port "
                              "- the scenario is not the one being measured");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)countWritesOnPin(module.sensorReader, 1),
                              "the module wrote an address into the device a second time - the "
                              "note the first refusal left holds off nothing");
}

/**
 * And the same write into the same row while the sweep has lost it: a row in error is a row
 * nothing is answering for, and a write nobody answered is not a refusal. The row keeps its place
 * in the plan of the next pass that resolves the registry, which is what makes it recoverable -
 * a note here would freeze the row until somebody restarted the module or asked for its port.
 */
void test_startup_a_row_the_sweep_has_lost_is_not_noted_by_a_write_no_pass_chose(void)
{
    ModuleUnderTest module(true);
    aRowOnAnAddressOfNobodys(module, false);

    module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.sensorRepository.sensors.size(),
                              "the cheap pass adopted nothing, so nothing made it resolve the "
                              "whole registry - the scenario is not the one being measured");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)countWritesOnPin(module.sensorReader, 1),
                              "the row was not written to once - the scenario is not the one "
                              "being measured");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.factoryAddressMoveRefusedCount(),
                              "a write nothing answered was counted as a device refusing the "
                              "address the module wrote it");

    // The same second pass, and this time the row is still in its plan.
    module.sensorRepository.remove("AA:BB:CC:DD:EE:FF.00");
    module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(2, (int)countWritesOnPin(module.sensorReader, 1),
                              "the row was never written to again - a write nothing answered is "
                              "holding every pass off it");
}

// --- the boot's own minute ----------------------------------------------------------------------

/**
 * The boot arms the discovery and the ladder waits before it uses it.
 *
 * A restart's expensive moments are its own: the TLS handshake, the first synchronizations, the
 * first structure. The discovery's first pass is expensive too - it plans over the registry, takes
 * a handle on the catalog and starts putting steps on the line - and without a delay the two would land on the
 * same seconds. Nothing is lost by moving one of them: the sensors the module already knows are
 * read from the registry and reported from the first second, and what a pass is for is finding
 * what is *not* in the registry. The one thing it costs is a sensor plugged in during the boot
 * minute, which is found a minute later than it would otherwise be.
 */
void test_startup_the_first_pass_waits_out_the_boot_minute(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);

    module.boot();
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.armCount(),
                              "the boot did not arm the discovery - there is no pass to have been "
                              "held");

    // The loop, from its first iteration to a second short of the window's end.
    module.pumpUntil(BOOT_SETTLE_MS - 1000);

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.takenStepCount(),
                              "the discovery put a step on the line inside the boot's own minute");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.scanSensorCallCount,
                              "the discovery pinged a sensor inside the boot's own minute");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.sensorReader.portPowerCalls.size(),
                              "the discovery moved a port's power inside the boot's own minute");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.rawBus.requests.size(),
                              "the discovery broadcast a presence poll inside the boot's own minute");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.isRunning(),
                             "the pass the boot armed was dropped rather than held back");

    // And past it, the pass is the pass it always was.
    module.settle();

    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.takenStepCount() > 0,
                             "the discovery never ran after the boot's minute was over");
    TEST_ASSERT_TRUE_MESSAGE(module.sensorReader.scanSensorCallCount > 0,
                             "the stored sensor was never pinged");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.prepareSensorsForReadingCallCount,
                              "the pass did not reach its preparation - it did not finish");
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isRunning(),
                              "the pass never ended");
}

/**
 * The one thing the window lets through: the pass a person asked for.
 *
 * A ScanSensors command is somebody standing in front of the module having pressed a button, and
 * the identification procedure that command runs has already had the line before this pass is
 * owed anything. Two things follow from holding it back with everything else, and both are worse
 * than the peak the window is there to avoid: the person watches a module that does nothing for a
 * minute, and on a module whose registry is empty the ladder is the only thing that starts the bus
 * task at all - so the command that arrives in that minute would be the one thing unable to move
 * it.
 *
 * The exception is what makes the ladder answer a person immediately; without it the module is
 * quiet in the boot minute whoever asks, and this is the test that tells the two apart.
 */
void test_startup_a_pass_a_person_asked_for_does_not_wait_out_the_boot_minute(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;

    module.boot();

    // Ten seconds in, with nobody having asked for anything: the ladder holds, which is what the
    // two tests around this one are about and what makes the second half of this one mean
    // something.
    module.pumpUntil(10000);
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.takenStepCount(),
                              "the discovery stepped inside the boot's own minute with nobody "
                              "having asked - this scene proves nothing about the exception");

    // And then the command lands, well inside the same minute.
    module.startupScan.rescan(1);
    module.pumpUntil(30000);

    TEST_ASSERT_TRUE_MESSAGE(module.nowMs() < BOOT_SETTLE_MS,
                             "the scene ran past the window it is about - what happened after it "
                             "would prove nothing");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.takenStepCount() > 0,
                             "a pass a person asked for was held back until the boot's own minute "
                             "was over");
    TEST_ASSERT_TRUE_MESSAGE(module.sensorReader.portPowerCalls.size() > 0,
                             "the steps the command released never reached the line");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.rescanRequestCount() > 0,
                             "the command was never recorded - the scene did not ask for a pass");
}

/** The same window, for the pass the clock asks for rather than the boot. */
void test_startup_the_periodic_pass_waits_out_the_boot_minute_too(void)
{
    ModuleUnderTest module(true);
    module.settings.setNoScanSensors(1);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);

    module.boot();
    module.pumpUntil(BOOT_SETTLE_MS - 1000);

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.takenStepCount(),
                              "a boot that skips the discovery still had the line inside its own "
                              "minute");

    module.pumpUntil(BOOT_SETTLE_MS + 5000);
    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.prepareSensorsForReadingCallCount,
                              "the registers the stored sensors are owed were never written");
}

/**
 * What the window is for: the boot's peak and the pass's peak are no longer the same moment.
 *
 * The heap is what this is about. A module coming up holds its TLS session and the buffers of its
 * first exchanges at once - tens of kilobytes on a heap with tens of kilobytes free - and the
 * pass, if it started in the same seconds, would hold its own plan on top of that: the registry's
 * uids, a handle on the whole sensor-type catalog, the entity of the step on the line. Neither is
 * large on its own and the sum is what the worst point of a boot would be made of.
 *
 * The boot's own peak is stood in for here by a block of the size the handshake takes, held for
 * as long as the boot holds it. What is measured is the module's real allocations against it: the
 * high-water mark while the block is held, and the high-water mark of the pass once it is freed.
 */
void test_startup_the_boot_minute_and_the_first_pass_do_not_peak_together(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(4);
    for (int pin = 0; pin < 2; pin++)
    {
        char uid[32];
        snprintf(uid, sizeof(uid), "AA:BB:CC:DD:EE:FF.0%d", pin);
        module.sensorRepository.sensors.push_back(
            makeStoredEntity(uid, pin, MODBUS_ADDRESS_MIN + pin, SENSOR_STATUS_ADDED));
    }
    for (int type = 0; type < 8; type++)
    {
        char uid[16];
        char name[16];
        snprintf(uid, sizeof(uid), "type-%d", type);
        snprintf(name, sizeof(name), "MODEL%d", type);
        module.sensorTypeRepository.addSensorType(makeModbusType(uid, name, 1 + type));
    }
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;

    module.boot();

    // The boot's own peak: what a TLS session and the first exchanges hold, for as long as they
    // hold it. The number is the one the module's own pool reserves for a handshake.
    const size_t HANDSHAKE_BYTES = 20 * 1024;
    const long long baseline = HostAllocationCounter::liveBytes();
    HostAllocationCounter::resetPeak();

    std::vector<char>* handshake = new std::vector<char>(HANDSHAKE_BYTES, 0);
    module.pumpUntil(30000);
    const long long peakWithTheBootHolding = HostAllocationCounter::peakBytes() - baseline;
    const int stepsWhileTheBootWasBusy = (int)module.startupScan.takenStepCount();
    delete handshake;
    handshake = nullptr;

    // The pass, on a heap the boot has given back.
    HostAllocationCounter::resetPeak();
    const long long afterTheBoot = HostAllocationCounter::liveBytes();
    module.settle();
    const long long peakOfThePass = HostAllocationCounter::peakBytes() - afterTheBoot;

    TEST_ASSERT_EQUAL_MESSAGE(0, stepsWhileTheBootWasBusy,
                              "the discovery was on the line while the boot held its own peak");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.takenStepCount() > 0,
                             "the pass never ran at all - there is no second peak to separate");
    TEST_ASSERT_TRUE_MESSAGE(peakOfThePass > 0,
                             "the pass held nothing on the heap - it is not a peak, and the "
                             "separation below would prove nothing");
    TEST_ASSERT_TRUE_MESSAGE(peakWithTheBootHolding < (long long)HANDSHAKE_BYTES + peakOfThePass,
                             "the boot's peak and the first pass's peak landed on one moment");
}

// --- what the periodic pass says to the log -----------------------------------------------------

/** @brief Lines the discovery wrote, which are the ones a field log is full of. */
size_t countScanLines(const MockLogger& logger)
{
    size_t count = 0;
    for (size_t i = 0; i < logger.messages.size(); i++)
    {
        if (logger.messages[i].find("STARTUP_SCAN:") != std::string::npos)
        {
            count++;
        }
    }
    return count;
}

/** @brief A module whose free ports are empty and stay empty - the steady state of every module. */
struct QuietModule
{
    ModuleUnderTest module;

    QuietModule() : module(true)
    {
        // The debug feature the discovery logs under is on, as it is on a module in the field.
        module.debugSystem.featureEnabled = true;
        module.moduleConfig.setSensorPinCount(2);
        module.sensorRepository.sensors.push_back(
            makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
        module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
        // A model of the catalog the ladder cannot try: every pass walks past it, and walking past
        // it is exactly the kind of thing a pass would otherwise say out loud.
        SensorType manual = makeModbusType("type-2", "MANUAL", 2);
        manual.canScan = false;
        module.sensorTypeRepository.addSensorType(manual);
        module.startupScan.setRawBusAccess(&module.rawBus);
        module.startupScan.setPortLines(twoPortsOnOneLine());
        module.rawBus.reader = &module.sensorReader;
        module.sensorReader.readSensorReturnValue = false;
        module.sensorReader.scanSensorReturnValue = false;
    }

    /** @brief Boot, and let the restart's own pass finish. */
    void bootAndSettle()
    {
        module.boot();
        module.runDiscovery();
        module.settle();
    }
};

/**
 * An hour of a module with nothing happening to it: the cheap pass comes round twice a minute,
 * finds what it found last time, and says nothing at all.
 *
 * The pass itself is unchanged - the same ports, the same models, the same steps on the line, and
 * the counters still say how many of each. What changed is that a pass which learned nothing has
 * nothing to say: a module in the field runs for months, and lines repeated twice a minute are
 * what buries the ones that mean something.
 */
void test_startup_a_quiet_hour_of_periodic_passes_says_nothing(void)
{
    QuietModule quiet;
    quiet.bootAndSettle();

    const size_t linesAfterTheBoot = countScanLines(quiet.module.logger);
    TEST_ASSERT_TRUE_MESSAGE(linesAfterTheBoot > 0,
                             "the restart's own pass said nothing - the log is not wired, and an "
                             "absence proved against it would prove nothing");

    const uint32_t passesAfterTheBoot = quiet.module.startupScan.freePortPassCount();
    quiet.module.runFor(3600000);

    const uint32_t passes = quiet.module.startupScan.freePortPassCount() - passesAfterTheBoot;
    TEST_ASSERT_TRUE_MESSAGE(passes >= 60,
                             "the periodic pass did not run for the hour - there is nothing to "
                             "have been quiet about");
    TEST_ASSERT_TRUE_MESSAGE(quiet.module.startupScan.cheapProbeCount() > 0,
                             "the periodic pass put nothing on the line - it was quiet because it "
                             "did nothing, which is not what is being asserted");

    TEST_ASSERT_EQUAL_MESSAGE((int)linesAfterTheBoot, (int)countScanLines(quiet.module.logger),
                              "the passes that found nothing wrote to the log");
}

/**
 * And the other half of it: quiet is not silence. A sensor plugged into a free port of a settled
 * module is found by the same pass, and that pass says so.
 */
void test_startup_a_sensor_plugged_into_a_quiet_module_is_said(void)
{
    QuietModule quiet;
    quiet.bootAndSettle();
    quiet.module.runFor(600000);

    const size_t linesBefore = countScanLines(quiet.module.logger);

    // A device of a model the catalog knows, at the address that model leaves the factory with.
    quiet.module.sensorReader.readSensorReturnValue = true;
    quiet.module.sensorReader.answeringAddresses.push_back(1);

    quiet.module.runFor(120000);

    TEST_ASSERT_TRUE_MESSAGE((bool)quiet.module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01"),
                             "the sensor was never adopted - there is no event to have been said");
    TEST_ASSERT_TRUE_MESSAGE(countScanLines(quiet.module.logger) > linesBefore,
                             "a sensor appearing on a free port went unsaid");
    TEST_ASSERT_TRUE_MESSAGE(quiet.module.logger.hasMessageContaining("detected TEMP sensor on pin 1"),
                             "the log does not say which sensor was found on which port");
}

/**
 * The whole of what a module in the field says when somebody plugs a sensor into it: ten minutes
 * of silence, the event, and silence again.
 *
 * This is the tape a person on site reads, and it is asserted whole rather than by keyword: the
 * claim is not only that the event is in the log but that it is the only thing in it, in an order
 * that can be followed - the port answered, the model was identified, the row was given its
 * address, the sensor was adopted and saved, the pass ended saying what it did. A line added to
 * the pass, or one of these lost, fails here.
 */
void test_startup_the_tape_a_plugged_in_sensor_leaves(void)
{
    QuietModule quiet;
    quiet.bootAndSettle();

    // Ten minutes of a settled module, which is what the tape has to begin from.
    quiet.module.runFor(600000);
    const size_t linesAfterTheBoot = countScanLines(quiet.module.logger);
    quiet.module.runFor(600000);
    TEST_ASSERT_EQUAL_MESSAGE((int)linesAfterTheBoot, (int)countScanLines(quiet.module.logger),
                              "the module was not silent before the sensor was plugged in");

    const size_t linesBefore = quiet.module.logger.messages.size();

    // A device of a model the catalog knows, at the address that model leaves the factory with.
    quiet.module.sensorReader.readAnsweringAddresses.push_back(1);
    quiet.module.sensorReader.answeringAddresses.push_back(1);

    quiet.module.runFor(120000);

    std::vector<std::string> tape;
    for (size_t i = linesBefore; i < quiet.module.logger.messages.size(); i++)
    {
        tape.push_back(quiet.module.logger.messages[i]);
    }

    // The last line accounts for the pass that is ending and for nothing else: seven steps on the
    // line - the ports switched on, the model tried on the free port at the three addresses of the
    // range the stored sensor of its wire does not hold and at its own, the address written, the
    // registry prepared - and the one sensor this pass adopted. A count of everything the module
    // has done since it started would say a different number every time the scene grew a minute.
    const char* const expected[] = {
        "STARTUP_SCAN: detected TEMP sensor on pin 1",
        "STARTUP_SCAN: successfully saved detected sensor AA:BB:CC:DD:EE:FF.01 to repository with "
        "NEW status",
        "STARTUP_SCAN: sensor AA:BB:CC:DD:EE:FF.01 has address 0x01 outside valid range 0x70-0x73, "
        "needs new address",
        "STARTUP_SCAN: assigning new addresses to 1 sensor(s)",
        "STARTUP_SCAN: attempting to set new address 0x71 for sensor AA:BB:CC:DD:EE:FF.01 "
        "(current: 0x01)",
        "STARTUP_SCAN: successfully changed sensor AA:BB:CC:DD:EE:FF.01 address from 0x01 to 0x71",
        "STARTUP_SCAN: finished - 7 step(s), 1 sensor(s) adopted"
    };
    const size_t expectedCount = sizeof(expected) / sizeof(expected[0]);

    for (size_t i = 0; i < tape.size() && i < expectedCount; i++)
    {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected[i], tape[i].c_str(),
                                         "the tape a plugged-in sensor leaves has changed");
    }
    TEST_ASSERT_EQUAL_MESSAGE((int)expectedCount, (int)tape.size(),
                              "the tape a plugged-in sensor leaves is longer or shorter than the "
                              "sequence a person on site is given to read");

    // And then the module goes quiet again: the pass that found it keeps finding it, and says so
    // no more.
    const size_t linesAfterTheEvent = quiet.module.logger.messages.size();
    quiet.module.runFor(600000);
    TEST_ASSERT_EQUAL_MESSAGE((int)linesAfterTheEvent, (int)quiet.module.logger.messages.size(),
                              "the module did not go quiet again after the sensor was adopted");
}

/**
 * The other side of the quiet: the passes somebody is waiting for still say what they are doing.
 *
 * A restart's pass and the pass a ScanSensors command asks for are both finite, and both are read
 * by a person who is watching that boot or standing at that module - the ports being walked, the
 * models being skipped, the power going up and coming back is what tells them the module is
 * working rather than stuck. Only the pass that comes round twice a minute for the life of the
 * module is quiet, and it is quiet because it repeats.
 */
void test_startup_the_passes_a_person_waits_for_still_say_what_they_are_doing(void)
{
    QuietModule quiet;
    quiet.bootAndSettle();

    TEST_ASSERT_TRUE_MESSAGE(quiet.module.logger.hasMessageContaining("full pass over"),
                             "the pass a restart arms did not say which ports it was walking");
    TEST_ASSERT_TRUE_MESSAGE(quiet.module.logger.hasMessageContaining("skipping sensor type MANUAL"),
                             "the pass a restart arms did not say which models it walked past");

    // Ten quiet minutes, and then somebody asks about a port from the app.
    quiet.module.runFor(600000);
    quiet.module.startupScan.rescan(1);
    quiet.module.runFor(120000);

    TEST_ASSERT_TRUE_MESSAGE(quiet.module.logger.hasMessageContaining("hand-asked pass over"),
                             "the pass a person asked for did not say which ports it was walking");
}

/** @brief The last line a pass ended with, or an empty string when no pass has ended. */
std::string lastFinishedLine(const MockLogger& logger)
{
    std::string found;
    for (size_t i = 0; i < logger.messages.size(); i++)
    {
        if (logger.messages[i].find("STARTUP_SCAN: finished - ") == 0)
        {
            found = logger.messages[i];
        }
    }
    return found;
}

/**
 * The line a pass ends with accounts for that pass, not for everything the module has done since
 * it was switched on.
 *
 * The distinction is invisible on a module that has just started and is the whole of the line on
 * one that has not: a sentence reading "finished - 51 234 step(s), 9 sensor(s) adopted" about a
 * pass that took four steps and adopted one is not a summary, it is two numbers from somewhere
 * else. The lifetime totals are still counted, and these tests are where they can be read: no line
 * the module writes in the field carries them.
 *
 * The scene puts the two apart: the same sensor is adopted twice by the same module - the
 * restart's pass finds it, somebody deletes it from the app, another of the same model goes into
 * the same port - so by the second pass every lifetime total is larger than what that pass did.
 */
void test_startup_the_line_a_pass_ends_with_counts_that_pass_only(void)
{
    QuietModule quiet;
    // A device of a model the catalog knows, on the free port, from the moment the module starts.
    quiet.module.sensorReader.readSensorReturnValue = true;
    quiet.module.sensorReader.answeringAddresses.push_back(1);
    quiet.bootAndSettle();

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)quiet.module.startupScan.adoptedCount(),
                              "the restart's pass did not adopt the device on the free port");

    // Deleted from the app, and another one of the same model plugged into the same port.
    TEST_ASSERT_TRUE_MESSAGE(quiet.module.sensorRepository.remove("AA:BB:CC:DD:EE:FF.01"),
                             "the row the pass adopted could not be deleted");
    quiet.module.sensorReader.answeringAddresses.clear();
    quiet.module.sensorReader.answeringAddresses.push_back(1);

    quiet.module.runFor(120000);

    TEST_ASSERT_EQUAL_MESSAGE(2, (int)quiet.module.startupScan.adoptedCount(),
                              "the module did not adopt a second time - there would be nothing to "
                              "tell a pass's own count from the module's");
    TEST_ASSERT_TRUE_MESSAGE(quiet.module.startupScan.appliedStepCount() > 4,
                             "the module has applied four steps or fewer in its whole life - the "
                             "two numbers cannot be told apart in this scene");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "STARTUP_SCAN: finished - 7 step(s), 1 sensor(s) adopted",
        lastFinishedLine(quiet.module.logger).c_str(),
        "the line a pass ends with reports something other than that pass");
}

/**
 * The same line, said by the pass a ScanSensors command asks for - the one a person is standing at
 * the module to read.
 *
 * Every pass begins its own count, and a pass that began on a module with a history behind it is
 * where that matters: a restart's pass and ten minutes of the periodic one have run before this
 * command was sent, so a count left over from any of them would show in the line as a number
 * larger than the handful of steps this pass took.
 */
void test_startup_the_line_a_commanded_pass_ends_with_counts_that_pass_only(void)
{
    QuietModule quiet;
    quiet.bootAndSettle();

    // Ten minutes of the periodic pass - the history the command's own pass has to be told apart
    // from. The last minute of it is measured on its own: what a pass that does not begin its own
    // count would carry into the line is what the pass immediately before it left there, so that
    // pass has to have left something.
    quiet.module.runFor(540000);
    const uint32_t stepsBeforeTheLastQuietMinute = quiet.module.startupScan.appliedStepCount();
    quiet.module.runFor(60000);

    const uint32_t stepsBeforeTheCommand = quiet.module.startupScan.appliedStepCount();
    TEST_ASSERT_TRUE_MESSAGE(stepsBeforeTheCommand > stepsBeforeTheLastQuietMinute,
                             "the pass that ran immediately before the command put nothing on the "
                             "line - a count carried over from it would be a zero, and this scene "
                             "would pass against a pass that never begins its own");

    // A sensor goes into the free port and the person asks the module about that port from the app.
    quiet.module.sensorReader.readSensorReturnValue = true;
    quiet.module.sensorReader.answeringAddresses.push_back(1);
    quiet.module.startupScan.rescan(1);
    quiet.module.runFor(120000);

    TEST_ASSERT_TRUE_MESSAGE((bool)quiet.module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01"),
                             "the pass the command asked for adopted nothing - a pass that finds "
                             "nothing ends without saying this line at all");
    TEST_ASSERT_TRUE_MESSAGE(quiet.module.startupScan.appliedStepCount() > 10,
                             "the module has done too little in its life for its totals to be "
                             "told apart from one pass's own count");

    const uint32_t stepsSinceTheCommand =
        quiet.module.startupScan.appliedStepCount() - stepsBeforeTheCommand;
    TEST_ASSERT_EQUAL_MESSAGE(4, (int)stepsSinceTheCommand,
                              "the pass the command asked for put another number of steps on the "
                              "line than the line below accounts for");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "STARTUP_SCAN: finished - 4 step(s), 1 sensor(s) adopted",
        lastFinishedLine(quiet.module.logger).c_str(),
        "the line the pass a person asked for ends with reports something other than that pass");
}

// --- what the discovery says in the periodic counters line --------------------------------------
//
// The passes themselves are quiet on a settled module, and the line a pass ends with counts that
// pass alone, so what the module has done over its life is in the counters this class keeps - and
// what a person in the field can read of them is whatever this line carries. Three of them are in
// it, and each of the three scenes below moves exactly one: a column reading somebody else's
// counter shows up here as a number in the wrong place.

/** @brief The counters line the discovery's columns make, registered the way globals.h does it. */
std::string scanColumnsOf(StartupSensorScanUseCase& scan)
{
    CounterReport report;
    registerScanCounters(report, scan);

    char line[CounterReport::LINE_BUFFER_CHARS];
    report.format(line, sizeof(line));
    return std::string(line);
}

/** A sensor plugged into a settled module: the adoption is in the line, and nothing else is. */
void test_startup_the_counters_line_carries_the_sensors_this_module_adopted(void)
{
    QuietModule quiet;
    quiet.bootAndSettle();
    quiet.module.runFor(600000);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "DIAG-COUNTERS: scan_adopted=0 scan_addr_refused=0 scan_identify_steps=0",
        scanColumnsOf(quiet.module.startupScan).c_str(),
        "a module that has found nothing does not start from zero");

    // A device of a model the catalog knows, at the address that model leaves the factory with.
    quiet.module.sensorReader.readSensorReturnValue = true;
    quiet.module.sensorReader.answeringAddresses.push_back(1);
    quiet.module.runFor(120000);

    TEST_ASSERT_TRUE_MESSAGE((bool)quiet.module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01"),
                             "the sensor was never adopted - there is nothing for the column to "
                             "carry");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "DIAG-COUNTERS: scan_adopted=1 scan_addr_refused=0 scan_identify_steps=0",
        scanColumnsOf(quiet.module.startupScan).c_str(),
        "the adoption did not reach the counters line, or reached it in another column");
}

/**
 * A device that answers the module and will not take the address it is written. The module writes
 * to it once and then leaves it where it is for good, so nothing further happens and nothing
 * further is said: this column is the only place in the field that state is visible from.
 */
void test_startup_the_counters_line_carries_a_device_that_would_not_move(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorRepository.sensors.push_back(
        makeRowOnItsFactoryAddress("AA:BB:CC:DD:EE:FF.01", 1, "type-1", 1));
    // Its own address is the only one anything answers at: the free port of this stand has nothing
    // on it, so the row that refuses the write is the whole scene.
    module.sensorReader.answeringAddresses.push_back(1);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.writeSensorAddressReturnValue = false;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.writeSensorAddressCallCount,
                              "nothing was written to the device - the scene is not the one being "
                              "measured");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "DIAG-COUNTERS: scan_adopted=0 scan_addr_refused=1 scan_identify_steps=0",
        scanColumnsOf(module.startupScan).c_str(),
        "the refused address write did not reach the counters line, or reached it in another "
        "column");
}

/**
 * A port with something on it that matches no model of the catalog. The identification procedure -
 * the rung that costs every port sharing the wire its power - runs once and is then rationed, and
 * this column is what says whether the rationing is holding.
 */
void test_startup_the_counters_line_carries_the_rationed_identification_rung(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.rawBus.answeringChannels.push_back(0);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "DIAG-COUNTERS: scan_adopted=0 scan_addr_refused=0 scan_identify_steps=1",
        scanColumnsOf(module.startupScan).c_str(),
        "the identification the port cost the module did not reach the counters line, or reached "
        "it in another column");

    // An hour of passes on the same port: the rung is rationed, and the column says so by standing
    // still. A column that climbed here would be reporting something else.
    module.runFor(3600000);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "DIAG-COUNTERS: scan_adopted=0 scan_addr_refused=0 scan_identify_steps=1",
        scanColumnsOf(module.startupScan).c_str(),
        "the identification rung ran again on a port already given up on, or the column counts "
        "something that is not it");
}

/**
 * @brief Ten quiet minutes, a sensor plugged into the free port, and the periodic pass adopting it.
 *
 * Stops the moment that pass has ended, and not a tick later: what a pass adopted is put back to
 * zero by the next pass that begins, so a module left running until the following periodic pass
 * cannot tell a count of its own from one carried over.
 */
void adoptASensorWithThePeriodicPass(QuietModule& quiet)
{
    quiet.bootAndSettle();
    quiet.module.runFor(600000);

    // A device of a model the catalog knows, at the address that model leaves the factory with.
    quiet.module.sensorReader.readAnsweringAddresses.push_back(1);
    quiet.module.sensorReader.answeringAddresses.push_back(1);

    const unsigned long tickMs = 5000;
    for (int tick = 0; tick < 24; tick++)
    {
        quiet.module.runFor(tickMs, tickMs);
        if (lastFinishedLine(quiet.module.logger).find("1 sensor(s) adopted") != std::string::npos)
        {
            break;
        }
    }

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "STARTUP_SCAN: finished - 7 step(s), 1 sensor(s) adopted",
        lastFinishedLine(quiet.module.logger).c_str(),
        "the periodic pass did not adopt the sensor - there is no adoption for a later pass to "
        "claim");
    TEST_ASSERT_TRUE_MESSAGE(quiet.module.startupScan.freePortPassCount() > 0,
                             "no periodic pass ever ran");
}

/**
 * A pass a person asked for says what it adopted itself.
 *
 * The scene puts a pass with an adoption immediately before a pass without one: the periodic pass
 * takes a sensor in, and the command arrives on that same port before the next one comes round. A
 * pass that began with the count the one before it left would tell the person standing at the
 * module that it had just found their sensor, on a port where it found nothing.
 */
void test_startup_a_commanded_pass_claims_no_adoption_of_an_earlier_one(void)
{
    QuietModule quiet;
    adoptASensorWithThePeriodicPass(quiet);
    const std::string lineOfTheAdoption = lastFinishedLine(quiet.module.logger);

    // The person asks the module about that same port from the app, before the next periodic pass
    // is due. The sensor is in the registry now, so this pass finds nothing.
    quiet.module.startupScan.rescan(1);
    quiet.module.runFor(120000);

    TEST_ASSERT_TRUE_MESSAGE(quiet.module.logger.hasMessageContaining("hand-asked pass over"),
                             "the pass the command asked for never ran");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        lineOfTheAdoption.c_str(), lastFinishedLine(quiet.module.logger).c_str(),
        "a pass that adopted nothing reported the adoption of the pass before it");
}

/**
 * And the heavier half of the same count: whether the module describes itself to the server again.
 *
 * A pass that is not the restart's own sends the structure when it adopted something, because a
 * sensor appearing is what the server has not got - and stays quiet otherwise, which is what keeps
 * a module that is not changing from republishing itself. An adoption left over from the pass
 * before would put a structure on the wire that nothing in it has changed.
 */
void test_startup_a_commanded_pass_that_found_nothing_sends_no_structure(void)
{
    QuietModule quiet;
    adoptASensorWithThePeriodicPass(quiet);
    const int republishesAfterTheAdoption = quiet.module.republish.calls;
    TEST_ASSERT_TRUE_MESSAGE(republishesAfterTheAdoption > 0,
                             "the pass that adopted the sensor sent no structure - the scene does "
                             "not distinguish a structure this pass sent from one it did not");

    quiet.module.startupScan.rescan(1);
    quiet.module.runFor(120000);

    TEST_ASSERT_TRUE_MESSAGE(quiet.module.logger.hasMessageContaining("hand-asked pass over"),
                             "the pass the command asked for never ran");
    TEST_ASSERT_EQUAL_MESSAGE(republishesAfterTheAdoption, quiet.module.republish.calls,
                              "a pass that adopted nothing sent the module structure to the server "
                              "again");
}

// --- the restart's window on a port the cheap rung could not name ------------------------------

/**
 * A device the module once gave an address answers at that address and at no other - not at its
 * model's factory address, not to a wildcard. The restart's cheap rung reads every address of the
 * module's range, and a read that answers there, confirmed by the model's scan, is an adoption at
 * that address: no window, no write, and no identification procedure.
 */
void test_startup_the_restarts_cheap_rung_finds_a_device_in_the_range_without_a_window(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.sensorReader.writeSensorAddressByBroadcastReturnValue = true;
    module.boot();
    module.runDiscovery();

    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00");
    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "a device answering at an address of the module's range was not found");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 2, adopted.value().modbusDeviceAddress,
                              "the device was not adopted at the address it answered at");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressByBroadcastCallCount,
                              "an address was written into a port whose device a read had already "
                              "named");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.identifyStepCount(),
                              "the identification procedure ran on a port a read had named");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.presencePollCount(),
                              "the port was given a window although the cheap rung had named it");
}

/**
 * The window still reads the range with the paired port dark, and that is where an address held by
 * the stored sensor of the paired port is asked: the cheap rung leaves it alone on the live wire,
 * where that sensor would answer for itself and the confirming scan would take the power of every
 * port to prove it.
 */
void test_startup_an_address_the_paired_port_holds_is_left_to_the_window(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(2);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    // The sensor the module reads on port 1 is at 0x72, and so is the device plugged into port 0.
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.boot();

    std::vector<int> askedOnTheLiveWire;
    bool foundByTheWindow = false;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 0)
        {
            askedOnTheLiveWire.push_back(step.entity.modbusDeviceAddress);
        }
        if (step.kind == StartupScanStep::PROBE_RANGE && step.pin == 0 && outcome.answeredModel >= 0)
        {
            foundByTheWindow = true;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    const int held = MODBUS_ADDRESS_MIN + 2;
    TEST_ASSERT_TRUE_MESSAGE(std::find(askedOnTheLiveWire.begin(), askedOnTheLiveWire.end(), held) ==
                                 askedOnTheLiveWire.end(),
                             "port 0 was asked, on the live wire, the address the sensor of the port "
                             "sharing it holds");
    TEST_ASSERT_TRUE_MESSAGE(std::find(askedOnTheLiveWire.begin(), askedOnTheLiveWire.end(),
                                       MODBUS_ADDRESS_MIN + 3) != askedOnTheLiveWire.end(),
                             "port 0 was not asked the rest of the range cheaply");
    TEST_ASSERT_TRUE_MESSAGE(foundByTheWindow,
                             "the device at the held address was not found by port 0's window");
    TEST_ASSERT_TRUE_MESSAGE((bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00"),
                             "the device on port 0 was not adopted");
}

/**
 * The same wire, but the sensor stored on port 1 does not answer its ping: nothing then says where the
 * device behind that row is, and port 0 is asked 0x72 on the live wire like any address of the range.
 * The device at 0x72 on port 0 is found by the cheap rung, and port 0 gets no window.
 */
void test_startup_an_address_held_by_a_row_silent_to_its_ping_is_asked_on_the_live_wire(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(2);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.boot();

    std::vector<int> askedOnTheLiveWire;
    int pings = 0;
    int windowsOfPort0 = 0;
    bool foundByTheCheapRung = false;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::VERIFY_KNOWN)
        {
            // The line said nothing to the ping of port 1's row.
            outcome = StartupScanOutcome();
            pings++;
        }
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 0)
        {
            askedOnTheLiveWire.push_back(step.entity.modbusDeviceAddress);
            foundByTheCheapRung = foundByTheCheapRung || (outcome.answered && outcome.answeredAddress == MODBUS_ADDRESS_MIN + 2);
        }
        if (step.kind == StartupScanStep::PROBE_PRESENCE && step.pin == 0)
        {
            windowsOfPort0++;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, pings, "port 1's row was not pinged once");
    TEST_ASSERT_TRUE_MESSAGE(std::find(askedOnTheLiveWire.begin(), askedOnTheLiveWire.end(),
                                       MODBUS_ADDRESS_MIN + 2) != askedOnTheLiveWire.end(),
                             "port 0 was not asked, on the live wire, the address of a row silent to its ping");
    TEST_ASSERT_TRUE_MESSAGE(foundByTheCheapRung, "the device at 0x72 on port 0 was not found by the cheap rung");
    TEST_ASSERT_TRUE_MESSAGE((bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00"),
                             "the device on port 0 was not adopted");
    TEST_ASSERT_EQUAL_MESSAGE(0, windowsOfPort0, "port 0 got a window although the cheap rung found its device");
}

/**
 * The row stored on port 1 holds 0x71, its own port's address, and does not answer its ping; the device
 * at 0x71 is on port 0. The cheap rung adopts it there, and the very next step is the write that moves
 * it onto port 0's own address - before port 2 is asked anything, not at the end of the pass.
 */
void test_startup_a_device_adopted_at_its_wires_row_address_is_moved_by_the_next_step(void)
{
    ModuleUnderTest module;
    // Ports 0 and 1 share a wire; port 2, on the other one, has nothing on it and is walked after 0.
    module.moduleConfig.setSensorPinCount(3);
    std::vector<int> lines = twoPortsOnOneLine();
    lines.push_back(1);
    module.startupScan.setPortLines(lines);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.boot();

    std::vector<StartupScanStep::Kind> afterTheFind;
    std::vector<StartupScanStep> writes;
    bool found = false;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::VERIFY_KNOWN || step.pin == 2)
        {
            // The line said nothing to the ping of port 1's row, and nothing on port 2.
            outcome = StartupScanOutcome();
        }
        if (found)
        {
            afterTheFind.push_back(step.kind);
        }
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            writes.push_back(step);
        }
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 0 && outcome.answered)
        {
            found = true;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(found, "the device at 0x71 on port 0 was not found by the cheap rung");
    TEST_ASSERT_TRUE_MESSAGE(!afterTheFind.empty() && afterTheFind[0] == StartupScanStep::WRITE_ADDRESS,
                             "the step after the find was not the write that parts the two rows");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)writes.size(), "not exactly one address write in the pass");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("AA:BB:CC:DD:EE:FF.00", writes[0].entity.uid.c_str(),
                                     "the write was not aimed at the row the cheap rung adopted");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN, writes[0].targetAddress,
                              "the adopted row was not moved onto its own port's address");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1,
                              module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01").value().modbusDeviceAddress,
                              "port 1's row did not keep the address that is its port's own");
}

/**
 * The other way round: the row stored on port 1, silent to its ping, holds 0x70 - port 0's own address
 * - and the device at 0x70 is on port 0. The cheap rung adopts it there, and it is not moved: the
 * address is its own port's, and the address phase leaves it with the row whose port it belongs to.
 */
void test_startup_a_device_adopted_at_its_own_ports_address_is_not_moved_off_it(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(2);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN);
    module.boot();

    bool found = false;
    std::vector<StartupScanStep> writes;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::VERIFY_KNOWN)
        {
            // The line said nothing to the ping of port 1's row.
            outcome = StartupScanOutcome();
        }
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            writes.push_back(step);
        }
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 0 && outcome.answered)
        {
            found = true;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(found, "the device at 0x70 on port 0 was not found by the cheap rung");
    for (size_t i = 0; i < writes.size(); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(writes[i].entity.uid != "AA:BB:CC:DD:EE:FF.00",
                                 "the row adopted at its own port's address was written another one");
    }
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN,
                              module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00").value().modbusDeviceAddress,
                              "the row adopted at its own port's address does not keep it");
}

/**
 * The row stored on port 1 holds 0x71, its own port's address, and answers its ping, so the cheap rung
 * leaves 0x71 to port 0's window. A second device at 0x71 on port 0 answers the window's read of the
 * range and is adopted there, and the very next step is the write that moves it onto port 0's own
 * address - inside the window, with port 1 still dark, before the step that closes it.
 */
void test_startup_a_device_the_window_finds_at_its_wires_row_address_is_moved_inside_the_window(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(2);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.boot();

    bool found = false;
    bool pinged = false;
    std::vector<StartupScanStep::Kind> afterTheFind;
    std::vector<StartupScanStep> writes;
    bool windowOpenAtTheWrite = false;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            writes.push_back(step);
            windowOpenAtTheWrite = module.startupScan.holdsAWindowOpen();
        }
        const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::VERIFY_KNOWN)
        {
            pinged = outcome.answered;
        }
        if (found)
        {
            afterTheFind.push_back(step.kind);
        }
        if (step.kind == StartupScanStep::PROBE_RANGE && step.pin == 0 && outcome.answeredModel >= 0)
        {
            found = true;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(pinged, "port 1's row did not answer its ping - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(found, "the device at 0x71 on port 0 was not found by its window");
    TEST_ASSERT_TRUE_MESSAGE(afterTheFind.size() >= 2 && afterTheFind[0] == StartupScanStep::WRITE_ADDRESS &&
                                 afterTheFind[1] == StartupScanStep::RELEASE_LINE,
                             "the step after the window's find was not the write that parts the two rows, "
                             "followed by the step that closes the window");
    TEST_ASSERT_TRUE_MESSAGE(windowOpenAtTheWrite, "the write that parts the two rows went out with the window closed");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)writes.size(), "not exactly one address write in the pass");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("AA:BB:CC:DD:EE:FF.00", writes[0].entity.uid.c_str(),
                                     "the write was not aimed at the row the window adopted");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN, writes[0].targetAddress,
                              "the adopted row was not moved onto its own port's address");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN, module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00").value().modbusDeviceAddress,
                              "the adopted row does not hold its own port's address after the write");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1,
                              module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01").value().modbusDeviceAddress,
                              "port 1's row did not keep the address that is its port's own");
}

/**
 * The same wire, and a device on port 0 that the window hears and cannot name: the poll is answered,
 * no read of the range is. The identification after the window names it at 0x71 - the address port
 * 1's row holds as its own port's - and the next step is the write that moves it onto port 0's own,
 * before port 2 is asked anything, not at the end of the pass.
 */
void test_startup_a_device_identified_at_its_wires_row_address_is_moved_by_the_next_step(void)
{
    ModuleUnderTest module;
    // Ports 0 and 1 share a wire; port 2, on the other one, has nothing on it and is walked after 0.
    module.moduleConfig.setSensorPinCount(3);
    std::vector<int> lines = twoPortsOnOneLine();
    lines.push_back(1);
    module.startupScan.setPortLines(lines);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.rawBus.answeringChannels.push_back(0);
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.boot();

    bool found = false;
    bool pinged = false;
    std::vector<StartupScanStep::Kind> afterTheFind;
    std::vector<StartupScanStep> writes;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::VERIFY_KNOWN)
        {
            pinged = outcome.answered;
        }
        if (found)
        {
            afterTheFind.push_back(step.kind);
        }
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            writes.push_back(step);
        }
        if (step.kind == StartupScanStep::SCAN_CANDIDATE && step.pin == 0 && outcome.answered &&
            outcome.answeredAddress == MODBUS_ADDRESS_MIN + 1)
        {
            found = true;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(pinged, "port 1's row did not answer its ping - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(found, "the device on port 0 was not identified at 0x71");
    TEST_ASSERT_TRUE_MESSAGE(!afterTheFind.empty() && afterTheFind[0] == StartupScanStep::WRITE_ADDRESS,
                             "the step after the identification's find was not the write that parts the "
                             "two rows");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)writes.size(), "not exactly one address write in the pass");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("AA:BB:CC:DD:EE:FF.00", writes[0].entity.uid.c_str(),
                                     "the write was not aimed at the row the identification adopted");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN, writes[0].targetAddress,
                              "the adopted row was not moved onto its own port's address");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1,
                              module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01").value().modbusDeviceAddress,
                              "port 1's row did not keep the address that is its port's own");
}

/**
 * A window find at an address port 1's row holds and that is not its own port's - 0x72: the address is
 * neither row's own, so nothing about the two says which of them keeps it, and the find is left to the
 * address phase at the end of the pass. The window closes with no write in it.
 */
void test_startup_a_window_find_at_an_address_its_wires_row_does_not_own_is_left_to_the_address_phase(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(2);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.boot();

    bool found = false;
    bool closed = false;
    std::vector<StartupScanStep::Kind> afterTheFind;
    std::vector<bool> writeAfterTheClose;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (found)
        {
            afterTheFind.push_back(step.kind);
        }
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            writeAfterTheClose.push_back(closed);
        }
        if (step.kind == StartupScanStep::PROBE_RANGE && step.pin == 0 && outcome.answeredModel >= 0)
        {
            found = true;
        }
        if (step.kind == StartupScanStep::RELEASE_LINE && step.pin == 0)
        {
            closed = true;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(found, "the device at 0x72 on port 0 was not found by its window");
    TEST_ASSERT_TRUE_MESSAGE(!afterTheFind.empty() && afterTheFind[0] == StartupScanStep::RELEASE_LINE,
                             "the window did not close straight after its find");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)writeAfterTheClose.size(),
                              "the address phase did not make the one write that parts the two rows");
    TEST_ASSERT_TRUE_MESSAGE(writeAfterTheClose[0], "the two rows were parted inside the window");
}

/**
 * A port nothing answered - not the poll, not a read of the range - is written the port's address,
 * model by model, and what takes it is adopted at that address.
 */
void test_startup_the_restarts_window_writes_the_address_into_a_silent_port(void)
{
    if (!StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS)
    {
        TEST_IGNORE_MESSAGE("this build's window writes no address (THE_WINDOW_WRITES_THE_PORT_ADDRESS)");
    }
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-2", "HUMID", 2));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.readSensorReturnValue = false;
    // The device takes the port's address from the first model's write, and the scan of that model
    // at that address passes; nothing answers the scan anywhere else.
    module.sensorReader.answeringAddresses.push_back(0);
    module.sensorReader.writeSensorAddressByBroadcastReturnValue = true;
    module.boot();
    module.runDiscovery();

    Utils::Optional<SensorEntity> adopted = module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00");
    TEST_ASSERT_TRUE_MESSAGE((bool)adopted,
                             "a device that took the port's address was not adopted");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN, adopted.value().modbusDeviceAddress,
                              "the device was not adopted at the port's address");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("type-1", adopted.value().sensorTypeUid.c_str(),
                                     "the device was not adopted as the model whose write it took");
    TEST_ASSERT_EQUAL_MESSAGE(1, module.sensorReader.writeSensorAddressByBroadcastCallCount,
                              "the window went on writing after a model's write was taken");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.lastBroadcastWriteSensor.pin,
                              "the address was written into another port");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("type-1",
                                     module.sensorReader.lastBroadcastWriteSensor.sensorTypeUid.c_str(),
                                     "the first write was not the first model's of the catalog");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.startupScan.identifyStepCount(),
                              "the identification procedure ran on a port the window had named");
}

/**
 * A port that answered the poll goes to the identification rung, and that procedure writes the
 * address itself - the model's write, the wildcard-directed one and the blind sequence. The window
 * does not make the same writes first.
 */
void test_startup_a_port_that_answered_the_poll_is_left_to_the_identification_rung(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.rawBus.answeringChannels.push_back(0);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.sensorReader.writeSensorAddressByBroadcastReturnValue = true;
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressByBroadcastCallCount,
                              "the window wrote the port's address into a port the identification "
                              "rung was going to write it into anyway");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.identifyStepCount(),
                              "a port that answered the poll did not get its identification pass");
}

/**
 * The window belongs to the restart. A pass after it - the periodic one, or the one a ScanSensors
 * command leaves behind - opens none: every read it makes is a cheap probe - on the periodic pass
 * the range as well as the models' own addresses - and it polls only the port a person named. No
 * read of the range inside a window and no address write.
 */
void test_startup_no_pass_after_the_restart_opens_a_window(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();

    // The restart's own pass: per free port the cheap rung - the four addresses of the range and
    // the model's own - and the window's four reads of the range, and one address write per model.
    TEST_ASSERT_EQUAL_MESSAGE(2 * (4 + 1 + 4), module.sensorReader.readSensorCallCount,
                              "the restart's pass did not ask each free port the module's range and "
                              "its model's own address cheaply, and the range again in its window");
    TEST_ASSERT_EQUAL_MESSAGE(StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS ? 2 : 0,
                              module.sensorReader.writeSensorAddressByBroadcastCallCount,
                              "the restart's pass did not write each free port's address");

    module.sensorReader.reset();
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    const uint32_t probesBefore = module.startupScan.cheapProbeCount();
    const uint32_t pollsBefore = module.startupScan.presencePollCount();

    module.runFor(600000);
    module.startupScan.rescan(1);
    module.runFor(120000);

    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() >= 5,
                             "the passes after the restart stopped - there is nothing to measure");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)(module.startupScan.presencePollCount() - pollsBefore),
                              "the hand-asked pass did not poll the port it names, or a periodic "
                              "pass polled one");
    TEST_ASSERT_EQUAL_MESSAGE((int)(module.startupScan.cheapProbeCount() - probesBefore),
                              module.sensorReader.readSensorCallCount,
                              "a pass after the restart read more than its cheap probes - it opened "
                              "a window and asked the module's range in it");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.sensorReader.writeSensorAddressByBroadcastCallCount,
                              "a pass after the restart wrote an address into a free port");
}

/**
 * A window takes the power of the port sharing its wire for its whole length, and a device needs
 * its ten seconds of power before it answers anything. So the port walked next gets its power back
 * first, and is asked nothing until it has had it for PORT_WAKE_UP_MS - otherwise the window of
 * one port would hide whatever is plugged into the other.
 */
void test_startup_the_next_port_gets_its_power_back_before_it_is_asked(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.rawBus.reader = &module.sensorReader;
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.skipTheBootSettleWindow();

    struct Seen
    {
        StartupScanStep::Kind kind;
        int pin;
        std::vector<int> pins;
        unsigned long atMs;
    };
    std::vector<Seen> seen;
    unsigned long nowMs = BOOT_SETTLE_MS;
    for (int tick = 0; tick < 600; tick++)
    {
        nowMs += 1000;
        module.arduino.setMillis(nowMs);
        StartupScanStep step;
        while (module.startupScan.takeNextStep(step))
        {
            Seen record = {step.kind, step.pin, step.pins, nowMs};
            seen.push_back(record);
            module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        }
        if (!module.startupScan.isRunning())
        {
            break;
        }
    }

    // The step that closes the window of port 0 is the one that gives port 1 its power back.
    size_t window = seen.size();
    for (size_t i = 0; i < seen.size(); i++)
    {
        if (seen[i].kind == StartupScanStep::RELEASE_LINE && seen[i].pin == 0)
        {
            window = i;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(window < seen.size(), "the window of port 0 was never closed");
    TEST_ASSERT_TRUE_MESSAGE(window + 1 < seen.size(), "nothing followed the window of port 0");

    const Seen& next = seen[window + 1];
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::POWER_FREE_PORTS, next.kind,
                              "the port after the window was asked something before its power was "
                              "seen to");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)next.pins.size(), "the power step is not about one port");
    TEST_ASSERT_EQUAL_MESSAGE(1, next.pins[0], "the power step is not about the port walked next");

    bool asked = false;
    for (size_t i = window + 1; i < seen.size(); i++)
    {
        if (seen[i].pin != 1 || (seen[i].kind != StartupScanStep::PROBE_CANDIDATE &&
                                 seen[i].kind != StartupScanStep::PROBE_PRESENCE))
        {
            continue;
        }
        asked = true;
        char message[200];
        snprintf(message, sizeof(message),
                 "port 1 was asked %lu ms after the window of port 0 gave its power back - its "
                 "device needs %lu ms to come up",
                 seen[i].atMs - seen[window].atMs,
                 (unsigned long)StartupSensorScanUseCase::PORT_WAKE_UP_MS);
        TEST_ASSERT_TRUE_MESSAGE(seen[i].atMs - seen[window].atMs >=
                                     StartupSensorScanUseCase::PORT_WAKE_UP_MS,
                                 message);
        break;
    }
    TEST_ASSERT_TRUE_MESSAGE(asked, "port 1 was never asked anything");
}

namespace {

/** @brief One step as the line saw it, and what the bus half said about the port's wire then. */
struct WindowStepSeen
{
    StartupScanStep::Kind kind;
    int pin;
    int model;
    bool windowOpenAfter;
    bool pairedPortKeptDarkAfter;
    bool pairedPortPoweredAfter;
};

/** @brief Walk the restart's pass step by step on a two-port wire, recording every step. */
std::vector<WindowStepSeen> walkTheRestartsPass(ModuleUnderTest& module, unsigned long& nowMs,
                                                int pairedPortOf0 = 1)
{
    std::vector<WindowStepSeen> seen;
    for (int tick = 0; tick < 600; tick++)
    {
        nowMs += 1000;
        module.arduino.setMillis(nowMs);
        StartupScanStep step;
        while (module.startupScan.takeNextStep(step))
        {
            StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
            WindowStepSeen record;
            record.kind = step.kind;
            record.pin = step.pin;
            record.model = step.model;
            record.windowOpenAfter = module.startupScan.holdsAWindowOpen();
            record.pairedPortKeptDarkAfter = module.startupScan.keepsThePortDark(pairedPortOf0);
            record.pairedPortPoweredAfter = module.sensorReader.isPortPowered(pairedPortOf0);
            seen.push_back(record);
            module.startupScan.applyStepOutcome(outcome);
        }
        if (!module.startupScan.isRunning())
        {
            break;
        }
    }
    return seen;
}

} // namespace

/**
 * The window of a port is a sequence of steps, one catalog model each: the poll that opens it, the
 * range of every scannable ModBus model in the catalog's order, the address writes of every such
 * model, and the step that closes it. From the first to the last the port sharing the wire is
 * dark and the bus half says so - to the sweep, which leaves that port alone, and to the command
 * queue, which waits - and no step of anything else of the pass comes in between.
 */
void test_startup_a_window_is_one_model_per_step_and_the_wire_dark_across_them(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    SensorType unscannable = makeModbusType("type-2", "SILENT", 2);
    unscannable.canScan = false;
    module.sensorTypeRepository.addSensorType(unscannable);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-3", "HUMID", 3));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.rawBus.reader = &module.sensorReader;
    // Port 1 is powered, the way a working sensor's port is.
    module.sensorReader.portPowered[1] = true;
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.skipTheBootSettleWindow();

    unsigned long nowMs = BOOT_SETTLE_MS;
    const std::vector<WindowStepSeen> seen = walkTheRestartsPass(module, nowMs);

    size_t opened = seen.size();
    for (size_t i = 0; i < seen.size(); i++)
    {
        if (seen[i].kind == StartupScanStep::PROBE_PRESENCE && seen[i].pin == 0)
        {
            opened = i;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(opened < seen.size(), "port 0 never got its window");

    std::vector<StartupScanStep::Kind> expected;
    std::vector<int> expectedModel;
    expected.push_back(StartupScanStep::PROBE_PRESENCE);
    expectedModel.push_back(-1);
    for (int model = 0; model <= 2; model += 2)
    {
        expected.push_back(StartupScanStep::PROBE_RANGE);
        expectedModel.push_back(model);
    }
    for (int model = 0; StartupSensorScanUseCase::THE_WINDOW_WRITES_THE_PORT_ADDRESS && model <= 2;
         model += 2)
    {
        expected.push_back(StartupScanStep::WRITE_PORT_ADDRESS);
        expectedModel.push_back(model);
    }
    expected.push_back(StartupScanStep::RELEASE_LINE);
    expectedModel.push_back(-1);
    const size_t steps = expected.size();
    TEST_ASSERT_TRUE_MESSAGE(opened + steps <= seen.size(), "the window of port 0 was cut short");

    char message[200];
    for (size_t i = 0; i < steps; i++)
    {
        const WindowStepSeen& step = seen[opened + i];
        snprintf(message, sizeof(message), "step %u of the window is %d on pin %d for model %d",
                 (unsigned)i, (int)step.kind, step.pin, step.model);
        TEST_ASSERT_EQUAL_MESSAGE(expected[i], step.kind, message);
        TEST_ASSERT_EQUAL_MESSAGE(0, step.pin, message);
        TEST_ASSERT_EQUAL_MESSAGE(expectedModel[i], step.model, message);

        const bool last = (i + 1 == steps);
        snprintf(message, sizeof(message), "after step %u of the window the wire was %s", (unsigned)i,
                 last ? "still held" : "given back");
        TEST_ASSERT_EQUAL_MESSAGE(!last, step.windowOpenAfter, message);
        TEST_ASSERT_EQUAL_MESSAGE(!last, !step.pairedPortPoweredAfter, message);
        // And after the close the port is still left alone, for the time what is on it needs.
        TEST_ASSERT_TRUE_MESSAGE(step.pairedPortKeptDarkAfter,
                                 "the sweep was let at the paired port while the window held it dark "
                                 "or before it had come up again");
    }

    module.arduino.setMillis(nowMs + StartupSensorScanUseCase::PORT_WAKE_UP_MS);
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.keepsThePortDark(1),
                              "the paired port is still held off the sweep after its warm-up");
}

/**
 * A window is closed before the walk leaves its port for any reason - here, the port is claimed
 * while the window is open. The wire gets its power back, and nothing more of the window is asked.
 */
void test_startup_a_window_on_a_claimed_port_is_closed_before_the_walk_moves_on(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.rawBus.reader = &module.sensorReader;
    module.sensorReader.portPowered[1] = true;
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.skipTheBootSettleWindow();

    bool claimed = false;
    bool wireGivenBack = false;
    std::vector<StartupScanStep::Kind> afterTheClaim;
    unsigned long nowMs = BOOT_SETTLE_MS;
    for (int tick = 0; tick < 600 && module.startupScan.isRunning(); tick++)
    {
        nowMs += 1000;
        module.arduino.setMillis(nowMs);
        StartupScanStep step;
        while (module.startupScan.takeNextStep(step))
        {
            if (claimed && step.pin == 0)
            {
                afterTheClaim.push_back(step.kind);
            }
            module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
            if (claimed && step.pin == 0 && step.kind == StartupScanStep::RELEASE_LINE)
            {
                wireGivenBack = module.sensorReader.isPortPowered(1) &&
                                !module.startupScan.holdsAWindowOpen();
            }
            if (!claimed && step.kind == StartupScanStep::PROBE_PRESENCE && step.pin == 0)
            {
                // A person accepts a sensor on the port while its window is open.
                module.sensorRepository.sensors.push_back(
                    makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_ADDED));
                claimed = true;
            }
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(claimed, "port 0 never had a window to claim it in");
    TEST_ASSERT_FALSE_MESSAGE(afterTheClaim.empty(), "the window of the claimed port was never closed");
    TEST_ASSERT_EQUAL_MESSAGE(StartupScanStep::RELEASE_LINE, afterTheClaim[0],
                              "the walk asked the claimed port something more before closing its "
                              "window");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)afterTheClaim.size(),
                              "the walk went on asking a port somebody has claimed");
    TEST_ASSERT_TRUE_MESSAGE(wireGivenBack,
                             "the step that closed the window of the claimed port did not give its "
                             "wire the power back");
}

// --- the restart's pass: the cheap rung on every free port first, a structure per sensor adopted ---

namespace {

/** @brief One step the discovery put on the line, and when. */
struct StepSeen
{
    StartupScanStep::Kind kind;
    int pin;
    unsigned long atMs;
};

/** @brief Walk the pass a clocked stand has armed, a second at a time, recording every step. */
std::vector<StepSeen> walkThePassRecordingItsSteps(ModuleUnderTest& module, unsigned long& nowMs)
{
    std::vector<StepSeen> seen;
    for (int tick = 0; tick < 2000; tick++)
    {
        nowMs += 1000;
        module.arduino.setMillis(nowMs);
        StartupScanStep step;
        while (module.startupScan.takeNextStep(step))
        {
            StepSeen record = {step.kind, step.pin, nowMs};
            seen.push_back(record);
            module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        }
        if (!module.startupScan.aPassIsInFlight())
        {
            break;
        }
    }
    return seen;
}

/** @brief The type of the first record of a row in a node, or "" when the node carries none. */
std::string typeOfRowIn(const Node& node, const std::string& rowUid)
{
    const std::string prefix = rowUid + ".";
    for (size_t i = 0; i < node.sensors.size(); i++)
    {
        if (node.sensors[i].uid.compare(0, prefix.size(), prefix) == 0)
        {
            return node.sensors[i].type;
        }
    }
    return "";
}

} // namespace

/**
 * On the restart's pass every free port is asked the cheap rung - the module's range and each model
 * at its own factory address - before any port is asked the rest: the window of a port is a minute
 * and more, and a device on a port walked later would wait for the window of every port before it.
 * The ports then get their windows in the order they were walked.
 */
void test_startup_the_restarts_pass_asks_every_free_port_cheaply_before_any_window(void)
{
    ModuleUnderTest module(true);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorTypeRepository.addSensorType(makeModbusType("type-2", "HUMID", 2));
    module.startupScan.setRawBusAccess(&module.rawBus); // nothing answers the poll anywhere
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.skipTheBootSettleWindow();

    unsigned long nowMs = BOOT_SETTLE_MS;
    const std::vector<StepSeen> seen = walkThePassRecordingItsSteps(module, nowMs);

    std::vector<int> cheapPins;
    std::vector<int> pollPins;
    size_t lastCheap = 0;
    size_t firstPoll = seen.size();
    for (size_t i = 0; i < seen.size(); i++)
    {
        if (seen[i].kind == StartupScanStep::PROBE_CANDIDATE)
        {
            cheapPins.push_back(seen[i].pin);
            lastCheap = i;
        }
        if (seen[i].kind == StartupScanStep::PROBE_PRESENCE)
        {
            pollPins.push_back(seen[i].pin);
            if (firstPoll == seen.size())
            {
                firstPoll = i;
            }
        }
    }

    // Per port: both models at the four addresses of the range, and at their own.
    const int perPort = 2 * (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1) + 2;
    TEST_ASSERT_EQUAL_MESSAGE(perPort * MODULE_PIN_COUNT, (int)cheapPins.size(),
                              "every free port was not asked both models at the range and at their "
                              "own addresses");
    TEST_ASSERT_EQUAL_MESSAGE(MODULE_PIN_COUNT, (int)pollPins.size(),
                              "every free port did not get its one window");
    char message[160];
    snprintf(message, sizeof(message),
             "a window opened at step %u, before the cheap rung was over at step %u",
             (unsigned)firstPoll, (unsigned)lastCheap);
    TEST_ASSERT_TRUE_MESSAGE(lastCheap < firstPoll, message);
    for (int pin = 0; pin < MODULE_PIN_COUNT; pin++)
    {
        snprintf(message, sizeof(message), "the cheap rung did not walk the ports in order at port %d",
                 pin);
        for (int q = 0; q < perPort; q++)
        {
            TEST_ASSERT_EQUAL_MESSAGE(pin, cheapPins[perPort * pin + q], message);
        }
        snprintf(message, sizeof(message), "the windows did not follow the walk's order at port %d", pin);
        TEST_ASSERT_EQUAL_MESSAGE(pin, pollPins[pin], message);
    }
}

namespace {

/** @brief One step of the restart's pass, and whether a sensor had been found on port 0 before it. */
struct TwinStep
{
    StartupScanStep::Kind kind;
    int pin;
    int address;
    bool afterPort0WasFound;
};

/**
 * @brief Two devices of one model at its factory address on ports 0 and 1, one wire; ports 2 and 3,
 *        the other wire, empty. Walk the restart's pass step by step.
 *
 * While both devices are at the factory address a read there is a collision and answers nothing;
 * both answer the wildcard poll. Port 0's identification writes its device onto the port's own
 * address. From then on the device of port 1 is alone at the factory address - it answers a read
 * there when `secondAnswersAlone`, and otherwise stays as silent as it was.
 */
std::vector<TwinStep> walkTwinsOnOneWire(ModuleUnderTest& module, bool secondAnswersAlone)
{
    module.moduleConfig.setSensorPinCount(4);
    std::vector<int> lines = twoPortsOnOneLine();
    lines.push_back(1);
    lines.push_back(1);
    module.startupScan.setPortLines(lines);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.rawBus.answeringChannels.push_back(0);
    module.rawBus.answeringChannels.push_back(1);
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.answeringAddresses.push_back(1);
    module.boot();

    std::vector<TwinStep> seen;
    bool port0Found = false;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        TwinStep record = {step.kind, step.pin, step.entity.modbusDeviceAddress, port0Found};
        seen.push_back(record);
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
        if (!port0Found && (bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00"))
        {
            port0Found = true;
            if (secondAnswersAlone)
            {
                module.sensorReader.readAnsweringAddresses.push_back(1);
                module.sensorReader.answeringAddresses.push_back(1);
            }
        }
    }
    return seen;
}

unsigned countSteps(const std::vector<TwinStep>& seen, StartupScanStep::Kind kind, int pin,
                    bool afterPort0WasFound)
{
    unsigned count = 0;
    for (size_t i = 0; i < seen.size(); i++)
    {
        if (seen[i].kind == kind && seen[i].pin == pin && seen[i].afterPort0WasFound == afterPort0WasFound)
        {
            count++;
        }
    }
    return count;
}

} // namespace

/**
 * A find on the rest of the ladder frees the wire it was made on: port 0's device was moved off the
 * factory address it collided at with the device of port 1. So port 1, still waiting for its
 * window, is asked the cheap rung again before it - and its device, alone at that address now, is
 * adopted by it: no window and no identification on port 1. The ports of the other wire are not
 * asked again.
 */
void test_startup_the_port_sharing_a_wire_with_a_find_is_asked_cheaply_again_before_its_window(void)
{
    ModuleUnderTest module;
    const std::vector<TwinStep> seen = walkTwinsOnOneWire(module, true);

    TEST_ASSERT_TRUE_MESSAGE((bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00"),
                             "port 0's device was not found - the scenario did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)countSteps(seen, StartupScanStep::PROBE_PRESENCE, 1, false),
                              "a window of port 1 opened before port 0's device was found - the "
                              "scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(countSteps(seen, StartupScanStep::PROBE_CANDIDATE, 1, true) > 0,
                             "port 1 was not asked the cheap rung again after the find on its wire");
    TEST_ASSERT_TRUE_MESSAGE((bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01"),
                             "port 1's device was not adopted");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)countSteps(seen, StartupScanStep::PROBE_PRESENCE, 1, true),
                              "port 1 was given a window although the cheap rung named its device");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)countSteps(seen, StartupScanStep::SCAN_CANDIDATE, 1, true),
                              "the identification procedure ran on port 1");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)(countSteps(seen, StartupScanStep::PROBE_CANDIDATE, 2, true) +
                                       countSteps(seen, StartupScanStep::PROBE_CANDIDATE, 3, true)),
                              "a port of the other wire was asked the cheap rung again");
}

/**
 * And when the cheap rung asked again names nothing, the port gets its window as it would have: the
 * question costs the port its reads and nothing else.
 */
void test_startup_a_port_asked_cheaply_again_that_stays_silent_gets_its_window(void)
{
    ModuleUnderTest module;
    const std::vector<TwinStep> seen = walkTwinsOnOneWire(module, false);

    TEST_ASSERT_TRUE_MESSAGE((bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00"),
                             "port 0's device was not found - the scenario did not happen");
    bool askedAgainBeforeItsWindow = false;
    bool windowAfter = false;
    for (size_t i = 0; i < seen.size(); i++)
    {
        if (!seen[i].afterPort0WasFound || seen[i].pin != 1)
        {
            continue;
        }
        if (seen[i].kind == StartupScanStep::PROBE_CANDIDATE && !windowAfter)
        {
            askedAgainBeforeItsWindow = true;
        }
        if (seen[i].kind == StartupScanStep::PROBE_PRESENCE)
        {
            windowAfter = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(askedAgainBeforeItsWindow,
                             "port 1 was not asked the cheap rung again before its window");
    TEST_ASSERT_TRUE_MESSAGE(windowAfter, "port 1 did not get its window after the cheap rung named nothing");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)countSteps(seen, StartupScanStep::PROBE_PRESENCE, 1, true),
                              "port 1 was not given exactly one window");
}

/**
 * The passes after the restart are what they were: port by port, the cheap rung - the module's range
 * and the model's own address - and then, on the port the restart gave up on, the poll, before the
 * next port is asked anything.
 */
void test_startup_a_pass_after_the_restart_still_asks_one_port_at_a_time(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.rawBus.answeringChannels.push_back(0); // something nobody can name on port 0
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();
    module.runDiscovery();
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.unidentifiedPinCount(),
                              "the restart did not give up on port 0 - the scenario did not happen");

    const uint32_t passesBefore = module.startupScan.freePortPassCount();
    unsigned long nowMs = module.nowMs() + StartupSensorScanUseCase::FREE_PORT_PROBE_INTERVAL_MS;
    module.arduino.setMillis(nowMs);
    const std::vector<StepSeen> seen = walkThePassRecordingItsSteps(module, nowMs);

    TEST_ASSERT_EQUAL_MESSAGE(passesBefore + 1, module.startupScan.freePortPassCount(),
                              "no pass after the restart ran - there is nothing to look at");
    std::vector<StartupScanStep::Kind> kinds;
    std::vector<int> pins;
    for (size_t i = 0; i < seen.size(); i++)
    {
        if (seen[i].kind == StartupScanStep::PROBE_CANDIDATE ||
            seen[i].kind == StartupScanStep::PROBE_PRESENCE)
        {
            kinds.push_back(seen[i].kind);
            pins.push_back(seen[i].pin);
        }
    }
    const size_t cheapPerPort = (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1) + 1;
    TEST_ASSERT_EQUAL_MESSAGE((int)(2 * cheapPerPort + 1), (int)kinds.size(),
                              "the pass did not ask port 0 its cheap rung and its poll and port 1 its cheap rung");
    for (size_t i = 0; i < cheapPerPort; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(kinds[i] == StartupScanStep::PROBE_CANDIDATE && pins[i] == 0,
                                 "the pass did not begin with the cheap rung of port 0");
    }
    TEST_ASSERT_TRUE_MESSAGE(kinds[cheapPerPort] == StartupScanStep::PROBE_PRESENCE && pins[cheapPerPort] == 0,
                             "the poll of the port given up on did not follow its own cheap rung");
    for (size_t i = cheapPerPort + 1; i < kinds.size(); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(kinds[i] == StartupScanStep::PROBE_CANDIDATE && pins[i] == 1,
                                 "port 1 was asked before port 0 was done with");
    }
}

/**
 * A sensor the restart adopts is sent to the server in a structure there and then, not when the
 * pass is over: the sweep reads it and its readings go out from that moment, and the app shows a
 * sensor only once a structure has described it. Every one of them, and the one the pass ends with.
 */
void test_startup_the_restart_sends_a_structure_for_each_sensor_as_it_adopts_it(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    // Whatever is asked at address 1 answers, on either port.
    module.sensorReader.readAnsweringAddresses.push_back(1);
    module.sensorReader.answeringAddresses.push_back(1);
    module.boot();
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.startupScan.adoptedCount(),
                              "the two ports were not adopted - the scenario did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(3, module.republish.calls,
                              "the pass did not send one structure per sensor adopted and one at its end");
    TEST_ASSERT_EQUAL_STRING("startup_scan_found", module.republish.reasons[0].c_str());
    TEST_ASSERT_EQUAL_STRING("startup_scan_found", module.republish.reasons[1].c_str());
    TEST_ASSERT_EQUAL_STRING("startup_scan", module.republish.reasons[2].c_str());
    TEST_ASSERT_TRUE_MESSAGE(module.republish.passRunningAtCall[0] && module.republish.passRunningAtCall[1],
                             "a structure for an adopted sensor waited for the pass to end");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.republish.rowsAtCall[0],
                              "the first structure was not asked for when the first sensor was adopted");
    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.republish.rowsAtCall[1],
                              "the second structure was not asked for when the second sensor was adopted");
    TEST_ASSERT_FALSE_MESSAGE(module.republish.passRunningAtCall[2],
                              "the last structure was asked for before the pass was over");
}

/**
 * While a structure is on its way, a sensor adopted is owed rather than sent - one structure is
 * owed however many are asked for - and the one owed goes when the one on its way has settled,
 * built from the registry as it is then. The structure the pass ends with, asked for while one is
 * still owed, is that same one.
 */
void test_startup_a_structure_asked_for_while_one_is_on_its_way_is_owed_and_goes_after_it(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.readAnsweringAddresses.push_back(1);
    module.sensorReader.answeringAddresses.push_back(1);
    module.boot();
    // A structure is on its way to the server for the whole pass.
    module.republish.pending = true;
    module.runDiscovery();

    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.startupScan.adoptedCount(),
                              "the two ports were not adopted - the scenario did not happen");
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isRunning(), "the pass did not end");
    TEST_ASSERT_EQUAL_MESSAGE(0, module.republish.calls,
                              "a structure was handed over while another was still on its way");

    // Asked again and again while the one on its way has not settled: still nothing.
    module.startupScan.reviewSensorLiveness();
    module.startupScan.reviewSensorLiveness();
    TEST_ASSERT_EQUAL_MESSAGE(0, module.republish.calls,
                              "the owed structure went out before the one on its way had settled");

    module.republish.pending = false;
    module.startupScan.reviewSensorLiveness();
    TEST_ASSERT_EQUAL_MESSAGE(1, module.republish.calls,
                              "the owed structure did not go out once the one on its way had settled - "
                              "or more than one was owed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("startup_scan", module.republish.lastReason.c_str(),
                                     "the owed structure is not the one the pass ends with");
    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.republish.rowsAtCall[0],
                              "the owed structure was not built from the registry as it is when it goes");

    module.startupScan.reviewSensorLiveness();
    TEST_ASSERT_EQUAL_MESSAGE(1, module.republish.calls, "the owed structure went out twice");
}

/**
 * The review of the rows' liveness sends a structure too, and it waits the same way: a structure
 * submitted while one is on its way goes out beside it. It compares the rows once that one has
 * settled.
 */
void test_startup_the_liveness_review_waits_for_a_structure_on_its_way(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(1);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.00", 0, MODBUS_ADDRESS_MIN, SENSOR_STATUS_NEW));
    module.sensorReader.scanSensorReturnValue = false; // the ping finds nothing
    module.boot();
    module.runDiscovery();
    const int callsAfterThePass = module.republish.calls;

    // The row answers the sweep, twice, a sweep apart: it is no longer the empty port the last
    // structure said it was.
    unsigned long nowMs = module.nowMs();
    module.sensorRepository.sensors[0].isError = false;
    for (int sweep = 0; sweep < 3; sweep++)
    {
        nowMs += StartupSensorScanUseCase::SWEEP_INTERVAL_MS;
        module.arduino.setMillis(nowMs);
        module.sensorValueModel.setLastReadTime("AA:BB:CC:DD:EE:FF.00", nowMs);
        module.republish.pending = true;
        module.startupScan.reviewSensorLiveness();
    }
    TEST_ASSERT_EQUAL_MESSAGE(callsAfterThePass, module.republish.calls,
                              "the liveness review sent a structure while another was on its way");

    module.republish.pending = false;
    for (int sweep = 0; sweep < 3 && module.republish.calls == callsAfterThePass; sweep++)
    {
        nowMs += StartupSensorScanUseCase::SWEEP_INTERVAL_MS;
        module.arduino.setMillis(nowMs);
        module.sensorValueModel.setLastReadTime("AA:BB:CC:DD:EE:FF.00", nowMs);
        module.startupScan.reviewSensorLiveness();
    }
    TEST_ASSERT_EQUAL_MESSAGE(callsAfterThePass + 1, module.republish.calls,
                              "the liveness review did not send the change once nothing was on its way");
    TEST_ASSERT_EQUAL_STRING("sensor_liveness_changed", module.republish.lastReason.c_str());
}

/**
 * The restart's pass takes the power of the ports it is not asking - every model scan of it takes
 * that of the port sharing its wire - and the sweep's first read of a port that has just had its
 * power back is answered by nobody. Those misses are the pass's own: a row is not given up on for them while the
 * pass runs, and its silence is counted from the end of the pass. A row that is really gone is
 * given up on as many sweeps after the pass as before it.
 */
void test_startup_the_silence_the_restarts_pass_caused_is_not_counted(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    const char* const ROW = "AA:BB:CC:DD:EE:FF.01";
    module.sensorRepository.sensors.push_back(
        makeStoredEntity(ROW, 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.startupScan.setRawBusAccess(&module.rawBus); // nothing answers the poll
    module.sensorReader.scanSensorReturnValue = true; // the ping of the row answers
    module.sensorReader.readSensorReturnValue = false; // the free port answers no model
    module.boot();
    module.skipTheBootSettleWindow();

    // The sweep read the row a moment before the pass began.
    unsigned long nowMs = BOOT_SETTLE_MS;
    module.sensorValueModel.setLastReadTime(ROW, nowMs);

    // Into the pass: the ping, and the free port's power.
    for (int i = 0; i < 2; i++)
    {
        StartupScanStep step;
        TEST_ASSERT_TRUE_MESSAGE(module.startupScan.takeNextStep(step), "the pass stopped short");
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.isRunning(), "the pass is not running");

    // A step of the pass took the row's port down; every read since has failed, for a minute.
    module.sensorRepository.sensors[0].isError = true;
    nowMs += 4 * StartupSensorScanUseCase::SWEEP_INTERVAL_MS;
    module.arduino.setMillis(nowMs);

    Node midPass;
    module.startupScan.describeStoredSensors(midPass);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("temperature", typeOfRowIn(midPass, ROW).c_str(),
                                     "a structure built in the middle of the pass described a row it "
                                     "had just heard as an empty port, for reads the pass itself "
                                     "made fail");

    // The pass ends; the row still fails, but only for the sweeps the pass itself cost it.
    const std::vector<StepSeen> rest = walkThePassRecordingItsSteps(module, nowMs);
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isRunning(), "the pass did not end");
    TEST_ASSERT_FALSE_MESSAGE(rest.empty(), "the pass had nothing left to do - the scenario did not happen");
    Node atTheEnd;
    module.startupScan.describeStoredSensors(atTheEnd);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("temperature", typeOfRowIn(atTheEnd, ROW).c_str(),
                                     "the structure the pass ends with described the row as an empty "
                                     "port for the silence the pass caused");

    // And a row that stays silent after the pass is given up on, the sweeps after it.
    nowMs += StartupSensorScanUseCase::SILENT_AFTER_MISSED_SWEEPS * StartupSensorScanUseCase::SWEEP_INTERVAL_MS;
    module.arduino.setMillis(nowMs);
    Node afterThePass;
    module.startupScan.describeStoredSensors(afterThePass);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", typeOfRowIn(afterThePass, ROW).c_str(),
                                     "a row silent for the sweeps after the pass was not given up on");
}

namespace {

/** @brief What one missed sweep of a row does at this uptime, long after the restart's pass. */
struct OneMissedSweep
{
    std::string described;
    int structuresSentByTheReview = 0;
};

OneMissedSweep oneMissedSweepAtUptime(uint64_t uptimeMs)
{
    OneMissedSweep seen;
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    const char* const ROW = "AA:BB:CC:DD:EE:FF.01";
    module.sensorRepository.sensors.push_back(
        makeStoredEntity(ROW, 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.scanSensorReturnValue = true;  // the ping of the row answers
    module.sensorReader.readSensorReturnValue = false; // the free port answers no model
    module.boot();
    module.runDiscovery();
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isRunning(), "the pass did not end");

    // The board's millis() is 32 bits wide and wraps after 49.7 days: that is the clock fed here.
    const uint32_t answeredAtMs = static_cast<uint32_t>(uptimeMs - 30000);
    const uint32_t nowMs = static_cast<uint32_t>(uptimeMs);

    // Long after the pass the row answered the sweep a moment ago, and is described as a sensor.
    module.arduino.setMillis(answeredAtMs);
    module.sensorValueModel.setLastReadTime(ROW, answeredAtMs);
    module.sensorRepository.sensors[0].isError = false;
    Node answering;
    module.startupScan.describeStoredSensors(answering);
    module.startupScan.onDescribedStructureSettled(true);
    const int callsBefore = module.republish.calls;

    // One read fails: one sweep missed, where SILENT_AFTER_MISSED_SWEEPS is three.
    module.arduino.setMillis(nowMs);
    module.sensorValueModel.setLastReadTime(ROW, static_cast<uint32_t>(uptimeMs - 16000));
    module.sensorRepository.sensors[0].isError = true;
    module.startupScan.reviewSensorLiveness();
    seen.structuresSentByTheReview = module.republish.calls - callsBefore;

    Node oneMiss;
    module.startupScan.describeStoredSensors(oneMiss);
    seen.described = typeOfRowIn(oneMiss, ROW);
    return seen;
}

} // namespace

/**
 * A single missed sweep does not give a row up, however long the module has been running - and a
 * module runs for longer than its millis() can count: the clock wraps after 49.7 days, and any
 * comparison of two moments rather than of two ages turns round halfway there.
 */
void test_startup_a_single_missed_sweep_gives_no_row_up_at_any_uptime(void)
{
    const uint64_t DAY_MS = 24ULL * 3600ULL * 1000ULL;
    const uint64_t uptimes[] = {1 * DAY_MS,  10 * DAY_MS, 24 * DAY_MS, 25 * DAY_MS, 30 * DAY_MS,
                                45 * DAY_MS, 49 * DAY_MS, (1ULL << 32) + 60000ULL};
    char message[200];
    int givenUp = 0;
    for (size_t i = 0; i < sizeof(uptimes) / sizeof(uptimes[0]); i++)
    {
        const OneMissedSweep seen = oneMissedSweepAtUptime(uptimes[i]);
        printf("  scan: uptime %llu ms (%.2f days): after one missed sweep the row is '%s', the "
               "liveness review sent %d structure(s)\n",
               (unsigned long long)uptimes[i], (double)uptimes[i] / (double)DAY_MS,
               seen.described.c_str(), seen.structuresSentByTheReview);
        if (seen.described != "temperature" || seen.structuresSentByTheReview != 0)
        {
            givenUp++;
        }
    }
    snprintf(message, sizeof(message),
             "%d uptime(s) gave a row up as an empty port after a single missed sweep", givenUp);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, givenUp, message);
}

namespace {

/**
 * @brief How a row that answered SILENT_AFTER_MISSED_SWEEPS + 1 sweeps ago, and not since, is
 *        described this long after the restart's pass ended.
 */
std::string rowSilentForFourSweepsAt(uint64_t sincePassEndMs)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    const char* const ROW = "AA:BB:CC:DD:EE:FF.01";
    module.sensorRepository.sensors.push_back(
        makeStoredEntity(ROW, 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.startupScan.setRawBusAccess(&module.rawBus);
    module.sensorReader.scanSensorReturnValue = true;
    module.sensorReader.readSensorReturnValue = false;
    module.boot();
    module.runDiscovery();
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.isRunning(), "the pass did not end");

    const uint64_t uptimeMs = module.nowMs() + sincePassEndMs;
    const uint64_t silentForMs =
        (StartupSensorScanUseCase::SILENT_AFTER_MISSED_SWEEPS + 1) * StartupSensorScanUseCase::SWEEP_INTERVAL_MS;
    const uint32_t answeredAtMs = static_cast<uint32_t>(uptimeMs - silentForMs);

    // The last answer: the row is a sensor, and the loop has been looking at the rows all along.
    module.arduino.setMillis(answeredAtMs);
    module.sensorValueModel.setLastReadTime(ROW, answeredAtMs);
    module.sensorRepository.sensors[0].isError = false;
    module.startupScan.reviewSensorLiveness();
    Node answering;
    module.startupScan.describeStoredSensors(answering);

    // Nothing since, for four sweeps.
    module.arduino.setMillis(static_cast<uint32_t>(uptimeMs));
    module.sensorRepository.sensors[0].isError = true;
    Node silent;
    module.startupScan.describeStoredSensors(silent);
    return typeOfRowIn(silent, ROW);
}

} // namespace

/**
 * And the other way: a row that really stopped answering is given up on however long after the
 * restart's pass that happens - including when the clock has gone once round since the pass ended
 * and its end reads as a few seconds ago.
 */
void test_startup_a_row_silent_for_four_sweeps_is_given_up_at_any_uptime(void)
{
    const uint64_t DAY_MS = 24ULL * 3600ULL * 1000ULL;
    const uint64_t sincePassEnd[] = {60000ULL, 1 * DAY_MS, 25 * DAY_MS, 49 * DAY_MS,
                                     (1ULL << 32) + 5000ULL, (1ULL << 32) + 60000ULL};
    char message[200];
    int keptAsSensors = 0;
    for (size_t i = 0; i < sizeof(sincePassEnd) / sizeof(sincePassEnd[0]); i++)
    {
        const std::string described = rowSilentForFourSweepsAt(sincePassEnd[i]);
        printf("  scan: %llu ms after the restart's pass, a row silent for four sweeps is '%s'\n",
               (unsigned long long)sincePassEnd[i], described.c_str());
        if (described != "none")
        {
            keptAsSensors++;
        }
    }
    snprintf(message, sizeof(message),
             "%d moment(s) kept a row that stopped answering four sweeps ago as a sensor", keptAsSensors);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, keptAsSensors, message);
}

// --- two devices at one address of one wire, around the window that finds the second ---------------

/** @brief Ports 0 and 1 on one UART and ports 2 and 3 on the other, the way globals.h wires the board. */
std::vector<int> twoPairsOfPorts()
{
    std::vector<int> lines = twoPortsOnOneLine();
    lines.push_back(1);
    lines.push_back(1);
    return lines;
}

/** @brief Has this port been switched on since this many power calls had been made? */
bool poweredSince(const MockSensorReader& reader, size_t callsBefore, int pin)
{
    for (size_t i = callsBefore; i < reader.portPowerCalls.size(); i++)
    {
        if (reader.portPowerCalls[i].first == pin && reader.portPowerCalls[i].second)
        {
            return true;
        }
    }
    return false;
}

bool listsPin(const std::vector<int>& pins, int pin)
{
    return std::find(pins.begin(), pins.end(), pin) != pins.end();
}

/** @brief Does the structure the module holds carry any record on this port? */
bool aRecordOfThePinIn(const ModuleStructure& structure, int pin)
{
    for (size_t n = 0; n < structure.nodes.size(); n++)
    {
        for (size_t i = 0; i < structure.nodes[n].sensors.size(); i++)
        {
            if (structure.nodes[n].sensors[i].pin == pin)
            {
                return true;
            }
        }
    }
    return false;
}

/**
 * The sensor stored on port 1 holds 0x71 and answers its ping, so port 0's cheap rung leaves 0x71 out -
 * whatever answers there on port 0 is the window's to find, with port 1 dark. Until then port 0 is dark
 * too: it is taken down by the step after its cheap rung, it is not among the ports a later step gives
 * their power back to - the cheap find on port 2 makes port 3's walk begin with such a step - and it has
 * its power back from the step that begins its own walk, before its window polls it.
 */
void test_startup_a_port_the_cheap_rung_could_not_ask_its_wires_sensors_address_is_dark_until_its_walk(void)
{
    ModuleUnderTest module;
    module.startupScan.setPortLines(twoPairsOfPorts());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.boot();

    bool cheapRungOfPort0Over = false;
    int takeDownsOfPort0 = 0;
    bool takenDownStraightAfterItsCheapRung = false;
    bool darkAfterTheTakeDown = false;
    size_t callsAtTheTakeDown = 0;
    bool aStepOfAnotherPortGaveItPower = false;
    bool itsWalkGaveItPower = false;
    bool poweredByTheStepBefore = false;
    bool polledLitByItsWalk = false;
    StartupScanStep::Kind previous = StartupScanStep::NONE;
    int previousPin = -1;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        if (poweredByTheStepBefore)
        {
            // The step that gave port 0 its power back is the one that begins its own walk only when
            // what follows it is about port 0.
            poweredByTheStepBefore = false;
            itsWalkGaveItPower = step.pin == 0;
            aStepOfAnotherPortGaveItPower = aStepOfAnotherPortGaveItPower || step.pin != 0;
        }
        if (cheapRungOfPort0Over && !itsWalkGaveItPower && step.kind == StartupScanStep::POWER_FREE_PORTS &&
            listsPin(step.pins, 0))
        {
            aStepOfAnotherPortGaveItPower =
                aStepOfAnotherPortGaveItPower || poweredSince(module.sensorReader, callsAtTheTakeDown, 0);
            poweredByTheStepBefore = true;
        }
        StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.pin >= 2)
        {
            // Nothing on the other wire but one device at its factory address on port 2.
            outcome = StartupScanOutcome();
        }
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 2 && step.entity.modbusDeviceAddress == 1)
        {
            outcome.answered = true;
            outcome.answeredAddress = 1;
        }
        if (step.kind == StartupScanStep::RELEASE_FREE_PORTS && step.pin == 0)
        {
            takeDownsOfPort0++;
            takenDownStraightAfterItsCheapRung = previous == StartupScanStep::PROBE_CANDIDATE && previousPin == 0 &&
                                                 step.pins.size() == 1 && step.pins[0] == 0;
            darkAfterTheTakeDown = !module.sensorReader.isPortPowered(0);
            callsAtTheTakeDown = module.sensorReader.portPowerCalls.size();
            cheapRungOfPort0Over = true;
        }
        if (step.kind == StartupScanStep::PROBE_PRESENCE && step.pin == 0)
        {
            polledLitByItsWalk = itsWalkGaveItPower && module.sensorReader.isPortPowered(0);
        }
        previous = step.kind;
        previousPin = step.pin;
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE((bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.02"),
                             "the device on port 2 was not found by the cheap rung - the scenario did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(1, takeDownsOfPort0, "port 0 was not taken down exactly once");
    TEST_ASSERT_TRUE_MESSAGE(takenDownStraightAfterItsCheapRung,
                             "port 0 was not taken down by the step right after its cheap rung, alone");
    TEST_ASSERT_TRUE_MESSAGE(darkAfterTheTakeDown, "the step that takes port 0 down left it with its power");
    TEST_ASSERT_FALSE_MESSAGE(aStepOfAnotherPortGaveItPower,
                              "port 0 had its power back from a step that begins the walk of another port");
    TEST_ASSERT_TRUE_MESSAGE(itsWalkGaveItPower, "port 0's own walk did not give it its power back");
    TEST_ASSERT_TRUE_MESSAGE(polledLitByItsWalk, "port 0's window polled it without the power its walk gave it");
    TEST_ASSERT_TRUE_MESSAGE((bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00"),
                             "the device at 0x71 on port 0 was not found by its window");
}

/**
 * The sensor stored on port 2 answers its ping at 0x72, so port 3's cheap rung will leave 0x72 out: port 3
 * is asked the cheap rung first, before ports 0 and 1 on the other wire, and taken down after it. The
 * windows still go over the ports in their own order - 0, 1, then 3.
 */
void test_startup_a_port_that_will_leave_an_address_out_is_asked_the_cheap_rung_first(void)
{
    ModuleUnderTest module;
    module.startupScan.setPortLines(twoPairsOfPorts());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.02", 2, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_NEW));
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.sensorReader.readSensorReturnValue = false;
    module.boot();

    std::vector<int> cheapOrder;
    std::vector<int> windows;
    int takeDownOf3 = -1;
    int stepIndex = 0;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.pin != 2 && step.kind != StartupScanStep::VERIFY_KNOWN)
        {
            // Nothing on the free ports.
            outcome = StartupScanOutcome();
        }
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && (cheapOrder.empty() || cheapOrder.back() != step.pin))
        {
            cheapOrder.push_back(step.pin);
        }
        if (step.kind == StartupScanStep::PROBE_PRESENCE)
        {
            windows.push_back(step.pin);
        }
        if (step.kind == StartupScanStep::RELEASE_FREE_PORTS && step.pin == 3 && takeDownOf3 < 0)
        {
            takeDownOf3 = stepIndex;
        }
        stepIndex++;
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(cheapOrder.size() >= 3, "the free ports were not all asked the cheap rung");
    TEST_ASSERT_EQUAL_MESSAGE(3, cheapOrder[0], "port 3 was not the first port asked the cheap rung");
    TEST_ASSERT_EQUAL_MESSAGE(0, cheapOrder[1], "the other ports were not asked in their own order after port 3");
    TEST_ASSERT_EQUAL_MESSAGE(1, cheapOrder[2], "the other ports were not asked in their own order after port 3");
    TEST_ASSERT_TRUE_MESSAGE(takeDownOf3 >= 0, "port 3 was not taken down after its cheap rung");
    TEST_ASSERT_EQUAL_MESSAGE(3, (int)windows.size(), "not every free port had its window");
    TEST_ASSERT_TRUE_MESSAGE(windows[0] == 0 && windows[1] == 1 && windows[2] == 3,
                             "the windows did not go over the ports in their own order");
}

/**
 * The same row silent to its ping: port 3's cheap rung asks 0x72 on the live wire and leaves nothing out,
 * so the cheap rung goes over the ports in their own order - 0, 1, then 3.
 */
void test_startup_a_port_beside_a_row_silent_to_its_ping_is_asked_the_cheap_rung_in_its_turn(void)
{
    ModuleUnderTest module;
    module.startupScan.setPortLines(twoPairsOfPorts());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.02", 2, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_NEW));
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();

    std::vector<int> cheapOrder;
    bool askedTheAddressOnTheLiveWire = false;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && (cheapOrder.empty() || cheapOrder.back() != step.pin))
        {
            cheapOrder.push_back(step.pin);
        }
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 3 &&
            step.entity.modbusDeviceAddress == MODBUS_ADDRESS_MIN + 2)
        {
            askedTheAddressOnTheLiveWire = true;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(askedTheAddressOnTheLiveWire,
                             "port 3 was not asked the address of a row silent to its ping - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(cheapOrder.size() >= 3, "the free ports were not all asked the cheap rung");
    TEST_ASSERT_TRUE_MESSAGE(cheapOrder[0] == 0 && cheapOrder[1] == 1 && cheapOrder[2] == 3,
                             "the cheap rung did not go over the ports in their own order");
}

/**
 * Nothing is taken down where the cheap rung left nothing out: the sensor stored on port 1 was silent
 * to its ping, so its address is asked on port 0's live wire like any other, and nothing that answers
 * it there is hidden from the cheap rung.
 */
void test_startup_a_port_whose_wires_sensor_was_silent_to_its_ping_is_not_taken_down(void)
{
    ModuleUnderTest module;
    module.startupScan.setPortLines(twoPairsOfPorts());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.readSensorReturnValue = false;
    module.sensorReader.scanSensorReturnValue = false;
    module.boot();

    bool askedTheAddressOnTheLiveWire = false;
    int takeDowns = 0;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 0 &&
            step.entity.modbusDeviceAddress == MODBUS_ADDRESS_MIN + 1)
        {
            askedTheAddressOnTheLiveWire = true;
        }
        if (step.kind == StartupScanStep::RELEASE_FREE_PORTS && step.pin >= 0)
        {
            takeDowns++;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(askedTheAddressOnTheLiveWire,
                             "port 0 was not asked the address of a row silent to its ping - the scenario did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(0, takeDowns, "a port was taken down although its cheap rung left nothing out");
}

/**
 * Nor where the port's window is the next thing the pass asks: port 0 is the module's only free port,
 * and the window that follows its cheap rung would only give it back the power a take-down took - for
 * the ten seconds what is on it then needs before it answers.
 */
void test_startup_a_port_whose_window_comes_next_is_not_taken_down(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(2);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.boot();

    bool leftOut = true;
    int takeDowns = 0;
    std::vector<StartupScanStep::Kind> afterTheCheapRung;
    bool cheapRungOver = false;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 0 &&
            step.entity.modbusDeviceAddress == MODBUS_ADDRESS_MIN + 1)
        {
            leftOut = false;
        }
        if (step.kind == StartupScanStep::RELEASE_FREE_PORTS && step.pin >= 0)
        {
            takeDowns++;
        }
        if (cheapRungOver && afterTheCheapRung.empty())
        {
            afterTheCheapRung.push_back(step.kind);
        }
        cheapRungOver = cheapRungOver || (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 0 &&
                                          step.entity.modbusDeviceAddress == 1);
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(leftOut, "port 0's cheap rung asked 0x71 on the live wire - the scenario did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(0, takeDowns, "port 0 was taken down although its window came next");
    TEST_ASSERT_TRUE_MESSAGE(!afterTheCheapRung.empty() && afterTheCheapRung[0] == StartupScanStep::PROBE_PRESENCE,
                             "the step after port 0's cheap rung was not the poll that opens its window");
}

/**
 * The device port 0's window finds at 0x71 - the address the sensor stored on port 1, answering its
 * ping, holds as its own port's - does not take the address that would part the two. It is not kept:
 * its row goes, and port 0 is taken down before the window gives port 1 back. The sweep is kept off
 * port 0 for as long as its list may still carry the row, and not longer; the cheap passes after the
 * restart's leave port 0 dark; the pass a ScanSensors command on port 0 asks for gives it its power,
 * and the cheap passes after that one leave it dark again.
 */
void test_startup_a_device_that_refuses_the_parting_write_is_not_kept_and_its_port_stays_dark(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.sensorReader.writeSensorAddressAnswerByUid["AA:BB:CC:DD:EE:FF.00"] = false;
    module.boot();
    module.skipTheBootSettleWindow();

    std::vector<StartupScanStep::Kind> afterTheRefusal;
    bool refused = false;
    bool rowGoneAtTheRefusal = false;
    bool recordGoneAtTheRefusal = false;
    bool readingsGoneAtTheRefusal = false;
    bool read = false;
    bool darkAfterTheTakeDown = false;
    bool keptOffAfterTheTakeDown = false;
    unsigned long takenDownAtMs = 0;
    unsigned long nowMs = module.nowMs();
    for (int tick = 0; tick < 600 && module.startupScan.isRunning(); tick++)
    {
        nowMs += 1000;
        module.arduino.setMillis(nowMs);
        StartupScanStep step;
        while (module.startupScan.takeNextStep(step))
        {
            const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
            if (refused)
            {
                afterTheRefusal.push_back(step.kind);
            }
            if (step.kind == StartupScanStep::RELEASE_FREE_PORTS && step.pin == 0)
            {
                darkAfterTheTakeDown = !module.sensorReader.isPortPowered(0);
                keptOffAfterTheTakeDown = module.startupScan.keepsThePortDark(0);
                takenDownAtMs = nowMs;
            }
            module.startupScan.applyStepOutcome(outcome);
            if (!read && module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00"))
            {
                // What the sweep leaves of a read of the adopted row.
                read = true;
                module.sensorValueModel.setSensorValue("AA:BB:CC:DD:EE:FF.00", 21.5f);
                module.sensorValueModel.setLastReadTime("AA:BB:CC:DD:EE:FF.00", (uint32_t)nowMs);
            }
            if (step.kind == StartupScanStep::WRITE_ADDRESS && !outcome.answered)
            {
                refused = true;
                rowGoneAtTheRefusal = !module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00");
                recordGoneAtTheRefusal = !aRecordOfThePinIn(module.moduleStructure, 0);
                readingsGoneAtTheRefusal = !module.sensorValueModel.hasSensorValue("AA:BB:CC:DD:EE:FF.00") &&
                                           module.sensorValueModel.getLastReadTime("AA:BB:CC:DD:EE:FF.00") == 0;
            }
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(refused, "no write was refused - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(rowGoneAtTheRefusal, "the row of the device that refused the parting write was kept");
    TEST_ASSERT_TRUE_MESSAGE(recordGoneAtTheRefusal,
                             "the structure the module holds still describes the device that was not kept");
    TEST_ASSERT_TRUE_MESSAGE(read, "the adopted row was never there to be read - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(readingsGoneAtTheRefusal, "the readings of the device that was not kept stayed under its uid");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.startupScan.factoryAddressMoveRefusedCount(), "the refusal was not counted once");
    TEST_ASSERT_TRUE_MESSAGE(afterTheRefusal.size() >= 2 && afterTheRefusal[0] == StartupScanStep::RELEASE_FREE_PORTS &&
                                 afterTheRefusal[1] == StartupScanStep::RELEASE_LINE,
                             "port 0 was not taken down before the window gave port 1 back");
    TEST_ASSERT_TRUE_MESSAGE(darkAfterTheTakeDown, "the step that takes port 0 down left it with its power");
    TEST_ASSERT_TRUE_MESSAGE(keptOffAfterTheTakeDown, "the sweep was let at port 0 the moment it was taken down");
    module.arduino.setMillis(takenDownAtMs + StartupSensorScanUseCase::THE_SWEEPS_LIST_LAGS_THE_REGISTRY_BY_MS - 1000);
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.keepsThePortDark(0),
                             "the sweep was let at port 0 while its list may still carry the row that went");
    module.arduino.setMillis(takenDownAtMs + StartupSensorScanUseCase::THE_SWEEPS_LIST_LAGS_THE_REGISTRY_BY_MS + 1000);
    TEST_ASSERT_FALSE_MESSAGE(module.startupScan.keepsThePortDark(0),
                              "port 0 is still kept from the sweep after its list has caught up with the registry");

    const size_t callsAfterThePass = module.sensorReader.portPowerCalls.size();
    const uint32_t passesBefore = module.startupScan.freePortPassCount();
    module.runFor(600000);
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() > passesBefore,
                             "no pass ran after the restart's - nothing to measure");
    TEST_ASSERT_FALSE_MESSAGE(poweredSince(module.sensorReader, callsAfterThePass, 0),
                              "a pass after the restart's gave port 0 its power back");

    module.startupScan.rescan(0);
    module.runFor(60000);
    TEST_ASSERT_TRUE_MESSAGE(poweredSince(module.sensorReader, callsAfterThePass, 0),
                             "the pass a ScanSensors command asked for on port 0 did not give it its power");
    module.settle();
    const size_t callsAfterTheCommandsPass = module.sensorReader.portPowerCalls.size();
    const uint32_t passesBeforeTheRest = module.startupScan.freePortPassCount();
    module.runFor(600000);
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.freePortPassCount() > passesBeforeTheRest,
                             "no cheap pass ran after the command's - nothing to measure");
    TEST_ASSERT_FALSE_MESSAGE(poweredSince(module.sensorReader, callsAfterTheCommandsPass, 0),
                              "a cheap pass after the command's gave port 0 its power back");
}

/**
 * Neither device holds the address of its own port: the row stored on port 1 answers its ping at 0x72,
 * and port 0's window finds a second device there. The window leaves the two to the address phase,
 * whose write is refused by whichever of them it is aimed at. The device the pass found is not kept, and
 * the step after the refused write takes its port down - in the address phase, before the preparation,
 * with the sweep kept off the port while its list may still carry the row.
 */
void test_startup_a_device_the_address_phase_cannot_part_is_not_kept_and_its_port_is_taken_down(void)
{
    ModuleUnderTest module(true);
    module.moduleConfig.setSensorPinCount(2);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 2, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.sensorReader.writeSensorAddressAnswerByUid["AA:BB:CC:DD:EE:FF.00"] = false;
    module.sensorReader.writeSensorAddressAnswerByUid["AA:BB:CC:DD:EE:FF.01"] = false;
    module.boot();
    module.skipTheBootSettleWindow();

    bool closed = false;
    bool refusedAfterTheWindow = false;
    bool rowGone = false;
    bool recordGone = false;
    bool readingsGone = false;
    bool read = false;
    bool keptOffAfterTheTakeDown = false;
    std::vector<StartupScanStep::Kind> afterTheRefusal;
    unsigned long nowMs = module.nowMs();
    for (int tick = 0; tick < 600 && module.startupScan.isRunning(); tick++)
    {
        nowMs += 1000;
        module.arduino.setMillis(nowMs);
        StartupScanStep step;
        while (module.startupScan.takeNextStep(step))
        {
            const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
            if (refusedAfterTheWindow)
            {
                afterTheRefusal.push_back(step.kind);
            }
            if (step.kind == StartupScanStep::RELEASE_FREE_PORTS && step.pin == 0 && refusedAfterTheWindow)
            {
                keptOffAfterTheTakeDown = module.startupScan.keepsThePortDark(0);
            }
            module.startupScan.applyStepOutcome(outcome);
            if (!read && module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00"))
            {
                // What the sweep leaves of a read of the adopted row.
                read = true;
                module.sensorValueModel.setSensorValue("AA:BB:CC:DD:EE:FF.00", 21.5f);
                module.sensorValueModel.setLastReadTime("AA:BB:CC:DD:EE:FF.00", (uint32_t)nowMs);
            }
            if (step.kind == StartupScanStep::RELEASE_LINE && step.pin == 0)
            {
                closed = true;
            }
            if (step.kind == StartupScanStep::WRITE_ADDRESS && !outcome.answered && closed && !refusedAfterTheWindow)
            {
                refusedAfterTheWindow = true;
                rowGone = !module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00");
                recordGone = !aRecordOfThePinIn(module.moduleStructure, 0);
                readingsGone = !module.sensorValueModel.hasSensorValue("AA:BB:CC:DD:EE:FF.00") &&
                               module.sensorValueModel.getLastReadTime("AA:BB:CC:DD:EE:FF.00") == 0;
            }
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(refusedAfterTheWindow, "no write after the window was refused - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(rowGone, "the row of the device the address phase could not part was kept");
    TEST_ASSERT_TRUE_MESSAGE(recordGone, "the structure the module holds still describes the device that was not kept");
    TEST_ASSERT_TRUE_MESSAGE(read, "the adopted row was never there to be read - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(readingsGone, "the readings of the device that was not kept stayed under its uid");
    TEST_ASSERT_TRUE_MESSAGE(afterTheRefusal.size() >= 2 && afterTheRefusal[0] == StartupScanStep::RELEASE_FREE_PORTS &&
                                 afterTheRefusal[1] == StartupScanStep::PREPARE_KNOWN,
                             "port 0 was not taken down between the refused write and the preparation");
    TEST_ASSERT_TRUE_MESSAGE(keptOffAfterTheTakeDown, "the sweep was let at port 0 the moment it was taken down");
    TEST_ASSERT_TRUE_MESSAGE((bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.01"), "the stored row went");
}

/**
 * The same refusal beside a stored row that was silent to its ping: nothing then says a second device is
 * on port 1 at all - the device at 0x71 on port 0, which the cheap rung found on the live wire, may well
 * be that row's own, moved. It is kept, and its port keeps its power.
 */
void test_startup_a_device_that_refuses_the_parting_write_beside_a_silent_row_is_kept(void)
{
    ModuleUnderTest module;
    module.moduleConfig.setSensorPinCount(2);
    module.startupScan.setPortLines(twoPortsOnOneLine());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN + 1, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 1);
    module.sensorReader.writeSensorAddressAnswerByUid["AA:BB:CC:DD:EE:FF.00"] = false;
    module.boot();

    bool refused = false;
    int takeDowns = 0;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::VERIFY_KNOWN)
        {
            // The line said nothing to the ping of port 1's row.
            outcome = StartupScanOutcome();
        }
        if (step.kind == StartupScanStep::RELEASE_FREE_PORTS && step.pin >= 0)
        {
            takeDowns++;
        }
        refused = refused || (step.kind == StartupScanStep::WRITE_ADDRESS && !outcome.answered);
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(refused, "no write was refused - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE((bool)module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00"),
                             "the row of a device beside a row silent to its ping was dropped");
    TEST_ASSERT_EQUAL_MESSAGE(0, takeDowns, "the port of a device beside a row silent to its ping was taken down");
}

/**
 * The other way round: the sensor stored on port 1 answers its ping at 0x70 - port 0's own address -
 * and port 0's window finds a second device there. The address stays with the device of the port it
 * belongs to and the stored sensor is moved, by the step right after the one that closes the window -
 * not inside it, whose to keep port 1 dark, and not at the end of the pass.
 */
void test_startup_a_window_find_at_its_own_ports_address_moves_its_wires_stored_row_once_the_window_closes(void)
{
    ModuleUnderTest module;
    module.startupScan.setPortLines(twoPairsOfPorts());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN);
    module.boot();

    bool found = false;
    bool closed = false;
    std::vector<StartupScanStep::Kind> afterTheFind;
    std::vector<StartupScanStep::Kind> afterTheClose;
    std::vector<StartupScanStep> writes;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.pin >= 2)
        {
            // Nothing on the other wire.
            outcome = StartupScanOutcome();
        }
        if (found && afterTheFind.empty())
        {
            afterTheFind.push_back(step.kind);
        }
        if (closed && afterTheClose.empty())
        {
            afterTheClose.push_back(step.kind);
        }
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            writes.push_back(step);
        }
        if (step.kind == StartupScanStep::PROBE_RANGE && step.pin == 0 && outcome.answeredModel >= 0)
        {
            found = true;
        }
        if (step.kind == StartupScanStep::RELEASE_LINE && step.pin == 0)
        {
            closed = true;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(found, "the device at 0x70 on port 0 was not found by its window");
    TEST_ASSERT_TRUE_MESSAGE(!afterTheFind.empty() && afterTheFind[0] == StartupScanStep::RELEASE_LINE,
                             "the window did not close straight after its find");
    TEST_ASSERT_TRUE_MESSAGE(!afterTheClose.empty() && afterTheClose[0] == StartupScanStep::WRITE_ADDRESS,
                             "the step after the window's close was not the write that moves the stored row");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)writes.size(), "not exactly one address write in the pass");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("AA:BB:CC:DD:EE:FF.01", writes[0].entity.uid.c_str(),
                                     "the write was not aimed at the stored row");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN + 1, writes[0].targetAddress,
                              "the stored row was not moved onto its own port's address");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_ADDRESS_MIN,
                              module.sensorRepository.get("AA:BB:CC:DD:EE:FF.00").value().modbusDeviceAddress,
                              "the device found at its own port's address does not keep it");
}

/**
 * The same, with the stored row silent to its ping: nothing then says its device is on port 1 at all,
 * and the cheap rung finds the device of port 0 at 0x70 on the live wire. Nothing is moved before the
 * address phase - port 2 is asked first - which is where a row nothing answered for is written to.
 */
void test_startup_a_find_at_its_own_ports_address_leaves_a_silent_stored_row_to_the_address_phase(void)
{
    ModuleUnderTest module;
    module.startupScan.setPortLines(twoPairsOfPorts());
    module.sensorTypeRepository.addSensorType(makeModbusType("type-1", "TEMP", 1));
    module.startupScan.setRawBusAccess(&module.rawBus); // silent to the wildcard
    module.sensorRepository.sensors.push_back(
        makeStoredEntity("AA:BB:CC:DD:EE:FF.01", 1, MODBUS_ADDRESS_MIN, SENSOR_STATUS_NEW));
    module.sensorReader.readAnsweringAddresses.push_back(MODBUS_ADDRESS_MIN);
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN);
    module.boot();

    bool found = false;
    bool port2AskedBeforeAnyWrite = false;
    bool written = false;
    StartupScanStep step;
    for (int guard = 0; module.startupScan.takeNextStep(step); guard++)
    {
        TEST_ASSERT_TRUE_MESSAGE(guard < 1000, "the discovery did not terminate");
        StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
        if (step.kind == StartupScanStep::VERIFY_KNOWN || step.pin >= 2)
        {
            // The line said nothing to the ping of port 1's row, and nothing on the other wire.
            outcome = StartupScanOutcome();
        }
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 0 && outcome.answered)
        {
            found = true;
        }
        if (step.kind == StartupScanStep::PROBE_CANDIDATE && step.pin == 2 && found && !written)
        {
            port2AskedBeforeAnyWrite = true;
        }
        if (step.kind == StartupScanStep::WRITE_ADDRESS)
        {
            written = true;
        }
        module.startupScan.applyStepOutcome(outcome);
    }

    TEST_ASSERT_TRUE_MESSAGE(found, "the device at 0x70 on port 0 was not found by the cheap rung");
    TEST_ASSERT_TRUE_MESSAGE(written, "the address phase did not part the two rows");
    TEST_ASSERT_TRUE_MESSAGE(port2AskedBeforeAnyWrite,
                             "a row silent to its ping was written to before the rest of the pass was asked");
}

extern "C" int run_startup_sensor_scan_tests(void)
{
    RUN_TEST(test_startup_the_structure_fill_makes_no_bus_transaction);
    RUN_TEST(test_startup_the_stored_sensors_are_in_the_structure_before_any_scan);
    RUN_TEST(test_startup_no_scan_sensors_leaves_the_discovery_unarmed);
    RUN_TEST(test_startup_no_scan_sensors_still_prepares_the_stored_sensors);
    RUN_TEST(test_startup_no_scan_sensors_on_a_module_with_no_sensor_arms_the_discovery);
    RUN_TEST(test_startup_no_boot_is_a_scan_entry);
    RUN_TEST(test_startup_a_normal_boot_arms_the_discovery);
    RUN_TEST(test_startup_only_one_step_is_offered_until_its_answer_comes_back);
    RUN_TEST(test_startup_a_candidate_step_probes_the_addresses_the_boot_scan_probed);
    RUN_TEST(test_startup_a_detected_sensor_is_adopted_on_the_spot);
    RUN_TEST(test_startup_a_port_that_answered_nothing_gets_an_empty_record);
    RUN_TEST(test_startup_a_silent_added_sensor_stays_in_the_structure);
    RUN_TEST(test_startup_a_silent_new_sensor_survives_the_discovery);
    RUN_TEST(test_startup_a_silent_stored_sensor_is_described_once);
    RUN_TEST(test_startup_an_adopted_sensor_is_republished);
    RUN_TEST(test_startup_the_finished_discovery_lets_the_catalog_go);
    RUN_TEST(test_startup_a_preparation_only_boot_holds_nothing_afterwards);
    RUN_TEST(test_startup_a_port_claimed_while_its_step_is_on_the_line_keeps_its_row);
    RUN_TEST(test_startup_a_port_claimed_between_steps_is_not_walked_further);
    RUN_TEST(test_startup_a_port_a_person_claimed_mid_pass_is_said_to_have_been_left_alone);
    RUN_TEST(test_startup_a_step_that_waited_is_dropped_when_its_port_is_claimed);
    RUN_TEST(test_startup_a_claimed_port_is_not_reported_as_empty);
    RUN_TEST(test_startup_a_stored_sensor_that_answers_is_reported_by_its_type);
    RUN_TEST(test_startup_a_stored_sensor_that_stays_silent_becomes_an_empty_port);
    RUN_TEST(test_startup_the_settled_structure_is_sent_even_when_nothing_was_adopted);
    RUN_TEST(test_startup_a_no_scan_boot_sends_no_second_structure);
    RUN_TEST(test_startup_a_no_scan_boot_reports_the_registry_as_it_stands);
    RUN_TEST(test_startup_a_sensor_accepted_while_its_ping_ran_is_described_as_itself);
    RUN_TEST(test_startup_the_entity_on_its_own_port_address_keeps_it);
    RUN_TEST(test_startup_a_collision_with_no_owner_keeps_the_first_entity);
    RUN_TEST(test_startup_a_refused_address_write_leaves_the_registry_alone);
    RUN_TEST(test_startup_an_address_taken_mid_phase_is_not_handed_out_again);
    RUN_TEST(test_startup_a_republish_describes_stored_sensors_the_same_way);
    RUN_TEST(test_startup_a_republish_keeps_the_type_of_a_sensor_that_answered);
    RUN_TEST(test_startup_the_preparation_is_the_last_step_and_runs_once);
    RUN_TEST(test_startup_a_free_port_is_asked_cheaply_before_it_is_identified);
    RUN_TEST(test_startup_a_port_that_answers_nothing_never_reaches_the_identification);
    RUN_TEST(test_startup_a_device_at_its_factory_address_is_adopted_and_then_moved);
    RUN_TEST(test_startup_a_cheap_probe_alone_adopts_nothing);
    RUN_TEST(test_startup_no_scan_sensors_stops_the_periodic_probing_too);
    RUN_TEST(test_startup_the_periodic_pass_is_only_the_cheap_rung);
    RUN_TEST(test_startup_only_a_hand_asked_pass_is_a_scan_entry);
    RUN_TEST(test_startup_an_unidentifiable_port_is_given_up_on_until_a_rescan);
    RUN_TEST(test_startup_a_hand_asked_pass_calls_a_silent_port_empty_not_unnameable);
    RUN_TEST(test_startup_a_hand_asked_pass_asks_the_port_it_names_and_no_other);
    RUN_TEST(test_startup_a_port_whose_occupant_left_returns_to_the_cheap_rung);
    RUN_TEST(test_startup_the_presence_poll_takes_the_paired_port_down_and_gives_it_back);
    RUN_TEST(test_startup_a_poll_outside_a_window_keeps_the_sweep_off_the_port_it_gave_back);
    RUN_TEST(test_startup_a_free_port_is_powered_for_the_pass_and_dark_between_two);
    RUN_TEST(test_startup_the_ports_are_given_time_to_come_up_before_they_are_asked);
    RUN_TEST(test_startup_a_port_given_up_on_is_not_polled_while_its_wire_carries_a_sensor);
    RUN_TEST(test_startup_a_sensor_put_where_the_module_gave_up_is_found_by_a_cheap_pass);
    RUN_TEST(test_startup_a_row_still_on_its_factory_address_is_moved_once_the_sweep_reads_it);
    RUN_TEST(test_startup_a_row_the_sweep_has_not_read_is_not_written_to);
    RUN_TEST(test_startup_a_row_the_sweep_has_lost_is_not_written_to);
    RUN_TEST(test_startup_a_device_that_will_not_move_is_written_to_once);
    RUN_TEST(test_startup_a_write_into_a_port_with_nothing_on_it_is_not_a_refusal);
    RUN_TEST(test_startup_a_row_added_after_the_restart_is_not_noted_by_a_write_it_never_answered);
    RUN_TEST(test_startup_a_row_of_an_unknown_model_is_left_where_it_is);
    RUN_TEST(test_startup_a_row_both_halves_of_a_pass_owe_a_move_is_written_to_once);
    RUN_TEST(test_startup_a_refused_row_is_not_queued_again_by_the_port_it_answers_for);
    RUN_TEST(test_startup_a_row_the_sweep_is_reading_is_noted_by_a_write_no_pass_chose);
    RUN_TEST(test_startup_a_row_the_sweep_has_lost_is_not_noted_by_a_write_no_pass_chose);
    RUN_TEST(test_startup_the_first_pass_waits_out_the_boot_minute);
    RUN_TEST(test_startup_a_pass_a_person_asked_for_does_not_wait_out_the_boot_minute);
    RUN_TEST(test_startup_the_periodic_pass_waits_out_the_boot_minute_too);
    RUN_TEST(test_startup_the_boot_minute_and_the_first_pass_do_not_peak_together);
    RUN_TEST(test_startup_a_quiet_hour_of_periodic_passes_says_nothing);
    RUN_TEST(test_startup_a_sensor_plugged_into_a_quiet_module_is_said);
    RUN_TEST(test_startup_the_tape_a_plugged_in_sensor_leaves);
    RUN_TEST(test_startup_the_passes_a_person_waits_for_still_say_what_they_are_doing);
    RUN_TEST(test_startup_the_line_a_pass_ends_with_counts_that_pass_only);
    RUN_TEST(test_startup_the_line_a_commanded_pass_ends_with_counts_that_pass_only);
    RUN_TEST(test_startup_the_counters_line_carries_the_sensors_this_module_adopted);
    RUN_TEST(test_startup_the_counters_line_carries_a_device_that_would_not_move);
    RUN_TEST(test_startup_the_counters_line_carries_the_rationed_identification_rung);
    RUN_TEST(test_startup_a_commanded_pass_claims_no_adoption_of_an_earlier_one);
    RUN_TEST(test_startup_a_commanded_pass_that_found_nothing_sends_no_structure);
    RUN_TEST(test_startup_the_restarts_cheap_rung_finds_a_device_in_the_range_without_a_window);
    RUN_TEST(test_startup_an_address_the_paired_port_holds_is_left_to_the_window);
    RUN_TEST(test_startup_an_address_held_by_a_row_silent_to_its_ping_is_asked_on_the_live_wire);
    RUN_TEST(test_startup_a_device_adopted_at_its_wires_row_address_is_moved_by_the_next_step);
    RUN_TEST(test_startup_a_device_adopted_at_its_own_ports_address_is_not_moved_off_it);
    RUN_TEST(test_startup_a_device_the_window_finds_at_its_wires_row_address_is_moved_inside_the_window);
    RUN_TEST(test_startup_a_device_identified_at_its_wires_row_address_is_moved_by_the_next_step);
    RUN_TEST(test_startup_a_window_find_at_an_address_its_wires_row_does_not_own_is_left_to_the_address_phase);
    RUN_TEST(test_startup_the_restarts_window_writes_the_address_into_a_silent_port);
    RUN_TEST(test_startup_a_port_that_answered_the_poll_is_left_to_the_identification_rung);
    RUN_TEST(test_startup_no_pass_after_the_restart_opens_a_window);
    RUN_TEST(test_startup_the_next_port_gets_its_power_back_before_it_is_asked);
    RUN_TEST(test_startup_a_window_is_one_model_per_step_and_the_wire_dark_across_them);
    RUN_TEST(test_startup_a_window_on_a_claimed_port_is_closed_before_the_walk_moves_on);
    RUN_TEST(test_startup_the_restarts_pass_asks_every_free_port_cheaply_before_any_window);
    RUN_TEST(test_startup_the_port_sharing_a_wire_with_a_find_is_asked_cheaply_again_before_its_window);
    RUN_TEST(test_startup_a_port_asked_cheaply_again_that_stays_silent_gets_its_window);
    RUN_TEST(test_startup_a_pass_after_the_restart_still_asks_one_port_at_a_time);
    RUN_TEST(test_startup_the_restart_sends_a_structure_for_each_sensor_as_it_adopts_it);
    RUN_TEST(test_startup_a_structure_asked_for_while_one_is_on_its_way_is_owed_and_goes_after_it);
    RUN_TEST(test_startup_the_liveness_review_waits_for_a_structure_on_its_way);
    RUN_TEST(test_startup_the_silence_the_restarts_pass_caused_is_not_counted);
    RUN_TEST(test_startup_a_single_missed_sweep_gives_no_row_up_at_any_uptime);
    RUN_TEST(test_startup_a_row_silent_for_four_sweeps_is_given_up_at_any_uptime);
    RUN_TEST(test_startup_a_port_the_cheap_rung_could_not_ask_its_wires_sensors_address_is_dark_until_its_walk);
    RUN_TEST(test_startup_a_port_that_will_leave_an_address_out_is_asked_the_cheap_rung_first);
    RUN_TEST(test_startup_a_port_beside_a_row_silent_to_its_ping_is_asked_the_cheap_rung_in_its_turn);
    RUN_TEST(test_startup_a_port_whose_wires_sensor_was_silent_to_its_ping_is_not_taken_down);
    RUN_TEST(test_startup_a_port_whose_window_comes_next_is_not_taken_down);
    RUN_TEST(test_startup_a_device_that_refuses_the_parting_write_is_not_kept_and_its_port_stays_dark);
    RUN_TEST(test_startup_a_device_that_refuses_the_parting_write_beside_a_silent_row_is_kept);
    RUN_TEST(test_startup_a_device_the_address_phase_cannot_part_is_not_kept_and_its_port_is_taken_down);
    RUN_TEST(test_startup_a_window_find_at_its_own_ports_address_moves_its_wires_stored_row_once_the_window_closes);
    RUN_TEST(test_startup_a_find_at_its_own_ports_address_leaves_a_silent_stored_row_to_the_address_phase);
    return 0;
}
