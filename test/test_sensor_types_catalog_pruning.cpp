#include <unity.h>
#include <algorithm>
#include <string>
#include <vector>

#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Infrastructure/Adapters/Json/JsonConverter.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"

static const char* PRUNING_UPDATED_AT_OLD = "2026-02-11T09:01:35.44352+00:00";
static const char* PRUNING_UPDATED_AT_NEW = "2026-08-15T09:01:35.44352+00:00";

static SensorType makePruningSensorType(const std::string& uid,
                                        const std::string& name,
                                        const std::string& updatedAt = PRUNING_UPDATED_AT_OLD) {
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " Display";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = updatedAt;

    SubSensorType subType;
    subType.name = name + "_channel";
    subType.displayName = "Channel";
    subType.isAnalog = false;
    subType.canCalibrate = false;
    subType.minValue = 0.0f;
    subType.maxValue = 100.0f;
    subType.unit = "test";
    subType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    subType.updatedAt = updatedAt;
    sensorType.sensorTypes.push_back(subType);

    return sensorType;
}

struct PruningFixture {
    MockHybridAPIService apiService;
    MockSensorTypeRepository repository;
    MockArduinoAdapter arduinoAdapter;
    MockNTPTimeAdapter ntpTimeAdapter;
    JsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SyncSensorTypesUseCase syncUseCase;

    PruningFixture()
        : syncUseCase(&apiService, &repository, &arduinoAdapter, &ntpTimeAdapter,
                      &jsonConverter, &logger, &debugSystem) {
        debugSystem.featureEnabled = true;
        syncUseCase.init();
    }

    bool catalogHas(const std::string& uid) {
        std::vector<SensorType> stored = repository.getSensorTypes();
        return std::any_of(stored.begin(), stored.end(),
                           [&uid](const SensorType& st) { return st.uid == uid; });
    }

    std::string catalogUpdatedAt(const std::string& uid) {
        std::vector<SensorType> stored = repository.getSensorTypes();
        for (const auto& st : stored) {
            if (st.uid == uid) {
                return st.updatedAt;
            }
        }
        return "";
    }

    size_t catalogSize() {
        return repository.getSensorTypes().size();
    }
};

static void test_type_missing_from_server_is_dropped_from_the_catalog(void) {
    PruningFixture f;

    f.repository.addSensorType(makePruningSensorType("uid-a", "sensor_a"));
    f.repository.addSensorType(makePruningSensorType("uid-b", "sensor_b"));
    f.repository.addSensorType(makePruningSensorType("uid-retired", "sensor_retired"));

    std::vector<SensorType> fromServer;
    fromServer.push_back(makePruningSensorType("uid-a", "sensor_a"));
    fromServer.push_back(makePruningSensorType("uid-b", "sensor_b"));
    f.apiService.sensorTypesListReturnValue = fromServer;

    f.syncUseCase.forceSync();

    TEST_ASSERT_EQUAL(2, static_cast<int>(f.catalogSize()));
    TEST_ASSERT_TRUE(f.catalogHas("uid-a"));
    TEST_ASSERT_TRUE(f.catalogHas("uid-b"));
    TEST_ASSERT_FALSE(f.catalogHas("uid-retired"));
}

static void test_pure_prune_is_written_and_reported_as_a_successful_sync(void) {
    PruningFixture f;

    f.repository.addSensorType(makePruningSensorType("uid-a", "sensor_a"));
    f.repository.addSensorType(makePruningSensorType("uid-b", "sensor_b"));
    f.repository.addSensorType(makePruningSensorType("uid-retired", "sensor_retired"));

    std::vector<SensorType> fromServer;
    fromServer.push_back(makePruningSensorType("uid-a", "sensor_a"));
    fromServer.push_back(makePruningSensorType("uid-b", "sensor_b"));
    f.apiService.sensorTypesListReturnValue = fromServer;

    f.syncUseCase.forceSync();

    TEST_ASSERT_TRUE(f.repository.saveAllCalled);
    TEST_ASSERT_TRUE(f.syncUseCase.getLastSyncStatus());
    TEST_ASSERT_TRUE(f.logger.hasMessageContaining(
        "Bulk saved 2 sensor types to storage, removed=1, bytes="));
    TEST_ASSERT_TRUE(f.logger.hasMessageContaining("maxAllocHeap="));
}

static void test_prune_and_update_in_the_same_sync(void) {
    PruningFixture f;

    f.repository.addSensorType(makePruningSensorType("uid-a", "sensor_a", PRUNING_UPDATED_AT_OLD));
    f.repository.addSensorType(makePruningSensorType("uid-b", "sensor_b"));
    f.repository.addSensorType(makePruningSensorType("uid-retired", "sensor_retired"));

    std::vector<SensorType> fromServer;
    fromServer.push_back(makePruningSensorType("uid-a", "sensor_a", PRUNING_UPDATED_AT_NEW));
    fromServer.push_back(makePruningSensorType("uid-b", "sensor_b"));
    f.apiService.sensorTypesListReturnValue = fromServer;

    f.syncUseCase.forceSync();

    TEST_ASSERT_EQUAL(2, static_cast<int>(f.catalogSize()));
    TEST_ASSERT_FALSE(f.catalogHas("uid-retired"));
    TEST_ASSERT_EQUAL_STRING(PRUNING_UPDATED_AT_NEW, f.catalogUpdatedAt("uid-a").c_str());
}

static void test_refused_write_reports_the_removal_count(void) {
    PruningFixture f;

    f.repository.addSensorType(makePruningSensorType("uid-a", "sensor_a"));
    f.repository.addSensorType(makePruningSensorType("uid-b", "sensor_b"));
    f.repository.addSensorType(makePruningSensorType("uid-retired", "sensor_retired"));
    f.repository.setSaveAllShouldFail(true);

    std::vector<SensorType> fromServer;
    fromServer.push_back(makePruningSensorType("uid-a", "sensor_a"));
    fromServer.push_back(makePruningSensorType("uid-b", "sensor_b"));
    f.apiService.sensorTypesListReturnValue = fromServer;

    f.syncUseCase.forceSync();

    TEST_ASSERT_TRUE(f.logger.hasMessageContaining(
        "saveAll wrote 0 of 2 sensor types, removed=1, reason="));
    TEST_ASSERT_FALSE(f.syncUseCase.getLastSyncStatus());
}

static void test_empty_server_response_leaves_the_catalog_untouched(void) {
    PruningFixture f;

    f.repository.addSensorType(makePruningSensorType("uid-a", "sensor_a"));
    f.repository.addSensorType(makePruningSensorType("uid-b", "sensor_b"));
    f.repository.addSensorType(makePruningSensorType("uid-c", "sensor_c"));

    f.apiService.sensorTypesListReturnValue = std::vector<SensorType>();

    f.syncUseCase.forceSync();

    TEST_ASSERT_TRUE(f.apiService.getSensorTypesListCalled);
    TEST_ASSERT_FALSE(f.repository.saveAllCalled);
    TEST_ASSERT_FALSE(f.repository.clearCalled);
    TEST_ASSERT_EQUAL(3, static_cast<int>(f.catalogSize()));
    TEST_ASSERT_TRUE(f.catalogHas("uid-a"));
    TEST_ASSERT_TRUE(f.catalogHas("uid-b"));
    TEST_ASSERT_TRUE(f.catalogHas("uid-c"));
}

static void test_response_with_only_invalid_types_leaves_the_catalog_untouched(void) {
    PruningFixture f;

    f.repository.addSensorType(makePruningSensorType("uid-a", "sensor_a"));
    f.repository.addSensorType(makePruningSensorType("uid-b", "sensor_b"));

    SensorType nameless = makePruningSensorType("uid-a", "sensor_a");
    nameless.name = "";
    std::vector<SensorType> fromServer;
    fromServer.push_back(nameless);
    f.apiService.sensorTypesListReturnValue = fromServer;

    f.syncUseCase.forceSync();

    TEST_ASSERT_FALSE(f.repository.saveAllCalled);
    TEST_ASSERT_FALSE(f.repository.clearCalled);
    TEST_ASSERT_EQUAL(2, static_cast<int>(f.catalogSize()));
}

static void test_published_but_invalid_type_is_not_pruned(void) {
    PruningFixture f;

    f.repository.addSensorType(makePruningSensorType("uid-a", "sensor_a"));
    f.repository.addSensorType(makePruningSensorType("uid-broken", "sensor_broken"));

    SensorType nameless = makePruningSensorType("uid-broken", "sensor_broken");
    nameless.name = "";
    std::vector<SensorType> fromServer;
    fromServer.push_back(makePruningSensorType("uid-a", "sensor_a", PRUNING_UPDATED_AT_NEW));
    fromServer.push_back(nameless);
    f.apiService.sensorTypesListReturnValue = fromServer;

    f.syncUseCase.forceSync();

    TEST_ASSERT_TRUE(f.catalogHas("uid-broken"));
    TEST_ASSERT_EQUAL(2, static_cast<int>(f.catalogSize()));
}

static void test_pruning_does_not_wipe_the_catalog_first(void) {
    PruningFixture f;

    f.repository.addSensorType(makePruningSensorType("uid-a", "sensor_a"));
    f.repository.addSensorType(makePruningSensorType("uid-retired", "sensor_retired"));

    std::vector<SensorType> fromServer;
    fromServer.push_back(makePruningSensorType("uid-a", "sensor_a"));
    f.apiService.sensorTypesListReturnValue = fromServer;

    f.syncUseCase.forceSync();

    TEST_ASSERT_TRUE(f.repository.saveAllCalled);
    TEST_ASSERT_FALSE(f.repository.clearCalled);
    TEST_ASSERT_FALSE(f.repository.removeCalled);
    TEST_ASSERT_FALSE(f.repository.removeAllCalled);
}

static void test_normal_sync_keeps_everything_the_server_publishes(void) {
    PruningFixture f;

    f.repository.addSensorType(makePruningSensorType("uid-a", "sensor_a", PRUNING_UPDATED_AT_OLD));
    f.repository.addSensorType(makePruningSensorType("uid-b", "sensor_b"));

    std::vector<SensorType> fromServer;
    fromServer.push_back(makePruningSensorType("uid-a", "sensor_a", PRUNING_UPDATED_AT_NEW));
    fromServer.push_back(makePruningSensorType("uid-b", "sensor_b"));
    fromServer.push_back(makePruningSensorType("uid-new", "sensor_new"));
    f.apiService.sensorTypesListReturnValue = fromServer;

    f.syncUseCase.forceSync();

    TEST_ASSERT_EQUAL(3, static_cast<int>(f.catalogSize()));
    TEST_ASSERT_TRUE(f.catalogHas("uid-a"));
    TEST_ASSERT_TRUE(f.catalogHas("uid-b"));
    TEST_ASSERT_TRUE(f.catalogHas("uid-new"));
    TEST_ASSERT_TRUE(f.logger.hasMessageContaining(
        "Bulk saved 3 sensor types to storage, removed=0, bytes="));
}

extern "C" int run_sensor_types_catalog_pruning_tests(void) {
    UNITY_BEGIN();

    RUN_TEST(test_type_missing_from_server_is_dropped_from_the_catalog);
    RUN_TEST(test_pure_prune_is_written_and_reported_as_a_successful_sync);
    RUN_TEST(test_prune_and_update_in_the_same_sync);
    RUN_TEST(test_refused_write_reports_the_removal_count);
    RUN_TEST(test_empty_server_response_leaves_the_catalog_untouched);
    RUN_TEST(test_response_with_only_invalid_types_leaves_the_catalog_untouched);
    RUN_TEST(test_published_but_invalid_type_is_not_pruned);
    RUN_TEST(test_pruning_does_not_wipe_the_catalog_first);
    RUN_TEST(test_normal_sync_keeps_everything_the_server_publishes);

    return UNITY_END();
}
