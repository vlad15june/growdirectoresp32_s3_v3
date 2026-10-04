// The sensor registry and the rule file keep every stored row through a write the flash stops part
// of the way, through a read that does not bring back what is stored, and through a power cut
// between the renames that publish a new file.
//
// Each is one JSON array (/data/sensors.json, /data/rules.json); a change of one row rewrites all of
// it, built from what the repository holds:
//
//   - the new array goes next to the file and takes its place only once it is all there, so a write
//     the flash stops leaves the stored file whole. An array cut short is refused whole by the reader
//     (JsonArrayStreamSplitter), and a module restarting on one has no sensors at all;
//   - a write is not built from a cache that a failed read filled: the store is read again first,
//     and the write is refused for as long as it does not come back whole. Whole means as many
//     bytes as the file system's index says the file holds (statFile()), checked on every read;
//   - a missing file is taken from its companion only while it is missing, and a companion is judged
//     by whether it is a whole list - an empty one included.
//
// What this file pins:
//
//   1. a write the flash cut short costs no row - not to the next write, not to a restart;
//   2. a read that does not bring back what is stored costs no row: a write is built from what the
//      module holds, never from a failed read, at a restart as well as after a write; a store that
//      answers "nothing" after the module put rows into it is a failed read too; and removing a row
//      the registry does not hold writes nothing;
//   3. what a restart reads from a registry file that was cut short - no sensors - and what the next
//      write does with a file that came back whole and broken, or with no bytes in it (replaces it);
//      a read that lost a character inside a value, an open that failed and an existence check
//      that answered "not there" are reads that failed, and nothing is written from them;
//   4. a power cut between the renames that publish a new file: the restart takes the new file, an
//      empty list included, and a leftover backup is never taken while the file is there;
//   5. the bytes on flash keep their layout: the same records in the same order;
//   6. a fresh module - nothing stored - stores its first row the same way;
//   7. the rule file, written by the same code, gets the same - a rule synchronization included.
//
// What runs is production code: SensorRepository and RuleRepository over SpiffsSensorDao and
// SpiffsRuleDao, the converter the device runs, and SpiffsFileReplacer for the renames. Replaced is
// the flash device (Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h): a file opened with "w" is
// emptied at the open, as SPIFFS empties it, and holds what went in before the stop; a companion
// file whose write was stopped is dropped by the writer; and a read it refuses comes back empty, as
// the device adapter answers a file it could not open; a read it shortens or drops a character from
// brings back less than the file; an existence check it refuses answers "not there", as
// SPIFFS.exists() does when it cannot open the file; an open it refuses fails every call that opens
// the file, while its index lookup (statFile(), stat() on the device) still answers. Twice, a store
// stands in for the whole DAO (ForgetfulStore, ForgetfulRules below) - to say "nothing is stored"
// about a store that holds entities.

#include <unity.h>

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"
#include "Infrastructure/Repositories/RuleRepository.h"
#include "Infrastructure/Repositories/SensorRepository.h"

#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorDao.h"

#include <algorithm>
#include <set>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* const REGISTRY_FILE = "/data/sensors.json";
const char* const REGISTRY_TEMP_FILE = "/data/sensors.json.tmp";
const char* const REGISTRY_BACKUP_FILE = "/data/sensors.json.bak";
const char* const RULES_FILE = "/data/rules.json";

const char* const MODULE_UID = "a1b2c3d4e5f6";

/** @brief A Modbus sensor with `channels` channels, under `<module>.<suffix>`. */
SensorEntity makeSensor(const char* suffix, int pin, int channels)
{
    SensorEntity sensor;
    sensor.uid = std::string(MODULE_UID) + "." + suffix;
    sensor.sensorType = "ry_ws301";
    sensor.model = "ry_ws301";
    sensor.protocol = "ModBus";
    sensor.createdAt = "2026-09-01T10:00:00.000000+00:00";
    sensor.isActive = true;
    sensor.isError = false;
    sensor.status = SENSOR_STATUS_ADDED;
    sensor.pin = pin;
    sensor.modbusDeviceAddress = MODBUS_ADDRESS_MIN + pin;
    sensor.baudRate = 9600;
    sensor.sensorTypeUid = "4719a986-a300-48d5-bd1f-e34a1329798a";

    for (int channel = 1; channel <= channels; channel++)
    {
        ModbusRegisterConfig config;
        config.functionCode = 3;
        config.startRegister = (uint16_t)(channel - 1);
        config.numRegisters = 1;
        config.multiplier = 0.01f;
        config.offset = 0.0f;
        char name[32];
        snprintf(name, sizeof(name), "channel_%d", channel);
        config.channelName = name;
        config.channel = (uint8_t)channel;
        config.sensorType = name;
        config.canConnectMinValue = 0.0f;
        config.canConnectMaxValue = 100.0f;
        sensor.modbusRegisterConfigs.push_back(config);
    }
    return sensor;
}

/** @brief The same sensor, told apart by a field a module does change: its address. */
SensorEntity readdressed(SensorEntity sensor, int address)
{
    sensor.modbusDeviceAddress = address;
    return sensor;
}

std::string joined(std::vector<std::string> items, bool sorted)
{
    if (sorted)
    {
        std::sort(items.begin(), items.end());
    }
    std::string text;
    for (size_t i = 0; i < items.size(); i++)
    {
        text += (i == 0 ? "" : ",") + items[i];
    }
    return text.empty() ? std::string("(none)") : text;
}

std::string uidsOf(const std::vector<SensorEntity>& sensors, bool sorted)
{
    std::vector<std::string> uids;
    for (size_t i = 0; i < sensors.size(); i++)
    {
        uids.push_back(sensors[i].uid);
    }
    return joined(uids, sorted);
}

std::string uidsOf(const std::vector<Rule>& rules)
{
    std::vector<std::string> uids;
    for (size_t i = 0; i < rules.size(); i++)
    {
        uids.push_back(rules[i].uid);
    }
    return joined(uids, true);
}

/** @brief The sensor registry over its real storage path, wired the way globals.h wires it. */
struct RegistryOnFlash
{
    MockSpiffsAdapter flash;
    ArduinoJsonConverter converter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsSensorDao dao;
    SensorRepository repository;

    RegistryOnFlash() : dao(&flash, &converter, &debugSystem, &logger), repository(&dao) {}

    /** @brief Store rows through the repository, the way the module stores them. */
    void store(const std::vector<SensorEntity>& rows)
    {
        for (size_t i = 0; i < rows.size(); i++)
        {
            TEST_ASSERT_TRUE_MESSAGE(repository.save(rows[i]), "the premise: a row was not stored");
        }
    }

    /** @brief What a module that restarts on this flash comes up with, in the order the file has. */
    std::vector<SensorEntity> afterRestart()
    {
        SpiffsSensorDao daoAfterBoot(&flash, &converter, &debugSystem, &logger);
        SensorRepository repositoryAfterBoot(&daoAfterBoot);
        // The count is what the boot asks first (ModuleSetupService::processSensorsFromRepository()).
        const int count = repositoryAfterBoot.count();
        std::vector<SensorEntity> stored;
        daoAfterBoot.getAll(stored);
        TEST_ASSERT_EQUAL_INT_MESSAGE(count, (int)stored.size(),
                                      "the repository and the file disagree after the restart");
        return stored;
    }

    std::string held() { return uidsOf(repository.getAll(), true); }

    size_t fileSize() { return flash.getFileContent(REGISTRY_FILE).size(); }

    bool companionsLeft()
    {
        return flash.fileExists(REGISTRY_TEMP_FILE) || flash.fileExists(REGISTRY_BACKUP_FILE);
    }
};

/** @brief The registry text as it lies on flash, built from the one record format there is. */
std::string registryText(IJsonConverter& converter, const std::vector<SensorEntity>& sensors)
{
    return converter.sensorsToJson(sensors);
}

const SensorEntity PORT_0 = makeSensor("00", 0, 2);
const SensorEntity PORT_1 = makeSensor("01", 1, 3);
const SensorEntity PORT_2 = makeSensor("02", 2, 4);
const SensorEntity PORT_3 = makeSensor("03", 3, 2);

} // namespace

// --------------------------------------------------------------------------------------------
// 1. A write the flash cut short.
// --------------------------------------------------------------------------------------------

/**
 * Three rows stored; the write of a fourth stops half-way; the next write - a row's new address -
 * goes through. The registry has the three rows, the first one with its new address, in the module
 * and on flash.
 */
static void test_a_write_the_flash_cut_short_costs_no_row_of_the_registry(void)
{
    RegistryOnFlash module;
    module.store({PORT_0, PORT_1, PORT_2});
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, module.repository.count(), "the premise: three rows held");

    module.flash.setMaxTotalWriteBytes(module.fileSize() / 2);
    TEST_ASSERT_FALSE_MESSAGE(module.repository.save(PORT_3),
                              "the premise: the flash was to stop this write half-way");
    module.flash.setMaxTotalWriteBytes(0);

    TEST_ASSERT_TRUE_MESSAGE(module.repository.save(readdressed(PORT_0, MODBUS_ADDRESS_MAX)),
                             "the premise: the next write went through");

    const std::vector<SensorEntity> stored = module.afterRestart();
    char message[256];
    snprintf(message, sizeof(message),
             "after a write the flash cut short and one that went through, the module holds %s and "
             "the flash %s",
             module.held().c_str(), uidsOf(stored, true).c_str());
    printf("  registry: %s\n", message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02",
                                     module.held().c_str(), message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02",
                                     uidsOf(stored, true).c_str(), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(MODBUS_ADDRESS_MAX, stored[0].modbusDeviceAddress,
                                  "the write that went through did not land");
}

/**
 * The same cut, and the module restarts before anything else is written: it comes up with the
 * three rows it had, and the flash holds nothing but the registry file.
 */
static void test_a_restart_after_a_write_the_flash_cut_short_finds_every_row(void)
{
    RegistryOnFlash module;
    module.store({PORT_0, PORT_1, PORT_2});

    module.flash.setMaxTotalWriteBytes(module.fileSize() / 2);
    TEST_ASSERT_FALSE_MESSAGE(module.repository.save(PORT_3),
                              "the premise: the flash was to stop this write half-way");
    module.flash.setMaxTotalWriteBytes(0);

    const std::vector<SensorEntity> stored = module.afterRestart();
    char message[200];
    snprintf(message, sizeof(message),
             "a restart after a write the flash cut short came up with %u sensors (%s)",
             (unsigned)stored.size(), uidsOf(stored, false).c_str());
    printf("  registry: %s\n", message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02",
                                     uidsOf(stored, false).c_str(), message);
    TEST_ASSERT_FALSE_MESSAGE(module.companionsLeft(),
                              "the write that was cut short left a file behind");
}

// --------------------------------------------------------------------------------------------
// 2. A read that does not bring back what is stored.
// --------------------------------------------------------------------------------------------

/**
 * The module holds three rows, loaded; the flash refuses the next read and goes on taking writes; a
 * row is written. The write is built from what the module holds, so the refused read is never
 * asked for, and the flash ends up with every row.
 */
static void test_a_write_from_the_registry_it_holds_does_not_read_the_file(void)
{
    RegistryOnFlash module;
    module.store({PORT_0, PORT_1, PORT_2});
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, module.repository.count(), "the premise: three rows held");

    module.flash.refuseNextReads(1);
    TEST_ASSERT_TRUE_MESSAGE(module.repository.save(readdressed(PORT_1, MODBUS_ADDRESS_MAX)),
                             "the premise: the write went through");
    module.flash.refuseNextReads(0);

    const std::vector<SensorEntity> stored = module.afterRestart();
    char message[200];
    snprintf(message, sizeof(message),
             "a write from the registry the module holds left the flash holding %s",
             uidsOf(stored, false).c_str());
    printf("  registry: %s\n", message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02",
                                     uidsOf(stored, false).c_str(), message);
    TEST_ASSERT_EQUAL_INT(MODBUS_ADDRESS_MAX, stored[1].modbusDeviceAddress);
}

/**
 * A write went through, which leaves the cache to be read back from the file; the flash refuses
 * that read. The next write of a row is not built from what the refused read left - the registry is
 * read again, and the flash ends up with every row.
 */
static void test_a_write_after_a_refused_reload_keeps_every_row(void)
{
    RegistryOnFlash module;
    module.store({PORT_0, PORT_1, PORT_2});

    module.flash.refuseNextReads(1);
    TEST_ASSERT_TRUE_MESSAGE(module.repository.save(readdressed(PORT_1, MODBUS_ADDRESS_MAX)),
                             "the premise: the write went through");
    module.flash.refuseNextReads(0);

    const std::vector<SensorEntity> stored = module.afterRestart();
    char message[200];
    snprintf(message, sizeof(message),
             "a write after a refused reload of the cache left the flash holding %s",
             uidsOf(stored, false).c_str());
    printf("  registry: %s\n", message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02",
                                     uidsOf(stored, false).c_str(), message);
    TEST_ASSERT_EQUAL_INT(MODBUS_ADDRESS_MAX, stored[1].modbusDeviceAddress);
}

/**
 * The flash refuses every read for a while. A write is refused - nothing on flash changes - and once
 * the flash reads again, the next write keeps every row.
 */
static void test_a_registry_that_does_not_read_back_is_not_written_over(void)
{
    RegistryOnFlash module;
    module.store({PORT_0, PORT_1, PORT_2});
    const std::string before = module.flash.getFileContent(REGISTRY_FILE);

    module.flash.refuseNextReads(1000);
    const bool written = module.repository.save(PORT_3);
    module.flash.refuseNextReads(0);

    printf("  registry: a write while the flash refuses every read was %s\n",
           written ? "made" : "refused");
    TEST_ASSERT_FALSE_MESSAGE(written, "a write was built from a read that brought nothing back");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), module.flash.getFileContent(REGISTRY_FILE).c_str(),
                                     "the registry on flash changed under a refused write");

    TEST_ASSERT_TRUE(module.repository.save(PORT_3));
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02,a1b2c3d4e5f6.03",
                             uidsOf(module.afterRestart(), false).c_str());
}

/**
 * A restart: the module has read nothing and stored nothing yet, so it knows nothing of the file but
 * what the read brings back - and the first read and the one after it are refused. The first write
 * of the boot is refused; the one after the flash reads again keeps every row.
 */
static void test_a_restart_that_cannot_read_the_registry_does_not_write_over_it(void)
{
    RegistryOnFlash before;
    before.store({PORT_0, PORT_1, PORT_2});

    SpiffsSensorDao daoAfterBoot(&before.flash, &before.converter, &before.debugSystem, &before.logger);
    SensorRepository afterBoot(&daoAfterBoot);
    before.flash.refuseNextReads(2);
    const bool written = afterBoot.save(PORT_3);
    before.flash.refuseNextReads(0);

    printf("  registry: the first write of a boot whose reads were refused was %s\n",
           written ? "made" : "refused");
    TEST_ASSERT_FALSE_MESSAGE(written, "a boot wrote over a registry it could not read");
    TEST_ASSERT_TRUE(afterBoot.save(PORT_3));
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02,a1b2c3d4e5f6.03",
                             uidsOf(before.afterRestart(), false).c_str());
}

namespace {

/**
 * @brief A store that answers "nothing is stored" when asked to - about rows it does hold.
 *
 * What the device's storage answers when the file system cannot even look the file up: the open
 * behind SPIFFS.exists() allocates (vfs_api.cpp: VFSFileImpl, malloc and fopen), and a lookup that
 * fails reads as a file that is not there.
 */
class ForgetfulStore : public MockSpiffsSensorDao {
public:
    int readsAnsweringNothing = 0;

    bool getAll(std::vector<SensorEntity>& entities) override
    {
        if (readsAnsweringNothing > 0)
        {
            readsAnsweringNothing--;
            entities.clear();
            return false;
        }
        return MockSpiffsSensorDao::getAll(entities);
    }
};

} // namespace

/**
 * The module put three rows into the store; reading it back, the store answers that nothing is
 * stored. That is a read that failed, not an empty registry: the next write reads again and keeps
 * every row.
 */
static void test_a_store_that_answers_nothing_after_holding_rows_is_not_written_over(void)
{
    ForgetfulStore store;
    SensorRepository repository(&store);
    TEST_ASSERT_TRUE(repository.save(PORT_0));
    TEST_ASSERT_TRUE(repository.save(PORT_1));
    TEST_ASSERT_TRUE(repository.save(PORT_2));

    store.readsAnsweringNothing = 1;
    TEST_ASSERT_TRUE_MESSAGE(repository.save(PORT_3), "the premise: the write went through");

    printf("  registry: a write after the store answered \"nothing\" left it holding %s\n",
           uidsOf(store.getStorage(), false).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02,a1b2c3d4e5f6.03",
        uidsOf(store.getStorage(), false).c_str(),
        "a store that answered \"nothing\" was written over with the row being saved");
}

/**
 * The same at the flash: after the module put three rows in, the reload of the cache finds the
 * registry's read refused and the existence check answering "not there". The file's index still has
 * it, so the read is one that failed, and the write that follows keeps every row.
 */
static void test_a_registry_the_lookup_calls_missing_is_not_written_over_after_rows_went_in(void)
{
    RegistryOnFlash module;
    module.store({PORT_0, PORT_1, PORT_2});

    module.flash.refuseNextReads(1);
    module.flash.refuseNextLookups(1);
    module.repository.count();
    module.flash.refuseNextReads(0);
    module.flash.refuseNextLookups(0);
    TEST_ASSERT_TRUE_MESSAGE(module.repository.save(PORT_3), "the premise: the write went through");

    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02,a1b2c3d4e5f6.03",
                             uidsOf(module.afterRestart(), false).c_str());
}

/**
 * A restart whose reads of the registry come back with half of the file - fewer bytes than it
 * holds, which is a read that failed and not a broken file. The first write of the boot is refused
 * and the file stays; once the reads come back whole, the next write keeps every row.
 */
static void test_a_restart_whose_reads_come_back_short_does_not_write_over_the_registry(void)
{
    RegistryOnFlash before;
    before.store({PORT_0, PORT_1, PORT_2});
    const std::string stored = before.flash.getFileContent(REGISTRY_FILE);

    SpiffsSensorDao daoAfterBoot(&before.flash, &before.converter, &before.debugSystem, &before.logger);
    SensorRepository afterBoot(&daoAfterBoot);
    before.flash.shortenNextReads(2);
    const bool written = afterBoot.save(PORT_3);
    before.flash.shortenNextReads(0);

    TEST_ASSERT_FALSE_MESSAGE(written, "a boot wrote over a registry its reads brought half of");
    TEST_ASSERT_EQUAL_STRING(stored.c_str(), before.flash.getFileContent(REGISTRY_FILE).c_str());
    TEST_ASSERT_TRUE(afterBoot.save(PORT_3));
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02,a1b2c3d4e5f6.03",
                             uidsOf(before.afterRestart(), false).c_str());
}

/**
 * A read that lost one character inside a value: the second row's Modbus address is 31 on flash,
 * and the read brings it back as 1 - still a list that parses, one byte shorter than the file. It
 * is a read that failed, not the registry: the next write keeps the address the file holds.
 */
static void test_a_read_that_lost_a_character_inside_a_value_is_not_written_back(void)
{
    RegistryOnFlash module;
    module.store({PORT_0, readdressed(PORT_1, 31), PORT_2});
    const std::string onFlash = module.flash.getFileContent(REGISTRY_FILE);
    const size_t secondRow = onFlash.find("\"uid\":\"a1b2c3d4e5f6.01\"");
    const size_t address = onFlash.find("\"modbusDeviceAddress\":31", secondRow);
    TEST_ASSERT_TRUE_MESSAGE(secondRow != std::string::npos && address != std::string::npos,
                             "the premise: the second row carries address 31 on flash");

    module.flash.dropCharacterFromNextRead(address + std::string("\"modbusDeviceAddress\":").size());
    TEST_ASSERT_TRUE_MESSAGE(module.repository.save(readdressed(PORT_0, MODBUS_ADDRESS_MAX)),
                             "the premise: the write went through");

    const std::vector<SensorEntity> stored = module.afterRestart();
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02",
                             uidsOf(stored, false).c_str());
    printf("  registry: after a read that lost a character the second row's address on flash is %d\n",
           stored[1].modbusDeviceAddress);
    TEST_ASSERT_EQUAL_INT_MESSAGE(31, stored[1].modbusDeviceAddress,
                                  "a read that lost a character was written back over the registry");
    TEST_ASSERT_EQUAL_INT(MODBUS_ADDRESS_MAX, stored[0].modbusDeviceAddress);
}

/**
 * A restart on a heap that cannot serve an open: the read of the registry, the existence check and
 * the size behind it all fail - each of them opens the file - while the file system's index still
 * answers. The first write of the boot is refused and the file stays; once opens work again, the
 * next write keeps every row. Four opens are refused: one read of the registry and of both
 * companions, and the existence check, as a store that took that check for the truth would ask.
 */
static void test_a_restart_whose_opens_fail_does_not_write_over_the_registry(void)
{
    RegistryOnFlash before;
    before.store({PORT_0, PORT_1, PORT_2});
    const std::string stored = before.flash.getFileContent(REGISTRY_FILE);

    SpiffsSensorDao daoAfterBoot(&before.flash, &before.converter, &before.debugSystem, &before.logger);
    SensorRepository afterBoot(&daoAfterBoot);
    before.flash.refuseNextOpens(4);
    const bool written = afterBoot.save(PORT_3);
    before.flash.refuseNextOpens(0);

    printf("  registry: the first write of a boot whose opens failed was %s\n",
           written ? "made" : "refused");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(stored.c_str(), before.flash.getFileContent(REGISTRY_FILE).c_str(),
                                     "a boot whose opens failed wrote over the registry");
    TEST_ASSERT_FALSE(written);
    TEST_ASSERT_TRUE(afterBoot.save(PORT_3));
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02,a1b2c3d4e5f6.03",
                             uidsOf(before.afterRestart(), false).c_str());
}

/**
 * A restart whose existence check of the registry answers "not there" and whose reads of it are
 * refused. The file system's index has the file, so the store is one that did not read, not one
 * that is empty: no write goes over it.
 */
static void test_a_restart_whose_existence_check_says_missing_does_not_write_over_the_registry(void)
{
    RegistryOnFlash before;
    before.store({PORT_0, PORT_1, PORT_2});
    const std::string stored = before.flash.getFileContent(REGISTRY_FILE);

    SpiffsSensorDao daoAfterBoot(&before.flash, &before.converter, &before.debugSystem, &before.logger);
    SensorRepository afterBoot(&daoAfterBoot);
    before.flash.refuseNextLookups(2);
    before.flash.refuseNextReads(2);
    const bool written = afterBoot.save(PORT_3);
    before.flash.refuseNextLookups(0);
    before.flash.refuseNextReads(0);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(stored.c_str(), before.flash.getFileContent(REGISTRY_FILE).c_str(),
                                     "a boot that was told the registry is missing wrote over it");
    TEST_ASSERT_FALSE(written);
    TEST_ASSERT_TRUE(afterBoot.save(PORT_3));
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02,a1b2c3d4e5f6.03",
                             uidsOf(before.afterRestart(), false).c_str());
}

/**
 * A restart whose lookups in the file system's index fail. A lookup that failed is not a file that
 * is missing: nothing is written over the registry, and once lookups work the next write keeps
 * every row.
 */
static void test_a_registry_that_cannot_be_looked_up_is_not_written_over(void)
{
    RegistryOnFlash before;
    before.store({PORT_0, PORT_1, PORT_2});
    const std::string stored = before.flash.getFileContent(REGISTRY_FILE);

    SpiffsSensorDao daoAfterBoot(&before.flash, &before.converter, &before.debugSystem, &before.logger);
    SensorRepository afterBoot(&daoAfterBoot);
    before.flash.refuseNextStats(1000);
    const bool written = afterBoot.save(PORT_3);
    before.flash.refuseNextStats(0);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(stored.c_str(), before.flash.getFileContent(REGISTRY_FILE).c_str(),
                                     "a boot whose lookups failed wrote over the registry");
    TEST_ASSERT_FALSE(written);
    TEST_ASSERT_TRUE(afterBoot.save(PORT_3));
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02,a1b2c3d4e5f6.03",
                             uidsOf(before.afterRestart(), false).c_str());
}

/** The DAO's own save of one row reads the file first, and does not write over a file it could not read. */
static void test_the_dao_does_not_write_a_row_over_a_registry_it_could_not_read(void)
{
    RegistryOnFlash module;
    module.store({PORT_0, PORT_1, PORT_2});
    const std::string before = module.flash.getFileContent(REGISTRY_FILE);

    module.flash.refuseNextReads(1);
    const bool written = module.dao.saveSensor(PORT_3);
    module.flash.refuseNextReads(0);

    TEST_ASSERT_FALSE_MESSAGE(written, "the DAO wrote a row over a registry it could not read");
    TEST_ASSERT_EQUAL_STRING(before.c_str(), module.flash.getFileContent(REGISTRY_FILE).c_str());
}

/** Removing a row the registry does not hold answers false and writes nothing. */
static void test_removing_a_row_the_registry_does_not_hold_writes_nothing(void)
{
    RegistryOnFlash module;
    module.store({PORT_0, PORT_1});
    const std::string before = module.flash.getFileContent(REGISTRY_FILE);
    module.flash.resetWriteStats();

    TEST_ASSERT_FALSE(module.repository.remove("a1b2c3d4e5f6.99"));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.flash.getWriteChunkCount(),
                                     "removing a row that is not there wrote the registry");
    TEST_ASSERT_EQUAL_STRING(before.c_str(), module.flash.getFileContent(REGISTRY_FILE).c_str());
}

// --------------------------------------------------------------------------------------------
// 3. What a restart reads from a registry file that was cut short.
// --------------------------------------------------------------------------------------------

/**
 * The reader's verdict: an array that never closes is refused whole, so a
 * registry file cut short is a module with no sensors. This is why no write may leave one.
 */
static void test_a_registry_file_cut_short_is_read_as_no_sensors(void)
{
    RegistryOnFlash module;
    const std::string whole = registryText(module.converter, {PORT_0, PORT_1, PORT_2});
    module.flash.writeToFile(REGISTRY_FILE, whole.substr(0, (whole.size() * 2) / 3).c_str());

    const std::vector<SensorEntity> stored = module.afterRestart();
    printf("  registry: a file cut short at two thirds of three sensors is read as %u sensors\n",
           (unsigned)stored.size());
    TEST_ASSERT_EQUAL_size_t(0, stored.size());
}

/**
 * A file that came back to its last byte and is not a whole list - what a write over the file in
 * place leaves when it is stopped. Nothing in it can be read, by this read or any other, so the next
 * write replaces it: the module can store sensors again without a factory reset.
 */
static void test_a_whole_registry_file_that_is_broken_is_replaced_by_the_next_write(void)
{
    RegistryOnFlash module;
    const std::string whole = registryText(module.converter, {PORT_0, PORT_1, PORT_2});
    module.flash.writeToFile(REGISTRY_FILE, whole.substr(0, (whole.size() * 2) / 3).c_str());

    TEST_ASSERT_TRUE_MESSAGE(module.repository.save(PORT_3),
                             "a broken registry file blocked every write");
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.03", uidsOf(module.afterRestart(), false).c_str());
}

/**
 * A file that is there with no bytes in it - what a write over the file in place leaves when the
 * power goes before its first bytes reach the file. The file system's index says so (size 0), the
 * read brings back all of it, and it holds nothing: the next write replaces it.
 */
static void test_a_registry_file_with_no_bytes_is_written_over(void)
{
    RegistryOnFlash module;
    module.flash.writeToFile(REGISTRY_FILE, "");

    const bool written = module.repository.save(PORT_3);
    printf("  registry: a write over a registry file with no bytes was %s\n",
           written ? "made" : "refused");
    TEST_ASSERT_TRUE_MESSAGE(written, "a registry file with no bytes blocked every write");
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.03", uidsOf(module.afterRestart(), false).c_str());
}

namespace {

Rule makeRule(const char* uid, int version)
{
    Rule rule;
    rule.uid = uid;
    rule.version = version;
    rule.deviceUid = "7c1a3b5d-2e64-4f80-b1c9-6a0d8e2f4b71";
    rule.sensorUid = "0a4c9d2e-53b1-4f6e-9c37-8d21e5a0b471";
    rule.ruleType = RuleType::SENSOR;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    rule.protectiveShutdown = true;
    rule.timeToSetPoint = 10;
    return rule;
}

const char* const RULE_A = "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a1b34";
const char* const RULE_B = "5d8e2f40-9a16-4c73-8e91-4f7a0b2c3d56";
const char* const RULE_C = "6e9f3051-ab27-4d84-9fa2-50db1c3d4e67";
const char* const RULE_D = "7fa04162-bc38-4e95-a0b3-61ec2d4e5f78";

} // namespace

// --------------------------------------------------------------------------------------------
// 4. A power cut between the renames that publish a new registry file.
// --------------------------------------------------------------------------------------------

/**
 * Publishing a new file is a sequence of renames: `<file>` to `<file>.bak`, `<file>.tmp` to
 * `<file>`, then `<file>.bak` dropped (SpiffsFileReplacer). Cut after the first: the name everyone
 * reads is free, the new registry is complete under `.tmp` and the previous one under `.bak`. The
 * restart takes the newer one and gives it its name back.
 */
static void test_a_restart_inside_the_swap_finds_the_registry_under_its_companion(void)
{
    RegistryOnFlash module;
    const std::string previous = registryText(module.converter, {PORT_0, PORT_1});
    const std::string published = registryText(module.converter, {PORT_0, PORT_1, PORT_2});

    // Cut between the two renames.
    module.flash.writeToFile(REGISTRY_BACKUP_FILE, previous.c_str());
    module.flash.writeToFile(REGISTRY_TEMP_FILE, published.c_str());
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02",
                             uidsOf(module.afterRestart(), false).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(published.c_str(),
                                     module.flash.getFileContent(REGISTRY_FILE).c_str(),
                                     "the registry did not get its name back");
    TEST_ASSERT_FALSE_MESSAGE(module.companionsLeft(),
                              "what was left of the replacement is still there");

    // Cut while the new file was still being streamed: it never closes, and the previous registry
    // is the one that is whole.
    RegistryOnFlash streaming;
    streaming.flash.writeToFile(REGISTRY_BACKUP_FILE, previous.c_str());
    streaming.flash.writeToFile(REGISTRY_TEMP_FILE, published.substr(0, published.size() / 2).c_str());
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01",
                             uidsOf(streaming.afterRestart(), false).c_str());
    TEST_ASSERT_FALSE(streaming.companionsLeft());
}

/**
 * The last row of the registry is removed, and the power is cut between the renames: the new
 * registry is a whole empty list under `.tmp`, the one with the row under `.bak`. The restart takes
 * the empty one - the row was removed - gives it the registry's name and drops the rest.
 */
static void test_a_restart_after_the_last_row_went_finds_no_rows(void)
{
    RegistryOnFlash module;
    module.flash.writeToFile(REGISTRY_BACKUP_FILE, registryText(module.converter, {PORT_0}).c_str());
    module.flash.writeToFile(REGISTRY_TEMP_FILE, registryText(module.converter, {}).c_str());

    const std::vector<SensorEntity> stored = module.afterRestart();
    printf("  registry: a restart after the last row went and the power was cut comes up with %s\n",
           uidsOf(stored, false).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("(none)", uidsOf(stored, false).c_str(),
                                     "a removed row came back from the file it was removed from");
    TEST_ASSERT_EQUAL_STRING("[]", module.flash.getFileContent(REGISTRY_FILE).c_str());
    TEST_ASSERT_FALSE(module.companionsLeft());
}

/** The same for the rule file: the last rule went, and the restart does not bring it back. */
static void test_a_restart_after_the_last_rule_went_finds_no_rules(void)
{
    MockSpiffsAdapter flash;
    ArduinoJsonConverter converter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    flash.writeToFile("/data/rules.json.bak",
                      converter.rulesToJson({makeRule(RULE_A, 1)}).c_str());
    flash.writeToFile("/data/rules.json.tmp", converter.rulesToJson({}).c_str());

    SpiffsRuleDao daoAfterBoot(&flash, &converter, &debugSystem, &logger);
    RuleRepository afterBoot(&daoAfterBoot, nullptr, &debugSystem, &logger);
    const int rules = afterBoot.count();
    printf("  rules: a restart after the last rule went and the power was cut comes up with %d rules\n",
           rules);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rules, "a removed rule came back from the file it was removed from");
    TEST_ASSERT_EQUAL_STRING("[]", flash.getFileContent(RULES_FILE).c_str());
    TEST_ASSERT_FALSE(flash.fileExists("/data/rules.json.tmp") || flash.fileExists("/data/rules.json.bak"));
}

/**
 * The power was cut after the new registry took its name and before the backup went: the registry
 * is whole, and a `.bak` with a row since removed is left beside it. A read of the registry is then
 * refused. The backup is not taken for the registry - only a missing registry is taken from a
 * companion - and the next write stores the registry, not the backup.
 */
static void test_a_leftover_backup_is_not_taken_while_the_registry_is_there(void)
{
    RegistryOnFlash module;
    module.flash.writeToFile(REGISTRY_FILE, registryText(module.converter, {PORT_0, PORT_1}).c_str());
    module.flash.writeToFile(REGISTRY_BACKUP_FILE,
                             registryText(module.converter, {PORT_0, PORT_1, PORT_2}).c_str());

    module.flash.refuseNextReads(1);
    TEST_ASSERT_TRUE_MESSAGE(module.repository.save(PORT_3), "the premise: the write went through");
    module.flash.refuseNextReads(0);

    const std::vector<SensorEntity> stored = module.afterRestart();
    printf("  registry: a write beside a leftover backup, its first read refused, left %s\n",
           uidsOf(stored, false).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.03",
                                     uidsOf(stored, false).c_str(),
                                     "a row removed before the power cut came back from the backup");
}

/**
 * The power was cut between the renames, and the reads of the restart are refused - the read that
 * reaches the new registry under `.tmp` among them. What `.tmp` holds cannot be judged, and `.bak`
 * is older: neither is taken. Once reads work, the new registry is the one the next write keeps.
 */
static void test_a_companion_that_does_not_read_back_is_not_passed_over_for_the_older_one(void)
{
    RegistryOnFlash module;
    module.flash.writeToFile(REGISTRY_TEMP_FILE,
                             registryText(module.converter, {PORT_0, PORT_1, PORT_2}).c_str());
    module.flash.writeToFile(REGISTRY_BACKUP_FILE, registryText(module.converter, {PORT_0, PORT_1}).c_str());

    module.flash.refuseNextReads(2);
    module.repository.count();
    module.flash.refuseNextReads(0);
    TEST_ASSERT_TRUE_MESSAGE(module.repository.save(PORT_3), "the premise: the write went through");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.02,a1b2c3d4e5f6.03",
                                     uidsOf(module.afterRestart(), false).c_str(),
                                     "the older registry was taken while the newer one did not read");
}

/**
 * The power was cut between the renames, and the new registry under `.tmp` cannot be given the
 * registry's name - the rename is refused. It is the registry all the same: the restart comes up
 * with its rows, and the name is tried again at the next read.
 */
static void test_a_companion_that_cannot_take_the_name_is_still_the_registry(void)
{
    RegistryOnFlash module;
    module.flash.writeToFile(REGISTRY_TEMP_FILE, registryText(module.converter, {PORT_0, PORT_1}).c_str());
    module.flash.setRenameFailSource(REGISTRY_TEMP_FILE);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, module.repository.count(),
                                  "the new registry was not taken because its name was refused");
    TEST_ASSERT_FALSE(module.flash.fileExists(REGISTRY_FILE));

    module.flash.setRenameFailSource("");
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01", uidsOf(module.afterRestart(), false).c_str());
    TEST_ASSERT_TRUE_MESSAGE(module.flash.fileExists(REGISTRY_FILE), "the name was not taken at the next read");
}

/**
 * The same leftover backup, and this time the existence check of the registry answers "not there"
 * as well. Whether the registry is there is taken from the file system's index, which has it, so
 * the read does not go to the companions: the next write stores the registry, not the backup.
 */
static void test_a_backup_is_not_taken_when_the_registry_lookup_is_refused(void)
{
    RegistryOnFlash module;
    module.flash.writeToFile(REGISTRY_FILE, registryText(module.converter, {PORT_0, PORT_1}).c_str());
    module.flash.writeToFile(REGISTRY_BACKUP_FILE,
                             registryText(module.converter, {PORT_0, PORT_1, PORT_2}).c_str());

    module.flash.refuseNextReads(1);
    module.flash.refuseNextLookups(1);
    module.repository.count();
    module.flash.refuseNextReads(0);
    module.flash.refuseNextLookups(0);
    TEST_ASSERT_TRUE_MESSAGE(module.repository.save(PORT_3), "the premise: the write went through");

    const std::vector<SensorEntity> stored = module.afterRestart();
    printf("  registry: a write beside a leftover backup, its read and lookup refused, left %s\n",
           uidsOf(stored, false).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01,a1b2c3d4e5f6.03",
                                     uidsOf(stored, false).c_str(),
                                     "a row removed before the power cut came back from the backup");
}

// --------------------------------------------------------------------------------------------
// 5. The layout on flash.
// --------------------------------------------------------------------------------------------

/**
 * Rows are written in the order the file has them: a row that is updated keeps its place, a new one
 * goes last, a removed one leaves no gap. Eight rows - so that an order the file does not have
 * cannot match it by chance - then one updated, one removed and one added.
 */
static void test_the_registry_keeps_its_layout_on_flash(void)
{
    RegistryOnFlash module;
    std::vector<SensorEntity> rows;
    for (int i = 0; i < 8; i++)
    {
        char suffix[8];
        snprintf(suffix, sizeof(suffix), "%02d", 10 + i);
        rows.push_back(makeSensor(suffix, i % 4, 1 + i % 3));
    }
    module.store(rows);

    TEST_ASSERT_TRUE(module.repository.save(readdressed(rows[3], MODBUS_ADDRESS_MAX)));
    TEST_ASSERT_TRUE(module.repository.remove(rows[5].uid.c_str()));
    const SensorEntity added = makeSensor("20", 1, 2);
    TEST_ASSERT_TRUE(module.repository.save(added));

    std::vector<SensorEntity> expected = rows;
    expected[3] = readdressed(rows[3], MODBUS_ADDRESS_MAX);
    expected.erase(expected.begin() + 5);
    expected.push_back(added);

    const std::string onFlash = module.flash.getFileContent(REGISTRY_FILE);
    printf("  registry: rows on flash after an update, a removal and an addition: %s\n",
           uidsOf(module.afterRestart(), false).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(registryText(module.converter, expected).c_str(), onFlash.c_str(),
                                     "the registry on flash is not laid out as it was");
}

// --------------------------------------------------------------------------------------------
// 6. A fresh module.
// --------------------------------------------------------------------------------------------

/**
 * Nothing stored yet. The first write is cut short: the module holds no row and a restart finds
 * none, with nothing left over. The next one goes through, and a restart finds that row.
 */
static void test_a_fresh_module_stores_its_first_row_whole(void)
{
    RegistryOnFlash module;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.repository.count(), "the premise: nothing stored");

    module.flash.setMaxTotalWriteBytes(16);
    TEST_ASSERT_FALSE_MESSAGE(module.repository.save(PORT_1),
                              "the premise: the flash was to stop this write");
    module.flash.setMaxTotalWriteBytes(0);
    TEST_ASSERT_EQUAL_INT(0, module.repository.count());
    TEST_ASSERT_EQUAL_size_t(0, module.afterRestart().size());
    TEST_ASSERT_FALSE(module.companionsLeft());

    TEST_ASSERT_TRUE(module.repository.save(PORT_1));
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6.01", uidsOf(module.afterRestart(), false).c_str());
    TEST_ASSERT_FALSE(module.companionsLeft());
}

// --------------------------------------------------------------------------------------------
// 7. The rule file.
// --------------------------------------------------------------------------------------------

static void test_a_rule_file_write_the_flash_cut_short_costs_no_rule(void)
{
    MockSpiffsAdapter flash;
    ArduinoJsonConverter converter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsRuleDao dao(&flash, &converter, &debugSystem, &logger);
    RuleRepository repository(&dao, nullptr, &debugSystem, &logger);

    TEST_ASSERT_TRUE(repository.save(makeRule(RULE_A, 1)));
    TEST_ASSERT_TRUE(repository.save(makeRule(RULE_B, 1)));
    TEST_ASSERT_TRUE(repository.save(makeRule(RULE_C, 1)));
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, repository.count(), "the premise: three rules held");

    flash.setMaxTotalWriteBytes(flash.getFileContent(RULES_FILE).size() / 2);
    TEST_ASSERT_FALSE_MESSAGE(repository.save(makeRule(RULE_D, 1)),
                              "the premise: the flash was to stop this write half-way");
    flash.setMaxTotalWriteBytes(0);
    TEST_ASSERT_TRUE(repository.save(makeRule(RULE_A, 2)));

    SpiffsRuleDao daoAfterBoot(&flash, &converter, &debugSystem, &logger);
    std::vector<Rule> stored;
    daoAfterBoot.getAll(stored);
    printf("  rules: after a write the flash cut short and one that went through the flash holds "
           "%u rules\n", (unsigned)stored.size());
    std::vector<std::string> expected = {RULE_A, RULE_B, RULE_C};
    TEST_ASSERT_EQUAL_STRING_MESSAGE(joined(expected, true).c_str(), uidsOf(stored).c_str(),
                                     "the rule file lost rules to a write the flash cut short");
}

namespace {

/** @brief The rule file over its real storage path, wired the way globals.h wires it. */
struct RulesOnFlash
{
    MockSpiffsAdapter flash;
    ArduinoJsonConverter converter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsRuleDao dao;
    RuleRepository repository;

    RulesOnFlash()
        : dao(&flash, &converter, &debugSystem, &logger),
          repository(&dao, nullptr, &debugSystem, &logger)
    {
    }

    /** @brief The rule uids a module restarting on this flash reads, sorted. */
    std::string afterRestart()
    {
        SpiffsRuleDao daoAfterBoot(&flash, &converter, &debugSystem, &logger);
        std::vector<Rule> stored;
        daoAfterBoot.getAll(stored);
        return uidsOf(stored);
    }
};

const std::string ALL_FOUR_RULES = joined({RULE_A, RULE_B, RULE_C, RULE_D}, true);

/** @brief A rule store that answers "nothing is stored" when asked to - about rules it holds. */
class ForgetfulRules : public MockSpiffsRuleDao {
public:
    int readsAnsweringNothing = 0;

    bool getAll(std::vector<Rule>& entities) override
    {
        if (readsAnsweringNothing > 0)
        {
            readsAnsweringNothing--;
            entities.clear();
            return false;
        }
        return MockSpiffsRuleDao::getAll(entities);
    }
};

} // namespace

/**
 * Three rules stored, the cache to be read back after the last write, and that read refused. A rule
 * pushed by the server (one rule through the synchronization, as RULE_PUSH applies it) is not merged
 * into an empty cache: the rule file is read again, and every rule stays.
 */
static void test_a_rule_push_after_a_refused_reload_keeps_every_rule(void)
{
    RulesOnFlash module;
    TEST_ASSERT_TRUE(module.repository.save(makeRule(RULE_A, 1)));
    TEST_ASSERT_TRUE(module.repository.save(makeRule(RULE_B, 1)));
    TEST_ASSERT_TRUE(module.repository.save(makeRule(RULE_C, 1)));

    module.flash.refuseNextReads(1);
    const int synced = module.repository.synchronizeRules({makeRule(RULE_D, 1)});
    module.flash.refuseNextReads(0);

    printf("  rules: a pushed rule after a refused reload synchronized %d, the flash holds %s\n",
           synced, module.afterRestart().c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(ALL_FOUR_RULES.c_str(), module.afterRestart().c_str(),
                                     "a pushed rule was merged into a cache a failed read left");
    TEST_ASSERT_EQUAL_INT(1, synced);
}

/**
 * A restart: the module knows nothing of the rule file but what reading it brings back, and both
 * reads are refused. The first rule synchronization of the boot stores nothing and says so (0); the
 * rule file stays; once it reads, the next one keeps every rule.
 */
static void test_a_restart_that_cannot_read_the_rule_file_does_not_write_over_it(void)
{
    RulesOnFlash before;
    TEST_ASSERT_TRUE(before.repository.save(makeRule(RULE_A, 1)));
    TEST_ASSERT_TRUE(before.repository.save(makeRule(RULE_B, 1)));
    TEST_ASSERT_TRUE(before.repository.save(makeRule(RULE_C, 1)));
    const std::string stored = before.flash.getFileContent(RULES_FILE);

    SpiffsRuleDao daoAfterBoot(&before.flash, &before.converter, &before.debugSystem, &before.logger);
    RuleRepository afterBoot(&daoAfterBoot, nullptr, &before.debugSystem, &before.logger);
    before.flash.refuseNextReads(2);
    const int synced = afterBoot.synchronizeRules({makeRule(RULE_D, 1)});
    before.flash.refuseNextReads(0);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(stored.c_str(), before.flash.getFileContent(RULES_FILE).c_str(),
                                     "a boot that could not read the rule file wrote over it");
    TEST_ASSERT_EQUAL_INT(0, synced);
    TEST_ASSERT_EQUAL_INT(1, afterBoot.synchronizeRules({makeRule(RULE_D, 1)}));
    TEST_ASSERT_EQUAL_STRING(ALL_FOUR_RULES.c_str(), before.afterRestart().c_str());
}

/**
 * A full snapshot that no longer names one stored rule arrives at a restart whose reads of the rule
 * file are refused. Merged into the empty cache those reads left, the rule would leave the file
 * without passing DELETING - the pass where the evaluation tick switches off a device the rule had
 * on. The synchronization stores nothing (0) and the file stays; once the file reads, the same
 * snapshot marks the rule DELETING.
 */
static void test_a_full_rule_snapshot_after_refused_reads_drops_no_rule_past_deleting(void)
{
    RulesOnFlash before;
    TEST_ASSERT_TRUE(before.repository.save(makeRule(RULE_A, 1)));
    TEST_ASSERT_TRUE(before.repository.save(makeRule(RULE_B, 1)));
    TEST_ASSERT_TRUE(before.repository.save(makeRule(RULE_C, 1)));
    const std::string stored = before.flash.getFileContent(RULES_FILE);

    SpiffsRuleDao daoAfterBoot(&before.flash, &before.converter, &before.debugSystem, &before.logger);
    RuleRepository afterBoot(&daoAfterBoot, nullptr, &before.debugSystem, &before.logger);
    const std::vector<Rule> snapshot = {makeRule(RULE_A, 1), makeRule(RULE_C, 1)};
    const std::set<std::string> snapshotUids = {RULE_A, RULE_C};

    before.flash.refuseNextReads(2);
    const int synced = afterBoot.synchronizeRules(snapshot, true, snapshotUids);
    before.flash.refuseNextReads(0);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(stored.c_str(), before.flash.getFileContent(RULES_FILE).c_str(),
                                     "a full snapshot was merged into a cache a failed read left");
    TEST_ASSERT_EQUAL_INT(0, synced);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, afterBoot.synchronizeRules(snapshot, true, snapshotUids),
                                  "the premise: the snapshot was taken once the file read");
    SpiffsRuleDao daoAfterSync(&before.flash, &before.converter, &before.debugSystem, &before.logger);
    Rule dropped;
    TEST_ASSERT_TRUE_MESSAGE(daoAfterSync.getRule(RULE_B, dropped),
                             "the rule the snapshot no longer names left the file without DELETING");
    printf("  rules: after the full snapshot the dropped rule is stored as %s\n",
           dropped.state == RuleState::DELETING ? "DELETING" : "something else");
    TEST_ASSERT_TRUE_MESSAGE(dropped.state == RuleState::DELETING,
                             "the rule the snapshot no longer names is not marked DELETING");
}

/**
 * Three rules arrive in one synchronization; reading the store back, it answers that nothing is
 * stored. The repository put rules into it, so that is a read that failed: the next synchronization
 * reads again and keeps every rule.
 */
static void test_a_rule_store_that_answers_nothing_after_a_sync_is_not_written_over(void)
{
    ForgetfulRules store;
    RuleRepository repository(&store);
    TEST_ASSERT_EQUAL_INT(3, repository.synchronizeRules(
                                 {makeRule(RULE_A, 1), makeRule(RULE_B, 1), makeRule(RULE_C, 1)}));

    store.readsAnsweringNothing = 1;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, repository.synchronizeRules({makeRule(RULE_D, 1)}),
                                  "the premise: the synchronization went through");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(ALL_FOUR_RULES.c_str(), uidsOf(store.getStorage()).c_str(),
                                     "a store that answered \"nothing\" was written over with the "
                                     "rule being synchronized");
}

extern "C" void run_registry_safe_write_tests(void)
{
    RUN_TEST(test_a_write_the_flash_cut_short_costs_no_row_of_the_registry);
    RUN_TEST(test_a_restart_after_a_write_the_flash_cut_short_finds_every_row);
    RUN_TEST(test_a_write_from_the_registry_it_holds_does_not_read_the_file);
    RUN_TEST(test_a_write_after_a_refused_reload_keeps_every_row);
    RUN_TEST(test_a_registry_that_does_not_read_back_is_not_written_over);
    RUN_TEST(test_a_restart_that_cannot_read_the_registry_does_not_write_over_it);
    RUN_TEST(test_a_store_that_answers_nothing_after_holding_rows_is_not_written_over);
    RUN_TEST(test_a_registry_the_lookup_calls_missing_is_not_written_over_after_rows_went_in);
    RUN_TEST(test_a_restart_whose_reads_come_back_short_does_not_write_over_the_registry);
    RUN_TEST(test_a_read_that_lost_a_character_inside_a_value_is_not_written_back);
    RUN_TEST(test_a_restart_whose_opens_fail_does_not_write_over_the_registry);
    RUN_TEST(test_a_restart_whose_existence_check_says_missing_does_not_write_over_the_registry);
    RUN_TEST(test_a_registry_that_cannot_be_looked_up_is_not_written_over);
    RUN_TEST(test_the_dao_does_not_write_a_row_over_a_registry_it_could_not_read);
    RUN_TEST(test_removing_a_row_the_registry_does_not_hold_writes_nothing);
    RUN_TEST(test_a_registry_file_cut_short_is_read_as_no_sensors);
    RUN_TEST(test_a_whole_registry_file_that_is_broken_is_replaced_by_the_next_write);
    RUN_TEST(test_a_registry_file_with_no_bytes_is_written_over);
    RUN_TEST(test_a_restart_inside_the_swap_finds_the_registry_under_its_companion);
    RUN_TEST(test_a_restart_after_the_last_row_went_finds_no_rows);
    RUN_TEST(test_a_restart_after_the_last_rule_went_finds_no_rules);
    RUN_TEST(test_a_leftover_backup_is_not_taken_while_the_registry_is_there);
    RUN_TEST(test_a_companion_that_does_not_read_back_is_not_passed_over_for_the_older_one);
    RUN_TEST(test_a_companion_that_cannot_take_the_name_is_still_the_registry);
    RUN_TEST(test_a_backup_is_not_taken_when_the_registry_lookup_is_refused);
    RUN_TEST(test_the_registry_keeps_its_layout_on_flash);
    RUN_TEST(test_a_fresh_module_stores_its_first_row_whole);
    RUN_TEST(test_a_rule_file_write_the_flash_cut_short_costs_no_rule);
    RUN_TEST(test_a_rule_push_after_a_refused_reload_keeps_every_rule);
    RUN_TEST(test_a_restart_that_cannot_read_the_rule_file_does_not_write_over_it);
    RUN_TEST(test_a_full_rule_snapshot_after_refused_reads_drops_no_rule_past_deleting);
    RUN_TEST(test_a_rule_store_that_answers_nothing_after_a_sync_is_not_written_over);
}
