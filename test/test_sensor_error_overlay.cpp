// A sensor that answers every other sweep must not write to flash for it.
//
// `isError` is runtime health, but it is a field of the persisted sensor entity, so every
// transition of it would travel the full storage path: updateSensorStatus() -> save() ->
// SpiffsSensorDao -> the whole registry serialized and rewritten -> invalidateCache(), and the
// next control loop iteration read the registry back from flash. A flapping sensor therefore
// bought two rewrites a minute, for ever, for a flag nobody reads from storage.
//
// The flag now lives in a runtime overlay held by the repository. The stored format does not
// change - the field stays in the file, and a save made for any other reason still writes the
// value the module currently holds. What disappears is the write made for the flag alone.
//
// ### Reproducing the red
//
// In SensorRepository::updateSensorStatus(), delete the branch that keeps an isError-only change
// in the overlay and let every change fall through to save() again: the two write-counting tests
// fail (20 rewrites where none are allowed) and the cache test fails next to them.
//
// The reader tests are green with and without the overlay, by design - they are what "nothing
// outside the repository can tell" means. To see them bite, apply the overlay in get()/getAll()
// only and leave getSensorsByProtocol() reading the raw cache: the Modbus snapshot test goes red,
// and with it the entities the recovery logic takes `isError` from.

#include <unity.h>

#include <cstdio>
#include <string>
#include <vector>

#include "Mock/ArduinoMock.h"

#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"
#include "Infrastructure/Repositories/SensorRepository.h"

#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

namespace {

const char* const SENSORS_FILE = "/data/sensors.json";

// Backend UIDs are GUID-sized; a short one would sit in the small-string buffer and hide the
// copies an overlay lookup makes.
const char* const SENSOR_A = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";
const char* const SENSOR_B = "1b5d0e3f-64c2-5a7f-ad48-9e32f6b1c582";

// How many times the sensor comes and goes. Every one of them was a rewrite of the whole
// registry file before this file's change.
const int FLAP_TRANSITIONS = 20;

SensorEntity makeSensor(const char* uid, int address)
{
    SensorEntity sensor;
    sensor.uid = uid;
    sensor.sensorType = "soil_temperature";
    sensor.model = "soil_moisture_temperature_ec_sensor";
    sensor.protocol = "ModBus";
    sensor.createdAt = "2026-08-27T10:00:00.0000000Z";
    sensor.pin = 0;
    sensor.isActive = true;
    sensor.isError = false;
    sensor.status = SENSOR_STATUS_ADDED;
    sensor.modbusDeviceAddress = address;
    return sensor;
}

/** @brief The sensor registry over its real storage path, wired the way globals.h wires it. */
struct RegistryStand
{
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;

    SpiffsSensorDao dao;
    SensorRepository repository;

    RegistryStand()
        : dao(&spiffs, &jsonConverter, &debugSystem, &logger),
          repository(&dao)
    {
        debugSystem.featureEnabled = false;
    }

    /** @brief Put a sensor in the file the way a first configuration does, then start counting. */
    void seed(const SensorEntity& sensor)
    {
        TEST_ASSERT_TRUE(dao.saveSensor(sensor));
        repository.refreshCache();
        spiffs.resetWriteStats();
        spiffs.resetFileReadCount();
    }

    std::string storedFile() { return spiffs.getFileContent(SENSORS_FILE); }

    bool storedErrorFlag(const char* uid)
    {
        SensorEntity stored;
        TEST_ASSERT_TRUE(dao.getById(uid, stored));
        return stored.isError;
    }
};

/** @brief Live error flag of one sensor as the active view reports it. */
bool activeViewErrorFlag(ISensorRepository& repository, const char* uid)
{
    for (const auto& sensor : repository.getActiveSensors())
    {
        if (sensor.uid == uid)
        {
            return sensor.isError;
        }
    }
    TEST_FAIL_MESSAGE("the sensor is not in the active view");
    return false;
}

/** @brief Live error flag as the Modbus snapshot reports it - what the recovery logic reads. */
bool modbusSnapshotErrorFlag(ISensorRepository& repository, const char* uid)
{
    for (const auto& sensor : repository.getModbusSensors())
    {
        if (sensor.uid == uid)
        {
            return sensor.isError;
        }
    }
    TEST_FAIL_MESSAGE("the sensor is not in the Modbus snapshot");
    return false;
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The flapping sensor stops writing
// --------------------------------------------------------------------------------------------

static void test_flapping_error_flag_writes_nothing_to_flash(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));

    for (int i = 0; i < FLAP_TRANSITIONS; i++)
    {
        const bool error = (i % 2) == 0;
        TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, error));
        TEST_ASSERT_EQUAL_MESSAGE(error, activeViewErrorFlag(stand.repository, SENSOR_A),
                                  "the transition did not reach the readers");
    }

    char message[192];
    snprintf(message, sizeof(message),
             "%d error-flag transitions rewrote the sensor registry %d times; a runtime flag must "
             "cost no flash writes at all",
             FLAP_TRANSITIONS, (int)stand.spiffs.getWriteChunkCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.spiffs.getWriteChunkCount(), message);
}

static void test_flapping_error_flag_does_not_drop_the_cache(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));

    // One read settles the cache; from here the loop asks the same question every iteration.
    TEST_ASSERT_EQUAL_INT(1, (int)stand.repository.getActiveSensors().size());
    const uint32_t generationBefore = stand.repository.generation();
    stand.spiffs.resetFileReadCount();

    for (int i = 0; i < FLAP_TRANSITIONS; i++)
    {
        TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, (i % 2) == 0));
        stand.repository.getActiveSensors();
    }

    char message[192];
    snprintf(message, sizeof(message),
             "the registry was read back from flash %d times across %d error-flag transitions; a "
             "runtime flag must not invalidate the cache",
             (int)stand.spiffs.getFileReadCount(), FLAP_TRANSITIONS);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.spiffs.getFileReadCount(), message);
    TEST_ASSERT_TRUE_MESSAGE(stand.repository.isCacheCurrent(),
                             "the cache was dropped by an error-flag transition");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationBefore, stand.repository.generation(),
                                     "an error-flag transition moved the storage generation, "
                                     "which is every derived view's signal to rebuild");
}

// --------------------------------------------------------------------------------------------
// 2. The readers - every place the firmware takes `isError` from - see the live value
// --------------------------------------------------------------------------------------------

// SensorService::applyReading() takes the entity of a finished acquisition from get().
static void test_get_carries_the_live_error_flag(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, true));
    TEST_ASSERT_TRUE(stand.repository.get(SENSOR_A).value().isError);

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, false));
    TEST_ASSERT_FALSE(stand.repository.get(SENSOR_A).value().isError);
}

// SensorService::createSensorUpdateDto() - the sensor update packet - reads the active view.
static void test_the_active_view_carries_the_live_error_flag(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));
    TEST_ASSERT_FALSE(activeViewErrorFlag(stand.repository, SENSOR_A));

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, true));
    TEST_ASSERT_TRUE_MESSAGE(activeViewErrorFlag(stand.repository, SENSOR_A),
                             "the active view still reports the sensor as healthy");

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, false));
    TEST_ASSERT_FALSE_MESSAGE(activeViewErrorFlag(stand.repository, SENSOR_A),
                              "the active view still reports the sensor as failed");
}

// The snapshot the recovery logic reads the flag from: SensorService::readModbusSensors() passes
// these entities to SensorRecoveryUseCase::observe(), and hands the same vector to the
// acquisition task, which observes from it too.
static void test_the_modbus_snapshot_carries_the_live_error_flag(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));
    TEST_ASSERT_FALSE(modbusSnapshotErrorFlag(stand.repository, SENSOR_A));

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, true));
    TEST_ASSERT_TRUE_MESSAGE(modbusSnapshotErrorFlag(stand.repository, SENSOR_A),
                             "the recovery logic would be told the sensor is healthy");

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, false));
    TEST_ASSERT_FALSE_MESSAGE(modbusSnapshotErrorFlag(stand.repository, SENSOR_A),
                              "the recovery logic would keep the port in recovery for good");
}

// getSensorsWithErrors() answers from the same source as everything else.
static void test_the_error_list_follows_the_live_error_flag(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));
    TEST_ASSERT_EQUAL_INT(0, (int)stand.repository.getSensorsWithErrors().size());

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, true));
    TEST_ASSERT_EQUAL_INT(1, (int)stand.repository.getSensorsWithErrors().size());

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, false));
    TEST_ASSERT_EQUAL_INT(0, (int)stand.repository.getSensorsWithErrors().size());
}

// A cache reload for an unrelated reason must not resurrect the value on flash: the runtime flag
// outlives the cache it is read next to.
static void test_the_live_error_flag_survives_a_cache_reload(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));
    TEST_ASSERT_TRUE(stand.repository.save(makeSensor(SENSOR_B, 2)));

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, true));

    // Saving another sensor drops the cache; the next read comes from the file, where sensor A is
    // still recorded as healthy.
    TEST_ASSERT_TRUE(stand.repository.save(makeSensor(SENSOR_B, 3)));
    TEST_ASSERT_FALSE(stand.repository.isCacheCurrent());

    TEST_ASSERT_TRUE_MESSAGE(activeViewErrorFlag(stand.repository, SENSOR_A),
                             "the live error flag was lost when the cache was reloaded");
    TEST_ASSERT_TRUE(stand.repository.get(SENSOR_A).value().isError);
    TEST_ASSERT_FALSE_MESSAGE(stand.storedErrorFlag(SENSOR_A),
                              "the flag reached the file after all");

    // And a refresh - the documented way to pick up a change made behind the repository's back -
    // is not a way to lose it either.
    stand.repository.refreshCache();
    TEST_ASSERT_TRUE(activeViewErrorFlag(stand.repository, SENSOR_A));
}

// --------------------------------------------------------------------------------------------
// 3. Everything else about the entity is stored exactly as before
// --------------------------------------------------------------------------------------------

// isActive is configuration: it travels through the same method and still goes to flash.
static void test_deactivating_a_sensor_is_still_written(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, false, false));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.spiffs.getWriteChunkCount(),
                                     "deactivating a sensor is a configuration change and must "
                                     "reach the file");
    TEST_ASSERT_TRUE(stand.storedFile().find("\"isActive\":false") != std::string::npos);
    TEST_ASSERT_EQUAL_INT(0, (int)stand.repository.getActiveSensors().size());
}

// A change of both at once is still one save, and the file gets both values.
static void test_a_deactivation_carrying_an_error_flag_is_written_whole(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, false, true));

    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)stand.spiffs.getWriteChunkCount());
    TEST_ASSERT_TRUE(stand.storedFile().find("\"isActive\":false") != std::string::npos);
    TEST_ASSERT_TRUE_MESSAGE(stand.storedErrorFlag(SENSOR_A),
                             "the error flag was dropped by a save that had to happen anyway");
}

// A save made for another reason writes the flag the module currently holds - the file keeps
// carrying the last value that was saved, exactly as it did before.
static void test_a_save_for_another_reason_persists_the_live_error_flag(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, true));
    TEST_ASSERT_FALSE(stand.storedErrorFlag(SENSOR_A));

    // Only what the next call writes is counted here; what the transition itself costs is the
    // subject of the first test in this file.
    stand.spiffs.resetWriteStats();

    // The lifecycle status is stored state and moves on its own path.
    TEST_ASSERT_TRUE(stand.repository.updateSensorStatusEnum(SENSOR_A, SENSOR_STATUS_ERROR));

    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)stand.spiffs.getWriteChunkCount());
    TEST_ASSERT_TRUE_MESSAGE(stand.storedErrorFlag(SENSOR_A),
                             "a save made for another reason wrote a stale error flag");
    TEST_ASSERT_TRUE(stand.storedFile().find("\"status\":\"error\"") != std::string::npos);
}

// An entity saved whole carries its own flag: the server pushing a configuration, or a sensor
// being re-detected, decides what the flag is - the overlay does not outvote the save.
static void test_an_explicit_save_replaces_the_live_error_flag(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, true));
    TEST_ASSERT_TRUE(stand.repository.createOrUpdateSensor(makeSensor(SENSOR_A, 7)));

    TEST_ASSERT_FALSE_MESSAGE(stand.repository.get(SENSOR_A).value().isError,
                              "a stale runtime flag survived the entity it belonged to");
    TEST_ASSERT_FALSE(activeViewErrorFlag(stand.repository, SENSOR_A));
}

// And it does not outlive the sensor: a uid that comes back is a new sensor, not the old flag.
static void test_the_live_error_flag_does_not_outlive_the_sensor(void)
{
    RegistryStand stand;
    stand.seed(makeSensor(SENSOR_A, 1));

    TEST_ASSERT_TRUE(stand.repository.updateSensorStatus(SENSOR_A, true, true));
    TEST_ASSERT_TRUE(stand.repository.remove(SENSOR_A));
    TEST_ASSERT_TRUE(stand.repository.save(makeSensor(SENSOR_A, 1)));

    TEST_ASSERT_FALSE_MESSAGE(stand.repository.get(SENSOR_A).value().isError,
                              "the removed sensor's error flag came back with its uid");
}

// --------------------------------------------------------------------------------------------
// 4. The whole path, from a bus that answers every other sweep to the packet the server gets
// --------------------------------------------------------------------------------------------

namespace {

/** @brief SensorService reading one Modbus sensor inline, over the real registry storage. */
struct FlappingSensorModule
{
    RegistryStand stand;

    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    MockSensorValueModel sensorValueModel;
    MockSensorReader sensorReader;
    MockArduinoAdapter arduino;
    MockHybridAPIService hybridApi;
    ModuleStructure moduleStructure;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorService sensorService;

    FlappingSensorModule()
        : virtualChannels(&vpdChannel),
          sensorService(&moduleConfig,
                        &moduleStructure,
                        &stand.repository,
                        &sensorReader,
                        &sensorValueModel,
                        &timeAdapter,
                        &arduino,
                        &hybridApi,
                        &stand.logger,
                        &stand.debugSystem,
                        nullptr,
                        nullptr,
                        nullptr,
                        &virtualChannels)
    {
        moduleConfig.setSettings(&settings);
        stand.seed(makeSensor(SENSOR_A, 1));
    }

    /** @brief One 15 s tick: the sensor either answers or does not. */
    void tick(unsigned long atMs, bool sensorAnswers)
    {
        sensorReader.readSensorReturnValue = sensorAnswers;
        sensorReader.readSensorValue = 21.5f;
        arduino.setMillis(atMs);
        sensorService.loop();
    }

    /** @brief State of the sensor in the packet the module last sent to the server. */
    std::string reportedState() const
    {
        for (const auto& sensorDto : hybridApi.lastUpdateData.sensors)
        {
            if (sensorDto.uid == SENSOR_A)
            {
                return sensorDto.state;
            }
        }
        TEST_FAIL_MESSAGE("the sensor is missing from the update packet");
        return "";
    }
};

} // namespace

static void test_a_flapping_sensor_reports_its_state_without_writing_to_flash(void)
{
    FlappingSensorModule module;

    unsigned long now = 0;
    for (int i = 0; i < FLAP_TRANSITIONS; i++)
    {
        const bool answers = (i % 2) == 1;
        now += 15000;
        module.tick(now, answers);

        TEST_ASSERT_TRUE_MESSAGE(module.hybridApi.sendModuleUpdateDataCalled,
                                 "the module stopped sending sensor updates");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(answers ? "online" : "disconnected",
                                         module.reportedState().c_str(),
                                         "the packet does not follow the sensor's live state");
    }

    char message[224];
    snprintf(message, sizeof(message),
             "a sensor that came and went %d times rewrote the registry %d times and read it back "
             "%d times; neither belongs to a runtime flag",
             FLAP_TRANSITIONS, (int)module.stand.spiffs.getWriteChunkCount(),
             (int)module.stand.spiffs.getFileReadCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.stand.spiffs.getWriteChunkCount(), message);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.stand.spiffs.getFileReadCount(), message);

    // The file still says what the last save said, and it is still a complete sensor record.
    TEST_ASSERT_FALSE(module.stand.storedErrorFlag(SENSOR_A));
    TEST_ASSERT_TRUE(module.stand.storedFile().find("\"isError\":false") != std::string::npos);
}

extern "C" void run_sensor_error_overlay_tests(void)
{
    printf("\n----- Running Sensor Error Overlay Tests -----\n");

    RUN_TEST(test_flapping_error_flag_writes_nothing_to_flash);
    RUN_TEST(test_flapping_error_flag_does_not_drop_the_cache);

    RUN_TEST(test_get_carries_the_live_error_flag);
    RUN_TEST(test_the_active_view_carries_the_live_error_flag);
    RUN_TEST(test_the_modbus_snapshot_carries_the_live_error_flag);
    RUN_TEST(test_the_error_list_follows_the_live_error_flag);
    RUN_TEST(test_the_live_error_flag_survives_a_cache_reload);

    RUN_TEST(test_deactivating_a_sensor_is_still_written);
    RUN_TEST(test_a_deactivation_carrying_an_error_flag_is_written_whole);
    RUN_TEST(test_a_save_for_another_reason_persists_the_live_error_flag);
    RUN_TEST(test_an_explicit_save_replaces_the_live_error_flag);
    RUN_TEST(test_the_live_error_flag_does_not_outlive_the_sensor);

    RUN_TEST(test_a_flapping_sensor_reports_its_state_without_writing_to_flash);
}
