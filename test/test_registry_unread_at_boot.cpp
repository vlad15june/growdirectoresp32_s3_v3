// A registry or rule file that did not read back is read again a sweep later - not left empty until
// the next write - and nothing that decides on the registry decides on the empty answer meanwhile.
//
// When a read of /data/sensors.json or /data/rules.json does not bring back what is stored (the open
// failed, the read lost bytes: SpiffsDao::readAll() answers Unreadable), the repository holds
// nothing, and every reader gets nothing - the sweep polls no sensor, the evaluation tick evaluates
// no rule. A write is never built on that (the store is read again first); the readers are given it
// until storage is read again, and that cannot wait for a write: a quiet module may write nothing
// for hours, an offline one never.
//
// What this file pins:
//
//   1. a boot whose first read of the registry was refused polls the stored sensors one sweep later
//      (SensorAcquisitionService::SWEEP_INTERVAL_MS), with nothing written in between;
//   2. for as long as storage does not serve the read, the file is read once a sweep, nothing is
//      written over it, and the tape says so at every read and once when it answers; a module with
//      nothing stored does not read again at all;
//   3. the rule file the same way. After a restart the relay is off - setup drives it off - and its
//      rule switches it on a sweep after a refused read of the rule file. A relay a rule had on is
//      switched off by its rule a sweep after a refused reload, instead of staying on until the next
//      write. For as long as the rule file does not read at all, the relay keeps the state its rule
//      left it in; that is pinned here, so that changing it is a decision of its own;
//   4. what decides on the registry does not decide on the empty answer: a structure described from
//      it is sent again once the registry reads back, and the discovery begins no pass and adopts no
//      sensor while it does not.
//
// What runs is production code: SensorRepository and RuleRepository over SpiffsSensorDao and
// SpiffsRuleDao and the converter the device runs; SensorService, EvaluateRulesUseCase and
// ProcessDeviceStateUseCase; ModuleSetupService for the boot, StartupSensorScanUseCase,
// RepublishModuleStructureUseCase and CommandService's loop. Replaced are the flash
// (Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h - a read it refuses comes back empty, as the device
// adapter answers a file it could not open, while the index lookup still answers), the clock, the
// line, the acquisition task and the server.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "Application/Services/CommandService.h"
#include "Application/Services/ModuleSetupService.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Entities/Rule.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/Enums/DeviceStateEnum.h"
#include "Core/Domain/Enums/DeviceTypeEnum.h"
#include "Core/Domain/Enums/RuleEnums.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsRuleDao.h"
#include "Infrastructure/Adapters/SPIFFS/SpiffsSensorDao.h"
#include "Infrastructure/Repositories/RuleRepository.h"
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
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

/** @brief Reaches the two steps of the boot this file needs, which are private to the setup service. */
class RegistryUnreadTestHelper {
public:
    static void fillStructure(ModuleSetupService& service) { service.fillModuleStructure(); }
    static bool setUpTheRelays(ModuleSetupService& service) { return service.setupRelayOutputs(); }
};

namespace {

const char* const REGISTRY_FILE = "/data/sensors.json";
const char* const RULES_FILE = "/data/rules.json";

const char* const MODULE_UID = "a1b2c3d4e5f6";

/** What the module may take to come back to the stored registry: one sweep of the sensors. */
const unsigned long ONE_SWEEP_MS = SensorAcquisitionService::SWEEP_INTERVAL_MS;
/** The step these stands move the clock by: one evaluation tick of the rules. */
const unsigned long TICK_MS = 1000;
/** How long a stand is watched for what it does after a refused read. */
const unsigned long WATCHED_MS = 120000;

/**
 * @brief The flash, refusing the reads of one file: the next few, or every one for a stretch of time.
 *
 * Counts every read and every index lookup of that file. A read refused here comes back empty, the
 * way SpiffsAdapter::readSPIFFSstr() answers a file it could not open; the lookup (statFile(),
 * stat() on the device) does not open the file and still answers.
 */
class FlakyFlash : public MockSpiffsAdapter {
public:
    IArduinoAdapter* clock = nullptr;
    std::string watchedFile;
    size_t readsToRefuse = 0;
    unsigned long refusedUntilMs = 0;
    size_t reads = 0;
    size_t refusedReads = 0;
    unsigned long lastRefusedAtMs = 0;
    size_t lookups = 0;

    void watch(const char* path) { watchedFile = path; }

    /** @brief Refuse the next `count` reads of the watched file. */
    void refuseNext(size_t count) { readsToRefuse = count; }

    /** @brief Refuse every read of the watched file made before this moment. */
    void refuseReadsUntil(unsigned long untilMs) { refusedUntilMs = untilMs; }

    std::string readSPIFFSstr(const char* path) override
    {
        if (watchedFile == path)
        {
            reads++;
            const bool refused = readsToRefuse > 0 || (clock != nullptr && clock->millis() < refusedUntilMs);
            if (refused)
            {
                if (readsToRefuse > 0)
                {
                    readsToRefuse--;
                }
                refusedReads++;
                lastRefusedAtMs = clock != nullptr ? clock->millis() : 0;
                return "";
            }
        }
        return MockSpiffsAdapter::readSPIFFSstr(path);
    }

    bool statFile(const char* path, size_t& size, bool& missing) override
    {
        if (watchedFile == path)
        {
            lookups++;
        }
        return MockSpiffsAdapter::statFile(path, size, missing);
    }
};

/** @brief A ModBus row of the registry on `pin`, at the address of its port. */
SensorEntity makeRow(int pin, const char* model = "ry_ws301", SensorStatusEnum status = SENSOR_STATUS_ADDED)
{
    SensorEntity row;
    char uid[32];
    snprintf(uid, sizeof(uid), "%s.%02d", MODULE_UID, pin);
    row.uid = uid;
    row.pin = pin;
    row.protocol = "ModBus";
    row.model = model;
    row.sensorType = "ry_ws301";
    row.sensorTypeUid = "4719a986-a300-48d5-bd1f-e34a1329798a";
    row.createdAt = "2026-09-01T10:00:00.000000+00:00";
    row.isActive = true;
    row.isError = false;
    row.status = status;
    row.modbusDeviceAddress = MODBUS_ADDRESS_MIN + pin;
    row.baudRate = 9600;
    for (int channel = 1; channel <= 2; channel++)
    {
        ModbusRegisterConfig config;
        config.functionCode = 3;
        config.startRegister = (uint16_t)(channel - 1);
        config.numRegisters = 1;
        config.multiplier = 0.01f;
        config.offset = 0.0f;
        config.channelName = channel == 1 ? "air_temp" : "humidity";
        config.channel = (uint8_t)channel;
        config.sensorType = config.channelName;
        config.canConnectMinValue = 0.0f;
        config.canConnectMaxValue = 100.0f;
        row.modbusRegisterConfigs.push_back(config);
    }
    return row;
}

/** @brief The uids of the rows, sorted: the registry hands its views out in no particular order. */
std::string uidsOf(const std::vector<SensorEntity>& rows)
{
    std::vector<std::string> uids;
    for (size_t i = 0; i < rows.size(); i++)
    {
        uids.push_back(rows[i].uid);
    }
    std::sort(uids.begin(), uids.end());
    std::string text;
    for (size_t i = 0; i < uids.size(); i++)
    {
        text += (i == 0 ? "" : ",") + uids[i];
    }
    return text.empty() ? std::string("(none)") : text;
}

size_t linesContaining(const MockLogger& logger, const std::string& needle)
{
    size_t count = 0;
    for (size_t i = 0; i < logger.messages.size(); i++)
    {
        if (logger.messages[i].find(needle) != std::string::npos)
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief The acquisition task, seen from the control loop: what it was last told to poll, and when
 *        it was first told to poll anything.
 */
class PollRecorder : public ISensorAcquisition {
public:
    IArduinoAdapter* clock = nullptr;
    std::vector<SensorEntity> polled;
    bool everPolled = false;
    unsigned long firstPolledAtMs = 0;

    void setSensors(const std::vector<SensorEntity>& modbusSensors) override
    {
        polled = modbusSensors;
        if (!modbusSensors.empty() && !everPolled)
        {
            everPolled = true;
            firstPolledAtMs = clock->millis();
        }
    }
    bool ensureRunning() override { return true; }
    bool drain(std::vector<AcquiredReading>&) override { return false; }
    uint32_t submitBusCommand(const std::string&, const std::string&) override { return kBusCommandRejected; }
    uint32_t submitComplexBusCommand(const ComplexCommandWork&) override { return kBusCommandRejected; }
    uint32_t submitStartupScanStep(const StartupScanStep&) override { return kBusCommandRejected; }
    bool drainBusCommandResults(std::vector<BusCommandCompletion>&) override { return false; }
};

/**
 * @brief The sweep of a module that has just restarted on a flash holding its registry, wired the way
 *        globals.h wires it.
 */
struct SweepAfterRestart {
    MockArduinoAdapter arduino;
    FlakyFlash flash;
    ArduinoJsonConverter converter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsSensorDao dao;
    SensorRepository repository;

    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockNTPTimeAdapter timeAdapter;
    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;
    MockSensorReader sensorReader;
    MockHybridAPIService server;
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    PollRecorder acquisition;
    SensorService sensorService;

    unsigned long now = 0;

    SweepAfterRestart()
        : dao(&flash, &converter, &debugSystem, &logger),
          repository(&dao, &debugSystem, &logger, &arduino),
          moduleConfig(&logger, &debugSystem, &settings),
          virtualChannels(&vpdChannel),
          sensorService(&moduleConfig, &moduleStructure, &repository, &sensorReader, &sensorValueModel,
                        &timeAdapter, &arduino, &server, &logger, &debugSystem, nullptr, nullptr, nullptr,
                        &virtualChannels)
    {
        flash.clock = &arduino;
        flash.watch(REGISTRY_FILE);
        acquisition.clock = &arduino;
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setSensorPinCount(4);
        settings.setUser("t104@example.com");
        settings.setAccountUid("t104-account");
        timeAdapter.setEpochTime(1757066400);
        sensorService.setAcquisition(&acquisition);
        arduino.setMillis(0);
    }

    /** @brief What the module wrote before the restart. */
    void storedBeforeTheRestart(const std::vector<SensorEntity>& rows)
    {
        TEST_ASSERT_TRUE(flash.writeToFile(REGISTRY_FILE, converter.sensorsToJson(rows).c_str()));
    }

    /** @brief The control loop's sweep, once a tick, up to and including this moment. */
    void runUntil(unsigned long untilMs)
    {
        while (now <= untilMs)
        {
            arduino.setMillis(now);
            sensorService.loop();
            now += TICK_MS;
        }
    }
};

}  // namespace

// --------------------------------------------------------------------------------------------
// 1-2. The registry.
// --------------------------------------------------------------------------------------------

/**
 * The module restarts on a flash holding two sensors, and the boot's read of the registry is
 * refused once. One sweep later the acquisition task is polling both - with nothing written in
 * between, the one thing that read the registry again before.
 */
static void test_a_boot_whose_registry_read_was_refused_polls_the_stored_sensors_a_sweep_later(void)
{
    SweepAfterRestart module;
    module.storedBeforeTheRestart({makeRow(0), makeRow(1)});
    module.flash.refuseNext(1);

    module.runUntil(0);
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.flash.refusedReads, "the premise: the boot's read was refused");
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.polled.empty(),
                             "the premise: the refused read left the sweep nothing to poll");

    module.runUntil(WATCHED_MS);

    char message[200];
    if (module.acquisition.everPolled)
    {
        snprintf(message, sizeof(message),
                 "a boot whose registry read was refused at %lu ms polled %s from %lu ms",
                 module.flash.lastRefusedAtMs, uidsOf(module.acquisition.polled).c_str(),
                 module.acquisition.firstPolledAtMs);
    }
    else
    {
        snprintf(message, sizeof(message),
                 "a boot whose registry read was refused polled no stored sensor in %lu ms", WATCHED_MS);
    }
    printf("  t104: %s\n", message);
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.everPolled, message);
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.firstPolledAtMs - module.flash.lastRefusedAtMs <= ONE_SWEEP_MS,
                             message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01",
                                     uidsOf(module.acquisition.polled).c_str(), message);
}

/**
 * Storage cannot serve the read of the registry for 100 s after the restart. The file is read once a
 * sweep while that lasts - not on every iteration, and not never - and the tape says so at each
 * read. A write of a row made in the middle of it is refused and the file keeps its bytes. Once
 * storage answers, the registry is back within a sweep, the tape says after how many reads, and the
 * file is not read again.
 */
static void test_a_registry_that_does_not_read_for_a_while_is_read_once_a_sweep_and_kept(void)
{
    const unsigned long OUTAGE_MS = 100000;
    SweepAfterRestart module;
    module.storedBeforeTheRestart({makeRow(0), makeRow(1)});
    const std::string stored = module.flash.getFileContent(REGISTRY_FILE);
    module.flash.refuseReadsUntil(OUTAGE_MS);

    module.runUntil(90000);
    const size_t readsWhileRefused = module.flash.reads;
    char message[200];
    snprintf(message, sizeof(message),
             "the registry was read %u time(s) in the first 90 s of storage refusing it",
             (unsigned)readsWhileRefused);
    printf("  t104: %s\n", message);
    // At 0, 15, 30, 45, 60, 75 and 90 s.
    TEST_ASSERT_EQUAL_MESSAGE(7, (int)readsWhileRefused, message);
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.polled.empty(), "a sensor was polled out of a refused read");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)linesContaining(module.logger, "did not come back whole (1 in a row)"),
                              "the first refused read is not on the tape");
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)linesContaining(module.logger, "did not come back whole (7 in a row)"),
                              "the seventh refused read is not on the tape");

    module.runUntil(95000);
    TEST_ASSERT_FALSE_MESSAGE(module.repository.save(makeRow(2)),
                              "a row was written over a registry that did not read back");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(stored.c_str(), module.flash.getFileContent(REGISTRY_FILE).c_str(),
                                     "the registry on flash changed while it did not read back");

    module.runUntil(OUTAGE_MS + ONE_SWEEP_MS);
    snprintf(message, sizeof(message), "after the outage the sweep polls %s",
             uidsOf(module.acquisition.polled).c_str());
    printf("  t104: %s\n", message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a1b2c3d4e5f6.00,a1b2c3d4e5f6.01",
                                     uidsOf(module.acquisition.polled).c_str(), message);
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)linesContaining(module.logger, "came back after"),
                              "the tape does not say when the registry came back");

    const size_t readsWhenBack = module.flash.reads;
    module.runUntil(OUTAGE_MS + 10 * ONE_SWEEP_MS);
    TEST_ASSERT_EQUAL_MESSAGE((int)readsWhenBack, (int)module.flash.reads,
                              "a registry that reads back was read again with nothing written");
    TEST_ASSERT_EQUAL_STRING(stored.c_str(), module.flash.getFileContent(REGISTRY_FILE).c_str());
}

/**
 * A fresh module: no registry on flash. Nothing is stored, which is not a read that failed - the file
 * is looked up once and never read, then or later, and the tape says nothing of a failed read.
 */
static void test_a_fresh_module_does_not_read_its_missing_registry_again(void)
{
    SweepAfterRestart module;

    module.runUntil(10 * ONE_SWEEP_MS);

    printf("  t104: a fresh module looked its registry up %u time(s) and read it %u time(s) in %lu ms\n",
           (unsigned)module.flash.lookups, (unsigned)module.flash.reads, 10 * ONE_SWEEP_MS);
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.flash.lookups, "a missing registry was looked up again");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)module.flash.reads, "a missing registry was read");
    TEST_ASSERT_TRUE(module.acquisition.polled.empty());
    TEST_ASSERT_EQUAL(0, (int)linesContaining(module.logger, "did not come back whole"));
}

// --------------------------------------------------------------------------------------------
// 3. The rule file and the relay.
// --------------------------------------------------------------------------------------------

namespace {

const char* const RULE_UID = "3f6b1c2d-8e04-4a51-9b7c-2d5e8f0a1b34";
const char* const DEVICE_UID = "a1b2c3d4e5f6.relay.1";
const char* const SENSOR_UID = "a1b2c3d4e5f6.00.01";
/** MockModuleConfig::getRelayPins() -> {1, 2, 3, 4}; the relay is channel 0. */
const uint8_t RELAY_PIN = 1;

/** Above valueHigh the rule (direction DOWN) wants the relay on; below valueLow, off. */
const float VALUE_WANTS_ON = 25.0f;
const float VALUE_WANTS_OFF = 5.0f;

Rule makeSensorRule()
{
    Rule rule;
    rule.uid = RULE_UID;
    rule.version = 1;
    rule.ruleType = RuleType::SENSOR;
    rule.deviceUid = DEVICE_UID;
    rule.sensorUid = SENSOR_UID;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    rule.direction = RuleDirection::DOWN;
    rule.valueLow = 10;
    rule.valueHigh = 20;
    return rule;
}

class OneRelay : public IDeviceModel {
public:
    Device* device = nullptr;

    bool init() override { return true; }
    Device* getDeviceByUid(const char* uid) override
    {
        if (device && device->deviceUid && uid && std::string(device->deviceUid) == uid)
        {
            return device;
        }
        return nullptr;
    }
    std::vector<Device*> getAllDevices() override
    {
        std::vector<Device*> devices;
        if (device)
        {
            devices.push_back(device);
        }
        return devices;
    }
    int getDeviceCount() const override { return device ? 1 : 0; }
    bool addDevice(Device*) override { return true; }
    bool removeDevice(const char*) override { return true; }
    void clearDevices() override {}
};

/**
 * @brief What outlives a restart: the flash, the relay's pin and the clock behind it, and the sensor
 *        the rule watches.
 */
struct Site {
    MockArduinoAdapter arduino;
    FlakyFlash flash;
    MockSensorValueModel sensors;

    Site()
    {
        flash.clock = &arduino;
        flash.watch(RULES_FILE);
        arduino.setMillis(0);
    }

    bool relayOn() { return arduino.digitalOutputs[RELAY_PIN] == arduino.getDigitalHigh(); }
};

/**
 * @brief One life of a relay module between two restarts: the rules over the rule file, the
 *        evaluation tick and the relay outputs, wired the way globals.h wires them; and the setup
 *        that drives the relays at the boot.
 */
struct RelayModule {
    Site& site;
    MockLogger logger;
    MockDebugSystem debugSystem;
    ArduinoJsonConverter converter;
    MockNTPTimeAdapter ntp;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    SpiffsRuleDao dao;
    RuleRepository repository;
    DeviceDigitalOut relay;
    OneRelay deviceModel;
    EvaluateRulesUseCase rules;
    ProcessDeviceStateUseCase outputs;
    ModuleSetupService setup;

    unsigned long now = 0;
    unsigned long relayOnAtMs = 0;
    unsigned long relayOffAtMs = 0;

    explicit RelayModule(Site& site)
        : site(site),
          moduleConfig(&logger, &debugSystem, &settings),
          dao(&site.flash, &converter, &debugSystem, &logger),
          repository(&dao, &ntp, &debugSystem, &logger, &site.arduino),
          relay(),
          rules(&deviceModel, &repository, &site.arduino, &ntp, &site.sensors),
          outputs(&logger, &debugSystem, &moduleConfig, &site.arduino, &deviceModel),
          setup(&logger, &debugSystem, &moduleConfig, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr, &site.arduino)
    {
        debugSystem.featureEnabled = false;
        ntp.setEpochTime(1757066400);
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleType(MODULE_TYPE_DEVICE);
        moduleConfig.setModuleSubType(MODULE_SUBTYPE_RELAY);
        moduleConfig.setDeviceCount(1);
        relay.deviceUid = DEVICE_UID;
        relay.deviceType = DEVICE_TYPE_DIGITAL;
        relay.state = DEVICE_STATE_OFF;
        relay.ruleStopReason = nullptr;
        deviceModel.device = &relay;
        now = site.arduino.millis();
    }

    /** @brief What setup does to the relays, before the loop starts. */
    void boot() { TEST_ASSERT_TRUE(RegistryUnreadTestHelper::setUpTheRelays(setup)); }

    /** @brief The rule, as the server's list stores it. */
    void synchronizeTheRule()
    {
        TEST_ASSERT_EQUAL_MESSAGE(1, repository.synchronizeRules({makeSensorRule()}),
                                  "the premise: the rule was stored");
    }

    void sensorReads(float value) { site.sensors.values[SENSOR_UID] = value; }

    /**
     * @brief The loop, a tick at a time up to and including this moment: the sensor is read on every
     *        tick, the rules are evaluated, and the outputs drive the relay.
     */
    void runUntil(unsigned long untilMs)
    {
        while (now <= untilMs)
        {
            site.arduino.setMillis(now);
            site.sensors.lastReadTimes[SENSOR_UID] = now;
            const bool wasOn = site.relayOn();
            rules.loop();
            outputs.loop();
            if (!wasOn && site.relayOn() && relayOnAtMs == 0)
            {
                relayOnAtMs = now;
            }
            if (wasOn && !site.relayOn() && relayOffAtMs == 0)
            {
                relayOffAtMs = now;
            }
            now += TICK_MS;
        }
    }
};

}  // namespace

/**
 * A relay module whose rule had its relay on restarts, and the boot's read of the rule file is
 * refused once. Setup leaves the relay off - it does not stay on across the restart - and the rule
 * switches it back on a sweep later, with nothing written in between.
 */
static void test_a_restart_whose_rule_file_read_was_refused_runs_its_rule_a_sweep_later(void)
{
    Site site;
    {
        RelayModule before(site);
        before.boot();
        before.synchronizeTheRule();
        before.sensorReads(VALUE_WANTS_ON);
        before.runUntil(10000);
        TEST_ASSERT_TRUE_MESSAGE(site.relayOn(), "the premise: the rule switched the relay on");
    }

    // The restart: the uptime starts again, the pin and the flash are what they were.
    site.arduino.setMillis(0);
    site.flash.refuseNext(1);
    RelayModule after(site);
    after.boot();
    TEST_ASSERT_FALSE_MESSAGE(site.relayOn(), "the relay stayed on across the restart");

    after.runUntil(WATCHED_MS);

    char message[200];
    if (after.relayOnAtMs != 0)
    {
        snprintf(message, sizeof(message),
                 "after a restart whose rule file read was refused at %lu ms the rule switched the relay on at "
                 "%lu ms",
                 site.flash.lastRefusedAtMs, after.relayOnAtMs);
    }
    else
    {
        snprintf(message, sizeof(message),
                 "after a restart whose rule file read was refused the rule did not switch the relay in %lu ms",
                 WATCHED_MS);
    }
    printf("  t104: %s\n", message);
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)site.flash.refusedReads, "the premise: the boot's read was refused");
    TEST_ASSERT_TRUE_MESSAGE(after.relayOnAtMs != 0, message);
    TEST_ASSERT_TRUE_MESSAGE(after.relayOnAtMs - site.flash.lastRefusedAtMs <= ONE_SWEEP_MS + TICK_MS, message);
}

/**
 * The relay is on by its rule. The rule list is stored again - what the five-minute synchronization
 * does - and the read that loads the rules back is refused once. The sensor then falls to where the
 * rule wants the relay off. The rule is not evaluated while its file has not read back, and the
 * relay holds; a sweep after the refused read the rule is back, and switches it off.
 */
static void test_a_relay_whose_rule_file_reload_was_refused_is_switched_off_by_its_rule_a_sweep_later(void)
{
    Site site;
    RelayModule module(site);
    module.boot();
    module.synchronizeTheRule();
    module.sensorReads(VALUE_WANTS_ON);
    module.runUntil(10000);
    TEST_ASSERT_TRUE_MESSAGE(module.relayOnAtMs != 0, "the premise: the rule switched the relay on");

    module.synchronizeTheRule();
    site.flash.refuseNext(1);
    module.runUntil(module.now);
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)site.flash.refusedReads, "the premise: the reload was refused");
    const unsigned long refusedAtMs = site.flash.lastRefusedAtMs;

    module.sensorReads(VALUE_WANTS_OFF);
    module.runUntil(refusedAtMs + 5000);
    TEST_ASSERT_TRUE_MESSAGE(site.relayOn(),
                             "the premise: the rule was evaluated although its file did not read back");

    module.runUntil(refusedAtMs + WATCHED_MS);

    char message[200];
    if (module.relayOffAtMs != 0)
    {
        snprintf(message, sizeof(message),
                 "a relay whose rule file reload was refused was switched off %lu ms after the refusal",
                 module.relayOffAtMs - refusedAtMs);
    }
    else
    {
        snprintf(message, sizeof(message),
                 "a relay whose rule file reload was refused stayed on %lu ms after its rule wanted it off",
                 WATCHED_MS);
    }
    printf("  t104: %s\n", message);
    TEST_ASSERT_TRUE_MESSAGE(module.relayOffAtMs != 0, message);
    TEST_ASSERT_TRUE_MESSAGE(module.relayOffAtMs - refusedAtMs <= ONE_SWEEP_MS + TICK_MS, message);
}

/**
 * The same relay, and the rule file does not read at all for 100 s. For that long the module does not
 * know the rule, and the relay keeps the state the rule left it in - the module's behaviour today.
 * Once storage answers, the rule is back within a sweep and switches the relay off.
 */
static void test_a_relay_holds_while_its_rule_file_does_not_read_and_follows_its_rule_a_sweep_after(void)
{
    const unsigned long OUTAGE_MS = 100000;
    Site site;
    RelayModule module(site);
    module.boot();
    module.synchronizeTheRule();
    module.sensorReads(VALUE_WANTS_ON);
    module.runUntil(10000);
    TEST_ASSERT_TRUE_MESSAGE(site.relayOn(), "the premise: the rule switched the relay on");

    module.synchronizeTheRule();
    const unsigned long outageFromMs = module.now;
    site.flash.refuseReadsUntil(outageFromMs + OUTAGE_MS);
    module.sensorReads(VALUE_WANTS_OFF);
    module.runUntil(outageFromMs + OUTAGE_MS - TICK_MS);

    char message[200];
    snprintf(message, sizeof(message),
             "while the rule file did not read for %lu ms the relay was %s, the file was read %u time(s)",
             OUTAGE_MS, site.relayOn() ? "held on" : "switched off", (unsigned)site.flash.refusedReads);
    printf("  t104: %s\n", message);
    TEST_ASSERT_TRUE_MESSAGE(site.relayOn(), message);

    module.runUntil(outageFromMs + OUTAGE_MS + WATCHED_MS);
    snprintf(message, sizeof(message),
             "once the rule file read again the relay was %s %lu ms after it did",
             module.relayOffAtMs != 0 ? "switched off" : "still on",
             module.relayOffAtMs != 0 ? module.relayOffAtMs - (outageFromMs + OUTAGE_MS) : WATCHED_MS);
    printf("  t104: %s\n", message);
    TEST_ASSERT_TRUE_MESSAGE(module.relayOffAtMs != 0, message);
    TEST_ASSERT_TRUE_MESSAGE(module.relayOffAtMs - (outageFromMs + OUTAGE_MS) <= ONE_SWEEP_MS + TICK_MS,
                             message);
}

// --------------------------------------------------------------------------------------------
// 4. What decides on the registry at boot: the structure and the discovery.
// --------------------------------------------------------------------------------------------

namespace {

/** @brief The far end of the module's calls, keeping by value every structure it took. */
class RecordingServer : public MockHybridAPIService {
public:
    std::vector<ModuleStructure> structures;
    std::vector<unsigned long> takenAtMs;
    IArduinoAdapter* clock = nullptr;

    bool sendToServerModuleStructure(const ModuleStructure& moduleStructure,
                                     ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        const bool accepted = MockHybridAPIService::sendToServerModuleStructure(moduleStructure, priority);
        if (accepted)
        {
            structures.push_back(moduleStructure);
            takenAtMs.push_back(clock != nullptr ? clock->millis() : 0);
        }
        return accepted;
    }
};

bool describes(const ModuleStructure& structure, const std::string& uidPrefix)
{
    if (structure.nodes.empty())
    {
        return false;
    }
    for (size_t i = 0; i < structure.nodes[0].sensors.size(); i++)
    {
        if (structure.nodes[0].sensors[i].uid.compare(0, uidPrefix.size(), uidPrefix) == 0 &&
            structure.nodes[0].sensors[i].type != "none")
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief The line, with one device on it, on `devicePin`, answering at its port's address.
 *
 * MockSensorReader answers by address alone; a port with nothing on it is silent here whatever it
 * is asked, so the discovery finds nothing but what is on that port.
 */
class OneDeviceOnTheLine : public MockSensorReader {
public:
    int devicePin = 0;

    bool scanSensor(const SensorEntity& sensor) override
    {
        if (sensor.pin != devicePin)
        {
            scanSensorCallCount++;
            return false;
        }
        return MockSensorReader::scanSensor(sensor);
    }
};

/** @brief A scannable ModBus model of the catalog, the one the discovery tries on a free port. */
SensorType makeScannableType()
{
    SensorType type;
    type.uid = "type-1";
    type.name = "TEMP";
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = true;
    type.isSupported = true;
    type.baudRate = 9600;
    type.modbusDeviceAddress = 1;
    type.modbusPingAddress = 0x0000;
    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = 0x0000;
    config.numRegisters = 1;
    config.channel = 1;
    config.channelName = "value";
    config.sensorType = "temperature";
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    type.modbusRegisterConfig.push_back(config);
    return type;
}

/**
 * @brief A sensor module that restarts on a flash holding its registry: the boot that builds the
 *        structure and arms the discovery, the sweep, the discovery, the republish and the control
 *        loop that drives them - wired the way globals.h wires them.
 */
struct BootAfterRestart {
    MockArduinoAdapter arduino;
    FlakyFlash flash;
    ArduinoJsonConverter converter;
    MockLogger logger;
    MockDebugSystem debugSystem;
    SpiffsSensorDao dao;
    SensorRepository sensorRepository;

    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockWifiAdapter wifiAdapter;
    MockMqttManager mqttManager;
    MockNTPTimeAdapter timeAdapter;
    RecordingServer server;
    OneDeviceOnTheLine sensorReader;
    MockSensorTypeRepository sensorTypeRepository;
    SensorValueModel sensorValueModel;
    ModuleStructure moduleStructure;

    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    SensorStructureUseCase sensorStructure;
    ModbusAddressAllocator modbusAddressAllocator;
    StartupSensorScanUseCase startupScan;
    RepublishModuleStructureUseCase republish;
    SensorService sensorService;
    CommandService commandService;
    ModuleSetupService setupService;

    unsigned long now = 0;

    BootAfterRestart()
        : dao(&flash, &converter, &debugSystem, &logger),
          sensorRepository(&dao, &debugSystem, &logger, &arduino),
          moduleConfig(&logger, &debugSystem, &settings),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels),
          modbusAddressAllocator(&sensorRepository),
          // No clock for the discovery: it holds no window after the boot, and a step it can take
          // is taken at once - what it decides is all there is to see.
          startupScan(&logger, &debugSystem, &moduleConfig, &sensorRepository, &sensorTypeRepository,
                      &sensorReader, &moduleStructure, &sensorStructure, &modbusAddressAllocator,
                      &sensorValueModel, &virtualChannels, nullptr),
          republish(&moduleStructure, &sensorRepository, &server, &sensorStructure, &logger, &debugSystem,
                    &arduino),
          sensorService(&moduleConfig, &moduleStructure, &sensorRepository, &sensorReader, &sensorValueModel,
                        &timeAdapter, &arduino, &server, &logger, &debugSystem, nullptr, nullptr, nullptr,
                        &virtualChannels),
          commandService(nullptr, nullptr, nullptr, &moduleStructure, &logger, &debugSystem, &timeAdapter,
                         &converter),
          setupService(&logger, &debugSystem, &moduleConfig, nullptr, nullptr, nullptr, nullptr, &wifiAdapter,
                       nullptr, nullptr, nullptr, &mqttManager, &moduleStructure, nullptr, nullptr,
                       &sensorRepository, &sensorTypeRepository, &sensorReader)
    {
        flash.clock = &arduino;
        flash.watch(REGISTRY_FILE);
        server.clock = &arduino;
        settings.setUser("t104@example.com");
        settings.setAccountUid("t104-account");
        settings.setNoScanSensors(false);
        moduleConfig.setSettings(&settings);
        moduleConfig.setWifiAdapter(&wifiAdapter);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setModuleSubType(MODULE_SUBTYPE_NONE);
        moduleConfig.setSensorPinCount(4);
        wifiAdapter.setMACAddress(MODULE_UID);
        wifiAdapter.setLocalIP("192.168.1.100");
        timeAdapter.setEpochTime(1757066400);

        setupService.setSensorStructureUseCase(&sensorStructure);
        setupService.setStartupSensorScan(&startupScan);
        startupScan.setRepublishModuleStructureUseCase(&republish);
        republish.setStoredSensorDescriber(&startupScan);
        commandService.setStartupSensorScan(&startupScan);
        arduino.setMillis(0);
    }

    void storedBeforeTheRestart(const std::vector<SensorEntity>& rows)
    {
        TEST_ASSERT_TRUE(flash.writeToFile(REGISTRY_FILE, converter.sensorsToJson(rows).c_str()));
    }

    /** @brief Setup's structure and the discovery it arms. */
    void boot() { RegistryUnreadTestHelper::fillStructure(setupService); }

    /** @brief The control loop, a tick at a time up to and including this moment. */
    void runUntil(unsigned long untilMs)
    {
        while (now <= untilMs)
        {
            arduino.setMillis(now);
            sensorService.loop();
            commandService.loop();
            now += TICK_MS;
        }
    }

    /** @brief The discovery as the loop and the bus run it: every step it hands out, at this moment. */
    int runTheDiscoveryAt(unsigned long atMs)
    {
        arduino.setMillis(atMs);
        int steps = 0;
        StartupScanStep step;
        while (startupScan.takeNextStep(step))
        {
            startupScan.applyStepOutcome(startupScan.runStepOnBus(step));
            if (++steps > 5000)
            {
                TEST_FAIL_MESSAGE("the discovery did not terminate");
            }
        }
        return steps;
    }

    /** @brief The row a restart on this flash reads, as stored. */
    SensorEntity storedRow(const std::string& uid)
    {
        SpiffsSensorDao daoAfterBoot(&flash, &converter, &debugSystem, &logger);
        std::vector<SensorEntity> rows;
        daoAfterBoot.getAll(rows);
        for (size_t i = 0; i < rows.size(); i++)
        {
            if (rows[i].uid == uid)
            {
                return rows[i];
            }
        }
        return SensorEntity();
    }
};

}  // namespace

/**
 * The boot's read of the registry is refused once, and setup describes the module from what that
 * left: no sensor - the structure the server would take for a module with every port empty. Once the
 * registry reads back, a sweep later, a structure that describes the stored sensor goes to the
 * server, with nothing written in between and no discovery run.
 */
static void test_a_structure_described_from_a_registry_that_did_not_read_back_is_sent_again_a_sweep_later(void)
{
    BootAfterRestart module;
    // A catalog to describe with; nothing pumps the discovery on this stand.
    module.sensorTypeRepository.addSensorType(makeScannableType());
    module.storedBeforeTheRestart({makeRow(1)});
    module.flash.refuseNext(1);
    module.boot();

    const std::string rowPrefix = std::string(MODULE_UID) + ".01";
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.flash.refusedReads, "the premise: the boot's read was refused");
    TEST_ASSERT_FALSE_MESSAGE(describes(module.moduleStructure, rowPrefix),
                              "the premise: the boot described a sensor out of a refused read");

    module.runUntil(60000);

    char message[200];
    long takenAtMs = -1;
    for (size_t i = 0; i < module.server.structures.size(); i++)
    {
        if (describes(module.server.structures[i], rowPrefix))
        {
            takenAtMs = (long)module.server.takenAtMs[i];
            break;
        }
    }
    if (takenAtMs >= 0)
    {
        snprintf(message, sizeof(message),
                 "the server took a structure describing the stored sensor at %ld ms (%u structure(s) in all)",
                 takenAtMs, (unsigned)module.server.structures.size());
    }
    else
    {
        snprintf(message, sizeof(message),
                 "no structure describing the stored sensor reached the server in 60000 ms (%u in all)",
                 (unsigned)module.server.structures.size());
    }
    printf("  t104: %s\n", message);
    TEST_ASSERT_TRUE_MESSAGE(takenAtMs >= 0, message);
    TEST_ASSERT_TRUE_MESSAGE(takenAtMs <= (long)(ONE_SWEEP_MS + TICK_MS), message);
}

/**
 * The registry holds a sensor the person accepted on port 0, under its port's uid; the boot's read
 * of it is refused once; the catalog has a model the device on port 0 answers to. The discovery that
 * boot arms hands out no step while the registry has not read back - on an empty registry port 0
 * looks free, and an answer there would be adopted over the accepted row the moment storage reads
 * again. Once it reads back, port 0 is the stored sensor's, and the row stays as it was.
 */
static void test_the_discovery_does_not_run_over_a_registry_that_did_not_read_back(void)
{
    BootAfterRestart module;
    module.sensorTypeRepository.addSensorType(makeScannableType());
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 0);
    module.sensorReader.readSensorReturnValue = false;
    const std::string rowUid = std::string(MODULE_UID) + ".00";
    module.storedBeforeTheRestart({makeRow(0, "configured_model", SENSOR_STATUS_ADDED)});
    module.flash.refuseNext(1);
    module.boot();
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.flash.refusedReads, "the premise: the boot's read was refused");
    TEST_ASSERT_TRUE_MESSAGE(module.startupScan.isRunning(), "the premise: the boot armed the discovery");

    const int stepsBeforeTheReadBack = module.runTheDiscoveryAt(0);
    const int stepsAfter = module.runTheDiscoveryAt(ONE_SWEEP_MS);

    const SensorEntity row = module.storedRow(rowUid);
    char message[256];
    snprintf(message, sizeof(message),
             "the discovery took %d step(s) before the registry read back and %d after; the row of port 0 "
             "is %s, status %d, %u adopted",
             stepsBeforeTheReadBack, stepsAfter, row.model.c_str(), (int)row.status,
             (unsigned)module.startupScan.adoptedCount());
    printf("  t104: %s\n", message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("configured_model", row.model.c_str(), message);
    TEST_ASSERT_EQUAL_MESSAGE(SENSOR_STATUS_ADDED, row.status, message);
    TEST_ASSERT_EQUAL_MESSAGE(0, stepsBeforeTheReadBack, message);
    TEST_ASSERT_TRUE_MESSAGE(module.logger.hasMessageContaining("the discovery waits for it"),
                             "the tape does not say the discovery waited for the registry");
    TEST_ASSERT_TRUE_MESSAGE(stepsAfter > 0, "the discovery did not go on once the registry read back");
}

/**
 * A step of the discovery is on the line, asking free port 2, where a device answers. Meanwhile the
 * person puts a sensor on port 2 (SetSensorModel stores its row), and the read that loads the
 * registry back after that write is refused. The answer arrives: on the registry that read left,
 * port 2 looks free - and the answer, adopted, would be written over the person's row the moment
 * storage reads again. It is dropped, and the row stays as the person stored it.
 */
static void test_an_answer_that_arrives_while_the_registry_does_not_read_back_is_not_adopted(void)
{
    BootAfterRestart module;
    module.sensorTypeRepository.addSensorType(makeScannableType());
    module.sensorReader.devicePin = 2;
    module.sensorReader.answeringAddresses.push_back(MODBUS_ADDRESS_MIN + 2);
    module.sensorReader.readSensorReturnValue = false;
    const std::string rowUid = std::string(MODULE_UID) + ".02";
    module.storedBeforeTheRestart({makeRow(0)});
    module.boot();

    StartupScanStep step;
    bool onTheLine = false;
    for (int taken = 0; taken < 5000 && module.startupScan.takeNextStep(step); taken++)
    {
        if (step.kind == StartupScanStep::SCAN_CANDIDATE && step.pin == 2)
        {
            onTheLine = true;
            break;
        }
        module.startupScan.applyStepOutcome(module.startupScan.runStepOnBus(step));
    }
    TEST_ASSERT_TRUE_MESSAGE(onTheLine, "the premise: the discovery asked port 2 for a model");
    const StartupScanOutcome outcome = module.startupScan.runStepOnBus(step);
    TEST_ASSERT_TRUE_MESSAGE(outcome.answered, "the premise: the device on port 2 answered");

    TEST_ASSERT_TRUE_MESSAGE(module.sensorRepository.save(makeRow(2, "person_model", SENSOR_STATUS_ADDED)),
                             "the premise: the person's row was stored");
    module.flash.refuseNext(1);
    module.startupScan.applyStepOutcome(outcome);
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)module.flash.refusedReads,
                              "the premise: the read after the person's write was refused");

    module.arduino.setMillis(ONE_SWEEP_MS);
    TEST_ASSERT_EQUAL_MESSAGE(2, module.sensorRepository.count(), "the premise: the registry read back");
    const SensorEntity row = module.storedRow(rowUid);
    char message[200];
    snprintf(message, sizeof(message),
             "an answer that arrived while the registry did not read back left the row of port 2 %s, status %d",
             row.model.c_str(), (int)row.status);
    printf("  t104: %s\n", message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("person_model", row.model.c_str(), message);
    TEST_ASSERT_EQUAL_MESSAGE(SENSOR_STATUS_ADDED, row.status, message);
}

extern "C" void run_registry_unread_at_boot_tests(void)
{
    RUN_TEST(test_a_boot_whose_registry_read_was_refused_polls_the_stored_sensors_a_sweep_later);
    RUN_TEST(test_a_registry_that_does_not_read_for_a_while_is_read_once_a_sweep_and_kept);
    RUN_TEST(test_a_fresh_module_does_not_read_its_missing_registry_again);
    RUN_TEST(test_a_restart_whose_rule_file_read_was_refused_runs_its_rule_a_sweep_later);
    RUN_TEST(test_a_relay_whose_rule_file_reload_was_refused_is_switched_off_by_its_rule_a_sweep_later);
    RUN_TEST(test_a_relay_holds_while_its_rule_file_does_not_read_and_follows_its_rule_a_sweep_after);
    RUN_TEST(test_a_structure_described_from_a_registry_that_did_not_read_back_is_sent_again_a_sweep_later);
    RUN_TEST(test_the_discovery_does_not_run_over_a_registry_that_did_not_read_back);
    RUN_TEST(test_an_answer_that_arrives_while_the_registry_does_not_read_back_is_not_adopted);
}
