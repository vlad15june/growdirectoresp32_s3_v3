// Scenario S17 - the OTA download is a bounded operation with a reasoned outcome.
//
// The download runs on the command network task; what it never came with is
// a limit. The production FirmwareRemoteUpdate streamed the image through Update.writeStream(),
// which owns its caller until the transfer ends - and a stream that freezes with the socket open
// never ends, so the cmd-net task and every command queued behind it were hostage to the network
// staying alive.
//
// This file runs the real FirmwareRemoteUpdate - the same download loop the device executes -
// inside the real command pipeline (CommandService, Outbox, NetworkCommandRunner) against
// simulated networks: a slow but alive stream, a stream that freezes mid-image, a connection
// that dies, a flash that refuses. Replaced are the socket (SimOtaHttpClient), the flash slot
// (MockFirmwareImageWriter) and time; the decisions are all production code.
//
// ### Reproducing the red
//
// A download session that names <WiFi.h>, <Update.h> and the concrete HttpClientAdapter cannot be
// executed by a host test at all: the class is then compiled for the device and proven nowhere.
// The frozen-stream test is the behavioural red on top of that: with the transport injected but
// no overall budget, the command never completes and the test fails on "the server never saw the
// outcome".

#include <unity.h>

#include "Application/Services/CommandService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Net/Outbox.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/FirmwareUpdate/ConfirmRunningFirmwareUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/FirmwareUpdateRecoveryUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Configuration/PendingFirmwareUpdateStore.h"
#include "Infrastructure/Controllers/HTTP/FirmwareRemoteUpdate.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"

#include "Mock/Application/MockClearModuleDataService.h"
#include "Mock/Core/Domain/MockFirmwareImageWriter.h"
#include "Mock/Core/Domain/MockNetworkStatusProvider.h"
#include "Mock/Core/Domain/MockOtaPartitionControl.h"
#include "Mock/Core/Domain/MockOtaUpdateLog.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsAdapter.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimOtaHttpClient.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimOtaHttpClient;
using hostsim::VirtualClock;

namespace {

const uint32_t CONTROL_PERIOD_MS = 10;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const MODULE_UID = "s17-module";

// The image the fleet actually ships: the current build is ~1.57 MB (build_manifest.json),
// rounded up to 1.6 MB here. The slow-network scenario uses the real order of magnitude so
// the budget it validates is the budget the field needs.
const int FIRMWARE_IMAGE_BYTES = 1677722;

// 4 KiB/s = 32 kbit/s: the worst link that still counts as alive rather than broken. The whole
// image takes ~410 s at this rate - well inside the download budget, and the test proves it.
const uint32_t SLOW_BUT_ALIVE_BYTES_PER_SECOND = 4096;

// --- the outside world ------------------------------------------------------------------------

/** @brief The far end of the command pipeline: hands out command lists, records reports. */
class SimServer : public MockHybridAPIService {
public:
    std::vector<ModuleCommandDto> pendingModuleCommands;
    uint32_t getModuleCommandCalls = 0;

    std::vector<std::vector<ModuleCommandUpdateDto>> moduleCommandReports;
    std::vector<CommandResultDto> commandReports;

    std::vector<ModuleCommandDto> getModuleCommand(bool* httpFailed = nullptr) override
    {
        getModuleCommandCalls++;
        if (httpFailed != nullptr)
        {
            *httpFailed = false;
        }
        std::vector<ModuleCommandDto> answer;
        answer.swap(pendingModuleCommands);
        return answer;
    }

    bool sendModuleCommandExecutionResult(const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        moduleCommandReports.push_back(updates);
        return true;
    }

    bool sendCommandResult(const CommandResultDto& result,
                           ApiPriority = ApiPriority::UNDEFINED) override
    {
        commandReports.push_back(result);
        return true;
    }
};

/** @brief The inbound half of MqttManager: a queue drained on the task that calls loop(). */
class ScriptedMqttManager : public IMqttManager {
public:
    bool connect() override { return true; }
    void disconnect() override {}
    bool isConnected() override { return true; }
    bool subscribe(const char*) override { return true; }
    bool unsubscribe(const char*) override { return true; }
    bool publish(const char*, const char*) override { return true; }
    void setMessageProcessor(IMessageProcessor*) override {}
    void setupMqttTopicHandlers() override {}
    int getMqttFails() const override { return 0; }
    void resetMqttFails() override {}
    uint32_t getInboundOversizeDrops() const override { return 0; }
    const char* getClientId() const override { return MODULE_UID; }
    void setClientId(const char*) override {}

    void loop() override
    {
        while (!_inbound.empty())
        {
            const std::string payload = _inbound.front();
            _inbound.pop_front();
            if (_handler != nullptr)
            {
                _handler->handleMessage("GrowDirectorMqttTopic/s17-module",
                                        reinterpret_cast<const uint8_t*>(payload.data()),
                                        static_cast<unsigned int>(payload.size()));
            }
            dispatched++;
        }
    }

    void deliver(const std::string& payload) { _inbound.push_back(payload); }
    void setHandler(ITopicHandler* handler) { _handler = handler; }

    uint32_t dispatched = 0;

private:
    std::deque<std::string> _inbound;
    ITopicHandler* _handler = nullptr;
};

class CountingSensorService : public ISensorService {
public:
    bool init() override { return true; }
    void loop() override {}
    bool readAllSensors() override { return true; }
    bool readSensor(SensorEntity) override { return true; }
};

class CountingRuleService : public IRuleService {
public:
    bool init() override { return true; }
    void loop() override {}
    bool forceSyncRules() override { return true; }
};

/**
 * @brief SimArduino that books every feedTaskWatchdog() with the task and the time it ran.
 *
 * On the device the updater's esp_task_wdt_reset() (through IArduinoAdapter) feeds the entry of
 * whichever task is downloading, so the record answers two questions the scenarios ask: whose
 * liveness the download vouched for, and how long the gaps between vouches were.
 */
class WatchdogAccountingArduino : public SimArduino {
public:
    WatchdogAccountingArduino(VirtualClock& clock, HostTaskScheduler& scheduler)
        : SimArduino(clock, &scheduler), _clock(clock), _scheduler(scheduler)
    {
    }

    struct Feed {
        std::string taskName;
        uint64_t atMs;
    };

    std::vector<Feed> feeds;

    void feedTaskWatchdog() override
    {
        SimArduino::feedTaskWatchdog();
        Feed feed;
        feed.taskName = _scheduler.currentTaskName();
        feed.atMs = _clock.nowMs();
        feeds.push_back(feed);
        _scheduler.watchdogFeed();
    }

    /** @brief Longest silence between consecutive feeds from the named task, ms. */
    uint64_t worstFeedGapMsFor(const char* taskName) const
    {
        uint64_t worst = 0;
        uint64_t previous = 0;
        bool seen = false;
        for (size_t i = 0; i < feeds.size(); i++)
        {
            if (feeds[i].taskName != taskName)
            {
                continue;
            }
            if (seen && feeds[i].atMs - previous > worst)
            {
                worst = feeds[i].atMs - previous;
            }
            previous = feeds[i].atMs;
            seen = true;
        }
        return worst;
    }

    uint64_t feedCountFor(const char* taskName) const
    {
        uint64_t count = 0;
        for (size_t i = 0; i < feeds.size(); i++)
        {
            if (feeds[i].taskName == taskName)
            {
                count++;
            }
        }
        return count;
    }

private:
    VirtualClock& _clock;
    HostTaskScheduler& _scheduler;
};

// The stream SimOtaHttpClient serves is byte n = n & 0xFF, so an image of a given size has a
// fixed digest. These two were computed outside this codebase, so that a wrong MD5 in the host
// stand-in shows up as a failing test instead of agreeing with itself:
//   python3 -c "import hashlib; print(hashlib.md5(bytes(i & 0xFF for i in range(N))).hexdigest())"
const int CHECKSUM_IMAGE_BYTES = 262144;
const char* const CHECKSUM_IMAGE_MD5 = "d19215b1d714757e1fdb0060c52fd4c8";
const int TRUNCATED_IMAGE_BYTES = 200000;
const char* const TRUNCATED_IMAGE_MD5 = "3cbe3eb4d79ebd2e7cc7b3e620f5ba60";

std::string moduleCommandJson(const char* command)
{
    char buffer[384];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"%s\",\"options\":{}}",
             MODULE_UID, command);
    return std::string(buffer);
}

ModuleCommandDto makeUpdateFirmwareCommand(const std::string& uid)
{
    ModuleCommandDto dto;
    dto.uid = uid;
    dto.command = "UpdateFirmware";
    dto.createdAt = "2026-08-21T10:00:00Z";
    dto.options =
        "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\",\"version\":\"9.9.9\"}";
    return dto;
}

/** @brief The same command with the optional image checksum the server may add to the options. */
ModuleCommandDto makeUpdateFirmwareCommandWithMd5(const std::string& uid, const char* md5)
{
    ModuleCommandDto dto = makeUpdateFirmwareCommand(uid);
    dto.options = "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\","
                  "\"version\":\"9.9.9\",\"md5\":\"" + std::string(md5) + "\"}";
    return dto;
}

/** @brief The network command runner, refusing the first command handed to it and serving the rest. */
class RefusesTheFirstCommandRunner : public INetworkCommandRunner {
public:
    explicit RefusesTheFirstCommandRunner(INetworkCommandRunner& real) : _real(real) {}

    uint32_t submit(const ComplexCommandWork& work) override
    {
        if (!_refused)
        {
            _refused = true;
            return kRejected;
        }
        return _real.submit(work);
    }

    bool drainResults(std::vector<NetworkCommandCompletion>& out) override { return _real.drainResults(out); }

private:
    INetworkCommandRunner& _real;
    bool _refused = false;
};

/**
 * @brief The module, wired the way globals.h wires the OTA path.
 */
struct ModuleUnderTest {
    VirtualClock clock;
    HostTaskScheduler scheduler;
    WatchdogAccountingArduino arduino;

    MockLogger logger;
    MockDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    MockSensorRepository sensorRepository;
    MockSensorTypeRepository sensorTypeRepository;
    MockNTPTimeAdapter timeAdapter;
    MockWiFiManager wifiManager;
    MockWifiAdapter wifiStation;
    MockClearModuleDataService clearService;
    ArduinoJsonConverter jsonConverter;
    ModuleStructure moduleStructure;

    SimServer server;

    SimOtaHttpClient otaHttp;
    MockFirmwareImageWriter imageWriter;
    MockOtaUpdateLog otaLog;
    FirmwareRemoteUpdate firmwareUpdater;

    // The recovery half of the OTA path, wired as globals.h wires it: the module's own view of
    // the HTTP link and the file an update waits in.
    MockNetworkStatusProvider networkStatus;
    MockSpiffsAdapter spiffs;
    ArduinoJsonConverter pendingUpdateConverter;
    PendingFirmwareUpdateStore pendingUpdateStore;
    MockOtaPartitionControl otaPartitionControl;
    FirmwareUpdateRecoveryUseCase updateRecovery;
    ConfirmRunningFirmwareUseCase confirmRunningFirmware;

    ModuleCommandExecutionUseCase moduleCommandUseCase;

    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    NetworkCommandRunner networkRunner;

    CommandService commandService;
    CommandTopicHandler topicHandler;
    ScriptedMqttManager mqttManager;

    CountingSensorService sensorService;
    CountingRuleService ruleService;
    LoopServiceConfigureDone loopService;

    ModuleUnderTest()
        : scheduler(clock),
          arduino(clock, scheduler),
          moduleConfig(&logger, &debugSystem, &settings),
          otaHttp(clock),
          firmwareUpdater(&otaHttp, &imageWriter, &arduino, &wifiStation,
                          &logger, &debugSystem, &otaLog),
          pendingUpdateStore(&spiffs, &pendingUpdateConverter),
          updateRecovery(&networkStatus, &moduleConfig, &settings, &pendingUpdateStore,
                         &logger, &debugSystem, &otaLog, &otaPartitionControl),
          confirmRunningFirmware(&otaPartitionControl, &moduleStructure, &arduino,
                                 &logger, &debugSystem, &otaLog),
          moduleCommandUseCase(&logger, &debugSystem, &timeAdapter, &firmwareUpdater, &arduino,
                               &sensorRepository, &sensorTypeRepository, nullptr,
                               &moduleConfig, &settings, &server, &clearService,
                               &moduleStructure, nullptr, &wifiManager, nullptr,
                               nullptr, nullptr, nullptr, &updateRecovery),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          networkRunner(&scheduler, &logger, &debugSystem),
          commandService(nullptr, &moduleCommandUseCase, nullptr, &moduleStructure,
                         &logger, &debugSystem, &timeAdapter, &jsonConverter),
          topicHandler(&logger, &debugSystem, MODULE_UID, &commandService, &jsonConverter)
    {
        debugSystem.featureEnabled = false;

        timeAdapter.setEpochTime(1755770400);
        timeAdapter.setCommandTimeValid(true);

        settings.setUser("s17@example.com");
        settings.setAccountUid("s17-account");
        moduleConfig.setModuleType(MODULE_TYPE_SENSOR);
        moduleConfig.setSensorPinCount(4);
        moduleConfig.setModuleUid(MODULE_UID);
        moduleConfig.setSettings(&settings);

        // The station link is up: the OTA scenarios are about the stream, not about WiFi.
        wifiStation.connected = true;

        networkRunner.setExecutor(&moduleCommandUseCase);
        commandService.setFirmwareUpdateRecovery(&updateRecovery);
        commandService.setOutbox(&outbox);
        commandService.setNetworkCommandRunner(&networkRunner);
        mqttManager.setHandler(&topicHandler);

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&outbox);
        loopService.setCommandService(&commandService);
        loopService.setConfirmRunningFirmwareUseCase(&confirmRunningFirmware);
        loopService.setSensorService(&sensorService);
        loopService.setRuleService(&ruleService);
        loopService.setMqttManager(&mqttManager);
    }

    /**
     * @brief The boot's first structure has settled: what BootSequencer asks of the command service
     *        then - the update kept for this boot, then the server's list - on the loop task, in its
     *        next iteration, before the loop service runs.
     */
    void settleTheBootsFirstStructure() { bootCommandsOwed = true; }

    bool bootCommandsOwed = false;
    bool bootCommandsAsked = false;

    /** @brief One task, mirroring main.cpp: loop, feed, sleep one period. */
    void startArduinoLoopTask()
    {
        scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [this]() {
            while (true)
            {
                if (bootCommandsOwed)
                {
                    bootCommandsOwed = false;
                    commandService.resumeKeptUpdate();
                    commandService.requestModuleCommandFetch();
                    bootCommandsAsked = true;
                }
                loopService.loop();
                scheduler.watchdogFeed();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }
};

void assertRunIsSound(ModuleUnderTest& module)
{
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.mqttManager.dispatched >= 1,
                             "the command signal was never dispatched - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.beginCalls >= 1,
                             "the download never opened the flash slot - nothing was exercised");
}

/**
 * @brief The same gate for the scenarios that enter through the boot's asks instead of MQTT.
 *
 * The boot asks once its first structure has settled, not on a signal over MQTT, so "the signal was
 * dispatched" is replaced by "the boot asked"; the rest is the same soundness question.
 */
void assertBootRunIsSound(ModuleUnderTest& module)
{
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.bootCommandsAsked,
                             "the boot never asked for its commands - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.beginCalls >= 1,
                             "the download never opened the flash slot - nothing was exercised");
}

/** @brief How many times the server was told this outcome for this command. */
int countReports(ModuleUnderTest& module, const char* uid, const char* status)
{
    int count = 0;
    for (size_t i = 0; i < module.server.moduleCommandReports.size(); i++)
    {
        for (size_t j = 0; j < module.server.moduleCommandReports[i].size(); j++)
        {
            const ModuleCommandUpdateDto& report = module.server.moduleCommandReports[i][j];
            if (report.uid == uid && report.status == status)
            {
                count++;
            }
        }
    }
    return count;
}

/** @brief How many statuses the server was told in all, whichever command they were for. */
int countAllReports(ModuleUnderTest& module)
{
    int count = 0;
    for (size_t i = 0; i < module.server.moduleCommandReports.size(); i++)
    {
        count += (int)module.server.moduleCommandReports[i].size();
    }
    return count;
}

const ModuleCommandUpdateDto* findReport(ModuleUnderTest& module, const char* uid,
                                         const char* status)
{
    for (size_t i = 0; i < module.server.moduleCommandReports.size(); i++)
    {
        for (size_t j = 0; j < module.server.moduleCommandReports[i].size(); j++)
        {
            const ModuleCommandUpdateDto& report = module.server.moduleCommandReports[i][j];
            if (report.uid == uid && report.status == status)
            {
                return &report;
            }
        }
    }
    return nullptr;
}

/** @brief How many lines the module's log holds that contain this text. */
int countLogLines(ModuleUnderTest& module, const char* text)
{
    int count = 0;
    for (size_t i = 0; i < module.logger.messages.size(); i++)
    {
        if (module.logger.messages[i].find(text) != std::string::npos)
        {
            count++;
        }
    }
    return count;
}

} // namespace

// --------------------------------------------------------------------------------------------
// 1. The legitimate worst case: the full image on a slow but alive link. The download succeeds,
//    and while it runs the watchdog hears from the downloading task at every step of progress -
//    a healthy transfer is never mistaken for a hang.
// --------------------------------------------------------------------------------------------

static void test_s17_a_slow_but_alive_download_succeeds_and_keeps_vouching(void)
{
    ModuleUnderTest module;
    module.otaHttp.queueHealthyDownload(FIRMWARE_IMAGE_BYTES, SLOW_BUT_ALIVE_BYTES_PER_SECOND);
    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommand("fw-slow"));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(600000);

    assertRunIsSound(module);

    // The whole image reached the flash slot and the slot was finalized exactly once.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)FIRMWARE_IMAGE_BYTES,
                                     (uint32_t)module.imageWriter.bytesWritten,
                                     "the flash slot did not receive the full image");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.endCalls,
                                  "the image was not finalized exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.abortCalls,
                                  "a successful download aborted the slot");

    // The outcome the backend sees, and the consequences on the module.
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-slow", "Done"),
                                 "the server never saw the Done outcome");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, module.settings.getUpdateStatus(),
                                    "the DONE update status never reached the settings");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the restart was not requested after the update");

    // Liveness: the download vouched for the downloading task all the way through - the longest
    // silence is far below any watchdog budget - and never for the control loop.
    const uint64_t downloadFeeds = module.arduino.feedCountFor(NetworkCommandRunner::TASK_NAME);
    TEST_ASSERT_TRUE_MESSAGE(downloadFeeds >= 100,
                             "the download barely reported progress - the scenario is empty");
    char gapMessage[160];
    snprintf(gapMessage, sizeof(gapMessage),
             "the longest gap between progress feeds was %lu ms - a healthy slow stream must "
             "keep the watchdog fed",
             (unsigned long)module.arduino.worstFeedGapMsFor(NetworkCommandRunner::TASK_NAME));
    TEST_ASSERT_TRUE_MESSAGE(
        module.arduino.worstFeedGapMsFor(NetworkCommandRunner::TASK_NAME) <= 5000, gapMessage);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.arduino.feedCountFor(LOOP_TASK_NAME),
                                     "a download progress feed ran on the control loop's stack");
}

// --------------------------------------------------------------------------------------------
// 2. The connection dies mid-image. The session ends with the reason on record, the slot is
//    released, the failure status is persisted from the loop - and nothing requests a restart.
// --------------------------------------------------------------------------------------------

static void test_s17_a_dropped_connection_ends_in_error_with_the_reason(void)
{
    ModuleUnderTest module;
    module.otaHttp.queueDroppedDownload(FIRMWARE_IMAGE_BYTES, 102400, 131072);
    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommand("fw-drop"));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);

    const ModuleCommandUpdateDto* error = findReport(module, "fw-drop", "Error");
    TEST_ASSERT_NOT_NULL_MESSAGE(error, "the server never saw the Error outcome");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error->errorMessage.c_str(), "connection lost"),
                                 error->errorMessage.c_str());

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.abortCalls,
                                  "the half-written slot was not released");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.endCalls,
                                  "a failed download still finalized the slot");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, module.settings.getUpdateStatus(),
                                    "the ERROR update status never reached the settings");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART,
                                  module.moduleConfig.getRequestedModuleMode(),
                                  "a failed download still requested a restart");
}

// --------------------------------------------------------------------------------------------
// 3. The flash refuses bytes mid-image (a failing sector, a torn write). Same contract: reason,
//    released slot, persisted status, no restart.
// --------------------------------------------------------------------------------------------

static void test_s17_a_flash_that_stops_accepting_bytes_ends_in_error(void)
{
    ModuleUnderTest module;
    module.otaHttp.queueHealthyDownload(FIRMWARE_IMAGE_BYTES, 1048576);
    module.imageWriter.acceptAtMostBytes = 65536;
    module.imageWriter.lastErrorValue = 6; // UPDATE_ERROR_WRITE's numeric value on the device
    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommand("fw-flash"));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);

    const ModuleCommandUpdateDto* error = findReport(module, "fw-flash", "Error");
    TEST_ASSERT_NOT_NULL_MESSAGE(error, "the server never saw the Error outcome");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error->errorMessage.c_str(), "flash write failed"),
                                 error->errorMessage.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.abortCalls,
                                  "the half-written slot was not released");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, module.settings.getUpdateStatus(),
                                    "the ERROR update status never reached the settings");
}

// --------------------------------------------------------------------------------------------
// 4. Every byte arrives but the image does not verify (truncated or corrupt content the size
//    happened to match). The finalize step refuses, and the refusal carries the writer's error.
// --------------------------------------------------------------------------------------------

static void test_s17_an_image_the_flash_rejects_at_finalize_ends_in_error(void)
{
    ModuleUnderTest module;
    module.otaHttp.queueHealthyDownload(262144, 1048576);
    module.imageWriter.endResult = false;
    module.imageWriter.lastErrorValue = 9; // UPDATE_ERROR_MD5's numeric value on the device
    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommand("fw-verify"));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);

    const ModuleCommandUpdateDto* error = findReport(module, "fw-verify", "Error");
    TEST_ASSERT_NOT_NULL_MESSAGE(error, "the server never saw the Error outcome");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error->errorMessage.c_str(), "Update.end failed err=9"),
                                 error->errorMessage.c_str());
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, module.settings.getUpdateStatus(),
                                    "the ERROR update status never reached the settings");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART,
                                  module.moduleConfig.getRequestedModuleMode(),
                                  "an unverified image still requested a restart");
}

// --------------------------------------------------------------------------------------------
// 5. The nasty case the field produces: the stream freezes with the socket open. Before the
//    download budget the session polled that socket forever - the command never completed, the
//    cmd-net task never took another job, and every command queued behind the OTA was hostage.
//    With the budget the session ends at its limit with the reason on record, and the next command runs.
// --------------------------------------------------------------------------------------------

static void test_s17_a_frozen_stream_ends_in_error_within_the_budget_and_the_queue_lives(void)
{
    ModuleUnderTest module;
    // Freezes at 256 KiB of a healthy-rate transfer; the socket stays open, so neither the
    // HTTP timeouts nor the connection check can end it - only the overall budget can.
    module.otaHttp.queueFrozenDownload(FIRMWARE_IMAGE_BYTES, 102400, 262144);
    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommand("fw-frozen"));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));

    // Deep into the stall: the stream froze at 256 KiB (a few seconds in), and by now the
    // session has been polling a dry socket for most of a minute.
    module.scheduler.runForMs(60000);
    const uint64_t feedsWhenStalled =
        module.arduino.feedCountFor(NetworkCommandRunner::TASK_NAME);
    TEST_ASSERT_TRUE_MESSAGE(feedsWhenStalled > 0,
                             "no chunk was ever written before the freeze - the scenario is empty");

    // The whole budget plus a margin. Without the budget this runs to the end of virtual
    // time with the command still in flight - the red of this test.
    module.scheduler.runForMs(FIRMWARE_DOWNLOAD_TOTAL_TIMEOUT_MS + 60000);

    assertRunIsSound(module);

    // A frozen stream stops vouching for its task the moment it stops writing chunks: not one
    // watchdog feed was made on the download's behalf between the stall and the verdict.
    // Polling a dry socket is not progress, and feeding on it would hide exactly the hang the
    // budget exists to end.
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        feedsWhenStalled, module.arduino.feedCountFor(NetworkCommandRunner::TASK_NAME),
        "the download fed the watchdog while its stream was frozen - a stalled transfer "
        "vouched for a task that was making no progress");

    const ModuleCommandUpdateDto* error = findReport(module, "fw-frozen", "Error");
    TEST_ASSERT_NOT_NULL_MESSAGE(error,
                                 "the frozen download never produced an outcome - the command "
                                 "is still in flight after the whole budget");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error->errorMessage.c_str(), "timed out"),
                                 error->errorMessage.c_str());

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.abortCalls,
                                  "the half-written slot was not released");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, module.settings.getUpdateStatus(),
                                    "the ERROR update status never reached the settings");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART,
                                  module.moduleConfig.getRequestedModuleMode(),
                                  "a timed-out download still requested a restart");

    // The queue lives: the next command - another update, this time over a healthy stream -
    // is served by the same cmd-net task and completes.
    module.otaHttp.queueHealthyDownload(262144, 1048576);
    module.imageWriter.endResult = true;
    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommand("fw-after"));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-after", "Done"),
                                 "the command after the frozen download never completed - "
                                 "the cmd-net queue did not survive the timeout");
}

extern "C" void run_s17_recoverable_update_tests(void)
{
    printf("\n----- Running S17 Recoverable Update Tests -----\n");

    RUN_TEST(test_s17_a_slow_but_alive_download_succeeds_and_keeps_vouching);
    RUN_TEST(test_s17_a_dropped_connection_ends_in_error_with_the_reason);
    RUN_TEST(test_s17_a_flash_that_stops_accepting_bytes_ends_in_error);
    RUN_TEST(test_s17_an_image_the_flash_rejects_at_finalize_ends_in_error);
    RUN_TEST(test_s17_a_frozen_stream_ends_in_error_within_the_budget_and_the_queue_lives);

    printf("\n----- S17 Recoverable Update Tests Complete -----\n");
}

// ==============================================================================================
// The image checksum: the update command may carry the digest the downloaded image must have.
//
// Same stand as the scenarios above - the production download session inside the production
// command pipeline - with one addition: the flash stand-in hashes the bytes it is given and
// refuses an image whose digest does not match the armed expectation, exactly as the device's
// update mechanism does. That is what makes "the image is rejected" a fact about the bytes
// rather than a scripted answer.
//
// The case that motivates all of it is the first one: a transfer that ends cleanly on a
// truncated file. Every byte the server announced arrives, so nothing in the transfer looks
// wrong, and the finalize checks that do not involve a digest - the image magic byte, a bootable
// partition - can pass on a cleanly cut image. Without a checksum such an image is installed and
// the device reboots into it.
// ==============================================================================================

namespace {

/** @brief The full OTA log of a run, joined, for assertion messages. */
std::string otaLogText(ModuleUnderTest& module)
{
    std::string text;
    for (size_t i = 0; i < module.otaLog.lines.size(); i++)
    {
        text += module.otaLog.lines[i];
        text += " | ";
    }
    return text;
}

} // namespace

// --------------------------------------------------------------------------------------------
// 6. The image does not match the checksum the command carried: a truncated file, served whole
//    and complete by its own Content-Length. It is refused at the finalize step, the slot is
//    released, the running firmware stays, and the verdict names the checksum.
// --------------------------------------------------------------------------------------------

static void test_ota_an_image_that_does_not_match_the_command_checksum_is_refused(void)
{
    ModuleUnderTest module;
    // The transfer itself is flawless: 200000 bytes announced, 200000 delivered. What arrives is
    // the first 200000 bytes of the image the command asked for.
    module.otaHttp.queueHealthyDownload(TRUNCATED_IMAGE_BYTES, 1048576);
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-truncated", CHECKSUM_IMAGE_MD5));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);

    // The download ran to its end - this is not a transfer failure.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)TRUNCATED_IMAGE_BYTES,
                                     (uint32_t)module.imageWriter.bytesWritten,
                                     "the announced bytes did not all reach the flash slot");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.endCalls,
                                  "the image was not finalized exactly once");
    // What reached the slot really is the truncated image and not something the stand-in made
    // up: its digest is the value computed outside this codebase for those 200000 bytes.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(TRUNCATED_IMAGE_MD5,
                                     module.imageWriter.writtenImageMd5().c_str(),
                                     "the bytes in the slot are not the image the test describes");

    // The digest decided it, and nothing was made bootable.
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.checksumRefused,
                             "the image was not refused on its checksum - it was accepted");
    TEST_ASSERT_FALSE_MESSAGE(module.imageWriter.imageAccepted,
                              "an image that does not match its checksum was made bootable");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.abortCalls,
                                  "the rejected image was left in the slot");

    // The running firmware carries on: no restart is requested, and the failure is persisted
    // and reported with the checksum named as the reason.
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART,
                                  module.moduleConfig.getRequestedModuleMode(),
                                  "a rejected image still requested a restart into itself");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, module.settings.getUpdateStatus(),
                                    "the ERROR update status never reached the settings");

    const ModuleCommandUpdateDto* error = findReport(module, "fw-truncated", "Error");
    TEST_ASSERT_NOT_NULL_MESSAGE(error, "the server never saw the Error outcome");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error->errorMessage.c_str(), "checksum"),
                                 error->errorMessage.c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error->errorMessage.c_str(), CHECKSUM_IMAGE_MD5),
                                 error->errorMessage.c_str());

    // And the same verdict is in the OTA log, where support reads it.
    char logMessage[512];
    snprintf(logMessage, sizeof(logMessage), "OTA log without a checksum verdict: %s",
             otaLogText(module).c_str());
    TEST_ASSERT_TRUE_MESSAGE(
        module.otaLog.hasLineContaining("ERROR: image rejected: checksum md5="), logMessage);
    TEST_ASSERT_TRUE_MESSAGE(module.otaLog.hasLineContaining(CHECKSUM_IMAGE_MD5), logMessage);
    TEST_ASSERT_FALSE_MESSAGE(module.otaLog.hasLineContaining("UPDATE OK"), logMessage);
}

// --------------------------------------------------------------------------------------------
// 7. The image matches: the install goes through the whole path of the recoverable update -
//    finalized slot, persisted DONE, restart requested - and the expectation was armed where
//    the device requires it, after the slot was opened and before the first byte.
// --------------------------------------------------------------------------------------------

static void test_ota_an_image_that_matches_the_command_checksum_is_installed(void)
{
    ModuleUnderTest module;
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-checked", CHECKSUM_IMAGE_MD5));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.setExpectedMd5Calls,
                                  "the checksum was not armed exactly once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(CHECKSUM_IMAGE_MD5,
                                     module.imageWriter.lastExpectedMd5.c_str(),
                                     "the writer was armed with a different digest");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)module.imageWriter.bytesWrittenWhenMd5Armed,
                                     "the checksum was armed after bytes had been written - part "
                                     "of the session ran on a slot that was verifying nothing");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCallsWhenMd5Armed,
                                  "the checksum was not armed inside the opened slot session - "
                                  "opening the slot clears the expectation");

    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted,
                             "a matching image was not accepted");
    TEST_ASSERT_FALSE_MESSAGE(module.imageWriter.checksumRefused,
                              "a matching image was refused on its checksum");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.abortCalls,
                                  "a successful download aborted the slot");

    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-checked", "Done"),
                                 "the server never saw the Done outcome");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, module.settings.getUpdateStatus(),
                                    "the DONE update status never reached the settings");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the restart into the verified image was not requested");

    char logMessage[512];
    snprintf(logMessage, sizeof(logMessage), "OTA log: %s", otaLogText(module).c_str());
    TEST_ASSERT_TRUE_MESSAGE(module.otaLog.hasLineContaining("CHECKSUM md5="), logMessage);
    TEST_ASSERT_TRUE_MESSAGE(module.otaLog.hasLineContaining("UPDATE OK"), logMessage);
}

// --------------------------------------------------------------------------------------------
// 8. The digest arrives in upper case. The update mechanism compares it against the lowercase
//    hex it computes itself, character by character, so an unconverted digest would reject every
//    image the server correctly described.
// --------------------------------------------------------------------------------------------

static void test_ota_a_checksum_in_upper_case_still_matches_the_image(void)
{
    std::string upper(CHECKSUM_IMAGE_MD5);
    for (size_t i = 0; i < upper.size(); i++)
    {
        if (upper[i] >= 'a' && upper[i] <= 'f')
        {
            upper[i] = (char)(upper[i] - 'a' + 'A');
        }
    }

    ModuleUnderTest module;
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-upper", upper.c_str()));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(CHECKSUM_IMAGE_MD5,
                                     module.imageWriter.lastExpectedMd5.c_str(),
                                     "the digest was handed to the writer as it arrived, and the "
                                     "writer compares against lowercase hex");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted,
                             "a correctly described image was refused over digest letter case");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-upper", "Done"),
                                 "the server never saw the Done outcome");
}

// --------------------------------------------------------------------------------------------
// 9. Backward compatibility, and the reason there is no third mode: a command without the field
//    is the command the fleet's server sends today. Not one line of the checksum path runs, and
//    the install ends exactly as it did before the field existed.
// --------------------------------------------------------------------------------------------

static void test_ota_a_command_without_a_checksum_installs_as_before(void)
{
    ModuleUnderTest module;
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommand("fw-nochecksum"));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);

    // Nothing was armed, so nothing was verified: the writer was never asked for an expectation.
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.setExpectedMd5Calls,
                                  "a command without a checksum still armed one");
    TEST_ASSERT_FALSE_MESSAGE(module.imageWriter.checksumRefused,
                              "a command without a checksum was refused on one");

    // The OTA log of such a session is the log it was before the field existed.
    char logMessage[512];
    snprintf(logMessage, sizeof(logMessage), "OTA log: %s", otaLogText(module).c_str());
    TEST_ASSERT_FALSE_MESSAGE(module.otaLog.hasLineContaining("CHECKSUM"), logMessage);
    TEST_ASSERT_FALSE_MESSAGE(module.otaLog.hasLineContaining("checksum"), logMessage);
    TEST_ASSERT_FALSE_MESSAGE(module.otaLog.hasLineContaining("md5"), logMessage);

    // And the outcome is the old one, down to the persisted status and the restart request.
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted,
                             "the image was not accepted");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-nochecksum", "Done"),
                                 "the server never saw the Done outcome");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, module.settings.getUpdateStatus(),
                                    "the DONE update status never reached the settings");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the restart after the update was not requested");
}

// --------------------------------------------------------------------------------------------
// 10. A checksum that is not a digest is a defect of the command, not a licence to install
//     unverified: the session stops before the connection is opened, so a broken release never
//     costs the fleet 1.6 MB per module either.
// --------------------------------------------------------------------------------------------

static void test_ota_a_checksum_that_is_not_a_digest_stops_the_command_before_the_download(void)
{
    ModuleUnderTest module;
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    // 31 hex characters: the shape of a digest truncated on its way through the platform.
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-baddigest", "d19215b1d714757e1fdb0060c52fd4c"));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.mqttManager.dispatched >= 1,
                             "the command signal was never dispatched - the scenario did not happen");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.otaHttp.beginCount,
                                  "the download was opened for a command whose checksum is unusable");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls,
                                  "the flash slot was opened for a command whose checksum is unusable");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.setExpectedMd5Calls,
                                  "an unusable checksum was handed to the writer");
    TEST_ASSERT_FALSE_MESSAGE(module.imageWriter.imageAccepted, "an image was installed anyway");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART,
                                  module.moduleConfig.getRequestedModuleMode(),
                                  "a refused command still requested a restart");

    const ModuleCommandUpdateDto* error = findReport(module, "fw-baddigest", "Error");
    TEST_ASSERT_NOT_NULL_MESSAGE(error, "the server never saw the Error outcome");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error->errorMessage.c_str(), "not an MD5 digest"),
                                 error->errorMessage.c_str());

    char logMessage[512];
    snprintf(logMessage, sizeof(logMessage), "OTA log: %s", otaLogText(module).c_str());
    TEST_ASSERT_TRUE_MESSAGE(
        module.otaLog.hasLineContaining("ERROR: image checksum is not an MD5 digest"), logMessage);
}

// --------------------------------------------------------------------------------------------
// 11. The writer refuses the expectation itself. Whatever the reason, the session stops rather
//     than continuing with a slot that verifies nothing.
// --------------------------------------------------------------------------------------------

static void test_ota_a_writer_that_refuses_the_checksum_stops_the_session(void)
{
    ModuleUnderTest module;
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.imageWriter.acceptExpectedMd5 = false;
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-refused", CHECKSUM_IMAGE_MD5));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.writeCalls,
                                  "the image was streamed into a slot that verifies nothing");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.abortCalls,
                                  "the opened slot was not released");
    TEST_ASSERT_FALSE_MESSAGE(module.imageWriter.imageAccepted, "an image was installed anyway");

    const ModuleCommandUpdateDto* error = findReport(module, "fw-refused", "Error");
    TEST_ASSERT_NOT_NULL_MESSAGE(error, "the server never saw the Error outcome");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error->errorMessage.c_str(), "refused checksum"),
                                 error->errorMessage.c_str());
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, module.settings.getUpdateStatus(),
                                    "the ERROR update status never reached the settings");
}

// --------------------------------------------------------------------------------------------
// 12. The other end of the field: the module command as the server actually delivers it. The
//     checksum is a new key inside the existing options object, and the converter carries the
//     options through verbatim - so the field reaches the updater without a parser change. This
//     is what makes the extension one-sided: a server that starts sending it needs nothing else
//     from the device, and a device that receives it from a server that does not send it behaves
//     as before.
// --------------------------------------------------------------------------------------------

static void test_ota_the_checksum_field_survives_the_module_command_parse(void)
{
    const std::string serverAnswer =
        "[{\"uid\":\"cmd-1\",\"command\":\"UpdateFirmware\",\"createdAt\":\"2026-08-21T10:00:00Z\","
        "\"options\":{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\","
        "\"version\":\"9.9.9\",\"md5\":\"d19215b1d714757e1fdb0060c52fd4c8\"}}]";
    const char* const expectedField = "\"md5\":\"d19215b1d714757e1fdb0060c52fd4c8\"";

    // The converter the device is wired with.
    ArduinoJsonConverter converter;
    std::vector<ModuleCommandDto> commands;
    TEST_ASSERT_TRUE_MESSAGE(converter.moduleCommandsFromJson(serverAnswer, commands),
                             "the command list with a checksum did not parse at all");
    TEST_ASSERT_EQUAL_size_t(1, commands.size());
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(commands[0].options.c_str(), expectedField),
                                 commands[0].options.c_str());
}

// --------------------------------------------------------------------------------------------
// 13. The recovery path: an update that could not reach the server is kept for the next boot and
//     carried out there. The command that comes back out of the file has to be the command that
//     went in - including its checksum, which is the one thing the module cannot reconstruct.
//     The scenario is the truncated image again, and the bad link is exactly when it matters:
//     the transfer that gets cut is the transfer that happens on a network that has been down.
// --------------------------------------------------------------------------------------------

static void test_ota_a_kept_update_is_still_checked_against_its_checksum_after_the_restart(void)
{
    ModuleUnderTest module;
    // The link the module reports as dead: the command is kept, not downloaded.
    module.networkStatus.httpDown = true;
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-kept", CHECKSUM_IMAGE_MD5));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls,
                                  "a download was attempted on a link reported as down");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-kept", "InProgress"),
                                 "the server was not told the update is still running");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the module did not ask for the restart that carries the update");

    // The restart. The link is usable again, and the server has nothing pending - the update the
    // module is about to perform can only have come out of the file.
    const uint32_t fetchesBeforeTheRestart = module.server.getModuleCommandCalls;
    module.networkStatus.httpDown = false;
    module.server.pendingModuleCommands.clear();
    // What the server serves is a cleanly truncated image: every announced byte arrives, so only
    // the checksum can tell it from the image the command asked for.
    module.otaHttp.queueHealthyDownload(TRUNCATED_IMAGE_BYTES, 1048576);

    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);
    // The boot asks the server once, and the server has nothing: the update can only be the kept one.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fetchesBeforeTheRestart + 1, module.server.getModuleCommandCalls,
                                     "the boot did not ask the server for its commands exactly once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)TRUNCATED_IMAGE_BYTES,
                                     (uint32_t)module.imageWriter.bytesWritten,
                                     "the resumed download did not run to its end");

    // The point of the scenario: the checksum survived the restart, so the image that does not
    // match it is refused exactly as it is on the direct path.
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.checksumRefused,
                             "the resumed update installed the image without checking it - the "
                             "checksum did not survive being kept for the next boot");
    TEST_ASSERT_FALSE_MESSAGE(module.imageWriter.imageAccepted,
                              "an image that does not match its checksum was made bootable");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, countReports(module, "fw-kept", "Done"),
                                  "the refused image was reported to the server as installed");

    char logMessage[512];
    snprintf(logMessage, sizeof(logMessage), "OTA log of the resumed update: %s",
             otaLogText(module).c_str());
    TEST_ASSERT_TRUE_MESSAGE(
        module.otaLog.hasLineContaining("ERROR: image rejected: checksum md5="), logMessage);
    TEST_ASSERT_TRUE_MESSAGE(module.otaLog.hasLineContaining(CHECKSUM_IMAGE_MD5), logMessage);
}

// --------------------------------------------------------------------------------------------
// 14. The order between the two halves of a recoverable update: a boot that finds a kept update
//     while its own image is still on probation must not start it. The download writes the other
//     app slot, and that slot is where the image this one falls back to is sitting - so starting
//     now would spend the fallback on a boot that has not yet shown it can work. The update
//     waits, the control loop confirms the image the way it always does, and the update starts
//     then, through the pipeline that carries every other command.
// --------------------------------------------------------------------------------------------

static void test_ota_a_kept_update_waits_while_the_running_image_is_on_probation(void)
{
    ModuleUnderTest module;

    // The boot after an update: this image runs on probation, and the other slot holds the
    // firmware the bootloader returns to if it is never confirmed.
    module.otaPartitionControl.runningState = OtaImageState::PENDING_VERIFY;
    module.otaPartitionControl.otherState = OtaImageState::VALID;
    // The structure the setup flow fills; the confirmation counts iterations only after it is read.
    module.moduleStructure.growdirectorUid = MODULE_UID;

    // The update the previous boot could not download, as it left it on the file system.
    ModuleCommandDto kept = makeUpdateFirmwareCommandWithMd5("fw-deferred", CHECKSUM_IMAGE_MD5);
    kept.createdAt = "";
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(kept),
                             "the scenario could not put an update on the file system");
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(30000);

    // Nothing was downloaded, and the fallback slot is untouched.
    TEST_ASSERT_TRUE_MESSAGE(module.bootCommandsAsked,
                             "the boot never asked for its commands - the scenario did not happen");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls,
                                  "the update wrote the slot holding the fallback image while "
                                  "this image was still on probation");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART,
                                  module.moduleConfig.getRequestedModuleMode(),
                                  "a restart was requested for an update that never ran");

    // And the update is still on the file system: a reset now - which on a probation image is a
    // rollback - finds it again on the image it lands on.
    ModuleCommandDto stillKept;
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.load(stillKept),
                             "the held update was dropped instead of kept");
    TEST_ASSERT_EQUAL_STRING("fw-deferred", stillKept.uid.c_str());

    // The control loop confirms the image the way it always does: uptime past the watchdog
    // threshold and a run of healthy iterations.
    module.scheduler.runForMs((uint32_t)WDT_TIMEOUT_S * 1000 + 30000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.otaPartitionControl.markValidCalls,
                                  "the loop never confirmed the running image");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls,
                                  "the update started before the confirmation was recorded");

    // The bootloader's half of that confirmation: the slot state the stand-in deliberately does
    // not change by itself, since it records the call rather than imitating what it does.
    module.otaPartitionControl.runningState = OtaImageState::VALID;

    module.scheduler.runForMs(60000);

    // The update runs now, on the network task, and reaches the server as its own outcome.
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the update did not start after the image was confirmed");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted,
                             "the resumed update did not install the image");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-deferred", "Done"),
                                 "the server never saw the outcome of the resumed update");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the installed image was never asked to be booted");

    // Taken once: the file is empty, so the next boot does not run it again.
    ModuleCommandDto leftOver;
    TEST_ASSERT_FALSE_MESSAGE(module.pendingUpdateStore.load(leftOver),
                              "the update stayed on the file system after it was carried out");

    assertBootRunIsSound(module);
}

// --------------------------------------------------------------------------------------------
// 15. The other door into the same boot. An update the module holds back is one thing; the one
//     the server offers over the same probation boot is another, and it lands in the same slot.
//     It is held by the same mechanism and answered "in progress", so the platform keeps the
//     session open instead of being told a failure that did not happen.
// --------------------------------------------------------------------------------------------

static void test_ota_an_update_from_the_server_waits_for_the_image_on_probation_too(void)
{
    ModuleUnderTest module;

    module.otaPartitionControl.runningState = OtaImageState::PENDING_VERIFY;
    module.otaPartitionControl.otherState = OtaImageState::VALID;
    module.moduleStructure.growdirectorUid = MODULE_UID;

    // Nothing kept on the file system this time: the update comes from the server, on the boot
    // that has just decided not to update itself.
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-server", CHECKSUM_IMAGE_MD5));
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(30000);

    TEST_ASSERT_TRUE_MESSAGE(module.bootCommandsAsked,
                             "the boot never asked for its commands - the scenario did not happen");
    TEST_ASSERT_TRUE_MESSAGE(module.server.getModuleCommandCalls >= 1,
                             "the server was never asked - the scenario did not happen");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls,
                                  "the update the server offered was downloaded into the slot "
                                  "holding the fallback image, on a boot still on probation");

    // What the platform is told: still running, not failed - the module has taken it.
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-server", "InProgress"),
                                 "the server was not told the update is still running");
    TEST_ASSERT_NULL_MESSAGE(findReport(module, "fw-server", "Error"),
                             "a failure was reported for an update that was merely postponed");

    // And it is on the file system, so a reset before the confirmation - a rollback - finds it.
    ModuleCommandDto held;
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.load(held),
                             "the postponed update was kept in memory alone");
    TEST_ASSERT_EQUAL_STRING("fw-server", held.uid.c_str());

    // The loop confirms the image, and the update runs then.
    module.scheduler.runForMs((uint32_t)WDT_TIMEOUT_S * 1000 + 30000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.otaPartitionControl.markValidCalls,
                                  "the loop never confirmed the running image");
    module.otaPartitionControl.runningState = OtaImageState::VALID;
    module.scheduler.runForMs(60000);

    assertBootRunIsSound(module);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the postponed update did not start after the confirmation");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted,
                             "the postponed update did not install the image");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-server", "Done"),
                                 "the server never saw the outcome of the postponed update");
}

// --------------------------------------------------------------------------------------------
// 16. Both doors at once, on the same command. The update was answered "in progress" before the
//     restart, so the platform keeps offering it; the module must recognise the command it is
//     already carrying and run it once, not once from the server and once from the file.
// --------------------------------------------------------------------------------------------

static void test_ota_an_update_offered_again_by_the_server_is_carried_out_once(void)
{
    ModuleUnderTest module;

    module.otaPartitionControl.runningState = OtaImageState::PENDING_VERIFY;
    module.otaPartitionControl.otherState = OtaImageState::VALID;
    module.moduleStructure.growdirectorUid = MODULE_UID;

    // The update the previous boot kept, and the very same command still open on the server.
    ModuleCommandDto kept = makeUpdateFirmwareCommandWithMd5("fw-same", CHECKSUM_IMAGE_MD5);
    kept.createdAt = "";
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(kept),
                             "the scenario could not put an update on the file system");
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-same", CHECKSUM_IMAGE_MD5));
    // Two downloads are on offer, so that a module that takes the command twice really can run
    // it twice: with one queued, the second attempt would fail on the server and hide the fault.
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(30000);

    module.scheduler.runForMs((uint32_t)WDT_TIMEOUT_S * 1000 + 30000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.otaPartitionControl.markValidCalls,
                                  "the loop never confirmed the running image");
    module.otaPartitionControl.runningState = OtaImageState::VALID;
    module.scheduler.runForMs(60000);

    assertBootRunIsSound(module);

    // One command, one download, one outcome - whichever door it came through.
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the same update was carried out twice: once from the server "
                                  "and once from the file it was kept in");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-same", "Done"),
                                  "the server saw more than one outcome for the same command");

    ModuleCommandDto leftOver;
    TEST_ASSERT_FALSE_MESSAGE(module.pendingUpdateStore.load(leftOver),
                              "the update stayed on the file system after it was carried out");
}

// --------------------------------------------------------------------------------------------
// 17. The last door of the family: an update arriving on a dead link while the image is on
//     probation and nothing is being held yet. The answer to a dead link is a restart - and a
//     restart of an unconfirmed image is a rollback, so the module would go back to the previous
//     firmware to perform an update it could have simply waited three minutes for. It waits.
// --------------------------------------------------------------------------------------------

static void test_ota_an_update_on_a_dead_link_does_not_restart_a_probation_image(void)
{
    ModuleUnderTest module;

    module.otaPartitionControl.runningState = OtaImageState::PENDING_VERIFY;
    module.otaPartitionControl.otherState = OtaImageState::VALID;
    module.moduleStructure.growdirectorUid = MODULE_UID;

    // Nothing is held: the file system is empty, and the update arrives the ordinary way - a
    // signal over MQTT - onto a module that reports its HTTP link as dead.
    module.networkStatus.httpDown = true;
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-dead-link", CHECKSUM_IMAGE_MD5));
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(30000);

    // No restart was asked for, and no boot type was armed for one: this image has not been
    // confirmed, so the restart would have been a rollback.
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART,
                                  module.moduleConfig.getRequestedModuleMode(),
                                  "a restart was requested on an image still on probation - the "
                                  "module would have rolled back to perform the update");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(BOOT_TYPE_FIRMWARE_UPDATE, module.settings.getBootType(),
                                  "the next boot was armed as a firmware update on an image "
                                  "that would not have survived the restart");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls,
                                  "the update was downloaded on a dead link");

    // The platform is told the same thing it is told on the recovery path: still running.
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-dead-link", "InProgress"),
                                 "the server was not told the update is still running");
    ModuleCommandDto held;
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.load(held),
                             "the update was not kept on the file system");
    TEST_ASSERT_EQUAL_STRING("fw-dead-link", held.uid.c_str());

    // Three minutes later the loop confirms the image and the link is usable again.
    module.scheduler.runForMs((uint32_t)WDT_TIMEOUT_S * 1000 + 30000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.otaPartitionControl.markValidCalls,
                                  "the loop never confirmed the running image");
    module.otaPartitionControl.runningState = OtaImageState::VALID;
    module.networkStatus.httpDown = false;
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the update did not start after the image was confirmed");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted,
                             "the update did not install the image");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-dead-link", "Done"),
                                 "the server never saw the outcome of the update");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the installed image was never asked to be booted");
}

// --------------------------------------------------------------------------------------------
// 18. The fourth door, and the ordinary one: a command signalled over MQTT on a link that works,
//     while the image is on probation. Nothing about a healthy link makes the fallback slot any
//     less occupied, so this update waits like the others.
// --------------------------------------------------------------------------------------------

static void test_ota_a_signalled_update_waits_for_the_image_on_probation_too(void)
{
    ModuleUnderTest module;

    module.otaPartitionControl.runningState = OtaImageState::PENDING_VERIFY;
    module.otaPartitionControl.otherState = OtaImageState::VALID;
    module.moduleStructure.growdirectorUid = MODULE_UID;

    // The link is fine and the file system is empty: nothing here except the probation.
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-signalled", CHECKSUM_IMAGE_MD5));
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(30000);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls,
                                  "a signalled update was downloaded into the slot holding the "
                                  "fallback image, on a boot still on probation");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-signalled", "InProgress"),
                                 "the server was not told the update is still running");
    TEST_ASSERT_NULL_MESSAGE(findReport(module, "fw-signalled", "Error"),
                             "a failure was reported for an update that was merely postponed");

    ModuleCommandDto held;
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.load(held),
                             "the postponed update was kept in memory alone");
    TEST_ASSERT_EQUAL_STRING("fw-signalled", held.uid.c_str());

    module.scheduler.runForMs((uint32_t)WDT_TIMEOUT_S * 1000 + 30000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.otaPartitionControl.markValidCalls,
                                  "the loop never confirmed the running image");
    module.otaPartitionControl.runningState = OtaImageState::VALID;
    module.scheduler.runForMs(60000);

    assertRunIsSound(module);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the postponed update did not start after the confirmation");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted,
                             "the postponed update did not install the image");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-signalled", "Done"),
                                 "the server never saw the outcome of the postponed update");
}

// --------------------------------------------------------------------------------------------
// 19. The same command through two doors at once: held from the file, and signalled again over
//     a healthy link inside the probation window. The module has to recognise the update it is
//     already carrying - by its uid - and answer "still running" instead of starting a second
//     copy of it.
// --------------------------------------------------------------------------------------------

static void test_ota_the_same_update_signalled_again_is_still_carried_out_once(void)
{
    ModuleUnderTest module;

    module.otaPartitionControl.runningState = OtaImageState::PENDING_VERIFY;
    module.otaPartitionControl.otherState = OtaImageState::VALID;
    module.moduleStructure.growdirectorUid = MODULE_UID;

    ModuleCommandDto kept = makeUpdateFirmwareCommandWithMd5("fw-twice", CHECKSUM_IMAGE_MD5);
    kept.createdAt = "";
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(kept),
                             "the scenario could not put an update on the file system");
    // Two downloads on offer, so that a module taking the command twice really can run it twice.
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(30000);

    // The platform still has the command open and offers it again, over a link that works.
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-twice", CHECKSUM_IMAGE_MD5));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(30000);

    // Every claim of this scenario is counted at the end, when both doors have had their turn:
    // whether anything was downloaded during the probation is the previous scenario's business.
    module.scheduler.runForMs((uint32_t)WDT_TIMEOUT_S * 1000 + 30000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.otaPartitionControl.markValidCalls,
                                  "the loop never confirmed the running image");
    module.otaPartitionControl.runningState = OtaImageState::VALID;
    module.scheduler.runForMs(60000);

    assertBootRunIsSound(module);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the same update was carried out twice: once from the signal "
                                  "and once from the file it was held in");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-twice", "Done"),
                                  "the server saw more than one outcome for the same command");

    // Recognised as the update already being carried, rather than taken on as a second one:
    // answered "still running" twice - the acknowledgement every accepted command gets on this
    // path, and the outcome of the step that recognised the uid - and never refused.
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, countReports(module, "fw-twice", "InProgress"),
                                  "the update offered again was not answered as still running");
    TEST_ASSERT_NULL_MESSAGE(findReport(module, "fw-twice", "Error"),
                             "the update the module is already carrying was refused as a stranger");
}

// --------------------------------------------------------------------------------------------
// 20. And a different command in the same window. One update is held per boot, so this one is
//     not taken - and saying so matters: an update answered "still running" that the module
//     never intends to carry out would leave the platform waiting for an outcome forever.
// --------------------------------------------------------------------------------------------

static void test_ota_a_different_update_is_refused_while_one_is_held(void)
{
    ModuleUnderTest module;

    module.otaPartitionControl.runningState = OtaImageState::PENDING_VERIFY;
    module.otaPartitionControl.otherState = OtaImageState::VALID;
    module.moduleStructure.growdirectorUid = MODULE_UID;

    ModuleCommandDto kept = makeUpdateFirmwareCommandWithMd5("fw-held", CHECKSUM_IMAGE_MD5);
    kept.createdAt = "";
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(kept),
                             "the scenario could not put an update on the file system");
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(30000);

    // A second, different update is signalled while the first one waits.
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-other", CHECKSUM_IMAGE_MD5));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(30000);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls,
                                  "a second update was downloaded on a boot still on probation");
    // The outcome, not the acknowledgement: this path acknowledges every accepted command with
    // an InProgress before it executes it, and what matters here is what it says afterwards.
    // An update answered "still running" and then never carried out would leave the platform
    // waiting for an outcome that never comes.
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-other", "Error"),
                                 "the update that was not taken was left without an outcome");

    // The held one is untouched by all this, and it is the one that runs.
    ModuleCommandDto stillHeld;
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.load(stillHeld),
                             "the held update was dropped");
    TEST_ASSERT_EQUAL_STRING("fw-held", stillHeld.uid.c_str());

    module.scheduler.runForMs((uint32_t)WDT_TIMEOUT_S * 1000 + 30000);
    module.otaPartitionControl.runningState = OtaImageState::VALID;
    module.scheduler.runForMs(60000);

    assertBootRunIsSound(module);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the held update did not start, or the refused one did");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-held", "Done"),
                                  "the held update was not carried out exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, countReports(module, "fw-other", "Done"),
                                  "an update that was refused was carried out anyway");
}

// --------------------------------------------------------------------------------------------
// 21. What the wait costs the update itself. Waiting spends no restart, so an update that comes
//     off the wait onto a link that still will not open must get the restart their recovery path
//     exists to give it - by then the image is confirmed, so that restart is no longer a
//     rollback - instead of burning as an error the platform can do nothing with.
// --------------------------------------------------------------------------------------------

static void test_ota_a_resumed_update_that_still_cannot_connect_is_kept_again(void)
{
    ModuleUnderTest module;

    module.otaPartitionControl.runningState = OtaImageState::PENDING_VERIFY;
    module.otaPartitionControl.otherState = OtaImageState::VALID;
    module.moduleStructure.growdirectorUid = MODULE_UID;

    ModuleCommandDto kept = makeUpdateFirmwareCommandWithMd5("fw-still-dark", CHECKSUM_IMAGE_MD5);
    kept.createdAt = "";
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(kept),
                             "the scenario could not put an update on the file system");

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(30000);

    // The image is confirmed, but the link the update was waiting for never came back.
    module.networkStatus.httpDown = true;
    module.scheduler.runForMs((uint32_t)WDT_TIMEOUT_S * 1000 + 30000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.otaPartitionControl.markValidCalls,
                                  "the loop never confirmed the running image");
    module.otaPartitionControl.runningState = OtaImageState::VALID;
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.bootCommandsAsked, "the boot never asked for its commands");

    // Not burned: no error went out, the update is on the file system again, and the restart it
    // is owed was asked for - the image is confirmed now, so it costs nothing to take it.
    TEST_ASSERT_NULL_MESSAGE(findReport(module, "fw-still-dark", "Error"),
                             "the update that waited was spent as an error instead of being kept");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls,
                                  "a download was attempted on a link reported as down");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-still-dark", "InProgress"),
                                 "the server was not told the update is still running");

    ModuleCommandDto keptAgain;
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.load(keptAgain),
                             "the update was not kept for the next boot");
    TEST_ASSERT_EQUAL_STRING("fw-still-dark", keptAgain.uid.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the restart that brings the network stack back was not requested");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(BOOT_TYPE_FIRMWARE_UPDATE, module.settings.getBootType(),
                                    "the next boot was not armed as a firmware update");
}

// --------------------------------------------------------------------------------------------
// 22. The boot's own doors, on a confirmed image. Once the boot's first structure has settled the
//     module carries out the update the previous boot kept, and asks the server for the commands
//     left for it while it was off - both through the pipeline every command takes, so the download
//     runs on the network task and its outcome reaches the server as a batch.
// --------------------------------------------------------------------------------------------

static void test_ota_a_kept_update_is_carried_out_once_the_boot_asks_for_its_commands(void)
{
    ModuleUnderTest module;
    ModuleCommandDto kept = makeUpdateFirmwareCommandWithMd5("fw-kept-boot", CHECKSUM_IMAGE_MD5);
    kept.createdAt = "";
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(kept),
                             "the scenario could not put an update on the file system");
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(60000);

    assertBootRunIsSound(module);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls, "the kept update was not downloaded once");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted, "the kept update did not install the image");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-kept-boot", "Done"),
                                  "the server did not see the kept update's outcome once");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                              "the installed image was never asked to be booted");
    ModuleCommandDto leftOver;
    TEST_ASSERT_FALSE_MESSAGE(module.pendingUpdateStore.load(leftOver),
                              "the update stayed on the file system after it was carried out");
}

// The one restart a kept update is worth is spent: resumed onto a link that still does not open, it
// ends as the error it is, with the reason, and asks for no second restart.
static void test_ota_a_kept_update_that_still_cannot_connect_stops_at_one_restart(void)
{
    ModuleUnderTest module;
    ModuleCommandDto kept = makeUpdateFirmwareCommandWithMd5("fw-kept-dark", CHECKSUM_IMAGE_MD5);
    kept.createdAt = "";
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(kept),
                             "the scenario could not put an update on the file system");
    // No download queued: the connection does not open (HTTP code -1).

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_TRUE_MESSAGE(module.bootCommandsAsked, "the boot never asked for its commands");
    const ModuleCommandUpdateDto* error = findReport(module, "fw-kept-dark", "Error");
    TEST_ASSERT_NOT_NULL_MESSAGE(error, "the server was not told the resumed update failed");
    TEST_ASSERT_EQUAL_STRING("Failed to start firmware update: HTTP code=-1", error->errorMessage.c_str());
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                                  "a second restart was asked for the same update");
    ModuleCommandDto leftOver;
    TEST_ASSERT_FALSE_MESSAGE(module.pendingUpdateStore.load(leftOver),
                              "the update was kept again for a third boot");
}

// The update the server offers on the boot's list is carried out like one that came on a signal.
static void test_ota_the_boots_command_fetch_carries_out_an_update_the_server_offers(void)
{
    ModuleUnderTest module;
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-server-boot", CHECKSUM_IMAGE_MD5));
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(60000);

    assertBootRunIsSound(module);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.server.getModuleCommandCalls,
                                     "the boot did not ask the server for its commands exactly once");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-server-boot", "InProgress"),
                                 "the server was not told the update was taken on");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-server-boot", "Done"),
                                  "the server did not see the update's outcome once");
    TEST_ASSERT_TRUE_MESSAGE(module.imageWriter.imageAccepted, "the offered update did not install the image");
}

// --------------------------------------------------------------------------------------------
// 23. The rule behind the boot's doors: a command whose download this boot has started is not
//     carried out again when it comes back - by a list the server keeps answering while no status
//     for it has arrived, or by a notification the broker keeps handing out - and the server is
//     told nothing more about it. A different command is carried out.
// --------------------------------------------------------------------------------------------

static void test_ota_a_command_whose_download_this_boot_started_is_not_carried_out_again(void)
{
    ModuleUnderTest module;
    // Downloads for three runs, so that a module that takes a command again really can run it again.
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-first", CHECKSUM_IMAGE_MD5));

    module.startArduinoLoopTask();
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(60000);

    assertBootRunIsSound(module);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls, "the premise: the command was downloaded once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-first", "InProgress"), "the premise: the command was taken on once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-first", "Done"), "the premise: the command's outcome was reported once");

    // The command comes back: on the list the server answers the broker's notification with.
    module.debugSystem.featureEnabled = true;
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-first", CHECKSUM_IMAGE_MD5));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(30000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, module.server.getModuleCommandCalls,
                                     "the premise: the notification made the module ask the server again");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "a command whose download this boot started was downloaded again");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-first", "InProgress"),
                                  "the server was told the update is in progress a second time");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-first", "Done"),
                                  "the server saw a second outcome for the same command");
    TEST_ASSERT_NULL_MESSAGE(findReport(module, "fw-first", "Error"),
                             "the command was refused as a stranger to the update it is");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countLogLines(module, "firmware update uid=fw-first was started on this boot already"),
                                  "the log does not say once that the command was not carried out again");

    // A different command is carried out.
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-second", CHECKSUM_IMAGE_MD5));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(60000);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, module.imageWriter.beginCalls, "a different command was not downloaded");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-second", "Done"),
                                  "the server did not see the different command's outcome once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-first", "Done"),
                                  "the first command's outcome was reported again with the second one");
}

// The other order of the same two doors: the server's list started the update first, and the boot then
// finds the same command in the file a restart kept it in. The file is emptied, and nothing is downloaded
// or reported a second time.
static void test_ota_an_update_the_server_started_is_not_started_again_from_the_file_of_the_restart(void)
{
    ModuleUnderTest module;
    ModuleCommandDto kept = makeUpdateFirmwareCommandWithMd5("fw-late-file", CHECKSUM_IMAGE_MD5);
    kept.createdAt = "";
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(kept),
                             "the scenario could not put an update on the file system");
    // Two downloads on offer, so that a module that takes the command twice really can run it twice.
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 16384);
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 16384);
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-late-file", CHECKSUM_IMAGE_MD5));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(5000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the premise: the notification's list started the download");

    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(60000);

    TEST_ASSERT_TRUE_MESSAGE(module.bootCommandsAsked,
                             "the boot never asked for its commands - the scenario did not happen");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the update the list started was downloaded again from the file of the restart");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-late-file", "Done"),
                                  "the server did not see the update's outcome once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-late-file", "InProgress"),
                                  "the server was told the update is in progress more than once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, countAllReports(module),
                                  "the server was told something besides the update's two statuses");
    ModuleCommandDto leftOver;
    TEST_ASSERT_FALSE_MESSAGE(module.pendingUpdateStore.load(leftOver),
                              "the update stayed on the file system after it was carried out");
}

// The same two doors with the HTTP link gone in between: the download the list started is still running when
// the file of the restart hands the same update over. It is recognised before it is validated - validating it
// on a dead link would keep it in the file again and ask for a restart in the middle of the download that is
// running.
static void test_ota_the_same_update_from_the_file_on_a_dead_link_does_not_restart_a_running_download(void)
{
    ModuleUnderTest module;
    // A download of 32 s, so that the file's turn comes while it is running.
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 8192);
    module.server.pendingModuleCommands.push_back(
        makeUpdateFirmwareCommandWithMd5("fw-running", CHECKSUM_IMAGE_MD5));

    module.startArduinoLoopTask();
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(5000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the premise: the notification's list started the download");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, countReports(module, "fw-running", "Done"),
                                  "the premise: the download was still running");

    // The HTTP link goes, and the boot finds the same command in the file.
    module.networkStatus.httpDown = true;
    ModuleCommandDto kept = makeUpdateFirmwareCommandWithMd5("fw-running", CHECKSUM_IMAGE_MD5);
    kept.createdAt = "";
    TEST_ASSERT_TRUE_MESSAGE(module.pendingUpdateStore.save(kept),
                             "the scenario could not put an update on the file system");
    module.settleTheBootsFirstStructure();
    module.scheduler.runForMs(2000);

    TEST_ASSERT_TRUE_MESSAGE(module.bootCommandsAsked,
                             "the boot never asked for its commands - the scenario did not happen");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, countReports(module, "fw-running", "Done"),
                                  "the premise: the download was still running when the file's turn came");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CONFIGURED_NEED_RESTART, module.moduleConfig.getRequestedModuleMode(),
                                  "a restart was requested in the middle of the download that is running");
    ModuleCommandDto leftOver;
    TEST_ASSERT_FALSE_MESSAGE(module.pendingUpdateStore.load(leftOver),
                              "the update the list is downloading was kept in the file again");

    module.networkStatus.httpDown = false;
    module.scheduler.runForMs(60000);
    assertBootRunIsSound(module);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "the update the list started was downloaded again");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-running", "Done"),
                                  "the server did not see the update's outcome once");
}

// What the module remembers is bounded, and a full memory does not refuse a new update: five different
// commands are each carried out, and the latest of them is still recognised when it comes back.
static void test_ota_the_memory_of_started_updates_is_bounded_and_keeps_the_latest(void)
{
    ModuleUnderTest module;
    const char* const uids[5] = {"fw-m1", "fw-m2", "fw-m3", "fw-m4", "fw-m5"};
    for (int i = 0; i < 6; i++)
    {
        module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    }

    module.startArduinoLoopTask();
    for (int i = 0; i < 5; i++)
    {
        module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommandWithMd5(uids[i], CHECKSUM_IMAGE_MD5));
        module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
        module.scheduler.runForMs(15000);
        TEST_ASSERT_EQUAL_INT_MESSAGE(i + 1, module.imageWriter.beginCalls,
                                      "a different update was not downloaded");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, uids[i], "Done"),
                                      "the server did not see the update's outcome once");
    }

    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommandWithMd5(uids[4], CHECKSUM_IMAGE_MD5));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(15000);

    TEST_ASSERT_EQUAL_INT_MESSAGE(5, module.imageWriter.beginCalls,
                                  "the latest update was downloaded again after four others had been");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, uids[4], "Done"),
                                  "the latest update's outcome was reported again");
}

// A command that carries no uid cannot be told from another one, so nothing is remembered about it: each
// is carried out.
static void test_ota_commands_without_a_uid_are_each_carried_out(void)
{
    ModuleUnderTest module;
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    for (int i = 0; i < 2; i++)
    {
        module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommandWithMd5("", CHECKSUM_IMAGE_MD5));
        module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
        module.scheduler.runForMs(15000);
        TEST_ASSERT_EQUAL_INT_MESSAGE(i + 1, module.imageWriter.beginCalls,
                                      "an update command without a uid was taken for one already carried out");
    }
}

// A command the network task never took did not start a download on this boot: offered again, it is
// carried out.
static void test_ota_a_command_the_network_task_refused_is_carried_out_when_offered_again(void)
{
    ModuleUnderTest module;
    RefusesTheFirstCommandRunner refusing(module.networkRunner);
    module.commandService.setNetworkCommandRunner(&refusing);
    module.otaHttp.queueHealthyDownload(CHECKSUM_IMAGE_BYTES, 1048576);

    module.startArduinoLoopTask();
    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommandWithMd5("fw-refused", CHECKSUM_IMAGE_MD5));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(15000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.imageWriter.beginCalls, "the premise: the refused command was downloaded");
    TEST_ASSERT_NOT_NULL_MESSAGE(findReport(module, "fw-refused", "Error"),
                                 "the premise: the server was not told the command was refused");

    module.server.pendingModuleCommands.push_back(makeUpdateFirmwareCommandWithMd5("fw-refused", CHECKSUM_IMAGE_MD5));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware"));
    module.scheduler.runForMs(15000);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, module.imageWriter.beginCalls,
                                  "a command whose download never started was taken for one already started");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countReports(module, "fw-refused", "Done"),
                                  "the server did not see the update's outcome once");
}

extern "C" void run_ota_image_checksum_tests(void)
{
    printf("\n----- Running OTA Image Checksum Tests -----\n");

    RUN_TEST(test_ota_the_checksum_field_survives_the_module_command_parse);
    RUN_TEST(test_ota_an_image_that_does_not_match_the_command_checksum_is_refused);
    RUN_TEST(test_ota_an_image_that_matches_the_command_checksum_is_installed);
    RUN_TEST(test_ota_a_checksum_in_upper_case_still_matches_the_image);
    RUN_TEST(test_ota_a_command_without_a_checksum_installs_as_before);
    RUN_TEST(test_ota_a_checksum_that_is_not_a_digest_stops_the_command_before_the_download);
    RUN_TEST(test_ota_a_writer_that_refuses_the_checksum_stops_the_session);
    RUN_TEST(test_ota_a_kept_update_is_still_checked_against_its_checksum_after_the_restart);
    RUN_TEST(test_ota_a_kept_update_waits_while_the_running_image_is_on_probation);
    RUN_TEST(test_ota_an_update_from_the_server_waits_for_the_image_on_probation_too);
    RUN_TEST(test_ota_an_update_offered_again_by_the_server_is_carried_out_once);
    RUN_TEST(test_ota_an_update_on_a_dead_link_does_not_restart_a_probation_image);
    RUN_TEST(test_ota_a_signalled_update_waits_for_the_image_on_probation_too);
    RUN_TEST(test_ota_the_same_update_signalled_again_is_still_carried_out_once);
    RUN_TEST(test_ota_a_different_update_is_refused_while_one_is_held);
    RUN_TEST(test_ota_a_resumed_update_that_still_cannot_connect_is_kept_again);
    RUN_TEST(test_ota_a_kept_update_is_carried_out_once_the_boot_asks_for_its_commands);
    RUN_TEST(test_ota_a_kept_update_that_still_cannot_connect_stops_at_one_restart);
    RUN_TEST(test_ota_the_boots_command_fetch_carries_out_an_update_the_server_offers);
    RUN_TEST(test_ota_a_command_whose_download_this_boot_started_is_not_carried_out_again);
    RUN_TEST(test_ota_an_update_the_server_started_is_not_started_again_from_the_file_of_the_restart);
    RUN_TEST(test_ota_the_same_update_from_the_file_on_a_dead_link_does_not_restart_a_running_download);
    RUN_TEST(test_ota_the_memory_of_started_updates_is_bounded_and_keeps_the_latest);
    RUN_TEST(test_ota_commands_without_a_uid_are_each_carried_out);
    RUN_TEST(test_ota_a_command_the_network_task_refused_is_carried_out_when_offered_again);

    printf("\n----- OTA Image Checksum Tests Complete -----\n");
}
