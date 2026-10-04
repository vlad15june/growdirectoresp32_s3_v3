// What the control loop reads while the catalog is being written, and what the module is left
// holding when that write is refused.
//
// SensorTypeCatalogWriter is a task that writes the catalog file without a lock around it, and the
// thing that makes that safe is written down in its own header: "the catalog the control loop reads
// comes from the repository's cache, which stays valid until the loop itself drops it, and the loop
// drops it only when it collects the outcome of the write - by which time this task is finished
// with the file." Everything in this file is that sentence, executed.
//
// Two consequences if the cache is not there for the length of the write:
//
//  - the first reader on the loop goes to the DAO and loads the whole file, in one string plus one
//    parse, at the exact moment the storage task is rewriting that file. That is both the transient
//    this catalog work exists to remove and a second reader of a file being replaced;
//  - the replacement is a sequence of renames, and between two of them the catalog exists under no
//    working name at all. A reader landing there finds no file, takes the recovery path, adopts the
//    temporary file the storage task is still publishing, renames it and drops the companion. The
//    write then reports itself refused although the new catalog is on flash, and the module records
//    a failed synchronization and re-arms - a false outcome on healthy hardware.
//
// And when a write really is refused - a full or broken file system - the module has to be left
// holding the catalog it had. That is the one it can still work from, and on the file system that
// just refused a write, reading the file back is not something to count on.

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

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimHttpClient.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimHttpClient;
using hostsim::VirtualClock;

namespace {

const size_t CATALOG_TYPES = 12;
const char* const CATALOG_FILE = "/data/sensorTypes.json";
const char* const STORED_TIMESTAMP = "2026-01-04T10:00:00.000000+00:00";
const char* const PUBLISHED_TIMESTAMP = "2026-02-11T09:01:35.443520+00:00";
const uint32_t HOST_TASK_STACK_BYTES = 512 * 1024;
const char* const SERVER_URL = "http://gd.test";
const char* const TOKEN = "TOK123";
const char* const MODULE_UID = "UID-1";

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
        subType.unit = "unit_" + std::to_string(channel);
        sensorType.sensorTypes.push_back(subType);

        ModbusRegisterConfig config;
        config.functionCode = 3;
        config.startRegister = channel;
        config.numRegisters = 2;
        config.channel = channel;
        config.channelName = name + "_channel_" + std::to_string(channel);
        config.sensorType = name + "_channel_" + std::to_string(channel);
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
                                         "sensor_model_" + std::to_string(i), timestamp));
    }
    return catalog;
}

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

/**
 * @brief The flash device: counts reads of the catalog and can run a reader mid-replacement.
 *
 * The replacement sequence itself is the product's own SpiffsFileReplacer; what is added here is a
 * hook on the rename that moves the catalog aside, which is the instant the file exists under no
 * working name.
 */
class ProbeFlash : public MockSpiffsAdapter {
public:
    uint32_t catalogReads = 0;
    std::function<void()> onTargetMovedAside;

    std::string readSPIFFSstr(const char* filePath) override
    {
        if (filePath != nullptr && std::string(filePath) == CATALOG_FILE)
        {
            catalogReads++;
        }
        return MockSpiffsAdapter::readSPIFFSstr(filePath);
    }

    bool renameFile(const char* fromPath, const char* toPath) override
    {
        const bool renamed = MockSpiffsAdapter::renameFile(fromPath, toPath);
        if (renamed && fromPath != nullptr && toPath != nullptr &&
            std::string(fromPath) == CATALOG_FILE &&
            std::string(toPath) == std::string(CATALOG_FILE) + ".bak" && onTargetMovedAside)
        {
            std::function<void()> reader = onTargetMovedAside;
            onTargetMovedAside = nullptr;
            reader();
        }
        return renamed;
    }
};

/** @brief The module, wired the way globals.h wires it, from the socket to the flash device. */
struct ModuleUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    MockSensorRepository sensorRepository;

    ProbeFlash flash;
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

    ModuleUnderTest()
        : scheduler(clock),
          arduino(clock, &scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          dao(&flash, &jsonConverter, &debugSystem, &logger),
          repository(&dao, &debugSystem, &logger),
          // Host stacks, not the device's: a fiber gets exactly the bytes it is asked for, and a
          // host frame is several times a device frame.
          catalogWriter(&dao, &scheduler, &logger, &debugSystem, HOST_TASK_STACK_BYTES),
          restApi(&http, &moduleConfig, &settings, &logger, &debugSystem, &jsonConverter),
          api(&restApi, nullptr, &moduleConfig, &logger, &debugSystem),
          completions(16),
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

    /** The catalog earlier synchronizations left on flash, and in the repository's cache. */
    void seedStoredCatalog(size_t typeCount)
    {
        TEST_ASSERT_TRUE_MESSAGE(dao.saveAll(makeCatalog(typeCount, STORED_TIMESTAMP)),
                                 "the scenario could not put a catalog on flash");
        repository.invalidateCache();
        TEST_ASSERT_EQUAL_size_t_MESSAGE(typeCount, repository.getAll().size(),
                                         "the seeded catalog is not in the repository");
    }

    /** What the server publishes now: the same models, edited. */
    void publishCatalog(size_t typeCount)
    {
        http.alwaysRespond(
            200, catalogJson(makeCatalog(typeCount, PUBLISHED_TIMESTAMP), jsonConverter).c_str());
    }

    /** Bring the module to the moment the merged catalog is on its way to flash. */
    void bringToSubmittedWrite()
    {
        seedStoredCatalog(CATALOG_TYPES);
        publishCatalog(CATALOG_TYPES);

        sync.forceSync();
        scheduler.runForMs(5000);

        OutboxCompletion completion;
        TEST_ASSERT_TRUE_MESSAGE(completions.pop(completion),
                                 "the outbox never answered the catalog request");
        TEST_ASSERT_TRUE_MESSAGE(completion.delivered, "the catalog was not delivered");
        sync.onOutboxCompletion(completion);
        TEST_ASSERT_TRUE_MESSAGE(sync.isWritePending(),
                                 "the merged catalog was not handed to the storage task");
    }

    /** Collect the outcome of the write on the control loop, as the loop service does. */
    size_t collectCatalogWrites(bool& written)
    {
        std::vector<CatalogWriteCompletion> writes;
        written = false;
        if (!catalogWriter.drainResults(writes))
        {
            return 0;
        }
        for (size_t i = 0; i < writes.size(); i++)
        {
            written = writes[i].written;
            sync.onCatalogWriteCompletion(writes[i]);
        }
        return writes.size();
    }

    /** What a module that has just booted would read out of storage. */
    size_t storedCatalogSize()
    {
        SpiffsSensorTypeDao daoAfterBoot(&flash, &jsonConverter, &debugSystem, &logger);
        std::vector<SensorType> stored;
        daoAfterBoot.getAll(stored);
        return stored.size();
    }
};

} // namespace

// --------------------------------------------------------------------------------------------
// 1. A reader on the control loop while the write is in flight. It is answered, and the file is
//    not touched: the storage task is the only thing reading or writing it just now.
// --------------------------------------------------------------------------------------------

static void test_a_reader_while_the_catalog_is_being_written_does_not_touch_the_file(void)
{
    ModuleUnderTest module;
    module.bringToSubmittedWrite();

    module.flash.catalogReads = 0;
    const Utils::Optional<SensorType> found = module.repository.get("type-uid-0");
    const uint32_t readsDuringFlight = module.flash.catalogReads;

    TEST_ASSERT_TRUE_MESSAGE(found.has_value(),
                             "the control loop was told the module has no such sensor type while "
                             "its catalog was being written");

    char message[224];
    snprintf(message, sizeof(message),
             "a reader on the control loop caused %u read(s) of the catalog file while the storage "
             "task was rewriting it",
             (unsigned)readsDuringFlight);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, readsDuringFlight, message);

    // And the write still finishes the way it should.
    module.scheduler.runForMs(5000);
    bool written = false;
    module.collectCatalogWrites(written);
    TEST_ASSERT_TRUE_MESSAGE(written, "the catalog was not written");
    TEST_ASSERT_TRUE_MESSAGE(module.sync.getLastSyncStatus(),
                             "a stored catalog was not reported as a successful synchronization");
}

// --------------------------------------------------------------------------------------------
// 2. The same reader, landing in the one instant inside the replacement where the catalog exists
//    under no working name. It must not go looking for the file there: the recovery path would
//    take the temporary file away from the task that is publishing it.
// --------------------------------------------------------------------------------------------

static void test_a_reader_inside_the_replacement_does_not_take_the_file_from_the_writer(void)
{
    ModuleUnderTest module;

    size_t seenInsideWindow = 0;
    module.flash.onTargetMovedAside = [&module, &seenInsideWindow]() {
        // What the control loop does at that instant: ask the repository for the catalog.
        seenInsideWindow = module.repository.getAll().size();
    };

    module.bringToSubmittedWrite();
    module.scheduler.runForMs(5000);

    bool written = false;
    const size_t drained = module.collectCatalogWrites(written);

    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, drained, "the storage task did not report its write");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(CATALOG_TYPES, seenInsideWindow,
                                     "the reader inside the replacement window was not answered "
                                     "with the catalog the module holds");

    char message[224];
    snprintf(message, sizeof(message),
             "the write reported itself refused although the catalog is on flash: %u types stored, "
             "sync reported %s",
             (unsigned)module.storedCatalogSize(),
             module.sync.getLastSyncStatus() ? "successful" : "failed");
    TEST_ASSERT_TRUE_MESSAGE(written, message);
    TEST_ASSERT_TRUE_MESSAGE(module.sync.getLastSyncStatus(), message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(CATALOG_TYPES, module.storedCatalogSize(),
                                     "the stored catalog is not the one the server published");
    TEST_ASSERT_FALSE_MESSAGE(module.flash.fileExists((std::string(CATALOG_FILE) + ".tmp").c_str()),
                              "the replacement left its temporary file behind");
}

// --------------------------------------------------------------------------------------------
// 3. A file system that stops taking writes. The module keeps the catalog it had - and does not
//    read the file to find it, because that file system has just refused a write.
// --------------------------------------------------------------------------------------------

static void test_a_refused_write_leaves_the_module_holding_its_catalog(void)
{
    ModuleUnderTest module;
    module.seedStoredCatalog(CATALOG_TYPES);
    module.publishCatalog(CATALOG_TYPES);

    // Writes stop being accepted once the catalog is already on flash; reads still work, which is
    // what a full file system looks like.
    module.flash.setMaxTotalWriteBytes(1);

    module.sync.forceSync();
    module.scheduler.runForMs(5000);
    OutboxCompletion completion;
    TEST_ASSERT_TRUE_MESSAGE(module.completions.pop(completion),
                             "the outbox never answered the catalog request");
    module.sync.onOutboxCompletion(completion);
    module.scheduler.runForMs(5000);

    bool written = true;
    module.collectCatalogWrites(written);
    TEST_ASSERT_FALSE_MESSAGE(written, "the file system was supposed to refuse this write");
    TEST_ASSERT_FALSE_MESSAGE(module.sync.getLastSyncStatus(),
                              "a refused write was reported as a successful synchronization");

    module.flash.catalogReads = 0;
    const size_t held = module.repository.getAll().size();

    char message[224];
    snprintf(message, sizeof(message),
             "after a refused write the module answers with %u sensor types and needed %u read(s) "
             "of the file to do it",
             (unsigned)held, (unsigned)module.flash.catalogReads);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(CATALOG_TYPES, held, message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.flash.catalogReads, message);
}

// --------------------------------------------------------------------------------------------
// 4. And the same with a file system that answers nothing at all. Whether the module still has a
//    catalog may not depend on the flash it has just failed to write to.
// --------------------------------------------------------------------------------------------

static void test_a_refused_write_on_a_dead_file_system_leaves_the_catalog_in_memory(void)
{
    ModuleUnderTest module;
    module.seedStoredCatalog(CATALOG_TYPES);
    module.publishCatalog(CATALOG_TYPES);

    module.sync.forceSync();
    module.scheduler.runForMs(5000);
    OutboxCompletion completion;
    TEST_ASSERT_TRUE_MESSAGE(module.completions.pop(completion),
                             "the outbox never answered the catalog request");

    // The file system dies between the answer and the write - open, read and rename all refuse.
    module.flash.setShouldFail(true);
    module.sync.onOutboxCompletion(completion);
    module.scheduler.runForMs(5000);

    bool written = true;
    module.collectCatalogWrites(written);
    TEST_ASSERT_FALSE_MESSAGE(written, "the file system was supposed to refuse this write");

    const size_t held = module.repository.getAll().size();
    char message[224];
    snprintf(message, sizeof(message),
             "with a file system that answers nothing the module is left holding %u sensor types",
             (unsigned)held);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(CATALOG_TYPES, held, message);
}

// --------------------------------------------------------------------------------------------
// 5. A module whose own catalog cannot be read off flash. The merge still has to ask what it
//    holds - but once, not once per published sensor type: each of those questions is the whole
//    file into one string plus a parser document per type inside it, on the control loop, in the
//    second the answer is already in RAM.
// --------------------------------------------------------------------------------------------

/** @brief Reads of the catalog file made while one answer is applied. */
uint32_t catalogReadsDuringOneMerge(ModuleUnderTest& module, size_t typeCount)
{
    module.publishCatalog(typeCount);
    module.sync.forceSync();
    module.scheduler.runForMs(5000);

    OutboxCompletion completion;
    TEST_ASSERT_TRUE_MESSAGE(module.completions.pop(completion),
                             "the outbox never answered the catalog request");
    TEST_ASSERT_TRUE_MESSAGE(completion.delivered, "the catalog was not delivered");

    module.flash.catalogReads = 0;
    module.sync.onOutboxCompletion(completion);
    return module.flash.catalogReads;
}

static void test_a_merge_asks_storage_once_when_the_catalog_cannot_be_read(void)
{
    ModuleUnderTest module;

    // A catalog file that is there and cannot be read:
    // one sensor type too large for the converter's document fails the parse of the whole file.
    module.seedStoredCatalog(CATALOG_TYPES);
    module.repository.invalidateCache();
    module.flash.writeToFile(CATALOG_FILE, "[{\"uid\":\"type-uid-0\",");

    const uint32_t reads = catalogReadsDuringOneMerge(module, CATALOG_TYPES);

    char message[224];
    snprintf(message, sizeof(message),
             "applying an answer of %u sensor types read the catalog file %u times from a module "
             "that cannot read it at all",
             (unsigned)CATALOG_TYPES, (unsigned)reads);
    TEST_ASSERT_TRUE_MESSAGE(reads <= 1, message);
}

static void test_a_merge_asks_storage_once_when_there_is_no_catalog_at_all(void)
{
    ModuleUnderTest module;

    // A module that has never stored a catalog: the first synchronization after a factory reset.
    const uint32_t reads = catalogReadsDuringOneMerge(module, CATALOG_TYPES);

    char message[224];
    snprintf(message, sizeof(message),
             "the first synchronization of a module with no catalog read the catalog file %u times",
             (unsigned)reads);
    TEST_ASSERT_TRUE_MESSAGE(reads <= 1, message);
}

/** And the ordinary module, whose catalog is in the cache: it goes to the file not at all. */
static void test_a_merge_on_a_warm_catalog_does_not_go_to_the_file(void)
{
    ModuleUnderTest module;
    module.seedStoredCatalog(CATALOG_TYPES);

    const uint32_t reads = catalogReadsDuringOneMerge(module, CATALOG_TYPES);

    char message[192];
    snprintf(message, sizeof(message),
             "applying an answer read the catalog file %u times from a module that had it in RAM",
             (unsigned)reads);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, reads, message);
}

extern "C" int run_catalog_write_in_flight_tests(void)
{
    RUN_TEST(test_a_reader_while_the_catalog_is_being_written_does_not_touch_the_file);
    RUN_TEST(test_a_reader_inside_the_replacement_does_not_take_the_file_from_the_writer);
    RUN_TEST(test_a_refused_write_leaves_the_module_holding_its_catalog);
    RUN_TEST(test_a_refused_write_on_a_dead_file_system_leaves_the_catalog_in_memory);
    RUN_TEST(test_a_merge_asks_storage_once_when_the_catalog_cannot_be_read);
    RUN_TEST(test_a_merge_asks_storage_once_when_there_is_no_catalog_at_all);
    RUN_TEST(test_a_merge_on_a_warm_catalog_does_not_go_to_the_file);
    return 0;
}
