// Storing the sensor type catalog, and the control loop that must not be doing it.
//
// The catalog is written as a stream: one chunk per sensor type into a temporary file, then the
// sequence of renames that publishes it - around thirty file operations for a catalog of fourteen
// types. Every one of them costs whatever SPIFFS decides to spend, and the file system may collect
// garbage inside a write. Executed where the answer from the server is applied, that whole stream
// is one control-loop iteration.
//
// This file holds a module inside a catalog synchronization and measures the worst control-loop
// step meanwhile, and it records which task made each file operation - the number alone would not
// say who paid for it. The rest of the sync must survive the move: the sensors bound to a changed
// type follow it, and a synchronization is reported as successful only once the catalog is on
// flash under its own name.
//
// What is under test is production code: the real LoopServiceConfigureDone drives the real
// SyncSensorTypesUseCase over the real Outbox with its network task, the real SensorTypeRepository,
// the real SpiffsSensorTypeDao with the real SpiffsFileReplacer under it, and the real
// SensorTypeCatalogWriter with its task. Replaced are the outside world - the server, the flash
// device - and time.
//
// ### Reproducing the red
//
// The red is the write executed inline: remove the `setCatalogWriter()` line from
// ModuleUnderTest's constructor and the use case stores the catalog itself, on whichever task
// applies the answer - the control loop. The worst step then becomes the duration of the write.
//
// The second scenario is a control: what the module reports about a write the file system refused
// must not depend on who made the attempt, so every assertion in it but the last one holds in both
// shapes.
//
// ### The storage task is there for the write and not after it
//
// The catalog changes a few times a year, and a module stores it at a boot that finds it changed
// on the server or when a re-armed fetch brings an edit. A task that stays after its write holds
// its stack - heap, on the device - for the rest of the boot, to write nothing. Scenarios 4 to 8
// hold the module to the opposite: the task exists while a write is queued or running, ends once
// the queue is empty, and the next write creates it again. Read through the scheduler stand-in,
// which keeps per task what the device keeps: whether it still exists, the stack the firmware
// asked for, and its own entry on the task watchdog's list.
//
// Reproducing the red of 4 and 7: the storage task as a body that never returns - an idle poll
// in place of the exit when the queue is empty. 5, 6 and 8 hold in both shapes: a second write
// stored exactly as the first, no file operation of either on the loop or on the bus task, and
// no watchdog entry left behind by a task that is gone - 8 turns red when the task ends without
// taking itself off the watchdog's list. 9 is about the write's own heap while it runs, and is
// red with ArduinoJsonConverter::sensorTypeToJson() serializing into a string it did not size.
// 10 and 11 are the storage task that could not be created; both are red with a refused creation
// taken for a live task - the write queued with nothing to take it.

#include <unity.h>

#include "constants.h"

#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Storage/SensorTypeCatalogWriter.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include "Sim/HostAllocationCounter.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimTaskWatchdog.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimTaskWatchdog;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

// The claim under test: with a catalog being stored, the worst control-loop step is one loop
// period - the step depends on the file system not at all.
const uint32_t MAX_ACCEPTED_STEP_MS = CONTROL_PERIOD_MS;

// What one file operation costs on the simulated flash. Nothing in SPIFFS promises a number; this
// one is deliberately modest against what the file system can spend when it collects garbage
// inside a write, and it already makes the whole write two orders of magnitude longer than a
// control-loop period.
const uint32_t FLASH_OP_MS = 5;

// The catalog a live account has, and the weight at which the write became a problem in the first
// place.
const size_t CATALOG_TYPES = 14;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const CATALOG_FILE = "/data/sensorTypes.json";
const char* const MODULE_UID = "catalog-module";
const char* const RETIRED_TYPE_UID = "type-uid-retired";

// --- the outside world --------------------------------------------------------------------

/**
 * @brief The flash device under the DAO: an in-memory file system that takes time.
 *
 * What it substitutes is hardware. The write itself - the streaming, the temporary file, the
 * renames that publish it - is the product's own code above this line and is not replaced.
 *
 * Two things are recorded. How long each operation takes, so that a write on the control loop is
 * visible as a stalled loop; and which task made it, because that is the actual claim: the number
 * of milliseconds says nothing about whose milliseconds they were. Only the write path is charged
 * - reads cost nothing here, so that what the measurement reports is the write and nothing else.
 */
class SimSlowFlash : public MockSpiffsAdapter {
public:
    SimSlowFlash(HostTaskScheduler& scheduler, VirtualClock& clock)
        : _scheduler(scheduler), _clock(clock) {}

    /** Charging starts when a test says so, so that staging the initial state is free. */
    void startCharging() { _opCostMs = FLASH_OP_MS; }

    uint64_t writeOpsTotal() const { return _writeOps; }

    uint64_t writeOpsBy(const char* taskName) const
    {
        std::map<std::string, uint64_t>::const_iterator it = _writeOpsByTask.find(taskName);
        return (it == _writeOpsByTask.end()) ? 0 : it->second;
    }

    /** Virtual time at which the catalog last took its own name, 0 if it never did. */
    uint64_t catalogPublishedAtMs() const { return _catalogPublishedAtMs; }

    std::unique_ptr<ISpiffsFileWriter> openForRewrite(const char* filePath) override
    {
        chargeWriteOp();
        std::unique_ptr<ISpiffsFileWriter> inner = MockSpiffsAdapter::openForRewrite(filePath);
        if (!inner)
        {
            return inner;
        }
        return std::unique_ptr<ISpiffsFileWriter>(new TimedWriter(*this, std::move(inner)));
    }

    bool renameFile(const char* fromPath, const char* toPath) override
    {
        chargeWriteOp();
        const bool renamed = MockSpiffsAdapter::renameFile(fromPath, toPath);
        if (renamed && toPath != nullptr && std::string(toPath) == CATALOG_FILE)
        {
            _catalogPublishedAtMs = _clock.nowMs();
        }
        return renamed;
    }

    bool deleteFile(const char* filePath) override
    {
        chargeWriteOp();
        return MockSpiffsAdapter::deleteFile(filePath);
    }

    void removeFile(const char* filePath) override
    {
        chargeWriteOp();
        MockSpiffsAdapter::removeFile(filePath);
    }

private:
    /** Delegates the write, and charges the time and the task for it. */
    class TimedWriter : public ISpiffsFileWriter {
    public:
        TimedWriter(SimSlowFlash& flash, std::unique_ptr<ISpiffsFileWriter> inner)
            : _flash(flash), _inner(std::move(inner)) {}

        bool write(const char* chunk) override
        {
            _flash.chargeWriteOp();
            return _inner->write(chunk);
        }

        bool commit() override
        {
            _flash.chargeWriteOp();
            return _inner->commit();
        }

        void abort() override
        {
            _flash.chargeWriteOp();
            _inner->abort();
        }

        size_t bytesWritten() const override { return _inner->bytesWritten(); }

    private:
        SimSlowFlash& _flash;
        std::unique_ptr<ISpiffsFileWriter> _inner;
    };

    void chargeWriteOp()
    {
        if (_opCostMs == 0)
        {
            return;
        }
        _writeOps++;
        _writeOpsByTask[_scheduler.currentTaskName()]++;
        _scheduler.delayMs(_opCostMs);
    }

    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
    uint32_t _opCostMs = 0;
    uint64_t _writeOps = 0;
    uint64_t _catalogPublishedAtMs = 0;
    std::map<std::string, uint64_t> _writeOpsByTask;
};

/** @brief The far end of the catalog fetch. Answers instantly; the flash is the slow part here. */
class SimServer : public MockHybridAPIService {
public:
    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        getSensorTypesListCallCount++;
        if (httpFailed != nullptr)
        {
            *httpFailed = !reachable;
        }
        return reachable ? sensorTypesListReturnValue : std::vector<SensorType>();
    }

    bool reachable = true;
};

/** @brief A sensor repository that remembers when a sensor was written. */
class RecordingSensorRepository : public MockSensorRepository {
public:
    explicit RecordingSensorRepository(VirtualClock& clock) : _clock(clock) {}

    bool save(const SensorEntity& sensor) override
    {
        saveCount++;
        lastSavedUid = sensor.uid;
        lastSaveAtMs = _clock.nowMs();
        return true;
    }

    uint32_t saveCount = 0;
    std::string lastSavedUid;
    uint64_t lastSaveAtMs = 0;

private:
    VirtualClock& _clock;
};

class CadenceRecorder {
public:
    void tick(uint64_t nowMs)
    {
        if (_count > 0)
        {
            const uint64_t gap = nowMs - _lastMs;
            if (gap > _worstGapMs)
            {
                _worstGapMs = gap;
                _worstGapStartedAtMs = _lastMs;
            }
        }
        _lastMs = nowMs;
        _count++;
    }

    void closeAt(uint64_t nowMs)
    {
        if (_count > 0 && (nowMs - _lastMs) > _worstGapMs)
        {
            _worstGapMs = nowMs - _lastMs;
            _worstGapStartedAtMs = _lastMs;
        }
    }

    uint64_t count() const { return _count; }
    uint64_t worstGapMs() const { return _worstGapMs; }
    uint64_t worstGapStartedAtMs() const { return _worstGapStartedAtMs; }

private:
    uint64_t _count = 0;
    uint64_t _lastMs = 0;
    uint64_t _worstGapMs = 0;
    uint64_t _worstGapStartedAtMs = 0;
};

/** @brief A sensor type of the weight the live catalog has: eight channels, ~1.4 KB of JSON. */
SensorType makeSensorType(const std::string& uid, const std::string& name)
{
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " display name for the module structure";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.baudRate = 9600;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = "2026-02-11T09:01:35.44352+00:00";

    for (int channel = 1; channel <= 8; channel++)
    {
        SubSensorType subType;
        subType.name = name + "_channel_" + std::to_string(channel);
        subType.displayName = name + " channel " + std::to_string(channel);
        subType.minValue = 0.0f;
        subType.maxValue = 1000.0f;
        subType.unit = "unit_" + std::to_string(channel);
        subType.createdAt = "2025-09-22T07:04:26.803128+00:00";
        subType.updatedAt = "2026-02-11T09:01:35.44352+00:00";
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

std::vector<SensorType> makeCatalog(size_t count)
{
    std::vector<SensorType> catalog;
    for (size_t i = 0; i < count; i++)
    {
        catalog.push_back(makeSensorType("type-uid-" + std::to_string(i),
                                         "sensor_model_" + std::to_string(i)));
    }
    return catalog;
}

/** @brief A Modbus sensor of the first catalog type: its registers follow that type. */
SensorEntity sensorOfFirstType()
{
    SensorEntity sensor;
    sensor.uid = "sensor-1";
    sensor.sensorTypeUid = "type-uid-0";
    sensor.model = "sensor_model_0";
    // The spelling isModbusSensor() tests for; a sensor of another protocol is not updated from
    // the catalog and would prove nothing here.
    sensor.protocol = "ModBus";
    sensor.pin = 1;
    sensor.modbusDeviceAddress = 1;
    return sensor;
}

/**
 * @brief The module, wired the way globals.h wires it.
 */
struct ModuleUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    RecordingSensorRepository sensorRepository;

    SimSlowFlash flash;
    ArduinoJsonConverter jsonConverter;
    SpiffsSensorTypeDao sensorTypeDao;
    SensorTypeRepository sensorTypeRepository;
    SensorTypeCatalogWriter catalogWriter;

    SimServer server;
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    SyncSensorTypesUseCase syncSensorTypes;

    LoopServiceConfigureDone loopService;

    ModuleUnderTest()
        : scheduler(clock),
          arduino(clock, &scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          sensorRepository(clock),
          flash(scheduler, clock),
          sensorTypeDao(&flash, &jsonConverter, &debugSystem, &logger),
          sensorTypeRepository(&sensorTypeDao, &debugSystem, &logger),
          catalogWriter(&sensorTypeDao, &scheduler, &logger, &debugSystem),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          syncSensorTypes(&server, &sensorTypeRepository, &arduino, &timeAdapter, &jsonConverter,
                          &logger, &debugSystem, &sensorRepository)
    {
        debugSystem.featureEnabled = false;
        timeAdapter.setEpochTime(1755690000);
        moduleConfig.setModuleUid(MODULE_UID);

        server.sensorTypesListReturnValue = makeCatalog(CATALOG_TYPES);

        syncSensorTypes.setOutbox(&outbox);
        // Remove this line to reproduce the red: the use case then stores the catalog itself, on
        // whichever task applies the server's answer.
        syncSensorTypes.setCatalogWriter(&catalogWriter);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setSyncSensorTypesUseCase(&syncSensorTypes);
        loopService.setSensorTypeCatalogWriter(&catalogWriter);
    }

    /**
     * @brief One task, mirroring main.cpp: the loop service, then one period of sleep.
     *
     * It also samples the reported sync status once per iteration, so that "when did the module
     * call this synchronization successful" is a time on the same clock as "when did the catalog
     * take its own name".
     */
    void startArduinoLoopTask(CadenceRecorder& cadence, uint32_t periodMs = CONTROL_PERIOD_MS)
    {
        scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [this, &cadence, periodMs]() {
            while (true)
            {
                cadence.tick(clock.nowMs());
                loopService.loop();
                if (syncReportedAtMs == 0 && syncSensorTypes.getLastSyncStatus())
                {
                    syncReportedAtMs = clock.nowMs();
                }
                scheduler.delayMs(periodMs);
            }
        });
    }

    /** Virtual time at which the module first reported this synchronization successful. */
    uint64_t syncReportedAtMs = 0;

    /** What a module that has just booted would read out of storage. */
    std::vector<SensorType> storedCatalog()
    {
        SpiffsSensorTypeDao daoAfterBoot(&flash, &jsonConverter, &debugSystem, &logger);
        std::vector<SensorType> stored;
        daoAfterBoot.getAll(stored);
        return stored;
    }

    size_t storedCatalogSize()
    {
        return storedCatalog().size();
    }
};

/** @brief The soundness gate every measurement scenario passes before its verdict counts. */
void assertRunIsSound(ModuleUnderTest& module, CadenceRecorder& cadence)
{
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");
    TEST_ASSERT_TRUE_MESSAGE(module.server.getSensorTypesListCallCount >= 1,
                             "the catalog was never fetched - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.flash.writeOpsTotal() >= CATALOG_TYPES,
                             "the catalog was never written - nothing slow was exercised");
}

void assertLoopStayedBounded(CadenceRecorder& cadence, const char* whatRan)
{
    char message[288];
    snprintf(message, sizeof(message),
             "worst control-loop step was %lu ms starting at t=%lu ms (limit %lu ms) while %s",
             (unsigned long)cadence.worstGapMs(),
             (unsigned long)cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS,
             whatRan);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(MAX_ACCEPTED_STEP_MS,
                                           (unsigned int)cadence.worstGapMs(),
                                           message);
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. A catalog of fourteen types arrives from the server and is stored on a flash device that
//    charges for every operation. The loop keeps its period, the write happens somewhere else,
//    and everything that depends on the stored catalog still happens - afterwards.
// --------------------------------------------------------------------------------------------

static void test_the_catalog_is_stored_without_the_control_loop_waiting_for_flash(void)
{
    ModuleUnderTest module;
    module.sensorRepository.sensors.push_back(sensorOfFirstType());
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.syncSensorTypes.forceSync();
    module.scheduler.runForMs(60000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);

    assertLoopStayedBounded(cadence, "a catalog of 14 sensor types was written to a flash device "
                                     "charging 5 ms per operation");

    // Where the write ran. These are consequences of running off the loop, so they come after the verdict:
    // before it they would be false by definition and hide the measured number.
    char message[224];
    snprintf(message, sizeof(message),
             "%lu of the %lu file operations of the catalog write ran on the control loop",
             (unsigned long)module.flash.writeOpsBy(LOOP_TASK_NAME),
             (unsigned long)module.flash.writeOpsTotal());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.flash.writeOpsBy(LOOP_TASK_NAME), message);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(module.flash.writeOpsTotal(),
                                     module.flash.writeOpsBy(SensorTypeCatalogWriter::TASK_NAME),
                                     "the catalog write did not run on the storage task");

    // The task that does the waiting answers for itself, the way the other background tasks do.
    TEST_ASSERT_TRUE_MESSAGE(module.scheduler.watchdogFeedsBy(SensorTypeCatalogWriter::TASK_NAME) >= 1,
                             "the storage task never reported its own liveness to the watchdog");

    // And the catalog is on flash, under its own name, complete.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(CATALOG_TYPES, (unsigned)module.storedCatalogSize(),
                                   "the stored catalog is not the one the server sent");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.syncSensorTypes.writeCount(),
                                     "the catalog was not handed to the storage task exactly once");

    // The report follows the write and never precedes it: the synchronization was called
    // successful only after the catalog existed under its own name.
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "a stored catalog was not reported as a successful synchronization");
    TEST_ASSERT_TRUE_MESSAGE(module.flash.catalogPublishedAtMs() > 0,
                             "the catalog never took its own name");
    snprintf(message, sizeof(message),
             "the sync was reported successful at t=%lu ms, the catalog reached flash at t=%lu ms",
             (unsigned long)module.syncReportedAtMs,
             (unsigned long)module.flash.catalogPublishedAtMs());
    TEST_ASSERT_TRUE_MESSAGE(module.syncReportedAtMs >= module.flash.catalogPublishedAtMs(),
                             message);

    // What a stored catalog is a precondition for still happens, and also not before it.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.sensorRepository.saveCount,
                                     "the sensor bound to a changed type was not updated from it");
    TEST_ASSERT_TRUE_MESSAGE(module.sensorRepository.lastSaveAtMs >= module.flash.catalogPublishedAtMs(),
                             "a sensor was tied to a sensor type that was not stored yet");
}

// --------------------------------------------------------------------------------------------
// 2. The same synchronization against a flash device that refuses the write. The outcome has to
//    travel back exactly as far as it did when the loop wrote the file itself: a failed sync,
//    with the reason in the log, and a module that keeps the catalog it had.
// --------------------------------------------------------------------------------------------

static void test_a_refused_catalog_write_is_reported_as_a_failed_synchronization(void)
{
    ModuleUnderTest module;
    // This scenario reads the log, which the sync only writes with its debug feature on.
    module.debugSystem.featureEnabled = true;
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    // Nothing can be opened for writing, which is how a full or broken file system answers.
    module.flash.setShouldFail(true);

    module.syncSensorTypes.forceSync();
    module.scheduler.runForMs(60000);
    cadence.closeAt(module.clock.nowMs());

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(cadence.count() > 100,
                             "the control loop barely ran - the harness, not the firmware, is at fault");
    TEST_ASSERT_TRUE_MESSAGE(module.server.getSensorTypesListCallCount >= 1,
                             "the catalog was never fetched - the scenario did not happen");

    TEST_ASSERT_FALSE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                              "a refused write was reported as a successful synchronization");
    TEST_ASSERT_TRUE_MESSAGE(module.logger.hasMessageContaining("saveAll wrote 0 of 14 sensor types"),
                             "the refusal was not reported with the count it refused");
    TEST_ASSERT_TRUE_MESSAGE(module.logger.hasMessageContaining("reason=open"),
                             "the refusal was not reported with its reason");
    TEST_ASSERT_FALSE_MESSAGE(module.flash.fileExists(CATALOG_FILE),
                              "a refused write left a catalog file behind");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.sensorRepository.saveCount,
                                     "sensors were tied to a catalog that was never stored");

    // A refusal must not be paid for on the control loop either.
    assertLoopStayedBounded(cadence, "a catalog write was refused by the file system");

    // Last, and the only assertion here that is about where the write ran: everything above holds
    // in both shapes, which is what makes this scenario a control on the reported outcome.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.syncSensorTypes.writeCount(),
                                     "the refused catalog was not handed to the storage task");
}

// --------------------------------------------------------------------------------------------
// 3. The catalog the module keeps follows the server down as well as up. A type the server has
//    withdrawn is dropped, and the drop has to survive the same journey the merge does - the
//    storage task, the temporary file, the rename - because that is the path this firmware takes
//    and not the one where the applying task writes the file itself.
// --------------------------------------------------------------------------------------------

static void test_a_type_the_server_withdrew_is_pruned_on_the_way_to_flash(void)
{
    ModuleUnderTest module;

    // The catalog as earlier synchronizations left it: everything the server still publishes,
    // plus one type it has since stopped publishing.
    std::vector<SensorType> storedBefore = makeCatalog(CATALOG_TYPES);
    storedBefore.push_back(makeSensorType(RETIRED_TYPE_UID, "sensor_model_retired"));
    SpiffsSensorTypeDao seedDao(&module.flash, &module.jsonConverter, &module.debugSystem,
                                &module.logger);
    TEST_ASSERT_TRUE_MESSAGE(seedDao.saveAll(storedBefore),
                             "the scenario could not put a catalog on flash");
    module.sensorTypeRepository.invalidateCache();
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    module.syncSensorTypes.forceSync();
    module.scheduler.runForMs(60000);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    assertLoopStayedBounded(cadence, "a catalog with a withdrawn type was rewritten");

    // What a module booting now would read: the server's catalog, and not one type more.
    std::vector<SensorType> stored = module.storedCatalog();
    TEST_ASSERT_EQUAL_UINT_MESSAGE(CATALOG_TYPES, (unsigned)stored.size(),
                                   "the stored catalog is not the one the server publishes");
    for (size_t i = 0; i < stored.size(); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(stored[i].uid != RETIRED_TYPE_UID,
                                 "the withdrawn type is still on flash");
    }

    // And it was pruned on the way through the storage task, not by the loop writing the file.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.syncSensorTypes.writeCount(),
                                     "the pruned catalog was not handed to the storage task");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.flash.writeOpsBy(LOOP_TASK_NAME),
                                     "the control loop wrote the pruned catalog itself");
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "a synchronization that only removed a type was reported as failed");
}

// --------------------------------------------------------------------------------------------
// The storage task's life: shared pieces of scenarios 4 to 7.
// --------------------------------------------------------------------------------------------

namespace {

/** The edit the server publishes for the second write: every type, a later updatedAt. */
const char* const EDITED_AT = "2026-09-30T13:24:25.117617+00:00";

/** How long one synchronization is given: the fetch, the merge, the write and its collection. */
const uint32_t ONE_SYNC_MS = 60000;

std::vector<SensorType> makeCatalogEditedAt(size_t count, const char* updatedAt)
{
    std::vector<SensorType> catalog = makeCatalog(count);
    for (size_t i = 0; i < catalog.size(); i++)
    {
        catalog[i].updatedAt = updatedAt;
    }
    return catalog;
}

/** The bytes the DAO stores for this catalog, written on a flash of its own with nothing before. */
std::string referenceFileFor(const std::vector<SensorType>& catalog, ModuleUnderTest& module)
{
    MockSpiffsAdapter referenceFlash;
    SpiffsSensorTypeDao referenceDao(&referenceFlash, &module.jsonConverter, &module.debugSystem,
                                     &module.logger);
    TEST_ASSERT_TRUE_MESSAGE(referenceDao.saveAll(catalog),
                             "the reference catalog could not be written");
    return referenceFlash.getFileContent(CATALOG_FILE);
}

/** One synchronization, from the ask to the collected outcome, with the loop running throughout. */
void synchronizeOnce(ModuleUnderTest& module)
{
    module.syncSensorTypes.forceSync();
    module.scheduler.runForMs(ONE_SYNC_MS);
}

/** The module holds a catalog already; the server has since edited every type of it. */
void storeCatalogBeforeTheRun(ModuleUnderTest& module)
{
    SpiffsSensorTypeDao seedDao(&module.flash, &module.jsonConverter, &module.debugSystem,
                                &module.logger);
    TEST_ASSERT_TRUE_MESSAGE(seedDao.saveAll(makeCatalog(CATALOG_TYPES)),
                             "the scenario could not put a catalog on flash");
    module.sensorTypeRepository.invalidateCache();
    module.server.sensorTypesListReturnValue = makeCatalogEditedAt(CATALOG_TYPES, EDITED_AT);
}

void assertTheStorageTaskIsGone(ModuleUnderTest& module, const char* afterWhat)
{
    // The scenario first: a write that never happened would leave no task behind for the wrong
    // reason.
    TEST_ASSERT_TRUE_MESSAGE(module.scheduler.createdTaskCount(SensorTypeCatalogWriter::TASK_NAME) >= 1,
                             "no storage task was ever created - the write did not happen");

    char message[256];
    snprintf(message, sizeof(message),
             "%lu storage task(s) still alive %s, holding %lu bytes of stack on the device",
             (unsigned long)module.scheduler.liveTaskCount(SensorTypeCatalogWriter::TASK_NAME),
             afterWhat,
             (unsigned long)module.scheduler.liveStackBytes(SensorTypeCatalogWriter::TASK_NAME));
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.isTaskAlive(SensorTypeCatalogWriter::TASK_NAME),
                              message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0,
                                     module.scheduler.liveStackBytes(SensorTypeCatalogWriter::TASK_NAME),
                                     message);
}

} // namespace

// --------------------------------------------------------------------------------------------
// 4. (a) Once the catalog is on flash and nothing else is queued, the storage task does not exist
//    and holds no stack - on a module that had no catalog at all, and on one that had one.
// --------------------------------------------------------------------------------------------

static void test_the_storage_task_ends_once_a_fresh_module_has_stored_its_catalog(void)
{
    ModuleUnderTest module;
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    synchronizeOnce(module);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(CATALOG_TYPES, (unsigned)module.storedCatalogSize(),
                                   "the catalog of the fresh module is not on flash");
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the first synchronization of the fresh module was not reported done");

    assertTheStorageTaskIsGone(module, "after the fresh module stored its first catalog");
}

static void test_the_storage_task_ends_once_an_edited_catalog_is_stored(void)
{
    ModuleUnderTest module;
    storeCatalogBeforeTheRun(module);
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    synchronizeOnce(module);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.syncSensorTypes.writeCount(),
                                     "the edited catalog was not handed to the storage task");
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the edited catalog was not reported stored");

    assertTheStorageTaskIsGone(module, "after the edited catalog was stored");
}

// --------------------------------------------------------------------------------------------
// 5. (b) A second write after the first: stored, and stored byte for byte as the DAO stores that
//    catalog - whoever ran the first write and whatever became of its task.
// --------------------------------------------------------------------------------------------

static void test_a_second_catalog_write_is_stored_like_the_first(void)
{
    ModuleUnderTest module;
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    synchronizeOnce(module);
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the first write did not land - the scenario did not happen");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(referenceFileFor(makeCatalog(CATALOG_TYPES), module).c_str(),
                                     module.flash.getFileContent(CATALOG_FILE).c_str(),
                                     "the first catalog is not stored as the DAO stores it");

    const std::vector<SensorType> edited = makeCatalogEditedAt(CATALOG_TYPES, EDITED_AT);
    module.server.sensorTypesListReturnValue = edited;
    synchronizeOnce(module);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.syncSensorTypes.writeCount(),
                                     "the second catalog was not handed to the storage task");
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the second write was not reported stored");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(referenceFileFor(edited, module).c_str(),
                                     module.flash.getFileContent(CATALOG_FILE).c_str(),
                                     "the second catalog is not stored as the DAO stores it");

    // What a module booting now reads, and what the running one holds: the edit, both of them.
    const std::vector<SensorType> stored = module.storedCatalog();
    TEST_ASSERT_EQUAL_UINT_MESSAGE(CATALOG_TYPES, (unsigned)stored.size(),
                                   "the stored catalog lost types on the second write");
    for (size_t i = 0; i < stored.size(); i++)
    {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(EDITED_AT, stored[i].updatedAt.c_str(),
                                         "a stored type is not the edited one");
    }
    const Utils::Optional<SensorType> held = module.sensorTypeRepository.get("type-uid-0");
    TEST_ASSERT_TRUE_MESSAGE(held.has_value(), "the module holds no type after the second write");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(EDITED_AT, held.value().updatedAt.c_str(),
                                     "the module holds the first catalog after the second write");
}

// --------------------------------------------------------------------------------------------
// 6. (c) The guard: no file operation of either write runs on the control loop or on the bus
//    task, and the loop keeps its period through both - the property the storage task exists for.
// --------------------------------------------------------------------------------------------

static void test_neither_write_runs_on_the_control_loop_or_the_bus_task(void)
{
    ModuleUnderTest module;
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    synchronizeOnce(module);
    const uint64_t firstWriteOps = module.flash.writeOpsTotal();
    module.server.sensorTypesListReturnValue = makeCatalogEditedAt(CATALOG_TYPES, EDITED_AT);
    synchronizeOnce(module);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.syncSensorTypes.writeCount(),
                                     "the two catalogs were not both handed to the storage task");
    TEST_ASSERT_TRUE_MESSAGE(firstWriteOps >= CATALOG_TYPES &&
                                 module.flash.writeOpsTotal() >= firstWriteOps + CATALOG_TYPES,
                             "one of the two writes made no file operations - nothing was guarded");

    char message[224];
    snprintf(message, sizeof(message),
             "of %lu file operations of two writes, %lu ran on the loop, %lu on the bus task and "
             "%lu on the storage task",
             (unsigned long)module.flash.writeOpsTotal(),
             (unsigned long)module.flash.writeOpsBy(LOOP_TASK_NAME),
             (unsigned long)module.flash.writeOpsBy(SensorAcquisitionService::TASK_NAME),
             (unsigned long)module.flash.writeOpsBy(SensorTypeCatalogWriter::TASK_NAME));
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.flash.writeOpsBy(LOOP_TASK_NAME), message);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.flash.writeOpsBy(SensorAcquisitionService::TASK_NAME),
                                     message);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(module.flash.writeOpsTotal(),
                                     module.flash.writeOpsBy(SensorTypeCatalogWriter::TASK_NAME),
                                     message);

    assertLoopStayedBounded(cadence, "two catalogs were written one after the other");
}

// --------------------------------------------------------------------------------------------
// 7. (d) What one finished write leaves held: on the device, the stack the scheduler took for the
//    task, and on the host heap, whatever the write and its task allocated and did not give back.
//
//    Driven at the writer itself, with nothing around it that keeps a record of what happened -
//    no logger, no repository - so that what is left is what the write left. The window is the
//    second write through the same scheduler, the DAO and the file: everything the bench keeps by
//    name (the task's watchdog history, the file the DAO opens) already exists, the catalog is the
//    one already stored and is taken back afterwards, so a write that gives back what it took
//    leaves the balance where it found it.
// --------------------------------------------------------------------------------------------

static void test_a_finished_write_leaves_nothing_held(void)
{
    VirtualClock clock;
    HostTaskScheduler scheduler(clock);
    MockSpiffsAdapter flash;
    ArduinoJsonConverter jsonConverter;
    SpiffsSensorTypeDao dao(&flash, &jsonConverter);

    SensorTypeCatalogWriter firstWriter(&dao, &scheduler);
    SensorTypeCatalogWriter measuredWriter(&dao, &scheduler);

    std::vector<SensorType> catalog = makeCatalog(CATALOG_TYPES);
    std::vector<CatalogWriteCompletion> results;
    results.reserve(ISensorTypeCatalogWriter::MAX_PENDING_WRITES);

    // The first write: the file, the DAO's path and the scheduler's records of the task name.
    const uint32_t firstTicket = firstWriter.submit(catalog);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ISensorTypeCatalogWriter::kRejected, firstTicket,
                                  "the first write was refused");
    scheduler.runForMs(5000);
    TEST_ASSERT_TRUE_MESSAGE(firstWriter.drainResults(results) && results.size() == 1 &&
                                 results[0].written,
                             "the first write did not land");
    results.clear();
    std::vector<SensorType> firstBack;
    firstWriter.takeStoredCatalog(firstTicket, firstBack);
    firstBack.clear();
    std::vector<SensorType>().swap(firstBack);

    const uint32_t stackBefore = scheduler.liveStackBytes(SensorTypeCatalogWriter::TASK_NAME);
    const long long heapBefore = HostAllocationCounter::liveBytes();

    const uint32_t ticket = measuredWriter.submit(std::move(catalog));
    scheduler.runForMs(5000);
    const bool drained = measuredWriter.drainResults(results);
    const bool takenBack = measuredWriter.takeStoredCatalog(ticket, catalog);

    const long long heapHeld = HostAllocationCounter::liveBytes() - heapBefore;
    const long long stackHeld =
        (long long)scheduler.liveStackBytes(SensorTypeCatalogWriter::TASK_NAME) - (long long)stackBefore;

    TEST_ASSERT_TRUE_MESSAGE(ticket != ISensorTypeCatalogWriter::kRejected && drained &&
                                 results.size() == 1 && results[0].written,
                             "the measured write did not land - nothing was measured");
    TEST_ASSERT_TRUE_MESSAGE(takenBack && catalog.size() == CATALOG_TYPES,
                             "the written catalog was not handed back - the balance would hold it");

    printf("  after one finished catalog write: %lld bytes of host heap and %lld bytes of device "
           "stack still held\n",
           heapHeld, stackHeld);

    char message[200];
    snprintf(message, sizeof(message),
             "one finished catalog write left %lld bytes of host heap and %lld bytes of device "
             "stack held",
             heapHeld, stackHeld);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, stackHeld, message);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, heapHeld, message);
}

// --------------------------------------------------------------------------------------------
// 8. The watchdog after the task is gone. The task subscribed itself and fed its entry around
//    every write; on the device, deleting it does not take that entry off the watchdog's list, and
//    an entry nobody feeds fires the watchdog one timeout later - a panic and a reboot, and with
//    the catalog still differing from the server's, the same write at the next boot. The stand-in
//    watchdog keeps the list per task as esp_task_wdt does; it is run here at the threshold the
//    device arms, over twice that threshold after the last write, across two task lives.
// --------------------------------------------------------------------------------------------

static void test_a_storage_task_that_ended_leaves_the_watchdog_nothing_to_fire_on(void)
{
    ModuleUnderTest module;
    SimTaskWatchdog watchdog(module.scheduler, module.clock, (uint32_t)WDT_TIMEOUT_S * 1000UL);
    watchdog.start();
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    synchronizeOnce(module);
    module.server.sensorTypesListReturnValue = makeCatalogEditedAt(CATALOG_TYPES, EDITED_AT);
    synchronizeOnce(module);
    module.scheduler.runForMs(2UL * (uint32_t)WDT_TIMEOUT_S * 1000UL);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.syncSensorTypes.writeCount(),
                                     "the two catalogs were not both handed to the storage task");
    TEST_ASSERT_TRUE_MESSAGE(module.scheduler.watchdogFeedsBy(SensorTypeCatalogWriter::TASK_NAME) >= 2,
                             "the storage task did not answer for itself to the watchdog");

    char message[224];
    snprintf(message, sizeof(message),
             "the task watchdog fired at t=%lu ms on '%s', unfed for %lu ms",
             (unsigned long)watchdog.firedAtMs(), watchdog.firedTaskName().c_str(),
             (unsigned long)watchdog.firedGapMs());
    TEST_ASSERT_FALSE_MESSAGE(watchdog.fired(), message);
}

// --------------------------------------------------------------------------------------------
// 9. What the write itself asks of the heap while it runs, per sensor type: the type serialized
//    for the file, one block of its own length - not a string grown by doubling, which costs the
//    old block and the new one at once and leaves the write holding up to twice the type. Measured
//    at three widths; the bytes stored are the same either way (scenario 5 compares them).
// --------------------------------------------------------------------------------------------

static void test_a_sensor_type_is_serialized_for_the_file_into_one_block_of_its_length(void)
{
    ArduinoJsonConverter converter;

    const int channelCounts[] = {2, 8, 12};
    for (size_t i = 0; i < sizeof(channelCounts) / sizeof(channelCounts[0]); i++)
    {
        SensorType type = makeSensorType("type-uid-0", "sensor_model_0");
        type.sensorTypes.resize(channelCounts[i] < 8 ? channelCounts[i] : 8);
        type.modbusRegisterConfig.resize(type.sensorTypes.size());
        for (int extra = 8; extra < channelCounts[i]; extra++)
        {
            type.sensorTypes.push_back(type.sensorTypes[0]);
            type.modbusRegisterConfig.push_back(type.modbusRegisterConfig[0]);
        }

        // The converter's document pool takes its slots on first use; that is not this write's.
        {
            const std::string warmup = converter.sensorTypeToJson(type);
        }

        const long long before = HostAllocationCounter::liveBytes();
        HostAllocationCounter::resetPeak();
        const std::string json = converter.sensorTypeToJson(type);
        const long long held = HostAllocationCounter::liveBytes() - before;
        const long long aboveWhatItHolds = HostAllocationCounter::peakBytes() - before - held;

        char message[200];
        snprintf(message, sizeof(message),
                 "a %d-channel type of %u bytes of JSON was serialized into %lld bytes, and needed "
                 "%lld bytes more on the way",
                 channelCounts[i], (unsigned)json.size(), held, aboveWhatItHolds);
        printf("  %s\n", message);
        TEST_ASSERT_TRUE_MESSAGE(json.size() > 1000, "the type did not serialize - nothing measured");
        TEST_ASSERT_EQUAL_INT64_MESSAGE(0, aboveWhatItHolds, message);
        TEST_ASSERT_TRUE_MESSAGE(held <= (long long)json.size() + 32, message);
    }
}

// --------------------------------------------------------------------------------------------
// 10. The storage task could not be created: xTaskCreatePinnedToCore() refuses when the heap has
//     no block for the stack, and the task is now created for every write, at whatever moment the
//     catalog arrives. The write is refused - not queued with nothing to take it, which would leave
//     the synchronization "in flight" for the rest of the boot and the catalog unstored - and the
//     refusal is a failed synchronization the next one makes good.
// --------------------------------------------------------------------------------------------

namespace {

/** Ask for the catalog and refuse the next task the module creates - the storage task. */
void synchronizeWithTheStorageTaskRefused(ModuleUnderTest& module)
{
    // The ask creates the outbox's task on the spot; the next task asked for is the storage task,
    // when the answer has been merged.
    module.syncSensorTypes.forceSync();
    TEST_ASSERT_TRUE_MESSAGE(module.scheduler.isTaskAlive("outbox-net"),
                             "the fetch did not start the outbox's task - the refusal would hit it");
    module.scheduler.failNextCreateTasks(1);
    module.scheduler.runForMs(ONE_SYNC_MS);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.server.getSensorTypesListCallCount,
                                  "the catalog was not fetched - the scenario did not happen");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.scheduler.refusedTaskCount(),
                                     "no task creation was refused - the scenario did not happen");
}

void assertTheWriteWasRefusedAndNothingIsLeftWaiting(ModuleUnderTest& module)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.catalogWriter.taskStartFailureCount(),
                                     "the refused task was not counted (catalog_task_fail)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.catalogWriter.refusedCount(),
                                     "a refused task was counted as a full queue");
    TEST_ASSERT_FALSE_MESSAGE(module.syncSensorTypes.isWritePending(),
                              "the write is waiting for a task that does not exist - the next "
                              "synchronization would find it 'still in flight', and so would every "
                              "one after it");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.syncSensorTypes.writeCount(),
                                     "a write was accepted with no task to make it");
    TEST_ASSERT_FALSE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                              "a catalog that was never stored was reported synchronized");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0,
                                     module.scheduler.createdTaskCount(SensorTypeCatalogWriter::TASK_NAME),
                                     "a storage task exists although its creation was refused");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.flash.writeOpsTotal(),
                                     "the flash was written although no task could write it");
    TEST_ASSERT_FALSE_MESSAGE(module.flash.fileExists(CATALOG_FILE),
                              "a catalog file appeared although the write was refused");
}

} // namespace

static void test_a_storage_task_that_could_not_be_created_refuses_the_write_and_the_next_sync_stores_it(void)
{
    ModuleUnderTest module;
    // What the refusal leaves in the log is part of the claim; the sync logs only with its debug
    // feature on.
    module.debugSystem.featureEnabled = true;
    module.flash.startCharging();

    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence);

    synchronizeWithTheStorageTaskRefused(module);
    assertTheWriteWasRefusedAndNothingIsLeftWaiting(module);
    TEST_ASSERT_TRUE_MESSAGE(module.logger.hasMessageContaining("could not create the task"),
                             "the refused task left no line in the log");
    TEST_ASSERT_TRUE_MESSAGE(module.logger.hasMessageContaining("reason=storage_task_refused"),
                             "the refused write was not reported with its reason");

    // The next synchronization - a forced one here, as a change of local IP asks for it.
    synchronizeOnce(module);
    cadence.closeAt(module.clock.nowMs());

    assertRunIsSound(module, cadence);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, module.server.getSensorTypesListCallCount,
                                  "the second synchronization did not fetch the catalog");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1,
                                     module.scheduler.createdTaskCount(SensorTypeCatalogWriter::TASK_NAME),
                                     "the second synchronization did not create the storage task");
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the second synchronization was not reported stored");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(referenceFileFor(makeCatalog(CATALOG_TYPES), module).c_str(),
                                     module.flash.getFileContent(CATALOG_FILE).c_str(),
                                     "the catalog stored after the refusal is not the server's");
    assertTheStorageTaskIsGone(module, "after the write that followed the refused one");
    assertLoopStayedBounded(cadence, "a storage task was refused and the next one wrote the catalog");
}

// --------------------------------------------------------------------------------------------
// 11. And with nobody forcing a synchronization: the module asks again by itself. The refusal is a
//     failed synchronization (SyncSensorTypesUseCase::trackSyncAttempt(false)), which arms the
//     use case's own loop() to fetch again - on the edge of its offline decision clearing, or,
//     with no such edge, once its safety-net interval has passed since the fetch. This module has
//     no offline decision wired and no change of IP, so the interval is the one that answers.
// --------------------------------------------------------------------------------------------

namespace {

/** SyncSensorTypesUseCase's safety-net interval: six hours from the last fetch. */
const uint64_t REARM_SAFETY_INTERVAL_MS = 6ULL * 60 * 60 * 1000;

/** A control loop of one second: six hours of it are 21 600 steps rather than two million. */
const uint32_t SLOW_CONTROL_PERIOD_MS = 1000;

} // namespace

static void test_after_a_refused_storage_task_the_module_asks_again_on_its_own(void)
{
    ModuleUnderTest module;
    CadenceRecorder cadence;
    module.startArduinoLoopTask(cadence, SLOW_CONTROL_PERIOD_MS);

    const uint64_t fetchedAtMs = module.clock.nowMs();
    synchronizeWithTheStorageTaskRefused(module);
    assertTheWriteWasRefusedAndNothingIsLeftWaiting(module);

    // A minute short of the interval: nothing has asked again.
    module.scheduler.runUntilMs(fetchedAtMs + REARM_SAFETY_INTERVAL_MS - 60000);
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.server.getSensorTypesListCallCount,
                                  "something asked for the catalog before the safety-net interval - "
                                  "this scenario would not show which");
    TEST_ASSERT_FALSE_MESSAGE(module.flash.fileExists(CATALOG_FILE),
                              "the catalog was stored before anything asked again");

    // And a minute past it.
    module.scheduler.runUntilMs(fetchedAtMs + REARM_SAFETY_INTERVAL_MS + 60000);
    cadence.closeAt(module.clock.nowMs());

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, module.server.getSensorTypesListCallCount,
                                  "the module did not ask for the catalog again on its own");
    TEST_ASSERT_TRUE_MESSAGE(module.syncSensorTypes.getLastSyncStatus(),
                             "the synchronization the module asked for itself was not stored");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(referenceFileFor(makeCatalog(CATALOG_TYPES), module).c_str(),
                                     module.flash.getFileContent(CATALOG_FILE).c_str(),
                                     "the catalog stored after the refusal is not the server's");
    assertTheStorageTaskIsGone(module, "after the write the module asked for itself");
}

extern "C" void run_catalog_write_off_loop_tests(void)
{
    RUN_TEST(test_the_catalog_is_stored_without_the_control_loop_waiting_for_flash);
    RUN_TEST(test_a_refused_catalog_write_is_reported_as_a_failed_synchronization);
    RUN_TEST(test_a_type_the_server_withdrew_is_pruned_on_the_way_to_flash);
    RUN_TEST(test_the_storage_task_ends_once_a_fresh_module_has_stored_its_catalog);
    RUN_TEST(test_the_storage_task_ends_once_an_edited_catalog_is_stored);
    RUN_TEST(test_a_second_catalog_write_is_stored_like_the_first);
    RUN_TEST(test_neither_write_runs_on_the_control_loop_or_the_bus_task);
    RUN_TEST(test_a_finished_write_leaves_nothing_held);
    RUN_TEST(test_a_storage_task_that_ended_leaves_the_watchdog_nothing_to_fire_on);
    RUN_TEST(test_a_sensor_type_is_serialized_for_the_file_into_one_block_of_its_length);
    RUN_TEST(test_a_storage_task_that_could_not_be_created_refuses_the_write_and_the_next_sync_stores_it);
    RUN_TEST(test_after_a_refused_storage_task_the_module_asks_again_on_its_own);
}
