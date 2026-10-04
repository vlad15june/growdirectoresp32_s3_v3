// What it costs to read the sensor type catalog.
//
// The catalog is the largest single thing the module holds: 1.4 to 2 KB per sensor type, so a
// twelve-type stand is ~16 KB and a field account with forty is around 55 KB - against a board
// that has tens of KB of heap free at the moment boot and scanning run. Every reader that took the
// catalog by value doubled it for as long as it read, and boot and scanning have seven such
// readers between them: three questions about how many types are held, the hydro pass, the
// per-pin scan of the setup service, and the three commands (PingPin, SetSensorModel,
// ScanSensors) that snapshot candidates for the acquisition task. A scan on four empty pins made
// four whole catalogs, one after another; a ScanSensors command made three at once - one out of
// the repository, one into the command's work, one into the bus queue.
//
// So the figure this file is about is not "how much does a reader allocate" - it is "does what a
// reader needs follow the size of the account's catalog". A cost that grows with the catalog is a
// module that works on a twelve-type stand and runs out of heap on a forty-type account; a cost
// that is one sensor type plus a constant is the same boot at any catalog size.
//
// Measured with Sim/HostAllocationCounter.h, which counts every operator new the test binary makes
// and samples the live balance at each of them. Every scenario is measured twice - at twelve types
// and at forty-eight - and the verdict is on the difference between the two, so no assertion here
// rests on a magic number: the plank is the weight of one sensor type as this module holds it,
// computed from the same two catalogs the scenario runs on.
//
// What runs is production code: the real SensorTypeRepository over the real reader path of
// ModuleSetupService and the real ModuleCommandExecutionUseCase, with the work handed to the real
// SensorAcquisitionService queue. Replaced are flash (a map of strings), the RS-485 reader, MQTT
// and time.
//
// To see these red: give any reader back its own copy of the catalog - `auto types =
// sensorTypeRepository->getAll()` in place of a snapshot, a by-value binding of `*catalog`, or a
// catalogSnapshot() that hands out a fresh list - and what the pass costs starts following the
// catalog size again, by thirty-six sensor types between the two.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/ModuleSetupService.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

#include "Mock/Application/MockOutbox.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"

#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorTypeDao.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostAllocationCounter.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

/** @brief Reaches the boot readers, which are private to the setup service. */
class CatalogReaderTestHelper {
public:
    static void fillStructure(ModuleSetupService& service) { service.fillModuleStructure(); }
};

namespace {

using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::VirtualClock;

/** The catalog of a stand. */
const size_t SMALL_CATALOG = 12;

/** And of a field account, at the top of the range the accounts in production hold. */
const size_t LARGE_CATALOG = 48;

const char* const MODULE_UID = "AA:BB:CC:DD:EE:FF";
const int MODULE_PIN_COUNT = 4;

/**
 * How many times an empty pin is probed for one sensor type: at the per-port address the scan
 * assigns, at the type's own default address, and then across the module's address range.
 */
const int PROBES_PER_TYPE = 2 + (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1);

// --- the catalog the measurements run on ----------------------------------------------------

/**
 * @brief A sensor type of the weight a live catalog entry has: two channels, ~1.8 KB of JSON.
 */
SensorType makeSensorType(size_t index)
{
    const std::string name = "sensor_model_" + std::to_string(index);

    SensorType sensorType;
    // Backend uids are GUID-sized, well past the 15-character small-string buffer of std::string
    // on this toolchain: a short uid would be copied without allocating and would quietly make a
    // red measurement green without anything being fixed.
    sensorType.uid = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b" + std::to_string(400 + index);
    sensorType.name = name;
    sensorType.displayName = name + " display name for the module structure";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.baudRate = 9600;
    sensorType.modbusDeviceAddress = 1;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = "2026-02-11T09:01:35.443520+00:00";

    for (int channel = 1; channel <= 2; channel++)
    {
        SubSensorType subType;
        subType.name = name + "_channel_" + std::to_string(channel);
        subType.displayName = name + " channel " + std::to_string(channel);
        subType.minValue = 0.0f;
        subType.maxValue = 1000.0f;
        subType.limitMinValue = 0.0f;
        subType.limitMaxValue = 1000.0f;
        subType.unit = "unit_" + std::to_string(channel);
        subType.createdAt = "2025-09-22T07:04:26.803128+00:00";
        subType.updatedAt = "2026-02-11T09:01:35.443520+00:00";
        sensorType.sensorTypes.push_back(subType);

        ModbusRegisterConfig config;
        config.functionCode = 3;
        config.startRegister = channel;
        config.numRegisters = 2;
        config.multiplier = 0.01f;
        config.channel = channel;
        config.channelName = name + "_channel_" + std::to_string(channel);
        config.sensorType = name + "_channel_" + std::to_string(channel);
        config.canConnectMinValue = 0.0f;
        config.canConnectMaxValue = 1000.0f;
        sensorType.modbusRegisterConfig.push_back(config);
    }

    return sensorType;
}

/**
 * @brief The same type as an analog hydro model: what the hydro pass picks out of the catalog.
 */
SensorType makeHydroSensorType(size_t index)
{
    SensorType sensorType = makeSensorType(index);
    sensorType.protocol = SensorProtocol::ANALOG_PH_PROTOCOL;

    AnalogPinConfig pinConfig;
    pinConfig.pin = (uint8_t)(1 + (index % MODULE_PIN_COUNT));
    pinConfig.multiplier = 1.0f;
    pinConfig.channel = 1;
    pinConfig.channelName = sensorType.name + "_ph";
    pinConfig.sensorType = "ph";
    sensorType.analogPinConfig.push_back(pinConfig);

    return sensorType;
}

// --- measuring --------------------------------------------------------------------------------

/**
 * @brief What a measured step cost the module above the state it started from.
 *
 * `before` is the live balance the step starts at - the module with whatever it already holds,
 * catalog included, which is why the figure is a difference and never read on its own. `peak` is
 * the highest the balance stood inside the step, `after` where it ended.
 *
 * The cost counts both: a copy the step throws away and a copy it hands on are the same wall to a
 * board that has to have the memory free while the step runs, and the scan that a prepared command
 * hands to the acquisition task holds its candidates for the whole sweep.
 */
struct Transient {
    long long before = 0;
    long long after = 0;
    long long peak = 0;

    long long cost() const
    {
        const long long top = (peak > after) ? peak : after;
        return top > before ? top - before : 0;
    }
};

void beginWindow(Transient& t)
{
    t.before = HostAllocationCounter::liveBytes();
    HostAllocationCounter::resetPeak();
}

void endWindow(Transient& t)
{
    t.after = HostAllocationCounter::liveBytes();
    t.peak = HostAllocationCounter::peakBytes();
}

/**
 * @brief The whole module a boot or a scan runs against, at one catalog size.
 *
 * Everything the readers touch is the production object; what is replaced is flash (the DAO holds
 * types in a vector), the RS-485 reader, MQTT, the clock and the Arduino layer.
 */
struct ModuleUnderTest {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockWifiAdapter wifiAdapter;
    MockMqttManager mqttManager;
    MockSensorReader sensorReader;
    MockSensorRepository sensorRepository;
    MockNTPTimeAdapter timeAdapter;
    MockSpiffsSensorTypeDao sensorTypeDao;
    SensorTypeRepository sensorTypeRepository;
    MockHybridAPIService apiService;
    MockOutbox outbox;
    ModuleStructure moduleStructure;

    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;

    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    ModbusAddressAllocator modbusAddressAllocator;
    ModuleCommandExecutionUseCase moduleCommandUseCase;
    SensorAcquisitionService acquisition;
    SyncSensorTypesUseCase syncSensorTypes;
    StartupSensorScanUseCase startupScan;

    ModuleSetupService setupService;

    /**
     * @brief Run the whole startup discovery here and now.
     *
     * The two halves in the order the module runs them - the loop plans a step, the bus task
     * executes it, the loop applies the answer - with the queue and the tasks left out: what is
     * measured is what the readers allocate, not how the steps are scheduled.
     */
    void runStartupScan()
    {
        // The pass gives the free ports it switches on the seconds they need to come up, and has
        // nothing to hand out while that lasts: the clock moves whenever it runs dry.
        for (int round = 0; round < 8; round++)
        {
            StartupScanStep step;
            while (startupScan.takeNextStep(step))
            {
                startupScan.applyStepOutcome(startupScan.runStepOnBus(step));
            }
            if (!startupScan.isRunning())
            {
                return;
            }
            clock.advanceMs(StartupSensorScanUseCase::PORT_WAKE_UP_MS);
        }
    }

    enum CatalogFlavour { MODBUS_CATALOG, HYDRO_CATALOG };

    explicit ModuleUnderTest(size_t catalogSize, CatalogFlavour flavour = MODBUS_CATALOG)
        : moduleConfig(&logger, &debugSystem, &settings),
          sensorTypeRepository(&sensorTypeDao),
          scheduler(clock),
          arduino(clock, &scheduler),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels),
          modbusAddressAllocator(&sensorRepository),
          moduleCommandUseCase(&logger, &debugSystem, &timeAdapter, nullptr, &arduino,
                               &sensorRepository, &sensorTypeRepository, &sensorReader,
                               &moduleConfig, &settings, nullptr, nullptr,
                               &moduleStructure, nullptr, nullptr, nullptr,
                               nullptr, nullptr, nullptr, nullptr,
                               &modbusAddressAllocator),
          acquisition(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          syncSensorTypes(&apiService, &sensorTypeRepository, &arduino, &timeAdapter, nullptr,
                          &logger, &debugSystem, &sensorRepository, &moduleConfig),
          startupScan(&logger, &debugSystem, &moduleConfig, &sensorRepository,
                      &sensorTypeRepository, &sensorReader, &moduleStructure, &sensorStructure,
                      &modbusAddressAllocator),
          setupService(&logger, &debugSystem, &moduleConfig,
                       nullptr, nullptr, nullptr, nullptr,
                       &wifiAdapter, nullptr, nullptr, nullptr, &mqttManager,
                       &moduleStructure, nullptr, nullptr,
                       &sensorRepository, &sensorTypeRepository, &sensorReader)
    {
        // The logging of the readers is off: what is measured is the reading, and a debug build
        // that formats every sensor type into a line measures the log instead.
        debugSystem.featureEnabled = false;

        settings.setUser("catalog@example.com");
        settings.setAccountUid("catalog-account");
        settings.setNoScanSensors(false);
        moduleConfig.setSettings(&settings);
        moduleConfig.setWifiAdapter(&wifiAdapter);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
        moduleConfig.setSensorPinCount(MODULE_PIN_COUNT);
        wifiAdapter.setMACAddress(MODULE_UID);
        wifiAdapter.setLocalIP("192.168.1.100");

        // Nothing answers on the line: every pin stays empty, so the pin scan walks the whole
        // catalog on every one of them - the pass this file is about.
        sensorReader.scanSensorReturnValue = false;
        sensorReader.readSensorReturnValue = false;

        setupService.setSensorStructureUseCase(&sensorStructure);
        setupService.setStartupSensorScan(&startupScan);
        moduleCommandUseCase.setSensorStructureUseCase(&sensorStructure);

        // The recorder inside the fake reader grows with the number of probes, and it must not
        // be what a measured window pays for: its buffer is taken before any window opens.
        sensorReader.probedAddresses.reserve(
            (size_t)MODULE_PIN_COUNT * catalogSize * (size_t)PROBES_PER_TYPE * 2 + 64);

        for (size_t i = 0; i < catalogSize; i++)
        {
            sensorTypeDao.addToStorage(flavour == HYDRO_CATALOG ? makeHydroSensorType(i)
                                                                : makeSensorType(i));
        }
        // Loaded once here, so that no measured window pays for the read from flash.
        sensorTypeRepository.count();
    }

    ModuleCommandDto command(const char* name, const char* options) const
    {
        ModuleCommandDto dto;
        dto.uid = "command-uid-1";
        dto.command = name;
        dto.options = options;
        return dto;
    }

    /** @brief What the catalog of this module weighs, per type, as the repository holds it. */
    long long heldBytes()
    {
        const long long withCatalog = HostAllocationCounter::liveBytes();
        sensorTypeRepository.invalidateCache();
        const long long without = HostAllocationCounter::liveBytes();
        sensorTypeRepository.count();
        return withCatalog - without;
    }
};

/** @brief One sensor type as this module holds it, from the two catalogs in play. */
long long oneTypeBytes()
{
    ModuleUnderTest small(SMALL_CATALOG);
    ModuleUnderTest large(LARGE_CATALOG);
    const long long difference = large.heldBytes() - small.heldBytes();
    return difference / (long long)(LARGE_CATALOG - SMALL_CATALOG);
}

/**
 * @brief The verdict every scenario here ends in.
 *
 * A reader pass may need memory - it builds sensor entities, formats strings, fills the module
 * structure. What it must not need is memory that follows the catalog: between a twelve-type and a
 * forty-eight-type account the extra must stay under the weight of a single sensor type. A pass
 * that copies the catalog once is thirty-six types over that plank; one that copies it four times
 * is a hundred and forty-four.
 */
void assertDoesNotFollowCatalogSize(const char* what, const Transient& small, const Transient& large)
{
    const long long typeBytes = oneTypeBytes();
    TEST_ASSERT_GREATER_THAN_MESSAGE(200, typeBytes,
                                     "the stand's sensor type is too light for the plank to mean anything");

    const long long growth = large.cost() - small.cost();

    char message[320];
    snprintf(message, sizeof(message),
             "%s cost %lld bytes at %u types and %lld at %u - %lld more, which is %lld sensor "
             "types (%lld bytes each); a reader pass must not carry the catalog",
             what, small.cost(), (unsigned)SMALL_CATALOG, large.cost(),
             (unsigned)LARGE_CATALOG, growth, typeBytes > 0 ? growth / typeBytes : growth,
             typeBytes);

    TEST_ASSERT_LESS_THAN_MESSAGE(typeBytes, growth, message);
}

// --- the reader passes ------------------------------------------------------------------------

/**
 * @brief The boot pass and the discovery it arms: the count questions and one scan per empty pin.
 *
 * Both are measured in one window because they are one pass over the catalog split across two
 * tasks; what moved off the boot path is when the scan runs, not what it reads.
 */
Transient bootPass(size_t catalogSize)
{
    ModuleUnderTest module(catalogSize);
    Transient t;

    beginWindow(t);
    CatalogReaderTestHelper::fillStructure(module.setupService);
    module.runStartupScan();
    endWindow(t);

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.moduleStructure.nodes.size(),
                              "the boot pass did not build a node - the scenario did not happen");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, module.sensorReader.scanSensorCallCount,
                                     "no pin was scanned - the pin scan reader never ran");
    return t;
}

/** @brief The hydro pass: the analog models of the catalog, picked out of it by protocol. */
Transient hydroPass(size_t catalogSize)
{
    ModuleUnderTest module(catalogSize, ModuleUnderTest::HYDRO_CATALOG);
    module.moduleConfig.setModuleType(MODULE_TYPE_HYDRO);
    module.moduleConfig.setDeviceCount(2);
    Transient t;

    beginWindow(t);
    CatalogReaderTestHelper::fillStructure(module.setupService);
    endWindow(t);

    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.moduleStructure.nodes.size(),
                              "the hydro pass did not build a node - the scenario did not happen");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, module.sensorReader.scanSensorCallCount,
                                     "no analog pin was probed - the hydro reader never ran");
    return t;
}

/**
 * @brief A catalog writer of the older kind: it stores, and hands nothing back.
 *
 * takeStoredCatalog() is deliberately not overridden - the base answers false, which is exactly
 * the writer the fallback read in onCatalogWriteCompletion() exists for.
 */
class WriterThatHandsNothingBack : public ISensorTypeCatalogWriter {
public:
    uint32_t submit(std::vector<SensorType> catalog) override
    {
        submittedCount = catalog.size();
        return ++lastTicket;
    }

    bool drainResults(std::vector<CatalogWriteCompletion>& out) override
    {
        (void)out;
        return false;
    }

    uint32_t lastTicket = 0;
    size_t submittedCount = 0;
};

/** @brief The server answering with no sensor models: the module keeps what it holds. */
Transient emptyServerAnswerPass(size_t catalogSize)
{
    ModuleUnderTest module(catalogSize);
    module.apiService.sensorTypesListReturnValue.clear();
    module.apiService.getSensorTypesListHttpFailed = false;
    TEST_ASSERT_TRUE(module.syncSensorTypes.init());

    Transient t;
    beginWindow(t);
    const bool synced = module.syncSensorTypes.syncNow();
    endWindow(t);

    TEST_ASSERT_TRUE_MESSAGE(synced, "the empty answer was not applied - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.apiService.getSensorTypesListCalled,
                             "the server was never asked - the scenario did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(catalogSize, module.sensorTypeRepository.count(),
                              "the module dropped its catalog on an empty answer");
    return t;
}

/** @brief Collecting a finished catalog write from a writer that hands the types back. */
Transient catalogWriteCollectedPass(size_t catalogSize)
{
    ModuleUnderTest module(catalogSize);
    WriterThatHandsNothingBack writer;
    module.syncSensorTypes.setOutbox(&module.outbox);
    module.syncSensorTypes.setCatalogWriter(&writer);
    TEST_ASSERT_TRUE(module.syncSensorTypes.init());

    // The answer the module is about to store: the catalog it holds, with one type edited, so that
    // the collection step has a changed uid to follow and a reason to read the catalog at all.
    std::vector<SensorType> fromServer;
    for (size_t i = 0; i < catalogSize; i++)
    {
        SensorType type = makeSensorType(i);
        if (i == 0)
        {
            type.updatedAt = "2026-03-02T14:22:07.101010+00:00";
        }
        fromServer.push_back(type);
    }
    module.outbox.sensorTypesResponse = fromServer;
    module.outbox.sensorTypesResponseReady = true;

    // The production sequence: the fetch is asked for through the outbox, the answer comes back on
    // the control loop, the catalog goes to the storage task - and the write is collected here.
    module.syncSensorTypes.forceSync();
    module.syncSensorTypes.loop();
    // The outbox of this stand hands out tickets from one upwards and nothing else asks it for
    // one, so the fetch that was just accepted carries ticket 1 - asserted rather than assumed.
    TEST_ASSERT_EQUAL_MESSAGE(1, module.outbox.sensorTypesRequests,
                              "the fetch was not handed to the outbox - the scenario did not happen");
    OutboxCompletion fetched;
    fetched.kind = OutboxKind::SENSOR_TYPES_FETCH;
    fetched.ticket = 1;
    fetched.delivered = true;
    module.syncSensorTypes.onOutboxCompletion(fetched);
    TEST_ASSERT_EQUAL_MESSAGE(catalogSize, writer.submittedCount,
                              "the catalog never reached the storage task - the scenario did not happen");

    CatalogWriteCompletion written;
    written.ticket = writer.lastTicket;
    written.written = true;
    written.typeCount = catalogSize;
    written.bytes = catalogSize * 1400;

    Transient t;
    beginWindow(t);
    module.syncSensorTypes.onCatalogWriteCompletion(written);
    endWindow(t);

    TEST_ASSERT_EQUAL_MESSAGE(catalogSize, module.sensorTypeRepository.count(),
                              "the module lost its catalog while collecting the write");
    return t;
}

/** @brief PingPin: the scannable types of the catalog, snapshotted for the acquisition task. */
Transient pingPinPass(size_t catalogSize)
{
    ModuleUnderTest module(catalogSize);
    Transient t;

    beginWindow(t);
    ComplexCommandWork work;
    ModuleCommandExecutionResult immediate("", "");
    const bool prepared = module.moduleCommandUseCase.prepareComplexCommand(
        module.command("PingPin", "{\"pin\":1}"), work, immediate);
    const uint32_t ticket = module.acquisition.submitComplexBusCommand(work);
    endWindow(t);

    TEST_ASSERT_TRUE_MESSAGE(prepared, "PingPin was not prepared - the scenario did not happen");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ISensorAcquisition::kBusCommandRejected, ticket,
                                  "the bus queue refused the work - the hand-off did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(catalogSize, work.candidates().size(),
                              "the work does not carry the catalog it is supposed to scan from");
    return t;
}

/** @brief SetSensorModel: one named type out of the catalog. */
Transient setSensorModelPass(size_t catalogSize)
{
    ModuleUnderTest module(catalogSize);
    const std::string options =
        std::string("{\"pin\":1,\"uid\":\"") + makeSensorType(catalogSize - 1).uid + "\"}";
    Transient t;

    beginWindow(t);
    ComplexCommandWork work;
    ModuleCommandExecutionResult immediate("", "");
    const bool prepared = module.moduleCommandUseCase.prepareComplexCommand(
        module.command("SetSensorModel", options.c_str()), work, immediate);
    endWindow(t);

    TEST_ASSERT_TRUE_MESSAGE(prepared, "SetSensorModel was not prepared - the scenario did not happen");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(makeSensorType(catalogSize - 1).name.c_str(),
                                     work.sensorToVerify.model.c_str(),
                                     "the command did not resolve the model it names");
    return t;
}

/** @brief ScanSensors: the whole catalog, snapshotted and handed to the acquisition task. */
Transient scanSensorsPass(size_t catalogSize)
{
    ModuleUnderTest module(catalogSize);
    Transient t;

    beginWindow(t);
    ComplexCommandWork work;
    ModuleCommandExecutionResult immediate("", "");
    const bool prepared = module.moduleCommandUseCase.prepareComplexCommand(
        module.command("ScanSensors", "{\"pin\":1}"), work, immediate);
    const uint32_t ticket = module.acquisition.submitComplexBusCommand(work);
    endWindow(t);

    TEST_ASSERT_TRUE_MESSAGE(prepared, "ScanSensors was not prepared - the scenario did not happen");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ISensorAcquisition::kBusCommandRejected, ticket,
                                  "the bus queue refused the work - the hand-off did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(catalogSize, work.candidates().size(),
                              "the work does not carry the catalog it is supposed to scan from");
    return t;
}

/** @brief The second half of a ScanSensors that had to fetch the catalog first. */
Transient scanAfterFetchPass(size_t catalogSize)
{
    ModuleUnderTest module(catalogSize);
    Transient t;

    ComplexCommandWork fetched;
    fetched.command = module.command("ScanSensors", "{\"pin\":1}");
    fetched.fetchCatalog = true;
    fetched.pin = 1;

    beginWindow(t);
    ComplexCommandOutcome outcome;
    ModuleCommandExecutionResult result("", "");
    ComplexCommandWork nextWork;
    const bool finished = module.moduleCommandUseCase.finishComplexCommand(fetched, outcome, result,
                                                                           nextWork);
    endWindow(t);

    TEST_ASSERT_FALSE_MESSAGE(finished,
                              "the fetch stage did not hand back the scan - the scenario did not happen");
    TEST_ASSERT_EQUAL_MESSAGE(catalogSize, nextWork.candidates().size(),
                              "the next stage does not carry the catalog it is supposed to scan from");
    return t;
}

} // namespace

// --- the measurements ---------------------------------------------------------------------

void test_a_boot_pass_over_the_catalog_does_not_copy_it(void)
{
    const Transient small = bootPass(SMALL_CATALOG);
    const Transient large = bootPass(LARGE_CATALOG);
    assertDoesNotFollowCatalogSize("a boot pass with four empty pins", small, large);
}

void test_a_hydro_pass_over_the_catalog_does_not_copy_it(void)
{
    const Transient small = hydroPass(SMALL_CATALOG);
    const Transient large = hydroPass(LARGE_CATALOG);
    assertDoesNotFollowCatalogSize("a hydro pass picking analog models", small, large);
}

void test_an_empty_server_answer_does_not_copy_the_catalog_to_count_it(void)
{
    const Transient small = emptyServerAnswerPass(SMALL_CATALOG);
    const Transient large = emptyServerAnswerPass(LARGE_CATALOG);
    assertDoesNotFollowCatalogSize("a synchronization the server answered empty", small, large);
}

void test_collecting_a_catalog_write_does_not_copy_the_catalog_back_out(void)
{
    const Transient small = catalogWriteCollectedPass(SMALL_CATALOG);
    const Transient large = catalogWriteCollectedPass(LARGE_CATALOG);
    assertDoesNotFollowCatalogSize("collecting a write from a writer that hands nothing back",
                                   small, large);
}

void test_a_pin_ping_snapshots_the_catalog_without_copying_it(void)
{
    const Transient small = pingPinPass(SMALL_CATALOG);
    const Transient large = pingPinPass(LARGE_CATALOG);
    assertDoesNotFollowCatalogSize("PingPin prepared and queued for the bus", small, large);
}

void test_setting_a_sensor_model_reads_one_type_and_not_the_catalog(void)
{
    const Transient small = setSensorModelPass(SMALL_CATALOG);
    const Transient large = setSensorModelPass(LARGE_CATALOG);
    assertDoesNotFollowCatalogSize("SetSensorModel prepared", small, large);
}

void test_a_sensor_scan_snapshots_the_catalog_without_copying_it(void)
{
    const Transient small = scanSensorsPass(SMALL_CATALOG);
    const Transient large = scanSensorsPass(LARGE_CATALOG);
    assertDoesNotFollowCatalogSize("ScanSensors prepared and queued for the bus", small, large);
}

void test_a_scan_that_had_to_fetch_the_catalog_does_not_copy_it_afterwards(void)
{
    const Transient small = scanAfterFetchPass(SMALL_CATALOG);
    const Transient large = scanAfterFetchPass(LARGE_CATALOG);
    assertDoesNotFollowCatalogSize("the scan stage after a catalog fetch", small, large);
}

/**
 * Four pin scans in one boot are four reader passes. Each must not copy a whole catalog; if any one
 * of them does, the boot as a whole follows the catalog size, which is what the plank catches
 * - but the count of passes is worth asserting on its own, because it is what makes this a
 * multiplier rather than a one-off.
 */
void test_every_empty_pin_is_a_reader_pass_and_none_of_them_costs_a_catalog(void)
{
    ModuleUnderTest module(LARGE_CATALOG);

    Transient t;
    beginWindow(t);
    CatalogReaderTestHelper::fillStructure(module.setupService);
    module.runStartupScan();
    endWindow(t);

    // Every empty pin walks the whole catalog looking for something that answers.
    TEST_ASSERT_EQUAL_MESSAGE(MODULE_PIN_COUNT * (int)LARGE_CATALOG * PROBES_PER_TYPE,
                              module.sensorReader.scanSensorCallCount,
                              "the pin scan did not walk the catalog on every pin");

    ModuleUnderTest reference(LARGE_CATALOG);
    const long long catalogBytes = reference.heldBytes();

    char message[256];
    snprintf(message, sizeof(message),
             "a boot pass over %u pins cost %lld bytes above the state it started from, and one "
             "catalog weighs %lld",
             (unsigned)MODULE_PIN_COUNT, t.cost(), catalogBytes);
    TEST_ASSERT_LESS_THAN_MESSAGE(catalogBytes, t.cost(), message);
}

// --- what the readers read ------------------------------------------------------------------

/**
 * The snapshot is shared, so the question "does a reader see the current catalog" stops being
 * obvious: a reader that kept a handle from an earlier pass would read a catalog the module no
 * longer holds. A value changed in storage has to reach every reader on the next pass.
 */
void test_a_changed_type_reaches_every_reader_on_the_next_pass(void)
{
    ModuleUnderTest module(SMALL_CATALOG);
    const std::string uid = makeSensorType(0).uid;

    // A pass before the catalog changes, so that every reader has already read the old catalog once.
    CatalogReaderTestHelper::fillStructure(module.setupService);
    module.runStartupScan();
    TEST_ASSERT_EQUAL(MODULE_PIN_COUNT * (int)SMALL_CATALOG * PROBES_PER_TYPE,
                      module.sensorReader.scanSensorCallCount);

    ComplexCommandWork before;
    ModuleCommandExecutionResult immediate("", "");
    TEST_ASSERT_TRUE(module.moduleCommandUseCase.prepareComplexCommand(
        module.command("ScanSensors", "{\"pin\":1}"), before, immediate));

    // One type of the stored catalog is renamed, and told it can no longer be scanned for.
    SensorType edited = makeSensorType(0);
    edited.name = "renamed_model";
    edited.baudRate = 19200;
    edited.canScan = false;
    module.sensorTypeDao.addToStorage(edited);
    module.sensorTypeRepository.invalidateCache();

    // The repository, by uid
    Utils::Optional<SensorType> fromRepository = module.sensorTypeRepository.get(uid.c_str());
    TEST_ASSERT_TRUE(fromRepository.has_value());
    TEST_ASSERT_EQUAL_STRING("renamed_model", fromRepository.value().name.c_str());
    TEST_ASSERT_EQUAL(19200, fromRepository.value().baudRate);

    // The by-name reader the plausibility gate uses
    const SensorType* byName = module.sensorTypeRepository.getSensorTypeByName("renamed_model");
    TEST_ASSERT_NOT_NULL_MESSAGE(byName, "the by-name lookup still answers with the old catalog");
    TEST_ASSERT_EQUAL_STRING(uid.c_str(), byName->uid.c_str());
    TEST_ASSERT_NULL_MESSAGE(module.sensorTypeRepository.getSensorTypeByName(makeSensorType(0).name),
                             "the by-name lookup still answers to the name that was replaced");

    // The ScanSensors reader
    ComplexCommandWork after;
    TEST_ASSERT_TRUE(module.moduleCommandUseCase.prepareComplexCommand(
        module.command("ScanSensors", "{\"pin\":1}"), after, immediate));
    const SensorType* seenByScan = nullptr;
    for (const auto& sensorType : after.candidates())
    {
        if (sensorType.uid == uid)
        {
            seenByScan = &sensorType;
        }
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(seenByScan, "the scan candidates lost the type that was edited");
    TEST_ASSERT_EQUAL_STRING("renamed_model", seenByScan->name.c_str());
    TEST_ASSERT_EQUAL(19200, seenByScan->baudRate);

    // The PingPin reader, which counts what the catalog says can be scanned for
    ComplexCommandWork ping;
    TEST_ASSERT_TRUE(module.moduleCommandUseCase.prepareComplexCommand(
        module.command("PingPin", "{\"pin\":1}"), ping, immediate));
    int scannable = 0;
    for (const auto& sensorType : ping.candidates())
    {
        if (sensorType.canScan)
        {
            scannable++;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE((int)SMALL_CATALOG - 1, scannable,
                              "PingPin still counts the type that was told it cannot be scanned");

    // The boot pass, at the only place it can be seen from: the line. One type fewer to try means
    // one probe fewer per pin.
    module.sensorReader.reset();
    // Both knobs, as the stand sets them: the ladder asks a free pin cheaply first and only runs
    // the identification procedure on what that hears nothing from.
    module.sensorReader.scanSensorReturnValue = false;
    module.sensorReader.readSensorReturnValue = false;
    CatalogReaderTestHelper::fillStructure(module.setupService);
    module.runStartupScan();
    TEST_ASSERT_EQUAL_MESSAGE(MODULE_PIN_COUNT * ((int)SMALL_CATALOG - 1) * PROBES_PER_TYPE,
                              module.sensorReader.scanSensorCallCount,
                              "the discovery still probes the line for the type it was told to skip");

    // And the work prepared before the catalog changed still reads the catalog it took, whole: a snapshot
    // is a snapshot, not a window onto whatever the repository holds now.
    const SensorType* seenByOldWork = nullptr;
    for (const auto& sensorType : before.candidates())
    {
        if (sensorType.uid == uid)
        {
            seenByOldWork = &sensorType;
        }
    }
    TEST_ASSERT_NOT_NULL(seenByOldWork);
    TEST_ASSERT_EQUAL_STRING(makeSensorType(0).name.c_str(), seenByOldWork->name.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(SMALL_CATALOG, before.candidates().size(),
                              "the snapshot taken before the catalog changed did not stay whole");
}

/**
 * A pin ping is handed the whole catalog and applies the can-be-scanned filter itself.
 * What reaches the line has to be what a list filtered by the repository would give.
 */
void test_a_pin_ping_tries_only_the_types_the_catalog_says_can_be_scanned(void)
{
    ModuleUnderTest module(SMALL_CATALOG);

    int scannable = 0;
    for (size_t i = 0; i < SMALL_CATALOG; i++)
    {
        SensorType type = makeSensorType(i);
        type.canScan = (i % 3 == 0);
        if (type.canScan)
        {
            scannable++;
        }
        module.sensorTypeDao.addToStorage(type);
    }
    module.sensorTypeRepository.invalidateCache();
    TEST_ASSERT_GREATER_THAN(0, scannable);
    TEST_ASSERT_LESS_THAN((int)SMALL_CATALOG, scannable);

    ComplexCommandWork work;
    ModuleCommandExecutionResult immediate("", "");
    TEST_ASSERT_TRUE(module.moduleCommandUseCase.prepareComplexCommand(
        module.command("PingPin", "{\"pin\":1}"), work, immediate));
    TEST_ASSERT_EQUAL_MESSAGE(SMALL_CATALOG, work.candidates().size(),
                              "the work does not carry the whole catalog");

    module.sensorReader.reset();
    module.sensorReader.scanSensorReturnValue = false;
    const ComplexCommandOutcome outcome = module.moduleCommandUseCase.executeBusWork(work);

    // One type is tried at its default address and then across the module's address range.
    const int probesPerTriedType = 1 + (MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1);
    TEST_ASSERT_EQUAL_MESSAGE(scannable * probesPerTriedType,
                              module.sensorReader.scanSensorCallCount,
                              "the ping tried types the catalog says cannot be scanned for");
    TEST_ASSERT_EQUAL(ModuleCommandStatus::ERROR, outcome.status);
}

/**
 * The other half of the same bargain, and the one the catalog write path depends on: a reader
 * holding a snapshot when the module adopts a newly stored catalog keeps reading the whole of the
 * one it took, and the next reader gets the whole of the new one.
 */
void test_a_snapshot_survives_the_catalog_being_replaced_under_it(void)
{
    ModuleUnderTest module(SMALL_CATALOG);

    SensorTypeCatalog held = module.sensorTypeRepository.catalogSnapshot();
    TEST_ASSERT_EQUAL(SMALL_CATALOG, held->size());
    const std::string firstUid = (*held)[0].uid;

    std::vector<SensorType> replacement;
    for (size_t i = 0; i < LARGE_CATALOG; i++)
    {
        SensorType type = makeSensorType(i);
        type.name = "replaced_model_" + std::to_string(i);
        replacement.push_back(type);
    }
    module.sensorTypeRepository.adoptStoredCatalog(std::move(replacement));

    TEST_ASSERT_EQUAL_MESSAGE(SMALL_CATALOG, held->size(),
                              "the snapshot lost types when the catalog was replaced");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(makeSensorType(0).name.c_str(), (*held)[0].name.c_str(),
                                     "the snapshot was rewritten under its holder");
    TEST_ASSERT_EQUAL_STRING(firstUid.c_str(), (*held)[0].uid.c_str());

    SensorTypeCatalog fresh = module.sensorTypeRepository.catalogSnapshot();
    TEST_ASSERT_EQUAL_MESSAGE(LARGE_CATALOG, fresh->size(),
                              "the next reader did not get the adopted catalog");
    TEST_ASSERT_EQUAL_STRING("replaced_model_0", (*fresh)[0].name.c_str());
}

/** @brief A module that holds no catalog answers readers with an empty list, not with nothing. */
void test_a_module_with_no_catalog_answers_readers_with_an_empty_list(void)
{
    ModuleUnderTest module(0);

    SensorTypeCatalog empty = module.sensorTypeRepository.catalogSnapshot();
    TEST_ASSERT_NOT_NULL_MESSAGE(empty.get(), "a reader was handed no list at all");
    TEST_ASSERT_EQUAL(0, (int)empty->size());

    module.sensorTypeDao.setShouldFail(true);
    SensorTypeCatalog unreadable = module.sensorTypeRepository.catalogSnapshot();
    TEST_ASSERT_NOT_NULL_MESSAGE(unreadable.get(), "a reader was handed no list when storage failed");
    TEST_ASSERT_EQUAL(0, (int)unreadable->size());

    ComplexCommandWork work;
    TEST_ASSERT_EQUAL(0, (int)work.candidates().size());
}

/** @brief An adopted catalog that names one uid twice is one type, not two. */
void test_a_repeated_uid_in_an_adopted_catalog_is_held_once(void)
{
    ModuleUnderTest module(SMALL_CATALOG);

    std::vector<SensorType> withRepeat;
    withRepeat.push_back(makeSensorType(0));
    withRepeat.push_back(makeSensorType(1));
    SensorType repeat = makeSensorType(0);
    repeat.name = "second_entry_for_the_same_uid";
    withRepeat.push_back(repeat);
    module.sensorTypeRepository.adoptStoredCatalog(std::move(withRepeat));

    TEST_ASSERT_EQUAL(2, module.sensorTypeRepository.count());
    Utils::Optional<SensorType> held = module.sensorTypeRepository.get(makeSensorType(0).uid.c_str());
    TEST_ASSERT_TRUE(held.has_value());
    TEST_ASSERT_EQUAL_STRING(makeSensorType(0).name.c_str(), held.value().name.c_str());
}

/**
 * The same question on the other way in, which is the one that can actually carry a repeat.
 *
 * Nothing filters on the way to flash: the catalog a scan fetches is written to storage as it
 * arrived (ModuleCommandExecutionUseCase::finishComplexCommand -> saveAll), and unlike the
 * synchronization - which collapses a uid the server sent twice - that path has no such step. So
 * the file can hold one uid twice, and which of the two the module then holds is decided where the
 * file is read back. It holds the first, the same one the synchronization keeps.
 */
void test_a_repeated_uid_read_back_from_storage_is_held_once(void)
{
    ModuleUnderTest module(0);

    std::vector<SensorType> written;
    written.push_back(makeSensorType(0));
    written.push_back(makeSensorType(1));
    SensorType repeat = makeSensorType(0);
    repeat.name = "second_entry_for_the_same_uid";
    written.push_back(repeat);

    TEST_ASSERT_EQUAL(3, module.sensorTypeRepository.saveAll(written));

    // Storage keeps what it was handed, repeat included - as the device's DAO does, which streams
    // every type it is given into the file. Without this the test would be measuring a stand-in
    // that quietly fixed the input.
    std::vector<SensorType> stored;
    TEST_ASSERT_TRUE(module.sensorTypeDao.getAll(stored));
    TEST_ASSERT_EQUAL_MESSAGE(3, (int)stored.size(),
                              "the stand-in for flash deduplicated the write, so the read has nothing to do");

    TEST_ASSERT_EQUAL_MESSAGE(2, module.sensorTypeRepository.count(),
                              "the module holds the repeated uid twice after reading storage back");
    TEST_ASSERT_EQUAL_MESSAGE(2, (int)module.sensorTypeRepository.catalogSnapshot()->size(),
                              "a reader is handed the repeated uid twice");
    Utils::Optional<SensorType> held = module.sensorTypeRepository.get(makeSensorType(0).uid.c_str());
    TEST_ASSERT_TRUE(held.has_value());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(makeSensorType(0).name.c_str(), held.value().name.c_str(),
                                     "the second entry of the repeated uid is the one being held");
}

extern "C" void run_catalog_reader_copies_tests(void)
{
    RUN_TEST(test_a_boot_pass_over_the_catalog_does_not_copy_it);
    RUN_TEST(test_a_hydro_pass_over_the_catalog_does_not_copy_it);
    RUN_TEST(test_an_empty_server_answer_does_not_copy_the_catalog_to_count_it);
    RUN_TEST(test_collecting_a_catalog_write_does_not_copy_the_catalog_back_out);
    RUN_TEST(test_a_pin_ping_snapshots_the_catalog_without_copying_it);
    RUN_TEST(test_setting_a_sensor_model_reads_one_type_and_not_the_catalog);
    RUN_TEST(test_a_sensor_scan_snapshots_the_catalog_without_copying_it);
    RUN_TEST(test_a_scan_that_had_to_fetch_the_catalog_does_not_copy_it_afterwards);
    RUN_TEST(test_every_empty_pin_is_a_reader_pass_and_none_of_them_costs_a_catalog);
    RUN_TEST(test_a_changed_type_reaches_every_reader_on_the_next_pass);
    RUN_TEST(test_a_pin_ping_tries_only_the_types_the_catalog_says_can_be_scanned);
    RUN_TEST(test_a_snapshot_survives_the_catalog_being_replaced_under_it);
    RUN_TEST(test_a_module_with_no_catalog_answers_readers_with_an_empty_list);
    RUN_TEST(test_a_repeated_uid_in_an_adopted_catalog_is_held_once);
    RUN_TEST(test_a_repeated_uid_read_back_from_storage_is_held_once);
}
