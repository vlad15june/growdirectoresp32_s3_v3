// What the three stored-data DAOs ask of the file system, and when.
//
// They are globals of the dependency graph, so their constructors run before app_main: before
// SPIFFS.begin(), before EEPROM.begin() and before the serial port is up. A file system call made
// there reaches a VFS with nothing mounted under it - it fails, and the VFS prints the failure
// itself. The DAOs do not need it: every entry point below them initializes on first use.
//
// The stand-in here counts what was asked of storage instead of answering differently, so the
// claims are about the calls the DAOs make and not about what came back.

#include <unity.h>

#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorTypeDao.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

/**
 * @brief Every way into the file system, counted; the mock underneath still serves the call.
 *
 * `paths` is what the calls named, in order, so a test can say which file was touched as well as
 * how many times storage was entered at all.
 */
class CountingSpiffsAdapter : public MockSpiffsAdapter
{
public:
    size_t calls = 0;
    size_t directoryCalls = 0;
    std::vector<std::string> paths;

    void initSPIFFS() override
    {
        note("initSPIFFS");
        MockSpiffsAdapter::initSPIFFS();
    }

    std::string readSPIFFSstr(const char* filePath) override
    {
        note(filePath);
        return MockSpiffsAdapter::readSPIFFSstr(filePath);
    }

    void removeFile(const char* filePath) override
    {
        note(filePath);
        MockSpiffsAdapter::removeFile(filePath);
    }

    bool rewriteToFile(const char* filePath, const char* body) override
    {
        note(filePath);
        return MockSpiffsAdapter::rewriteToFile(filePath, body);
    }

    bool appendToFile(const char* filePath, const char* body) override
    {
        note(filePath);
        return MockSpiffsAdapter::appendToFile(filePath, body);
    }

    std::unique_ptr<ISpiffsFileWriter> openForRewrite(const char* filePath) override
    {
        note(filePath);
        return MockSpiffsAdapter::openForRewrite(filePath);
    }

    unsigned int getFileSize(const char* filePath) override
    {
        note(filePath);
        return MockSpiffsAdapter::getFileSize(filePath);
    }

    bool fileExists(const char* filePath) override
    {
        note(filePath);
        return MockSpiffsAdapter::fileExists(filePath);
    }

    bool deleteFile(const char* filePath) override
    {
        note(filePath);
        return MockSpiffsAdapter::deleteFile(filePath);
    }

    bool renameFile(const char* fromPath, const char* toPath) override
    {
        note(fromPath);
        return MockSpiffsAdapter::renameFile(fromPath, toPath);
    }

    bool statFile(const char* filePath, size_t& size, bool& missing) override
    {
        note(filePath);
        return MockSpiffsAdapter::statFile(filePath, size, missing);
    }

    FileSystemStatusEnum getFileSystemStatus() override
    {
        note("getFileSystemStatus");
        return MockSpiffsAdapter::getFileSystemStatus();
    }

    bool directoryExists(const char* path) override
    {
        note(path);
        directoryCalls++;
        return MockSpiffsAdapter::directoryExists(path);
    }

    bool createDirectory(const char* path) override
    {
        note(path);
        directoryCalls++;
        return MockSpiffsAdapter::createDirectory(path);
    }

    /** @brief Every call this stand-in took, one per row - what a failed assertion shows. */
    std::string quoted() const
    {
        std::string all;
        for (size_t i = 0; i < paths.size(); i++)
        {
            all += "\n      | " + paths[i];
        }
        return all.empty() ? std::string(" (none)") : all;
    }

    bool tookAPathSaying(const char* needle) const
    {
        for (size_t i = 0; i < paths.size(); i++)
        {
            if (paths[i] == needle)
            {
                return true;
            }
        }
        return false;
    }

private:
    void note(const char* path)
    {
        calls++;
        paths.push_back(path != nullptr ? path : "(null)");
    }
};

/** The three DAOs of the dependency graph, on one file system. */
struct DaoFixture
{
    CountingSpiffsAdapter spiffs;
    ArduinoJsonConverter jsonConverter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsRuleDao ruleDao;
    SpiffsSensorDao sensorDao;
    SpiffsSensorTypeDao sensorTypeDao;

    DaoFixture()
        : ruleDao(&spiffs, &jsonConverter, &debugSystem, &logger),
          sensorDao(&spiffs, &jsonConverter, &debugSystem, &logger),
          sensorTypeDao(&spiffs, &jsonConverter, &debugSystem, &logger)
    {
    }

    /** One read through each DAO: the first use, which is what initializes the file path. */
    void readEachOnce()
    {
        std::vector<Rule> rules;
        ruleDao.getRules(rules);
        std::vector<SensorEntity> sensors;
        sensorDao.getSensors(sensors);
        std::vector<SensorType> sensorTypes;
        sensorTypeDao.getAll(sensorTypes);
    }
};

} // namespace

/**
 * Constructing the three DAOs asks storage for nothing at all. On the device this is the
 * difference between a boot that works the file system before it is mounted and one that does not.
 */
void test_spiffs_dao_construction_asks_the_file_system_for_nothing(void)
{
    DaoFixture f;

    std::string message = "a DAO constructor went to the file system:" + f.spiffs.quoted();
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, f.spiffs.calls, message.c_str());
}

/**
 * And the file each DAO then reads is the one it always read: the lazy path is the same path, so
 * a module that was storing its data under these names goes on finding it there.
 */
void test_spiffs_dao_first_use_reads_the_stored_file_it_always_read(void)
{
    DaoFixture f;

    f.readEachOnce();

    std::string message = "the DAOs went to:" + f.spiffs.quoted();
    TEST_ASSERT_TRUE_MESSAGE(f.spiffs.tookAPathSaying("/data/rules.json"), message.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.spiffs.tookAPathSaying("/data/sensors.json"), message.c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.spiffs.tookAPathSaying("/data/sensorTypes.json"), message.c_str());
}

/**
 * Nothing asks this file system about a directory, on any call. SPIFFS is flat: it has no
 * directories to ask about, mkdir() on it fails with ENOTSUP whenever it is called, and "/data/"
 * is part of the name of a file.
 */
void test_spiffs_dao_never_asks_the_file_system_about_a_directory(void)
{
    DaoFixture f;

    f.readEachOnce();

    Rule rule;
    rule.uid = "rule-1";
    f.ruleDao.saveRule(rule);

    SensorEntity sensor;
    sensor.uid = "sensor-1";
    f.sensorDao.saveSensor(sensor);

    std::vector<SensorType> oneType(1);
    oneType[0].uid = "type-1";
    oneType[0].name = "type";
    f.sensorTypeDao.saveAll(oneType);

    f.ruleDao.clear();
    f.sensorDao.clear();
    f.sensorTypeDao.clear();

    std::string message = "a DAO asked about a directory; the calls were:" + f.spiffs.quoted();
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, f.spiffs.directoryCalls, message.c_str());
}

extern "C" void run_spiffs_dao_construction_tests(void)
{
    printf("\n----- Running SPIFFS DAO construction Tests -----\n");

    RUN_TEST(test_spiffs_dao_construction_asks_the_file_system_for_nothing);
    RUN_TEST(test_spiffs_dao_first_use_reads_the_stored_file_it_always_read);
    RUN_TEST(test_spiffs_dao_never_asks_the_file_system_about_a_directory);

    printf("\n----- SPIFFS DAO construction Tests Complete -----\n");
}
