// GDV1-3827: the sensor type catalog must always be stored.
//
// The catalog used to be serialized into one std::string and handed to the file system as a single
// buffer. For 14 sensor types that asked for a ~28 KB contiguous heap block; on a module that had been
// running for a while the largest free block was ~26 KB, the std::bad_alloc was swallowed and turned
// into "[]", the DAO refused the write, and the sync reported "saveAll wrote 0 of 14" with no reason.
// After a "reboot with scan" — which cleared the catalog first — the module came up with no sensor
// types at all and never scanned.
//
// These tests pin the four properties that make the write survive that: the payload is streamed type
// by type, a refusal is reported with a reason, a failed write keeps the previous catalog (and leaves
// no temporary file), and the reboot path no longer throws the catalog away.

#include <unity.h>
#include <algorithm>
#include <new>
#include <string>
#include <vector>

#include "Mock/ArduinoMock.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Core/Domain/Data/Entities/Sensor/SensorType.h"

static const char* BULK_SAVE_FILE = "/data/sensorTypes.json";
static const char* BULK_SAVE_TEMP_FILE = "/data/sensorTypes.json.tmp";

// A sensor type of realistic weight: the live catalog averages ~1.4 KB of JSON per type, with
// XF700A at ~3.9 KB, and that weight is what the single-buffer write could not allocate.
static SensorType createHeavySensorType(const std::string& uid, const std::string& name) {
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " display name for the module structure";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.id = 1;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = "2026-02-11T09:01:35.44352+00:00";

    for (int channel = 1; channel <= 8; channel++) {
        SubSensorType subType;
        subType.name = name + "_channel_" + std::to_string(channel);
        subType.displayName = name + " channel " + std::to_string(channel) + " display name";
        subType.isAnalog = false;
        subType.canCalibrate = (channel % 2) == 0;
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
        config.offset = 0.0f;
        config.channelName = name + "_channel_" + std::to_string(channel);
        config.channel = channel;
        config.sensorType = name + "_channel_" + std::to_string(channel);
        config.canConnectMinValue = 0.0f;
        config.canConnectMaxValue = 1000.0f;
        sensorType.modbusRegisterConfig.push_back(config);
    }

    return sensorType;
}

static std::vector<SensorType> createCatalog(size_t count) {
    std::vector<SensorType> catalog;
    for (size_t i = 0; i < count; i++) {
        catalog.push_back(createHeavySensorType("type-uid-" + std::to_string(i),
                                                "sensor_model_" + std::to_string(i)));
    }
    return catalog;
}

/**
 * @brief Converter on a heap that can only hand out blocks up to `budget` bytes.
 *
 * Serializing one sensor type stays inside the budget; serializing the whole catalog into one string
 * does not, and that allocation is exactly what used to fail on a fragmented heap.
 */
class BudgetedJsonConverter : public JsonConverter {
public:
    explicit BudgetedJsonConverter(size_t budget) : budget(budget) {}

    std::string sensorTypeToJson(const SensorType& sensorType) override {
        std::string result = plain().sensorTypeToJson(sensorType);
        if (result.size() > budget) throw std::bad_alloc();
        return result;
    }

    std::string sensorTypesToJson(const std::vector<SensorType>& sensorTypes) override {
        std::string result = plain().sensorTypesToJson(sensorTypes);
        if (result.size() > budget) throw std::bad_alloc();
        return result;
    }

private:
    // JsonConverter implements the interface with private members, so the real conversion is reached
    // through the interface of a plain instance rather than by calling the base class directly.
    IJsonConverter& plain() { return inner; }

    JsonConverter inner;
    size_t budget;
};

static size_t largestTypeJsonSize(IJsonConverter& converter, const std::vector<SensorType>& catalog) {
    size_t largest = 0;
    for (const auto& sensorType : catalog) {
        largest = std::max(largest, converter.sensorTypeToJson(sensorType).size());
    }
    return largest;
}

// The whole catalog of 14 types is stored, and no single write carried the whole payload.
static void test_bulk_save_streams_the_whole_catalog(void) {
    MockSpiffsAdapter spiffsAdapter;
    JsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsSensorTypeDao dao(&spiffsAdapter, &jsonConverter, &debugSystem, &logger);

    std::vector<SensorType> catalog = createCatalog(14);

    TEST_ASSERT_TRUE(dao.saveAll(catalog));

    std::vector<SensorType> stored;
    TEST_ASSERT_TRUE(dao.getAll(stored));
    TEST_ASSERT_EQUAL(14, stored.size());

    size_t fileLength = spiffsAdapter.getFileContent(BULK_SAVE_FILE).size();
    size_t largestChunk = spiffsAdapter.getLargestWriteChunk();

    // The payload is big enough to be the problem it was (well over 10 KB of JSON)...
    TEST_ASSERT_TRUE(fileLength > 10000);
    // ...and it never reached storage as one block: the biggest single write is one sensor type.
    TEST_ASSERT_TRUE(largestChunk < fileLength / 3);
    IJsonConverter& converterInterface = jsonConverter;
    TEST_ASSERT_TRUE(largestChunk <= largestTypeJsonSize(converterInterface, catalog));
    // A successful write leaves no temporary file behind.
    TEST_ASSERT_FALSE(spiffsAdapter.fileExists(BULK_SAVE_TEMP_FILE));
}

// A heap that can serve one sensor type but not the whole catalog still stores the catalog.
static void test_bulk_save_survives_heap_too_small_for_whole_catalog(void) {
    MockSpiffsAdapter spiffsAdapter;
    JsonConverter plainConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;

    std::vector<SensorType> catalog = createCatalog(14);
    IJsonConverter& plainInterface = plainConverter;
    size_t budget = largestTypeJsonSize(plainInterface, catalog) + 64;

    // Precondition of the bug: the whole array needs a block far bigger than any single type.
    TEST_ASSERT_TRUE(plainInterface.sensorTypesToJson(catalog).size() > budget);

    BudgetedJsonConverter budgetedConverter(budget);
    SpiffsSensorTypeDao dao(&spiffsAdapter, &budgetedConverter, &debugSystem, &logger);

    TEST_ASSERT_TRUE(dao.saveAll(catalog));

    std::vector<SensorType> stored;
    TEST_ASSERT_TRUE(dao.getAll(stored));
    TEST_ASSERT_EQUAL(14, stored.size());
}

// Serialization that fails is reported as such and does not touch the stored catalog.
static void test_failed_serialization_reports_reason_and_keeps_catalog(void) {
    MockSpiffsAdapter spiffsAdapter;
    JsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;

    SpiffsSensorTypeDao dao(&spiffsAdapter, &jsonConverter, &debugSystem, &logger);
    std::vector<SensorType> catalog = createCatalog(12);
    TEST_ASSERT_TRUE(dao.saveAll(catalog));
    std::string storedBefore = spiffsAdapter.getFileContent(BULK_SAVE_FILE);

    // Now nothing can be serialized at all: every sensor type is over the budget.
    BudgetedJsonConverter brokenConverter(10);
    SpiffsSensorTypeDao failingDao(&spiffsAdapter, &brokenConverter, &debugSystem, &logger);

    std::vector<SensorType> grownCatalog = createCatalog(14);
    TEST_ASSERT_FALSE(failingDao.saveAll(grownCatalog));
    TEST_ASSERT_EQUAL(STORAGE_WRITE_ERROR_SERIALIZATION, failingDao.getLastSaveError());

    // The 12 types that were there are still there, and no half-written file is left around.
    TEST_ASSERT_EQUAL_STRING(storedBefore.c_str(), spiffsAdapter.getFileContent(BULK_SAVE_FILE).c_str());
    TEST_ASSERT_FALSE(spiffsAdapter.fileExists(BULK_SAVE_TEMP_FILE));

    std::vector<SensorType> stored;
    TEST_ASSERT_TRUE(dao.getAll(stored));
    TEST_ASSERT_EQUAL(12, stored.size());
}

// A write that dies in the middle neither corrupts nor replaces the stored catalog.
static void test_partial_write_keeps_catalog_and_leaves_no_temp_file(void) {
    MockSpiffsAdapter spiffsAdapter;
    JsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsSensorTypeDao dao(&spiffsAdapter, &jsonConverter, &debugSystem, &logger);

    std::vector<SensorType> catalog = createCatalog(12);
    TEST_ASSERT_TRUE(dao.saveAll(catalog));
    std::string storedBefore = spiffsAdapter.getFileContent(BULK_SAVE_FILE);

    // Storage stops accepting bytes half-way through the next write.
    spiffsAdapter.setMaxTotalWriteBytes(storedBefore.size() / 2);
    TEST_ASSERT_FALSE(dao.saveAll(createCatalog(14)));
    TEST_ASSERT_EQUAL(STORAGE_WRITE_ERROR_WRITE, dao.getLastSaveError());

    TEST_ASSERT_EQUAL_STRING(storedBefore.c_str(), spiffsAdapter.getFileContent(BULK_SAVE_FILE).c_str());
    TEST_ASSERT_FALSE(spiffsAdapter.fileExists(BULK_SAVE_TEMP_FILE));

    spiffsAdapter.setMaxTotalWriteBytes(0);
    std::vector<SensorType> stored;
    TEST_ASSERT_TRUE(dao.getAll(stored));
    TEST_ASSERT_EQUAL(12, stored.size());
}

// A refused bulk write is reported with its reason and the heap numbers that decided it.
static void test_sync_logs_reason_and_heap_when_bulk_write_is_refused(void) {
    MockHybridAPIService apiService;
    MockSensorTypeRepository repository;
    MockArduinoAdapter arduinoAdapter;
    MockNTPTimeAdapter ntpTimeAdapter;
    JsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;

    SyncSensorTypesUseCase syncUseCase(&apiService, &repository, &arduinoAdapter, &ntpTimeAdapter,
                                       &jsonConverter, &logger, &debugSystem);
    TEST_ASSERT_TRUE(syncUseCase.init());

    apiService.sensorTypesListReturnValue = createCatalog(14);
    repository.setSaveAllShouldFail(true);

    syncUseCase.forceSync();

    TEST_ASSERT_TRUE(logger.hasMessageContaining("saveAll wrote 0 of 14 sensor types"));
    TEST_ASSERT_TRUE(logger.hasMessageContaining("reason=serialization"));
    TEST_ASSERT_TRUE(logger.hasMessageContaining("freeHeap="));
    TEST_ASSERT_TRUE(logger.hasMessageContaining("maxAllocHeap="));
}

// "Reboot with scan" keeps the catalog: the scan after the reboot needs it, and a write that fails
// later must not be able to leave the module with no sensor types at all.
static void test_reboot_with_scan_keeps_sensor_type_catalog(void) {
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockArduinoAdapter arduinoAdapter;
    MockSensorTypeRepository sensorTypeRepository;
    MockSettings settings;

    for (const auto& sensorType : createCatalog(3)) {
        sensorTypeRepository.addSensorType(sensorType);
    }

    ModuleCommandExecutionUseCase useCase(
        &logger,                 // logger
        &debugSystem,            // debugSystem
        nullptr,                 // timeAdapter
        nullptr,                 // firmwareUpdater
        &arduinoAdapter,         // arduinoAdapter
        nullptr,                 // sensorRepository
        &sensorTypeRepository,   // sensorTypeRepository
        nullptr,                 // sensorReader
        nullptr,                 // moduleConfig
        &settings                // settings
    );

    bool result = useCase.execute("Reboot", "{\"noScanSensors\":false}", "command-uid", "2026-08-14T10:00:00Z");

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(arduinoAdapter.restartCalled);
    // The scan is requested (flag cleared)...
    TEST_ASSERT_EQUAL(0, settings.getNoScanSensors());
    // ...and the catalog it will need is still there.
    TEST_ASSERT_EQUAL(3, sensorTypeRepository.getAll().size());
}

extern "C" int run_sensor_types_bulk_save_tests(void) {
    RUN_TEST(test_bulk_save_streams_the_whole_catalog);
    RUN_TEST(test_bulk_save_survives_heap_too_small_for_whole_catalog);
    RUN_TEST(test_failed_serialization_reports_reason_and_keeps_catalog);
    RUN_TEST(test_partial_write_keeps_catalog_and_leaves_no_temp_file);
    RUN_TEST(test_sync_logs_reason_and_heap_when_bulk_write_is_refused);
    RUN_TEST(test_reboot_with_scan_keeps_sensor_type_catalog);
    return 0;
}
