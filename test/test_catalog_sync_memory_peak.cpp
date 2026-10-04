// What one catalog synchronization has to have free at once.
//
// The catalog endpoint answers with an array of sensor models - 1.4 to 2 KB of JSON per type, so
// twelve types are ~16 KB and an account with forty is around 55 KB. A board that has ~46 KB of
// heap at that moment has to hold, at the same instant: the body of the answer, the types built
// out of it, the catalog it already has, and the merged catalog on its way to flash. The board
// measured on 27.08 came within 2 388 bytes of the wall in that second with twelve types; the
// arithmetic of an account with forty does not fit at all.
//
// So the number this file is about is not "how much does the sync allocate" - it is "how much does
// the sync need on top of what it produces, and does that follow the size of the catalog". A cost
// that is a function of the account's catalog is a module that works on the stand and fails in the
// field; a cost that is one sensor type plus a constant is the same second at any catalog size.
//
// Measured with Sim/HostAllocationCounter.h, which counts every operator new the test binary makes
// and samples the live balance at each of them. Each scenario is measured twice - at twelve types
// and at forty-eight - and the verdict is about the difference between the two, so nothing here
// depends on a magic number: the plank is derived from the weight of one sensor type as this
// module holds it, computed from the same catalogs the scenario runs on.
//
// The second is measured in the three windows it is made of - reading the answer, merging it, and
// settling once it is stored - rather than as one. The flash device is simulated by a map of
// strings in this same heap, and a window with a file write inside it would report the stand-in's
// file as the module's memory; the windows are placed so that no file write falls inside one.
//
// What runs is production code. The real RestAPIService reads a body the real
// ArduinoJsonConverter parses, the real Outbox carries the request on its own task, the real
// SyncSensorTypesUseCase merges into the real SensorTypeRepository over the real
// SpiffsSensorTypeDao, and the real SensorTypeCatalogWriter writes it on the storage task.
// Replaced are the socket (Sim/SimHttpClient.h), the flash device and time.

#include <unity.h>

#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Storage/SensorTypeCatalogWriter.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Controllers/Api/HybridAPIService.h"
#include "Infrastructure/Controllers/HTTP/RestApiService.h"
#include "Infrastructure/Repositories/SensorTypeRepository.h"

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
#include "Sim/SimHttpClient.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimHttpClient;
using hostsim::VirtualClock;

namespace {

/** The catalog of the stand the 2 388-byte measurement came from. */
const size_t SMALL_CATALOG = 12;

/** And a field account, at the top of the range the accounts in production hold. */
const size_t LARGE_CATALOG = 48;

const char* const STORED_TIMESTAMP = "2026-01-04T10:00:00.000000+00:00";
const char* const PUBLISHED_TIMESTAMP = "2026-02-11T09:01:35.443520+00:00";
/** The edit after that one: what the server publishes for the measured synchronization. */
const char* const REPUBLISHED_TIMESTAMP = "2026-03-02T14:22:07.101010+00:00";

/** See the comment at the task constructions below: host frames, not the device's. */
const uint32_t HOST_TASK_STACK_BYTES = 512 * 1024;

const char* const CATALOG_FILE = "/data/sensorTypes.json";

const char* const SERVER_URL = "http://gd.test";
const char* const TOKEN = "TOK123";
const char* const MODULE_UID = "UID-1";

// --- the catalog the measurements run on ----------------------------------------------------

/**
 * @brief A sensor type of the weight a live catalog entry has: two channels, ~1.8 KB of JSON.
 *
 * Two and not more, because of a ceiling that has nothing to do with what is measured here:
 * ArduinoJsonConverter - the converter the device runs - reads one sensor type into a document of
 * 8 192 bytes, and a type of this shape stops fitting at seven channels (~5 KB of JSON). Above
 * that the type does not parse, from the network or from flash, whatever it costs.
 */
SensorType makeSensorType(const std::string& uid, const std::string& name, const char* timestamp)
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
    sensorType.updatedAt = timestamp;

    for (int channel = 1; channel <= 2; channel++)
    {
        SubSensorType subType;
        subType.name = name + "_channel_" + std::to_string(channel);
        subType.displayName = name + " channel " + std::to_string(channel);
        subType.minValue = 0.0f;
        subType.maxValue = 1000.0f;
        subType.unit = "unit_" + std::to_string(channel);
        subType.createdAt = "2025-09-22T07:04:26.803128+00:00";
        subType.updatedAt = timestamp;
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

std::vector<SensorType> makeCatalog(size_t count, const char* timestamp)
{
    std::vector<SensorType> catalog;
    catalog.reserve(count);
    for (size_t i = 0; i < count; i++)
    {
        catalog.push_back(makeSensorType("type-uid-" + std::to_string(i),
                                         "sensor_model_" + std::to_string(i),
                                         timestamp));
    }
    return catalog;
}

/** @brief The catalog as the backend puts it on the wire: the array the endpoint answers with. */
std::string catalogJson(const std::vector<SensorType>& catalog, IJsonConverter& converter)
{
    std::string body = "[";
    for (size_t i = 0; i < catalog.size(); i++)
    {
        if (i > 0)
        {
            body += ",";
        }
        body += converter.sensorTypeToJson(catalog[i]);
    }
    body += "]";
    return body;
}

// --- measuring --------------------------------------------------------------------------------

/**
 * @brief What a measured step held, and what it needed on top of that.
 *
 * `before` and `after` are the live balances the step runs between - both hold whatever the module
 * already had, so neither is a number to read on its own. `peak` is the largest balance seen
 * inside the step. What the step needed above the states it connects is the peak minus the larger
 * of the two, and that is the only figure any assertion here is made on.
 */
struct PeakMeasurement {
    long long before = 0;
    long long after = 0;
    long long peak = 0;
    size_t typeCount = 0;
    /** Bytes of one sensor type as this module holds it, for the plank. */
    long long typeBytes = 0;

    long long overhead() const
    {
        const long long floorBytes = (after > before) ? after : before;
        return peak - floorBytes;
    }
};

/** @brief Bytes a catalog of `count` types occupies in RAM, measured rather than assumed. */
long long catalogBytesFor(size_t count, const char* timestamp)
{
    const long long before = HostAllocationCounter::liveBytes();
    std::vector<SensorType> catalog = makeCatalog(count, timestamp);
    const long long after = HostAllocationCounter::liveBytes();
    return after - before;
}

// --- 1. reading the answer --------------------------------------------------------------------

/**
 * @brief The flash device, counting how often the stored catalog is read off it.
 *
 * The file is ~1.8 KB per sensor type and is read into one string; whether the module reads it
 * back in the second it has just written it is a question the byte counters answer only
 * indirectly, and this one answers directly.
 */
class CountingFlash : public MockSpiffsAdapter {
public:
    std::string readSPIFFSstr(const char* filePath) override
    {
        if (filePath != nullptr && std::string(filePath) == CATALOG_FILE)
        {
            catalogReads++;
        }
        return MockSpiffsAdapter::readSPIFFSstr(filePath);
    }

    uint32_t catalogReads = 0;
};

/** @brief The transport, with the socket replaced and nothing else. */
struct FetchFixture {
    MockModuleConfig moduleConfig;
    MockSettings settings;
    MockLogger logger;
    MockDebugSystem debugSystem;
    ArduinoJsonConverter jsonConverter;
    SimHttpClient http;
    RestAPIService service;

    FetchFixture()
        : service(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter)
    {
        debugSystem.featureEnabled = false;
        settings.setServerUrl(SERVER_URL);
        settings.setToken(TOKEN);
        moduleConfig.setModuleUid(MODULE_UID);
        service.init();
    }
};

PeakMeasurement measureCatalogFetch(size_t typeCount)
{
    FetchFixture fixture;
    const std::string body =
        catalogJson(makeCatalog(typeCount, PUBLISHED_TIMESTAMP), fixture.jsonConverter);
    fixture.http.alwaysRespond(200, body.c_str());

    {
        // One fetch before the measured one. Whatever a first parse allocates once - the JSON
        // document pool takes its slots on first use - belongs to neither catalog size and would
        // be read as a cost of the smaller one.
        bool warmupFailed = false;
        std::vector<SensorType> warmup = fixture.service.getSensorTypesList(&warmupFailed);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(typeCount, warmup.size(),
                                         "the warm-up fetch did not read the catalog");
    }

    PeakMeasurement measurement;
    measurement.typeBytes = catalogBytesFor(1, PUBLISHED_TIMESTAMP);

    HostAllocationCounter::resetPeak();
    measurement.before = HostAllocationCounter::liveBytes();

    bool httpFailed = false;
    std::vector<SensorType> types = fixture.service.getSensorTypesList(&httpFailed);

    measurement.after = HostAllocationCounter::liveBytes();
    measurement.peak = HostAllocationCounter::peakBytes();
    measurement.typeCount = types.size();

    TEST_ASSERT_FALSE_MESSAGE(httpFailed, "the catalog fetch failed - the scenario did not happen");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(typeCount, types.size(),
                                     "the fetch did not read the whole catalog");
    return measurement;
}

// --- 2 and 3. applying the answer -------------------------------------------------------------

/**
 * @brief A module wired the way globals.h wires it, from the socket to the flash device.
 */
struct SyncFixture {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    MockSensorRepository sensorRepository;

    CountingFlash flash;
    ArduinoJsonConverter jsonConverter;
    SpiffsSensorTypeDao dao;
    SensorTypeRepository repository;
    SensorTypeCatalogWriter catalogWriter;

    SimHttpClient http;
    RestAPIService restApi;
    HybridAPIService api;

    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    SyncSensorTypesUseCase sync;

    SyncFixture()
        : scheduler(clock),
          arduino(clock, &scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          dao(&flash, &jsonConverter, &debugSystem, &logger),
          repository(&dao, &debugSystem, &logger),
          catalogWriter(&dao, &scheduler, &logger, &debugSystem, HOST_TASK_STACK_BYTES),
          restApi(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter),
          api(&restApi, nullptr, &moduleConfig, &logger, &debugSystem),
          completions(16),
          // Host stacks, not the device's. A fiber gets exactly the bytes it is asked for, and a
          // host frame is several times a device frame - the transport's own 2 KB log buffer plus
          // the JSON parser's recursion overflow the 8 KB the device gives these tasks. What is
          // measured here is the heap; the stack watermarks are measured on the board.
          outbox(&api, &scheduler, &completions, &logger, &debugSystem, HOST_TASK_STACK_BYTES),
          sync(&api, &repository, &arduino, &timeAdapter, &jsonConverter, &logger, &debugSystem,
               &sensorRepository)
    {
        debugSystem.featureEnabled = false;
        timeAdapter.setEpochTime(1755690000);
        moduleConfig.setModuleUid(MODULE_UID);
        settings.setServerUrl(SERVER_URL);
        settings.setToken(TOKEN);
        restApi.init();

        sync.setOutbox(&outbox);
        sync.setCatalogWriter(&catalogWriter);
    }

    /** @brief The catalog earlier synchronizations left on flash, and in the repository's cache. */
    void seedStoredCatalog(size_t typeCount)
    {
        TEST_ASSERT_TRUE_MESSAGE(dao.saveAll(makeCatalog(typeCount, STORED_TIMESTAMP)),
                                 "the scenario could not put a catalog on flash");
        repository.invalidateCache();
        // Warm, the way a module that has booted and read its catalog is warm: the loop holds the
        // catalog in the repository's cache when the answer arrives.
        TEST_ASSERT_EQUAL_size_t_MESSAGE(typeCount, repository.getAll().size(),
                                         "the seeded catalog is not in the repository");
    }

    /** @brief What the server publishes now: the same models, edited. */
    void publishCatalog(size_t typeCount, const char* timestamp = PUBLISHED_TIMESTAMP)
    {
        http.alwaysRespond(200,
                           catalogJson(makeCatalog(typeCount, timestamp), jsonConverter).c_str());
    }

    /** @brief Collect the write the apply handed to the storage task, on the control loop. */
    void collectCatalogWrites()
    {
        std::vector<CatalogWriteCompletion> writes;
        if (!catalogWriter.drainResults(writes))
        {
            return;
        }
        for (size_t i = 0; i < writes.size(); i++)
        {
            sync.onCatalogWriteCompletion(writes[i]);
        }
    }

    /** @brief What a module that has just booted would read out of storage. */
    size_t storedCatalogSize()
    {
        SpiffsSensorTypeDao daoAfterBoot(&flash, &jsonConverter, &debugSystem, &logger);
        std::vector<SensorType> stored;
        daoAfterBoot.getAll(stored);
        return stored.size();
    }
};

/**
 * @brief Bring a module to the moment the answer is in the outbox's slot and nothing is applied.
 *
 * Everything a measurement needs to exist beforehand exists here: the catalog on flash and in the
 * repository's cache, the types the server sent held by the outbox - and the outbox's task, which
 * the first request creates. The module here runs one complete synchronization first, exactly as a
 * module that has synchronized before, and what the measured window then contains is the work.
 *
 * The storage task is not among what exists beforehand: it lives only while a write runs, so the
 * merge window creates it, at the end, when the merged catalog is handed over - as the device
 * does. What that adds to the window is the scheduler's record of the task, the same few bytes at
 * every catalog size. Not the task's stack: a host fiber's stack is not metered (Sim/HostFiber),
 * and had it been, the half megabyte of HOST_TASK_STACK_BYTES would be a floor under the balance
 * at the end that every transient below it is subtracted away by.
 */
void bringToDeliveredCatalog(SyncFixture& fixture, size_t typeCount, OutboxCompletion& completion)
{
    fixture.seedStoredCatalog(typeCount);
    fixture.publishCatalog(typeCount);

    // One synchronization before the measured one, carried to its end: it creates the outbox's
    // task, and it leaves the module holding exactly what the server publishes.
    fixture.sync.forceSync();
    fixture.scheduler.runForMs(5000);
    OutboxCompletion warmup;
    TEST_ASSERT_TRUE_MESSAGE(fixture.completions.pop(warmup),
                             "the outbox never answered the first catalog request");
    fixture.sync.onOutboxCompletion(warmup);
    fixture.scheduler.runForMs(5000);
    fixture.collectCatalogWrites();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, fixture.catalogWriter.taskStartCount(),
                                     "the first catalog was not written by the storage task");
    TEST_ASSERT_TRUE_MESSAGE(fixture.sync.getLastSyncStatus(),
                             "the first synchronization did not land before the measured one");

    // And now the answer the measurement is about: the server has edited every type again, so the
    // measured merge does the most work a merge can do.
    fixture.publishCatalog(typeCount, REPUBLISHED_TIMESTAMP);
    fixture.sync.forceSync();
    fixture.scheduler.runForMs(5000);

    TEST_ASSERT_TRUE_MESSAGE(fixture.completions.pop(completion),
                             "the outbox never answered the catalog request");
    TEST_ASSERT_TRUE_MESSAGE(completion.delivered,
                             "the catalog was not delivered - the scenario did not happen");
}

/**
 * @brief Measure the merge: the step the control loop runs when the answer arrives.
 *
 * What it is given - the types the server sent, and the catalog the module holds - is alive before
 * the measurement begins, so the figure is what merging the two costs on top of them.
 *
 * The window ends where the merged catalog is handed to the storage task, and that is deliberate:
 * the flash device is simulated by a map of strings in this same heap, so a window containing the
 * write would be measuring the stand-in's file rather than the module's memory. What the write
 * itself costs is bounded by its own test - test_sensor_types_bulk_save asserts the catalog leaves
 * for storage one sensor type at a time.
 */
PeakMeasurement measureCatalogMerge(size_t typeCount, bool holdOneCatalogInside = false)
{
    SyncFixture fixture;
    OutboxCompletion completion;
    bringToDeliveredCatalog(fixture, typeCount, completion);

    PeakMeasurement measurement;
    measurement.typeBytes = catalogBytesFor(1, PUBLISHED_TIMESTAMP);

    HostAllocationCounter::resetPeak();
    measurement.before = HostAllocationCounter::liveBytes();

    {
        // The control: one copy of the catalog, alive only inside the window and released before
        // it closes - the shape of transient this measurement exists to forbid. Off in the real
        // measurement, on in the one that checks the measurement can see such a thing at all.
        std::vector<SensorType> transientCopy;
        if (holdOneCatalogInside)
        {
            transientCopy = makeCatalog(typeCount, PUBLISHED_TIMESTAMP);
        }

        fixture.sync.onOutboxCompletion(completion);

        measurement.peak = HostAllocationCounter::peakBytes();
    }

    measurement.after = HostAllocationCounter::liveBytes();
    measurement.typeCount = typeCount;

    TEST_ASSERT_TRUE_MESSAGE(fixture.sync.isWritePending(),
                             "the merged catalog was not handed to the storage task");

    // The rest of the synchronization, outside the window: it has to reach its end, or the
    // measurement above is of a step that did not do its job.
    fixture.scheduler.runForMs(5000);
    fixture.collectCatalogWrites();
    TEST_ASSERT_TRUE_MESSAGE(fixture.sync.getLastSyncStatus(),
                             "the synchronization did not succeed - the scenario did not happen");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(typeCount, fixture.storedCatalogSize(),
                                     "the stored catalog is not the one the server published");
    return measurement;
}

/**
 * @brief What happens once the catalog is on flash and the next reader asks for a sensor type.
 *
 * The module has just written the file, out of types it is still holding. If it drops them, the
 * reader loads the file back - the whole file, in one string, plus a parse - in the same second
 * the module has already paid for receiving and merging it. The reader is not hypothetical: the
 * scan and the plausibility gate ask the repository for types, and the acquisition service is
 * given a fresh set every fifteen seconds.
 *
 * The verdict is the number of reads of the stored file, which is where the cost is; the bytes are
 * measured as well and reported, because they are what the reads are worth.
 */
PeakMeasurement measureCatalogSettle(size_t typeCount, uint32_t& catalogReads)
{
    SyncFixture fixture;
    OutboxCompletion completion;
    bringToDeliveredCatalog(fixture, typeCount, completion);

    fixture.sync.onOutboxCompletion(completion);
    fixture.scheduler.runForMs(5000);

    std::vector<CatalogWriteCompletion> writes;
    TEST_ASSERT_TRUE_MESSAGE(fixture.catalogWriter.drainResults(writes),
                             "the storage task never reported the write");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, writes.size(), "more than one write was made");
    TEST_ASSERT_TRUE_MESSAGE(writes[0].written, "the catalog was not written");

    PeakMeasurement measurement;
    measurement.typeBytes = catalogBytesFor(1, PUBLISHED_TIMESTAMP);

    fixture.flash.catalogReads = 0;
    HostAllocationCounter::resetPeak();
    measurement.before = HostAllocationCounter::liveBytes();

    fixture.sync.onCatalogWriteCompletion(writes[0]);
    // A reader that asks for one sensor type, which is what the scan and the plausibility gate do.
    // getAll() would answer with a copy of the whole catalog - a cost of its own, unrelated to what is measured and
    // large enough to hide it.
    const bool readerFound = fixture.repository.get("type-uid-0").has_value();

    measurement.after = HostAllocationCounter::liveBytes();
    measurement.peak = HostAllocationCounter::peakBytes();
    measurement.typeCount = typeCount;
    catalogReads = fixture.flash.catalogReads;

    TEST_ASSERT_TRUE_MESSAGE(fixture.sync.getLastSyncStatus(),
                             "the synchronization did not succeed - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(readerFound,
                             "the reader after the sync did not find a type of the stored catalog");
    return measurement;
}

/**
 * @brief How much more the overhead may be at forty-eight types than at twelve.
 *
 * One sensor type - not one per type, one in total. Reading the answer and applying it both cost
 * what they cost for one type at a time, so thirty-six types more may not cost measurably more at
 * all, and the plank is set at the smallest thing this module could possibly hold: a single type.
 * Anything proportional to the catalog - a copy of it, an index over it, a list as long as it -
 * is thirty-six times this at the sizes measured here and fails loudly.
 */
long long growthBudget(long long typeBytes)
{
    return typeBytes;
}

void report(const char* what, const PeakMeasurement& small, const PeakMeasurement& large)
{
    printf("  catalog %s: %u types needed %lld bytes above what it held, %u types needed %lld "
           "(one type is %lld bytes)\n",
           what,
           (unsigned)small.typeCount, small.overhead(),
           (unsigned)large.typeCount, large.overhead(),
           large.typeBytes);
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. Reading the answer. What the transport needs on top of the types it hands back must be one
//    object's worth of text and one document - not the body of the answer.
// --------------------------------------------------------------------------------------------

static void test_reading_a_catalog_costs_one_type_and_not_the_catalog(void)
{
    const PeakMeasurement small = measureCatalogFetch(SMALL_CATALOG);
    const PeakMeasurement large = measureCatalogFetch(LARGE_CATALOG);
    report("fetch", small, large);

    const long long growth = large.overhead() - small.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "reading %u types needed %lld bytes above the catalog it built and reading %u needed "
             "%lld - %lld bytes more for %u types more, which is the answer being held whole "
             "(one sensor type is %lld bytes)",
             (unsigned)SMALL_CATALOG, small.overhead(), (unsigned)LARGE_CATALOG, large.overhead(),
             growth, (unsigned)(LARGE_CATALOG - SMALL_CATALOG), large.typeBytes);
    TEST_ASSERT_TRUE_MESSAGE(growth <= growthBudget(large.typeBytes), message);
}

// --------------------------------------------------------------------------------------------
// 2. Applying the answer. The merge is given the types the server sent and the catalog the module
//    holds; what it needs above the two of them may not be a third and a fourth copy of them.
// --------------------------------------------------------------------------------------------

static void test_applying_a_catalog_costs_one_type_and_not_the_catalog(void)
{
    const PeakMeasurement small = measureCatalogMerge(SMALL_CATALOG);
    const PeakMeasurement large = measureCatalogMerge(LARGE_CATALOG);
    report("merge", small, large);

    const long long growth = large.overhead() - small.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "applying %u types needed %lld bytes above the answer and the stored catalog, "
             "applying %u needed %lld - %lld bytes more for %u types more, which is another copy "
             "of the catalog (one sensor type is %lld bytes)",
             (unsigned)SMALL_CATALOG, small.overhead(), (unsigned)LARGE_CATALOG, large.overhead(),
             growth, (unsigned)(LARGE_CATALOG - SMALL_CATALOG), large.typeBytes);
    TEST_ASSERT_TRUE_MESSAGE(growth <= growthBudget(large.typeBytes), message);
}

/**
 * And the control on the measurement above: a window that cannot see a copy of the catalog inside
 * it proves nothing by staying green.
 *
 * The same window, the same plank, with one copy of the catalog deliberately alive inside it and
 * released before it closes - the exact shape a merge that copied its way through would have. It
 * has to fail the plank, and by a margin that grows with the catalog, or the number the scenario
 * above reports is a floor rather than a measurement.
 */
static void test_the_merge_window_sees_a_catalog_held_inside_it(void)
{
    const PeakMeasurement small = measureCatalogMerge(SMALL_CATALOG, true);
    const PeakMeasurement large = measureCatalogMerge(LARGE_CATALOG, true);
    report("merge with one catalog held inside the window", small, large);

    const long long growth = large.overhead() - small.overhead();

    char message[288];
    snprintf(message, sizeof(message),
             "a copy of the catalog held inside the measured window moved it by %lld bytes at %u "
             "types and %lld at %u - a growth of %lld against a plank of %lld, so the window "
             "cannot see a transient of that size and its verdict means nothing",
             small.overhead(), (unsigned)SMALL_CATALOG, large.overhead(), (unsigned)LARGE_CATALOG,
             growth, growthBudget(large.typeBytes));
    TEST_ASSERT_TRUE_MESSAGE(growth > growthBudget(large.typeBytes), message);
}

// --------------------------------------------------------------------------------------------
// 3. And the end of that second: the catalog is on flash and the next reader asks for it. Reading
//    back the file the module has just written costs the file, in one string, at the worst moment
//    there is - and the module already holds every type that is in it.
// --------------------------------------------------------------------------------------------

static void test_the_catalog_just_stored_is_not_read_back_from_flash(void)
{
    uint32_t smallReads = 0;
    uint32_t largeReads = 0;
    const PeakMeasurement small = measureCatalogSettle(SMALL_CATALOG, smallReads);
    const PeakMeasurement large = measureCatalogSettle(LARGE_CATALOG, largeReads);
    report("settle", small, large);
    printf("  catalog settle: the stored file was read %u times after a %u-type write and %u times "
           "after a %u-type one\n",
           (unsigned)smallReads, (unsigned)SMALL_CATALOG, (unsigned)largeReads,
           (unsigned)LARGE_CATALOG);

    char message[288];
    snprintf(message, sizeof(message),
             "the module read the catalog file %u times in the second it wrote it, with a reader "
             "asking for one of the %u types it had just written (%lld bytes needed above what it "
             "was holding)",
             (unsigned)largeReads, (unsigned)LARGE_CATALOG, large.overhead());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, largeReads, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, smallReads, message);
}

extern "C" int run_catalog_sync_memory_peak_tests(void)
{
    RUN_TEST(test_reading_a_catalog_costs_one_type_and_not_the_catalog);
    RUN_TEST(test_applying_a_catalog_costs_one_type_and_not_the_catalog);
    RUN_TEST(test_the_merge_window_sees_a_catalog_held_inside_it);
    RUN_TEST(test_the_catalog_just_stored_is_not_read_back_from_flash);
    return 0;
}
