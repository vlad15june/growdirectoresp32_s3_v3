// A factory reset has to leave the module without a sensor type catalog - and keep it that way.
//
// The reset happens in two halves: ClearModuleDataService::clearAllData() marks storage and the
// module restarts, then the boot half drops the files before anything reads them. The stored
// catalog, however, is not one file. A replacement that was interrupted leaves the catalog complete
// under `<file>.tmp` (the version being published) or `<file>.bak` (the one it was replacing), and
// the next load takes the catalog from there and gives it its name back - that recovery is what
// makes a power cut in the middle of a write survivable.
//
// So a reset that drops only `<file>` is undone by the first read after it: the module comes back
// with the catalog the user asked it to forget. These tests hold the reset to the whole catalog.
//
// The sensor registry is stored the same way, and the reset is held to all of it as well. And the
// control loop runs on between the two halves: the registry and the rule repository still hold
// what they held, and a row they write in that time is the whole of it - the boot half is what
// takes it away again.
//
// ### Reproducing the red
//
// Replace the body of ClearModuleDataService::clearStoredFiles() with the four removeFile() calls
// of the boot path (`/data/sensorTypes.json`, `/data/sensors.json`,
// `/sensors/sensors.json`, `/wifiNetworks.json`) and the first two tests fail: the catalog comes
// back from its companion file.

#include <unity.h>

#include <string>
#include <vector>

#include "Mock/ArduinoMock.h"

#include "Application/Services/ClearModuleDataService.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Infrastructure/Repositories/RuleRepository.h"
#include "Infrastructure/Repositories/SensorRepository.h"

#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

namespace {

const char* const CATALOG_FILE = "/data/sensorTypes.json";
const char* const CATALOG_TEMP_FILE = "/data/sensorTypes.json.tmp";
const char* const CATALOG_BACKUP_FILE = "/data/sensorTypes.json.bak";
const char* const SENSORS_FILE = "/data/sensors.json";
const char* const SENSORS_TEMP_FILE = "/data/sensors.json.tmp";
const char* const SENSORS_BACKUP_FILE = "/data/sensors.json.bak";
const char* const RULES_FILE = "/data/rules.json";
const char* const RULES_TEMP_FILE = "/data/rules.json.tmp";
const char* const RULES_BACKUP_FILE = "/data/rules.json.bak";
const char* const LEGACY_SENSORS_FILE = "/sensors/sensors.json";
const char* const WIFI_FILE = "/wifiNetworks.json";

SensorType makeSensorType(const std::string& uid, const std::string& name)
{
    SensorType sensorType;
    sensorType.uid = uid;
    sensorType.name = name;
    sensorType.displayName = name + " display name";
    sensorType.protocol = SensorProtocol::MODBUS_PROTOCOL;
    sensorType.canScan = true;
    sensorType.isSupported = true;
    sensorType.createdAt = "2025-09-22T07:04:26.803128+00:00";
    sensorType.updatedAt = "2026-02-11T09:01:35.44352+00:00";

    ModbusRegisterConfig config;
    config.functionCode = 3;
    config.startRegister = 1;
    config.numRegisters = 2;
    config.multiplier = 0.01f;
    config.channel = 1;
    config.channelName = name + "_channel";
    config.sensorType = "temperature";
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    sensorType.modbusRegisterConfig.push_back(config);

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

/** @brief The module's storage, wired the way globals.h wires it. */
struct StorageUnderTest
{
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;

    SpiffsRuleDao ruleDao;
    SpiffsSensorDao sensorDao;
    SpiffsSensorTypeDao sensorTypeDao;
    ClearModuleDataService clearService;

    StorageUnderTest()
        : ruleDao(&spiffs, &jsonConverter, &debugSystem, &logger),
          sensorDao(&spiffs, &jsonConverter, &debugSystem, &logger),
          sensorTypeDao(&spiffs, &jsonConverter, &debugSystem, &logger),
          clearService(&ruleDao, &sensorDao, &sensorTypeDao, &logger, &debugSystem, &settings,
                       &spiffs)
    {
    }

    /**
     * @brief What flash holds when power was cut in the middle of publishing a new catalog.
     *
     * Staged exactly the way the storage tests stage it: the catalog being published is complete
     * under the temporary name, the one it replaced waits in the reserve, and the name everyone
     * reads is momentarily free.
     */
    void stageInterruptedReplacement()
    {
        TEST_ASSERT_TRUE(sensorTypeDao.saveAll(makeCatalog(12)));
        const std::string previousCatalog = spiffs.getFileContent(CATALOG_FILE);
        TEST_ASSERT_TRUE(sensorTypeDao.saveAll(makeCatalog(14)));
        const std::string newCatalog = spiffs.getFileContent(CATALOG_FILE);

        spiffs.writeToFile(CATALOG_BACKUP_FILE, previousCatalog.c_str());
        spiffs.writeToFile(CATALOG_TEMP_FILE, newCatalog.c_str());
        spiffs.removeFile(CATALOG_FILE);
    }

    /** @brief What the module reads at the next boot, from a DAO that has just been constructed. */
    size_t catalogSizeAfterReboot()
    {
        SpiffsSensorTypeDao daoAfterBoot(&spiffs, &jsonConverter, &debugSystem, &logger);
        std::vector<SensorType> stored;
        daoAfterBoot.getAll(stored);
        return stored.size();
    }

    size_t sensorsAfterReboot()
    {
        SpiffsSensorDao daoAfterBoot(&spiffs, &jsonConverter, &debugSystem, &logger);
        std::vector<SensorEntity> stored;
        daoAfterBoot.getAll(stored);
        return stored.size();
    }

    size_t rulesAfterReboot()
    {
        SpiffsRuleDao daoAfterBoot(&spiffs, &jsonConverter, &debugSystem, &logger);
        std::vector<Rule> stored;
        daoAfterBoot.getAll(stored);
        return stored.size();
    }
};

SensorEntity makeSensor(const char* uid, int pin)
{
    SensorEntity sensor;
    sensor.uid = uid;
    sensor.sensorType = "ry_ws301";
    sensor.model = "ry_ws301";
    sensor.protocol = "ModBus";
    sensor.pin = pin;
    sensor.modbusDeviceAddress = MODBUS_ADDRESS_MIN + pin;
    sensor.baudRate = 9600;
    sensor.isActive = true;
    sensor.status = SENSOR_STATUS_ADDED;
    return sensor;
}

Rule makeRule(const char* uid, RuleMode mode)
{
    Rule rule;
    rule.uid = uid;
    rule.version = 1;
    rule.deviceUid = "7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71";
    rule.sensorUid = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";
    rule.ruleType = RuleType::SENSOR;
    rule.mode = mode;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.timeToSetPoint = 10;
    return rule;
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The whole reset, both halves: a module whose last catalog write was interrupted is reset by
//    the user, restarts, and must come up with no sensor types.
// --------------------------------------------------------------------------------------------

static void test_a_factory_reset_does_not_leave_the_catalog_recoverable(void)
{
    StorageUnderTest module;
    module.stageInterruptedReplacement();

    // The user holds the reset button: storage is cleared and the boot half is armed.
    module.clearService.clearAllData();
    TEST_ASSERT_EQUAL_MESSAGE(NEED_CLEAR_SPIFFS_MARKER, module.settings.getNeedClearSpiffs(),
                              "the reset did not arm the boot half - the scenario did not happen");

    // ...the module restarts, and the boot half removes the files.
    module.clearService.clearStoredFiles();

    const unsigned typesAfterReboot = (unsigned)module.catalogSizeAfterReboot();
    char message[160];
    snprintf(message, sizeof(message),
             "the module came back with %u sensor types after a factory reset", typesAfterReboot);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, typesAfterReboot, message);
}

// --------------------------------------------------------------------------------------------
// 2. The boot half alone, against both leftovers at once: nothing of the catalog survives it,
//    under any of its three names.
// --------------------------------------------------------------------------------------------

static void test_the_boot_half_of_a_reset_takes_the_leftover_copies_too(void)
{
    StorageUnderTest module;
    module.stageInterruptedReplacement();

    module.clearService.clearStoredFiles();

    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(CATALOG_FILE),
                              "the stored catalog is still there");
    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(CATALOG_TEMP_FILE),
                              "the catalog that was being published is still there");
    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(CATALOG_BACKUP_FILE),
                              "the catalog that was being replaced is still there");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.catalogSizeAfterReboot(),
                                   "the catalog was recovered from a leftover file after the reset");
}

// --------------------------------------------------------------------------------------------
// 3. And the rest of the reset is still a reset: the other three files the boot half has always
//    removed are gone. This one holds whatever the catalog files do - it is here so that dropping
//    the catalog cannot be achieved by dropping everything else on the way.
// --------------------------------------------------------------------------------------------

static void test_the_boot_half_of_a_reset_still_removes_sensors_and_wifi_networks(void)
{
    StorageUnderTest module;
    module.spiffs.writeToFile(SENSORS_FILE, "[{\"uid\":\"sensor-1\"}]");
    module.spiffs.writeToFile(LEGACY_SENSORS_FILE, "[{\"uid\":\"sensor-legacy\"}]");
    module.spiffs.writeToFile(WIFI_FILE, "[{\"ssid\":\"greenhouse\"}]");

    TEST_ASSERT_TRUE(module.clearService.clearStoredFiles());

    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(SENSORS_FILE),
                              "the stored sensors survived the reset");
    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(LEGACY_SENSORS_FILE),
                              "the sensors under the legacy path survived the reset");
    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(WIFI_FILE),
                              "the stored WiFi networks survived the reset");
}

// --------------------------------------------------------------------------------------------
// 4. The sensor registry is stored as up to three files as well: the boot half takes all of them,
//    and the next read does not bring the registry back from a leftover.
// --------------------------------------------------------------------------------------------

static void test_the_boot_half_of_a_reset_takes_the_registry_leftover_copies_too(void)
{
    StorageUnderTest module;
    TEST_ASSERT_TRUE(module.sensorDao.saveSensors({makeSensor("sensor-1", 0)}));
    const std::string previous = module.spiffs.getFileContent(SENSORS_FILE);
    TEST_ASSERT_TRUE(module.sensorDao.saveSensors({makeSensor("sensor-1", 0), makeSensor("sensor-2", 1)}));
    const std::string published = module.spiffs.getFileContent(SENSORS_FILE);
    module.spiffs.writeToFile(SENSORS_BACKUP_FILE, previous.c_str());
    module.spiffs.writeToFile(SENSORS_TEMP_FILE, published.c_str());

    module.clearService.clearStoredFiles();

    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(SENSORS_FILE), "the stored sensors are still there");
    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(SENSORS_TEMP_FILE),
                              "the registry that was being published is still there");
    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(SENSORS_BACKUP_FILE),
                              "the registry that was being replaced is still there");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.sensorsAfterReboot(),
                                   "the registry was recovered from a leftover file after the reset");
}

// The same for the rule file: the boot half takes it with both of its companions.
static void test_the_boot_half_of_a_reset_takes_the_rule_file_leftover_copies_too(void)
{
    StorageUnderTest module;
    TEST_ASSERT_TRUE(module.ruleDao.saveRules({makeRule("rule-1", RuleMode::ON)}));
    const std::string previous = module.spiffs.getFileContent(RULES_FILE);
    TEST_ASSERT_TRUE(module.ruleDao.saveRules({makeRule("rule-1", RuleMode::ON), makeRule("rule-2", RuleMode::ON)}));
    const std::string published = module.spiffs.getFileContent(RULES_FILE);
    module.spiffs.writeToFile(RULES_BACKUP_FILE, previous.c_str());
    module.spiffs.writeToFile(RULES_TEMP_FILE, published.c_str());

    module.clearService.clearStoredFiles();

    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(RULES_FILE), "the stored rules are still there");
    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(RULES_TEMP_FILE),
                              "the rule file that was being published is still there");
    TEST_ASSERT_FALSE_MESSAGE(module.spiffs.fileExists(RULES_BACKUP_FILE),
                              "the rule file that was being replaced is still there");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.rulesAfterReboot(),
                                   "the rules were recovered from a leftover file after the reset");
}

// --------------------------------------------------------------------------------------------
// 5. Between the two halves. A DeleteModule that the server confirms resets the module and asks for
//    the restart, and the control loop goes on until the restart comes: a rule or a sensor written
//    through its repository in that time is written as the repository holds it - the rule set and
//    the registry the reset has just emptied. None of it may be there after the restart.
// --------------------------------------------------------------------------------------------

static void test_a_row_written_between_the_reset_and_the_restart_does_not_survive_it(void)
{
    StorageUnderTest module;
    RuleRepository rules(&module.ruleDao, nullptr, &module.debugSystem, &module.logger);
    SensorRepository sensors(&module.sensorDao, &module.debugSystem, &module.logger);
    TEST_ASSERT_TRUE(rules.save(makeRule("rule-1", RuleMode::ON)));
    TEST_ASSERT_TRUE(rules.save(makeRule("rule-2", RuleMode::ON)));
    TEST_ASSERT_TRUE(sensors.save(makeSensor("sensor-1", 0)));
    TEST_ASSERT_TRUE(sensors.save(makeSensor("sensor-2", 1)));
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, rules.count(), "the premise: two rules held");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, sensors.count(), "the premise: two sensors held");

    TEST_ASSERT_TRUE(module.clearService.clearAllData());
    TEST_ASSERT_EQUAL_MESSAGE(NEED_CLEAR_SPIFFS_MARKER, module.settings.getNeedClearSpiffs(),
                              "the reset did not arm the boot half - the scenario did not happen");

    // The loop, before the restart: a rule handed back to the user, a sensor's address.
    TEST_ASSERT_TRUE(rules.save(makeRule("rule-1", RuleMode::OFF)));
    SensorEntity readdressed = makeSensor("sensor-1", 0);
    readdressed.modbusDeviceAddress = MODBUS_ADDRESS_MAX;
    TEST_ASSERT_TRUE(sensors.save(readdressed));

    module.clearService.clearStoredFiles();

    char message[160];
    snprintf(message, sizeof(message),
             "the module came back from a factory reset with %u rules and %u sensors",
             (unsigned)module.rulesAfterReboot(), (unsigned)module.sensorsAfterReboot());
    printf("  reset: %s\n", message);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.rulesAfterReboot(), message);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)module.sensorsAfterReboot(), message);
    TEST_ASSERT_FALSE(module.spiffs.fileExists(RULES_FILE));
}

extern "C" void run_factory_reset_storage_tests(void)
{
    RUN_TEST(test_a_factory_reset_does_not_leave_the_catalog_recoverable);
    RUN_TEST(test_the_boot_half_of_a_reset_takes_the_leftover_copies_too);
    RUN_TEST(test_the_boot_half_of_a_reset_still_removes_sensors_and_wifi_networks);
    RUN_TEST(test_the_boot_half_of_a_reset_takes_the_registry_leftover_copies_too);
    RUN_TEST(test_the_boot_half_of_a_reset_takes_the_rule_file_leftover_copies_too);
    RUN_TEST(test_a_row_written_between_the_reset_and_the_restart_does_not_survive_it);
}
