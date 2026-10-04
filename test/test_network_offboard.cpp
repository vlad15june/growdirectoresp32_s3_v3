// The network off the control loop, on one task, with the link's jobs in the same queue.
//
// A device run this file answers had two readings. A WiFi link dropped after eleven minutes up;
// eighteen seconds later the diagnostic pass found it down and restarted the radio - on the
// control loop, which stood still for the seven seconds the association took (max_loop_step_ms
// 7521). One second after the link was back the free heap went from sixty-odd thousand to 4 300:
// the TLS handshake and the diagnostic body on the network task, the MQTT socket being torn down
// and rebuilt on the client's own task, the message the broker had held being parsed on the
// control loop and the discovery's pass on the acquisition task - all at once, and the heap has to
// hold the sum. And during the eighteen seconds down, a telemetry request spent its four attempts
// on a link that was not there.
//
// Three changes, one shape: the radio restart is a job of the outbox, served before any request;
// the MQTT connect state machine is beaten by the outbox's network task between its requests and
// holds them while an act is in progress; and no request is attempted while the link is down. The
// control loop only decides. With every network participant on one task the heap holds the
// largest of them, not their sum - by construction, not by a schedule.
//
// What is under test is production code, wired the way globals.h wires it: the real
// LoopServiceConfigureDone driving the real LinkSupervisor, DiagnosticService and CommandService, the
// real Outbox on its own network task with the real MqttManager as its link machine, the real
// MessageProcessor, TopicHandlerFactory and CommandTopicHandler, the real ArduinoJsonConverter,
// StartupSensorScanUseCase and SyncRulesUseCase. Replaced are the outside world and the board:
// the server (a stand-in behind IHybridAPIService), the broker (Sim/SimMqttBroker.h behind the
// host AsyncMqttClient), the radio (a stand-in whose restart blocks its caller for the seven
// seconds the recorded one took), the RS-485 line, the storage DAOs, the scheduler, the clock -
// and the heap, which is a number the participants take from and give back to (SimHeap below).
//
// One figure is measured: the handshake's 13 416 bytes, the module's own TLS churn meter for that
// very handshake. The rest are a model, and a conservative one. What the tests prove does not
// turn on the numbers: with the participants on separate tasks the floor is their sum, on one
// task it is the largest of them.
//
// ### Reproducing the red
//
// The storm runs one stand in two shapes. RECONNECT_ON_THE_LOOP is the wiring with the
// reconnect left on the loop: the MQTT machine beaten by a task of its own at its own cadence, and the
// radio restart made on the task that decided it - the control loop. ONE_NETWORK_TASK is
// globals.h. The first shape is the red: it asserts the loop stood still for the restart and the
// floor was the sum. The smaller stands below each name the mutation that turns them red.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "constants.h"

#include "Application/Services/CommandService.h"
#include "Application/Services/DiagnosticService.h"
#include "Application/Services/Net/LinkSupervisor.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/Net/Outbox.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Rules/SyncRulesUseCase.h"
#include "Core/Domain/UseCases/Sensors/ModbusAddressAllocator.h"
#include "Core/Domain/UseCases/Sensors/StartupSensorScanUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Controllers/MQTT/MessageProcessor.h"
#include "Infrastructure/Controllers/MQTT/MqttManager.h"
#include "Infrastructure/Repositories/RuleRepository.h"

#include "Mock/Application/MockDiagnosticService.h"
#include "Mock/Core/Domain/Adapters/Sensors/MockSensorReader.h"
#include "Mock/Core/Domain/Models/MockSensorValueModel.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Logging/MockDebugSystem.h"
#include "Mock/Infrastructure/Logging/MockLogger.h"
#include "Mock/Infrastructure/MockArduinoAdapter.h"
#include "Mock/Infrastructure/Mqtt/MockMqttManager.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimLinkProbe.h"
#include "Sim/SimMqttBroker.h"
#include "Sim/SimTaskWatchdog.h"
#include "Sim/VirtualClock.h"

#include "freertos/task.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <vector>

using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimMqttBroker;
using hostsim::SimTaskWatchdog;
using hostsim::VirtualClock;

namespace {

const char *const MODULE_UID = "AA:BB:CC:DD:EE:75";
const char *const CLIENT_ID_PREFIX = "GrowDirectorWifiClient";
const char *const NETWORK_TASK = "outbox-net";
const char *const CONTROL_TASK = "control";

std::string clientId() { return std::string(CLIENT_ID_PREFIX) + MODULE_UID; }
std::string commandTopic() { return std::string("GrowDirectorMqttTopic/") + MODULE_UID; }

// --- what a participant of a recovery costs the heap -------------------------------------------

/** The level the recorded run sat at between storms. */
const uint32_t STEADY_FREE_BYTES = 62000;

// Measured, and the only measured figure here: "TLS_CHURN: handshake=full n=6 dur_ms=1913
// peak=13416" for the handshake of that very storm. Charged whole to the transient.
const uint32_t TLS_HANDSHAKE_BYTES = 13416;
const uint32_t TLS_HANDSHAKE_MS = 1913;

// Modelled; see the note at the top of the file.
const uint32_t MQTT_SOCKET_BYTES = 9000;
const uint32_t DIAGNOSTIC_BODY_BYTES = 11000;
const uint32_t TELEMETRY_BODY_BYTES = 3000;
const uint32_t RULE_FETCH_BYTES = 7500;
const uint32_t COMMAND_FETCH_BYTES = 4000;
const uint32_t PUSH_PARSE_BYTES = 4784;
const uint32_t LADDER_PASS_BYTES = 12000;

/**
 * How long the control loop is busy with one inbound message: the parse and the dispatch of a
 * 156-byte notification, inside one iteration. Milliseconds on the board; here a span, so that
 * the memory it holds can meet the network task's.
 */
const uint32_t PUSH_PARSE_MS = 20;

/** How long a request body is on the network once the session is up. */
const uint32_t REQUEST_BODY_MS = 300;

/** The control loop's period, and the step it must never exceed (the S1/S6 figure). */
const uint32_t CONTROL_PERIOD_MS = 100;
const uint32_t MAX_ACCEPTED_STEP_MS = 50;

/** The beat of the MQTT client's own recovery task, in the shape that had one. */
const uint32_t MQTT_RECOVERY_PERIOD_MS = 200;

/** The diagnostic period LoopServiceConfigureDone keeps, and the sensor service's. */
const uint64_t DIAGNOSTIC_INTERVAL_MS = 30000;
const uint64_t TELEMETRY_PERIOD_MS = 15000;

/** The module's ports, and the models the discovery tries on the free ones. */
const int MODULE_PIN_COUNT = 4;
const int CATALOG_MODEL_COUNT = 3;

/**
 * The recorded storm's two spans: from the link dropping to the diagnostic pass that found it
 * down (@433.291 to @451.126), and from the restart to the association (@451.126 to @458.146).
 */
const uint64_t EDGE_TO_TICK_MS = 18000;
const uint32_t RADIO_RESTART_MS = 7020;

/**
 * What one WiFiMulti::run() takes at worst on the device: the blocking scan (WiFiScanClass waits
 * up to 10 s for the scan-done bit) and one association (connectTimeout, 5 s). The recorded
 * restart associated within its first.
 */
const uint32_t WIFI_MULTI_RUN_MS = 15000;

/** How long after the link returns the storm is watched. */
const uint64_t OBSERVATION_WINDOW_MS = 25000;

/** The network participants, by the names they charge the heap under. */
std::vector<std::string> networkParticipants()
{
    std::vector<std::string> names;
    names.push_back("tls-handshake");
    names.push_back("mqtt-socket");
    names.push_back("diagnostic");
    names.push_back("telemetry");
    names.push_back("rule-fetch");
    names.push_back("command-fetch");
    return names;
}

// --- shared fixtures ---------------------------------------------------------------------------

/** @brief Every log line with the moment it was written - which is what "in what order" means. */
class TapeLogger : public MockLogger
{
public:
    struct Line
    {
        uint64_t atMs;
        std::string text;
    };

    explicit TapeLogger(VirtualClock &clock) : _clock(clock) {}

    void println(const char *topic, const char *message) override { record(message); }
    void println(const char *topic, const char *message, MqttLoggerMode) override
    {
        record(message);
    }

    /** @brief When the first line containing this text was written, or -1 if there is none. */
    long long firstSaying(const char *needle, uint64_t fromMs = 0) const
    {
        for (size_t i = 0; i < tape.size(); i++)
        {
            if (tape[i].atMs >= fromMs && tape[i].text.find(needle) != std::string::npos)
            {
                return (long long)tape[i].atMs;
            }
        }
        return -1;
    }

    size_t countSaying(const char *needle, uint64_t fromMs = 0,
                       uint64_t toMs = (uint64_t)-1) const
    {
        size_t found = 0;
        for (size_t i = 0; i < tape.size(); i++)
        {
            if (tape[i].atMs >= fromMs && tape[i].atMs <= toMs &&
                tape[i].text.find(needle) != std::string::npos)
            {
                found++;
            }
        }
        return found;
    }

    std::vector<Line> tape;

private:
    void record(const char *message)
    {
        Line line;
        line.atMs = _clock.nowMs();
        line.text = message ? message : "";
        tape.push_back(line);
    }

    VirtualClock &_clock;
};

/** @brief A scannable ModBus model of the catalog the discovery walks. */
SensorType makeModbusType(int index)
{
    SensorType type;
    char uid[24];
    char name[24];
    snprintf(uid, sizeof(uid), "type-%d", index);
    snprintf(name, sizeof(name), "MODEL_%d", index);
    type.uid = uid;
    type.name = name;
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = true;
    type.isSupported = true;
    type.baudRate = 9600;
    type.modbusDeviceAddress = 1 + index;
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
 * @brief The radio, as far as a restart is concerned.
 *
 * reconnect() holds the calling task - through the scheduler, as WifiAdapter::reconnect() holds
 * its caller on the device - for the seconds the association takes. It runs one WiFiMulti::run()
 * after another until the association or the minute is up, and feeds the watchdog the way the real
 * loop does with esp_task_wdt_reset(): after a run() that failed with time left, and not after the
 * run() that ends the minute - that one is followed by a scan of what is in range, a blocking call
 * with no feed inside it. It records which task it ran on, which is the whole question of this file.
 */
class StormWifi : public MockWifiAdapter
{
public:
    struct Restart
    {
        uint64_t startedAtMs = 0;
        uint64_t finishedAtMs = 0;
        std::string task;
        bool associated = false;
    };

    StormWifi(HostTaskScheduler &scheduler, VirtualClock &clock)
        : _scheduler(scheduler), _clock(clock)
    {
    }

    uint32_t restartMs = RADIO_RESTART_MS;
    /** What one WiFiMulti::run() takes; the storm's radio associates within its first. */
    uint32_t attemptMs = WIFI_MULTI_RUN_MS;
    bool restartSucceeds = true;
    uint32_t scanAfterFailureMs = 0;

    std::vector<Restart> restarts;

    bool reconnect() override
    {
        Restart restart;
        restart.startedAtMs = _clock.nowMs();
        restart.task = _scheduler.currentTaskName();
        reconnectCalled = true;
        reconnectCallCount++;

        // The radio is off from here: whatever the link was, it is not there during the restart.
        connected = false;

        uint32_t elapsed = 0;
        while (elapsed < restartMs)
        {
            const uint32_t run = std::min(attemptMs, restartMs - elapsed);
            _scheduler.delayMs(run);
            elapsed += run;
            if (elapsed < restartMs)
            {
                _scheduler.watchdogFeed();
            }
        }

        if (!restartSucceeds && scanAfterFailureMs > 0)
        {
            _scheduler.delayMs(scanAfterFailureMs);
        }

        connected = restartSucceeds;
        restart.associated = connected;
        restart.finishedAtMs = _clock.nowMs();
        restarts.push_back(restart);
        return connected;
    }

private:
    HostTaskScheduler &_scheduler;
    VirtualClock &_clock;
};

/**
 * @brief The device heap, as a number the participants of a recovery take from and give back to.
 *
 * Not an allocator. What it models is the one property a floor is about: how much is held at once,
 * and by whom. Every change is written straight into SimArduino, so the module's own watermark
 * tracer reads it exactly as it reads the platform's.
 */
class SimHeap
{
public:
    struct Entry
    {
        uint64_t atMs;
        std::string who;
        bool taken;
        uint32_t bytes;
        uint32_t freeAfter;
    };

    SimHeap(VirtualClock &clock, SimArduino &arduino) : _clock(clock), _arduino(arduino)
    {
        publish();
    }

    void take(const char *who, uint32_t bytes)
    {
        _held += bytes;
        note(who, true, bytes);
    }

    void release(const char *who, uint32_t bytes)
    {
        _held -= bytes;
        note(who, false, bytes);
    }

    uint32_t freeBytes() const { return STEADY_FREE_BYTES - _held; }

    /** @brief The lowest the heap went inside a window, taking the level entering it as the start. */
    uint32_t floorBetween(uint64_t fromMs, uint64_t toMs) const
    {
        uint32_t floor = STEADY_FREE_BYTES;
        for (size_t i = 0; i < tape.size(); i++)
        {
            if (tape[i].atMs <= fromMs)
            {
                floor = tape[i].freeAfter;
                continue;
            }
            if (tape[i].atMs > toMs)
            {
                break;
            }
            floor = std::min(floor, tape[i].freeAfter);
        }
        return floor;
    }

    /**
     * @brief Did this participant hold bytes at any moment inside the window?
     *
     * A participant that took its bytes before the window and still held them on entry counts:
     * the discovery's pass begins a little before the link is back on purpose, and is a part of
     * the storm for the whole of it.
     */
    bool heldBytesBetween(const char *who, uint64_t fromMs, uint64_t toMs) const
    {
        bool holding = false;
        for (size_t i = 0; i < tape.size(); i++)
        {
            if (tape[i].who != who)
            {
                continue;
            }
            if (tape[i].atMs < fromMs)
            {
                holding = tape[i].taken;
                continue;
            }
            if (tape[i].atMs > toMs)
            {
                break;
            }
            if (holding || tape[i].taken)
            {
                return true;
            }
        }
        return holding;
    }

    long long firstTakeBetween(const char *who, uint64_t fromMs, uint64_t toMs) const
    {
        for (size_t i = 0; i < tape.size(); i++)
        {
            if (tape[i].taken && tape[i].atMs >= fromMs && tape[i].atMs <= toMs &&
                tape[i].who == who)
            {
                return (long long)tape[i].atMs;
            }
        }
        return -1;
    }

    /**
     * @brief The most of the named participants that held bytes at one moment inside the window.
     *
     * One is the claim of this file: the network participants never meet. The tape is replayed
     * from its start so that a participant holding bytes on entry to the window counts.
     */
    size_t mostHeldTogether(const std::vector<std::string> &who, uint64_t fromMs, uint64_t toMs) const
    {
        std::set<std::string> holding;
        size_t most = 0;
        for (size_t i = 0; i < tape.size(); i++)
        {
            if (std::find(who.begin(), who.end(), tape[i].who) == who.end())
            {
                continue;
            }
            if (tape[i].taken)
            {
                holding.insert(tape[i].who);
            }
            else
            {
                holding.erase(tape[i].who);
            }
            if (tape[i].atMs >= fromMs && tape[i].atMs <= toMs)
            {
                most = std::max(most, holding.size());
            }
        }
        return most;
    }

    void printTape(const char *title, uint64_t fromMs, uint64_t toMs) const
    {
        printf("  %s\n", title);
        for (size_t i = 0; i < tape.size(); i++)
        {
            if (tape[i].atMs < fromMs || tape[i].atMs > toMs)
            {
                continue;
            }
            printf("    +%6lld ms  %-14s %s%5u  free %6u\n",
                   (long long)tape[i].atMs - (long long)fromMs, tape[i].who.c_str(),
                   tape[i].taken ? "-" : "+", (unsigned)tape[i].bytes, (unsigned)tape[i].freeAfter);
        }
    }

    std::vector<Entry> tape;

private:
    void note(const char *who, bool taken, uint32_t bytes)
    {
        Entry entry;
        entry.atMs = _clock.nowMs();
        entry.who = (who != nullptr) ? who : "";
        entry.taken = taken;
        entry.bytes = bytes;
        entry.freeAfter = freeBytes();
        tape.push_back(entry);
        publish();
    }

    void publish()
    {
        _arduino.freeHeapValue = freeBytes();
        if (freeBytes() < _watermark)
        {
            _watermark = freeBytes();
        }
        _arduino.minFreeHeapValue = _watermark;
    }

    VirtualClock &_clock;
    SimArduino &_arduino;
    uint32_t _held = 0;
    uint32_t _watermark = STEADY_FREE_BYTES;
};

/**
 * @brief The far side of the REST calls: what each one costs the heap, and the session under them.
 *
 * The handshake is charged where the module pays for it - on the first call after the link came
 * back - and the body of the call on top of whatever the session costs. Both are spent on the
 * calling task in virtual time. A call made over a link that is down fails the way the recorded
 * ones did: quickly, on the DNS lookup, and without a handshake.
 */
class StormServer : public MockHybridAPIService
{
public:
    struct Call
    {
        std::string who;
        uint64_t startedAtMs = 0;
        uint64_t finishedAtMs = 0;
        bool answered = false;
    };

    StormServer(HostTaskScheduler &scheduler, SimHeap &heap, IWifiAdapter &wifi, VirtualClock &clock)
        : _scheduler(scheduler), _heap(heap), _wifi(wifi), _clock(clock)
    {
    }

    /** What a call over a link that is down costs before it fails: the DNS lookup of the record. */
    uint32_t failedCallMs = 300;

    bool sendDiagnosticData(const DiagnosticDto &, ApiPriority = ApiPriority::UNDEFINED) override
    {
        return call("diagnostic", DIAGNOSTIC_BODY_BYTES);
    }

    std::vector<Rule> getRuleList(bool *httpFailed = nullptr, bool *readWhole = nullptr) override
    {
        const bool answered = call("rule-fetch", RULE_FETCH_BYTES);
        if (httpFailed != nullptr)
        {
            *httpFailed = !answered;
        }
        if (readWhole != nullptr)
        {
            *readWhole = answered;
        }
        return std::vector<Rule>();
    }

    std::vector<DeviceCommandDto> getDeviceCommand(bool *httpFailed = nullptr) override
    {
        const bool answered = call("command-fetch", COMMAND_FETCH_BYTES);
        if (httpFailed != nullptr)
        {
            *httpFailed = !answered;
        }
        return std::vector<DeviceCommandDto>();
    }

    bool sendModuleUpdateData(const UpdateDto &, ApiPriority = ApiPriority::UNDEFINED) override
    {
        return call("telemetry", TELEMETRY_BODY_BYTES);
    }

    bool sendByUidRuleStateCommand(const char *, const char *) override
    {
        return call("telemetry", TELEMETRY_BODY_BYTES);
    }

    /** The session goes with the link, which is what makes the next call pay for a handshake. */
    void forgetTheSession() { _sessionWarm = false; }

    size_t callsTo(const char *who, uint64_t fromMs = 0, uint64_t toMs = (uint64_t)-1) const
    {
        size_t count = 0;
        for (size_t i = 0; i < calls.size(); i++)
        {
            if (calls[i].who == who && calls[i].startedAtMs >= fromMs && calls[i].startedAtMs <= toMs)
            {
                count++;
            }
        }
        return count;
    }

    long long firstCallTo(const char *who, uint64_t fromMs) const
    {
        for (size_t i = 0; i < calls.size(); i++)
        {
            if (calls[i].who == who && calls[i].startedAtMs >= fromMs)
            {
                return (long long)calls[i].startedAtMs;
            }
        }
        return -1;
    }

    std::vector<Call> calls;
    uint32_t handshakes = 0;

private:
    bool call(const char *who, uint32_t bodyBytes)
    {
        Call record;
        record.who = who;
        record.startedAtMs = _clock.nowMs();

        if (!_wifi.isConnected())
        {
            _scheduler.delayMs(failedCallMs);
            record.finishedAtMs = _clock.nowMs();
            calls.push_back(record);
            return false;
        }

        if (!_sessionWarm)
        {
            handshakes++;
            _heap.take("tls-handshake", TLS_HANDSHAKE_BYTES);
            _scheduler.delayMs(TLS_HANDSHAKE_MS);
            _heap.release("tls-handshake", TLS_HANDSHAKE_BYTES);
            if (!_wifi.isConnected())
            {
                // The link went under the handshake: no session, and the call fails as a socket
                // does when the radio goes away beneath it.
                record.finishedAtMs = _clock.nowMs();
                calls.push_back(record);
                return false;
            }
            _sessionWarm = true;
        }

        if (bodyBytes > 0)
        {
            _heap.take(who, bodyBytes);
            _scheduler.delayMs(REQUEST_BODY_MS);
            _heap.release(who, bodyBytes);
        }

        record.finishedAtMs = _clock.nowMs();
        if (!_wifi.isConnected())
        {
            _sessionWarm = false;
            calls.push_back(record);
            return false;
        }
        record.answered = true;
        calls.push_back(record);
        return true;
    }

    HostTaskScheduler &_scheduler;
    SimHeap &_heap;
    IWifiAdapter &_wifi;
    VirtualClock &_clock;
    bool _sessionWarm = false;
};

/**
 * @brief The MQTT link machine as the task that beats it sees it, with the act on the heap's tape.
 *
 * Two things a beat needs that the manager reads from the board: the uptime (MqttManager keeps
 * its timers on millis(), which on the host is a variable this relay sets from the virtual clock
 * before every beat) and the heap, charged for the socket from the beat that tears the stale one
 * down to the one that finds the broker answering. The journal of acts - when each began, when it
 * ended - is what the ordering claims are read off.
 */
class ClockedLink : public IMqttLinkMachine
{
public:
    struct Act
    {
        uint64_t startedAtMs = 0;
        uint64_t endedAtMs = 0;
    };

    ClockedLink(MqttManager &mqtt, AsyncMqttClient &client, VirtualClock &clock, SimHeap *heap)
        : _mqtt(mqtt), _client(client), _clock(clock), _heap(heap)
    {
    }

    bool serviceLinkOnce() override
    {
        hostUptimeMs = (unsigned long)_clock.nowMs();
        beats++;

        const int teardownsBefore = _client.disconnectAttempts;
        const bool inProgress = _mqtt.serviceLinkOnce();

        if (_heap != nullptr)
        {
            if (!_socketHeld && _client.disconnectAttempts != teardownsBefore)
            {
                _heap->take("mqtt-socket", MQTT_SOCKET_BYTES);
                _socketHeld = true;
            }
            else if (_socketHeld && _mqtt.isConnected())
            {
                _heap->release("mqtt-socket", MQTT_SOCKET_BYTES);
                _socketHeld = false;
            }
        }

        if (inProgress && !_wasInProgress)
        {
            Act act;
            act.startedAtMs = _clock.nowMs();
            acts.push_back(act);
        }
        else if (!inProgress && _wasInProgress && !acts.empty())
        {
            acts.back().endedAtMs = _clock.nowMs();
        }
        _wasInProgress = inProgress;

        if (_mqtt.isConnected() && !_wasConnected)
        {
            connackAtMs.push_back(_clock.nowMs());
        }
        _wasConnected = _mqtt.isConnected();

        return inProgress;
    }

    /** @brief Whether any act was in progress at this moment. */
    bool actInProgressAt(uint64_t atMs) const
    {
        for (size_t i = 0; i < acts.size(); i++)
        {
            const uint64_t end = (acts[i].endedAtMs == 0) ? (uint64_t)-1 : acts[i].endedAtMs;
            if (atMs >= acts[i].startedAtMs && atMs < end)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<Act> acts;
    std::vector<uint64_t> connackAtMs;
    uint64_t beats = 0;

private:
    MqttManager &_mqtt;
    AsyncMqttClient &_client;
    VirtualClock &_clock;
    SimHeap *_heap;
    bool _socketHeld = false;
    bool _wasInProgress = false;
    bool _wasConnected = false;
};

/** @brief The real dispatcher, with what one inbound message costs charged around it. */
class ChargingMessageProcessor : public IMessageProcessor
{
public:
    ChargingMessageProcessor(IMessageProcessor &real, SimHeap &heap, HostTaskScheduler &scheduler,
                             VirtualClock &clock)
        : _real(real), _heap(heap), _scheduler(scheduler), _clock(clock)
    {
    }

    void processMessage(const char *topic, const uint8_t *payload, unsigned int length) override
    {
        _heap.take("push-parse", PUSH_PARSE_BYTES);
        _real.processMessage(topic, payload, length);
        _scheduler.delayMs(PUSH_PARSE_MS);
        _heap.release("push-parse", PUSH_PARSE_BYTES);
        dispatched++;
        dispatchedAtMs.push_back(_clock.nowMs());
    }

    void addTopicHandler(ITopicHandler *handler) override { _real.addTopicHandler(handler); }
    void removeTopicHandler(ITopicHandler *handler) override { _real.removeTopicHandler(handler); }

    uint32_t dispatched = 0;
    std::vector<uint64_t> dispatchedAtMs;

private:
    IMessageProcessor &_real;
    SimHeap &_heap;
    HostTaskScheduler &_scheduler;
    VirtualClock &_clock;
};

/** @brief The rule half of the rule service: the real sync use case, driven where the loop drives it. */
class RuleSyncOnlyService : public IRuleService
{
public:
    explicit RuleSyncOnlyService(SyncRulesUseCase &sync) : _sync(sync) {}

    bool init() override { return _sync.init(); }
    void loop() override { _sync.loop(); }
    bool forceSyncRules() override { return _sync.forceSync(); }
    void onOutboxCompletion(const OutboxCompletion &completion) override
    {
        _sync.onOutboxCompletion(completion);
    }

private:
    SyncRulesUseCase &_sync;
};

/** @brief The diagnostic service, with the moment every pass was started recorded. */
class RecordingDiagnostics : public IDiagnosticService
{
public:
    RecordingDiagnostics(IDiagnosticService &real, VirtualClock &clock) : _real(real), _clock(clock)
    {
    }

    DiagnosticDto perform() override { return _real.perform(); }
    bool send(const DiagnosticDto &data) override { return _real.send(data); }

    bool performAndSend() override
    {
        startedAtMs.push_back(_clock.nowMs());
        return _real.performAndSend();
    }

    std::vector<uint64_t> startedAtMs;

private:
    IDiagnosticService &_real;
    VirtualClock &_clock;
};

/** @brief What the module would have done to a relay, in place of the GPIO writer. */
class RecordingDeviceCommands : public IDeviceCommandExecutionUseCase
{
public:
    uint32_t executions = 0;

    bool execute(const std::string &, const std::string &, int, bool = false, int = 0,
                 const std::string & = "") override
    {
        executions++;
        return true;
    }
};

/**
 * @brief The outbox as the diagnostics see it when the restart is not a job: every request goes to
 *        the real queue, and the restart is made right here, on the task that asked for it.
 *
 * The one relay of this file that is a shape and not a substitute. It is the wiring that keeps
 * the red a run of the same stand and not a memory:
 * the diagnostic's check of the link calls the adapter itself, on the control
 * loop, and reports the result on the spot. The completion is handed back so that the service's
 * bookkeeping - one restart in flight at a time - runs its course here too.
 */
class RestartOnTheCallerOutbox : public IOutbox
{
public:
    RestartOnTheCallerOutbox(IOutbox &real, IWifiAdapter &wifi,
                             HostMessageQueue<OutboxCompletion> &completions)
        : _real(real), _wifi(wifi), _completions(completions)
    {
    }

    uint32_t submitWifiReconnect() override
    {
        OutboxCompletion completion;
        completion.kind = OutboxKind::WIFI_RECONNECT;
        completion.ticket = ++_ticket;
        completion.attempts = 1;
        completion.delivered = _wifi.reconnect();
        _completions.push(completion);
        return completion.ticket;
    }

    uint32_t submitSensorUpdate(const UpdateDto &u) override { return _real.submitSensorUpdate(u); }
    uint32_t submitDeviceUpdate(const UpdateDto &u) override { return _real.submitDeviceUpdate(u); }
    uint32_t submitDiagnostic(const DiagnosticDto &d) override { return _real.submitDiagnostic(d); }
    uint32_t submitRuleListRequest() override { return _real.submitRuleListRequest(); }
    uint32_t submitRuleStateBulk(const std::string &j) override { return _real.submitRuleStateBulk(j); }
    uint32_t submitRuleStateByUid(const std::string &u, const std::string &j) override
    {
        return _real.submitRuleStateByUid(u, j);
    }
    uint32_t submitRuleSensorOffline(const std::string &j) override { return _real.submitRuleSensorOffline(j); }
    uint32_t submitDeviceCommandFetch() override { return _real.submitDeviceCommandFetch(); }
    uint32_t submitModuleCommandFetch() override { return _real.submitModuleCommandFetch(); }
    uint32_t submitSensorTypesRequest() override { return _real.submitSensorTypesRequest(); }
    uint32_t submitWifiRoam(const uint8_t *bssid, uint8_t channel) override
    {
        return _real.submitWifiRoam(bssid, channel);
    }
    bool ensureNetworkTask() override { return _real.ensureNetworkTask(); }
    uint32_t submitLinkProbe() override { return _real.submitLinkProbe(); }
    // The restart runs here, on the caller, at once: the link it is asked for is the one the caller
    // just saw down, so the lost-link restart is the same restart. A link that is up is not a lost
    // one: nothing is restarted and the request completes delivered, as the outbox treats it.
    uint32_t submitWifiReconnectForALostLink() override
    {
        if (!_wifi.isConnected())
        {
            return submitWifiReconnect();
        }
        OutboxCompletion completion;
        completion.kind = OutboxKind::WIFI_RECONNECT;
        completion.ticket = ++_ticket;
        completion.attempts = 1;
        completion.delivered = true;
        _completions.push(completion);
        return completion.ticket;
    }
    uint32_t submitCommandResult(const CommandResultDto &r, ApiPriority p) override
    {
        return _real.submitCommandResult(r, p);
    }
    uint32_t submitDeviceCommandResult(const DeviceCommandUpdateDto &u) override
    {
        return _real.submitDeviceCommandResult(u);
    }
    uint32_t submitModuleCommandResult(const std::vector<ModuleCommandUpdateDto> &u) override
    {
        return _real.submitModuleCommandResult(u);
    }
    uint32_t submitRawIoResult(const RawIoResultDto &r) override { return _real.submitRawIoResult(r); }
    uint32_t submitModuleStructure(const ModuleStructure &s, ApiPriority p) override
    {
        return _real.submitModuleStructure(s, p);
    }
    bool pollCompletion(OutboxCompletion &out) override { return _real.pollCompletion(out); }
    bool takeRuleListResponse(std::vector<Rule> &out) override { return _real.takeRuleListResponse(out); }
    bool takeDeviceCommandResponse(std::vector<DeviceCommandDto> &out) override
    {
        return _real.takeDeviceCommandResponse(out);
    }
    bool takeModuleCommandResponse(std::vector<ModuleCommandDto> &out) override
    {
        return _real.takeModuleCommandResponse(out);
    }
    bool takeSensorTypesResponse(std::vector<SensorType> &out) override
    {
        return _real.takeSensorTypesResponse(out);
    }
    uint32_t collapsedCount() const override { return _real.collapsedCount(); }
    uint32_t refusedEventCount() const override { return _real.refusedEventCount(); }
    uint32_t undeliveredCount() const override { return _real.undeliveredCount(); }
    uint32_t lostCompletionCount() const override { return _real.lostCompletionCount(); }

private:
    IOutbox &_real;
    IWifiAdapter &_wifi;
    HostMessageQueue<OutboxCompletion> &_completions;
    // Far from the real outbox's tickets, which start at one: a completion this relay hands back
    // must be recognised as the restart's and as nobody else's.
    uint32_t _ticket = 1000000;
};

/** @brief Every control-loop step, so the worst one inside any window can be named. */
class StepRecorder
{
public:
    struct Step
    {
        uint64_t startedAtMs;
        uint64_t lengthMs;
    };

    void record(uint64_t startedAtMs, uint64_t finishedAtMs)
    {
        Step step;
        step.startedAtMs = startedAtMs;
        step.lengthMs = finishedAtMs - startedAtMs;
        steps.push_back(step);
    }

    uint64_t worstBetween(uint64_t fromMs, uint64_t toMs, uint64_t *startedAtMs = nullptr) const
    {
        uint64_t worst = 0;
        for (size_t i = 0; i < steps.size(); i++)
        {
            if (steps[i].startedAtMs < fromMs || steps[i].startedAtMs > toMs)
            {
                continue;
            }
            if (steps[i].lengthMs > worst)
            {
                worst = steps[i].lengthMs;
                if (startedAtMs != nullptr)
                {
                    *startedAtMs = steps[i].startedAtMs;
                }
            }
        }
        return worst;
    }

    std::vector<Step> steps;
};

// ================================================================================================
// The storm
// ================================================================================================

/** The two shapes the stand runs in; see the note at the top of the file. */
enum class Shape
{
    RECONNECT_ON_THE_LOOP,
    ONE_NETWORK_TASK
};

/** @brief The whole module, wired the way globals.h wires it, on the host stand. */
struct StormStand
{
    explicit StormStand(Shape shape) : _shape(shape) {}

    VirtualClock clock;
    HostTaskScheduler scheduler{clock};
    SimArduino arduino{clock, &scheduler};
    SimHeap heap{clock, arduino};
    TapeLogger logger{clock};
    MockDebugSystem debug;
    MockSettings settings;
    MockModuleConfig config{&logger, &debug, &settings};
    MockNTPTimeAdapter timeAdapter;
    StormWifi wifi{scheduler, clock};
    MockRestAPIService restApi;
    ArduinoJsonConverter json;

    StormServer server{scheduler, heap, wifi, clock};
    HostMessageQueue<OutboxCompletion> completions{Outbox::COMPLETION_QUEUE_DEPTH};
    Outbox outbox{&server, &scheduler, &completions, &logger, &debug};
    RestartOnTheCallerOutbox restartOnTheCaller{outbox, wifi, completions};

    // The MQTT leg: the real manager over the host client and the broker simulation.
    SimMqttBroker broker;
    AsyncMqttClient client;
    std::recursive_mutex clientMutex;
    MockSpiffsRuleDao ruleDao;
    RuleRepository rules{&ruleDao};
    MockSensorRepository sensors;
    RecordingDeviceCommands deviceCommands;
    CommandService commands{&deviceCommands, nullptr,     nullptr, nullptr, &logger,
                            &debug,          &timeAdapter, &json,   &rules,  &settings};
    MessageProcessor processor{&logger, &debug};
    ChargingMessageProcessor chargingProcessor{processor, heap, scheduler, clock};
    TopicHandlerFactory handlerFactory{&logger, &debug, &config, &commands, &json};
    MqttManager mqtt{client,          &logger,         &debug,
                     CLIENT_ID_PREFIX, IPAddress(116, 203, 235, 144), 1883,
                     "user",          "secret",        &chargingProcessor,
                     &handlerFactory, &wifi,           &clientMutex};
    ClockedLink link{mqtt, client, clock, &heap};

    // The discovery.
    MockSensorReader sensorReader;
    MockSensorTypeRepository sensorTypes;
    ModuleStructure moduleStructure;
    VpdChannelUseCase vpd;
    VirtualSensorChannelsUseCase virtualChannels{&vpd};
    SensorStructureUseCase sensorStructure{&virtualChannels};
    ModbusAddressAllocator allocator{&sensors};
    MockSensorValueModel sensorValues;
    StartupSensorScanUseCase scan{&logger,          &debug,      &config,      &sensors,
                                  &sensorTypes,     &sensorReader, &moduleStructure,
                                  &sensorStructure, &allocator,  &sensorValues,
                                  &virtualChannels, &arduino};

    // The rule sync.
    SyncRulesUseCase syncRules{&server, &rules, &arduino, &timeAdapter, &json, &logger, &debug};
    RuleSyncOnlyService ruleService{syncRules};

    DiagnosticService realDiagnostics{&restApi,   &config, logger, debug,   &json, &wifi,
                                      &timeAdapter, &arduino, &mqtt, &server};
    RecordingDiagnostics diagnostics{realDiagnostics, clock};
    // The decisions about the link, on the loop's tick of the diagnostic's moments.
    LinkSupervisor linkSupervisor{&restApi, &config, logger, debug, &wifi, &timeAdapter, &arduino, &mqtt};

    LoopServiceConfigureDone loopService;
    StepRecorder steps;

    uint64_t lastPassEndedAtMs = 0;
    bool passWasInFlight = false;
    uint64_t lastTelemetryAtMs = 0;
    uint64_t linkDroppedAtMs = 0;

    /** Assemble the graph, boot the MQTT leg, and put the discovery where a restart puts it. */
    void boot()
    {
        debug.featureEnabled = true;
        settings.setUser("bench@example.com");
        settings.setAccountUid("bench-account");
        config.setSettings(&settings);
        config.setModuleUid(MODULE_UID);
        config.setFirmwareVersion("test");
        // A sensor director: the module type is what decides whether the ports exist at all, and
        // a module with no ports has no free ones for the discovery to walk.
        config.setModuleType(MODULE_TYPE_SENSOR);
        config.setSensorPinCount(MODULE_PIN_COUNT);

        for (int model = 0; model < CATALOG_MODEL_COUNT; model++)
        {
            sensorTypes.addSensorType(makeModbusType(model));
        }
        sensorReader.scanSensorReturnValue = false;
        moduleStructure.nodes.push_back(Node());

        wifi.setConnected(true);
        client.attachBroker(&broker);

        if (_shape == Shape::ONE_NETWORK_TASK)
        {
            // globals.h: the radio and the link machine are the network task's.
            outbox.setWifiAdapter(&wifi);
            outbox.setMqttLink(&link);
            realDiagnostics.setOutbox(&outbox);
            linkSupervisor.setOutbox(&outbox);
        }
        else
        {
            // The shape before: the restart on the task that decided it, the link machine on a
            // task of its own (started below).
            realDiagnostics.setOutbox(&restartOnTheCaller);
            linkSupervisor.setOutbox(&restartOnTheCaller);
        }
        commands.setOutbox(&outbox);
        syncRules.setOutbox(&outbox);

        loopService.setLogging(&logger, &debug);
        loopService.setModuleConfig(&config);
        loopService.setArduinoAdapter(&arduino);
        loopService.setMqttManager(&mqtt);
        loopService.setDiagnosticService(&diagnostics);
        loopService.setLinkSupervisor(&linkSupervisor);
        realDiagnostics.setLinkSupervisor(&linkSupervisor);
        loopService.setOutbox(&outbox);
        loopService.setCommandService(&commands);
        loopService.setRuleService(&ruleService);

        // The boot of the MQTT leg, in the order ModuleSetupService::setupMqttConnection() takes.
        hostUptimeMs = 0;
        mqtt.setClientId(clientId().c_str());
        mqtt.connect();
        mqtt.setupMqttTopicHandlers();

        scan.arm();

        startTasks();
    }

    /** One iteration of every task whose turn has come. */
    void step() { scheduler.runForMs(CONTROL_PERIOD_MS); }

    void runForMs(uint64_t ms)
    {
        const uint64_t until = clock.nowMs() + ms;
        while (clock.nowMs() < until)
        {
            step();
        }
    }

    /**
     * @brief Run quietly until the storm can be placed as the recorded one fell.
     *
     * The link drops EDGE_TO_TICK_MS before a diagnostic pass, which is the pass that finds it
     * down and asks for the restart; the restart takes RADIO_RESTART_MS; and the discovery's
     * periodic pass has to begin while the link's first seconds back are the busiest - so the
     * harness looks for a moment at which the two periods fall that way rather than assuming one.
     *
     * @return when the link should drop
     */
    uint64_t planTheStorm()
    {
        const uint64_t budgetMs = 30 * 60 * 1000;
        const uint64_t start = clock.nowMs();

        while (clock.nowMs() - start < budgetMs)
        {
            step();

            if (scan.aPassIsInFlight() || lastPassEndedAtMs == 0 ||
                diagnostics.startedAtMs.empty())
            {
                continue;
            }

            const uint64_t diagnosticDue = diagnostics.startedAtMs.back() + DIAGNOSTIC_INTERVAL_MS;
            const uint64_t ladderDue =
                lastPassEndedAtMs + StartupSensorScanUseCase::FREE_PORT_PROBE_INTERVAL_MS;
            const uint64_t linkBackEta = diagnosticDue + RADIO_RESTART_MS;

            if (diagnosticDue > clock.nowMs() + EDGE_TO_TICK_MS + 500 &&
                ladderDue + 3000 >= linkBackEta && ladderDue <= linkBackEta + 1000)
            {
                return diagnosticDue - EDGE_TO_TICK_MS;
            }
        }

        TEST_FAIL_MESSAGE("the harness found no moment at which the discovery's pass falls on the "
                          "link's return - widen the tolerance in planTheStorm()");
        return 0;
    }

    /** Take the link away, and let the module find out at its own pace. */
    void dropTheLink(uint64_t dropAt)
    {
        runForMs(dropAt - clock.nowMs());
        wifi.setConnected(false);
        linkDroppedAtMs = clock.nowMs();
        // The radio takes the sockets with it: the TLS session is gone, which is what makes the
        // next request pay for a handshake, and the broker sees the client drop off rather than
        // going on writing to a socket that is not there.
        server.forgetTheSession();
        hostUptimeMs = (unsigned long)clock.nowMs();
        client.disconnect();

        // What the broker holds for the session while the module is away: the message that comes
        // straight back down the moment it reconnects.
        broker.publishFromServer(commandTopic(), deviceCommandPush(75, "on"), 2, false);
    }

    /** Run until the radio restart the diagnostics ask for has brought the link back. */
    void runUntilTheLinkIsBack()
    {
        const uint64_t deadline = clock.nowMs() + EDGE_TO_TICK_MS + 2 * RADIO_RESTART_MS + 5000;
        while (!wifi.isConnected() && clock.nowMs() < deadline)
        {
            step();
        }
        TEST_ASSERT_TRUE_MESSAGE(wifi.isConnected(),
                                 "the link never came back - nobody restarted the radio");
    }

private:
    void startTasks()
    {
        scheduler.createTask(CONTROL_TASK, 512 * 1024, 1, [this]() {
            while (true)
            {
                hostUptimeMs = (unsigned long)clock.nowMs();
                const uint64_t stepStartedAt = clock.nowMs();
                loopService.loop();
                steps.record(stepStartedAt, clock.nowMs());
                pumpTheLadder();
                noteWhetherAPassEnded();
                produceTelemetry();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });

        if (_shape == Shape::RECONNECT_ON_THE_LOOP)
        {
            // The MQTT client's recovery task of the shape before: the same state machine, on a
            // task of its own, at the interval that task kept.
            scheduler.createTask("mqtt-recovery", 256 * 1024, 1, [this]() {
                while (true)
                {
                    link.serviceLinkOnce();
                    scheduler.delayMs(MQTT_RECOVERY_PERIOD_MS);
                }
            });
        }
    }

    /**
     * @brief One discovery step per control-loop iteration, which is what the line gives it.
     *
     * The acquisition task serves a discovery step between two sweeps of the stored sensors;
     * the task is substituted here and the use case is not.
     */
    void pumpTheLadder()
    {
        StartupScanStep step;
        if (!scan.takeNextStep(step))
        {
            return;
        }
        scan.applyStepOutcome(scan.runStepOnBus(step));
    }

    /** What a pass costs the heap, taken when one begins and given back when it ends. */
    void noteWhetherAPassEnded()
    {
        const bool inFlight = scan.aPassIsInFlight();
        if (inFlight && !passWasInFlight)
        {
            heap.take("discovery-pass", LADDER_PASS_BYTES);
        }
        else if (!inFlight && passWasInFlight)
        {
            heap.release("discovery-pass", LADDER_PASS_BYTES);
            lastPassEndedAtMs = clock.nowMs();
        }
        passWasInFlight = inFlight;
    }

    /** The sensor service's periodic submit, which is all of it the network ever sees. */
    void produceTelemetry()
    {
        if (clock.nowMs() - lastTelemetryAtMs < TELEMETRY_PERIOD_MS)
        {
            return;
        }
        lastTelemetryAtMs = clock.nowMs();
        outbox.submitSensorUpdate(UpdateDto());
    }

    /** A device command notification, as SendDeviceCommandNotificationAsync builds it. */
    static std::string deviceCommandPush(int commandId, const std::string &state)
    {
        return std::string("{\"growDirectorUid\":\"") + MODULE_UID +
               "\",\"deviceUid\":\"relay-0\",\"commandId\":" + std::to_string(commandId) +
               ",\"requestedState\":\"" + state +
               "\",\"analogControlPinValue\":0,\"status\":\"new\",\"needToSwitchBack\":false," +
               "\"secondsToSwitchBack\":0,\"switchType\":\"\",\"createdDate\":\"2026-09-15T11:00:00\"," +
               "\"type\":\"DeviceCommandNotification\"}";
    }

    Shape _shape;
};

/** @brief One run of the scene, and what it left behind. */
struct StormResult
{
    uint64_t droppedAtMs = 0;
    uint64_t restoredAtMs = 0;
    uint64_t windowEndMs = 0;
    uint32_t floorBytes = 0;
    bool tlsMet = false;
    bool mqttMet = false;
    bool diagnosticMet = false;
    bool pushMet = false;
    bool ladderMet = false;
    size_t heapLowLines = 0;
    size_t networkHeldTogether = 0;
    uint64_t worstStepMs = 0;
    uint64_t worstStepAtMs = 0;
    std::string restartTask;
    long long restartEndedAtMs = -1;
    long long socketAtMs = -1;
    long long handshakeAtMs = -1;
    long long commandFetchAtMs = -1;
    uint32_t undelivered = 0;
};

void runTheStorm(StormStand &module, StormResult &result)
{
    module.boot();

    const uint64_t dropAt = module.planTheStorm();
    module.dropTheLink(dropAt);
    module.runUntilTheLinkIsBack();

    // The window opens the moment the restart returned - not at the end of the scheduler step
    // that saw it, which can be a hundred milliseconds later, by when the first participants
    // have already taken their bytes.
    const uint64_t from = module.wifi.restarts.back().finishedAtMs;
    module.runForMs(OBSERVATION_WINDOW_MS);
    const uint64_t to = module.clock.nowMs();

    result.droppedAtMs = module.linkDroppedAtMs;
    result.restoredAtMs = from;
    result.windowEndMs = to;
    result.floorBytes = module.heap.floorBetween(from, to);
    result.tlsMet = module.heap.heldBytesBetween("tls-handshake", from, to);
    result.mqttMet = module.heap.heldBytesBetween("mqtt-socket", from, to);
    result.diagnosticMet = module.heap.heldBytesBetween("diagnostic", from, to);
    result.pushMet = module.heap.heldBytesBetween("push-parse", from, to);
    result.ladderMet = module.heap.heldBytesBetween("discovery-pass", from, to);
    result.heapLowLines = module.logger.countSaying("HEAP_LOW: new watermark", from, to);
    result.networkHeldTogether = module.heap.mostHeldTogether(networkParticipants(), from, to);
    result.worstStepMs = module.steps.worstBetween(module.linkDroppedAtMs, to, &result.worstStepAtMs);
    if (!module.wifi.restarts.empty())
    {
        result.restartTask = module.wifi.restarts.back().task;
        result.restartEndedAtMs = (long long)module.wifi.restarts.back().finishedAtMs;
    }
    result.socketAtMs = module.heap.firstTakeBetween("mqtt-socket", from, to);
    result.handshakeAtMs = module.heap.firstTakeBetween("tls-handshake", from, to);
    result.commandFetchAtMs = module.server.firstCallTo("command-fetch", from);
    result.undelivered = module.outbox.undeliveredCount();
}

/** Every participant named by the recorded storm has to be in the run, or it proves nothing. */
void assertEveryParticipantTookPart(const StormStand &module, const StormResult &result, const char *which)
{
    char message[160];
    snprintf(message, sizeof(message), "%s: the radio was never restarted", which);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)module.wifi.restarts.size(), message);
    snprintf(message, sizeof(message), "%s: the TLS handshake was not part of the storm", which);
    TEST_ASSERT_TRUE_MESSAGE(result.tlsMet, message);
    snprintf(message, sizeof(message), "%s: the MQTT socket was not part of the storm", which);
    TEST_ASSERT_TRUE_MESSAGE(result.mqttMet, message);
    snprintf(message, sizeof(message), "%s: the diagnostic pass was not part of the storm", which);
    TEST_ASSERT_TRUE_MESSAGE(result.diagnosticMet, message);
    snprintf(message, sizeof(message), "%s: the queued message was not part of the storm", which);
    TEST_ASSERT_TRUE_MESSAGE(result.pushMet, message);
    snprintf(message, sizeof(message), "%s: the discovery's pass was not part of the storm", which);
    TEST_ASSERT_TRUE_MESSAGE(result.ladderMet, message);
    TEST_ASSERT_FALSE_MESSAGE(module.scheduler.stepBudgetExhausted(),
                              "harness gave up: a task kept running without letting time advance");
}

void printTheStorm(const StormStand &module, const StormResult &result, const char *title)
{
    printf("  %s\n", title);
    printf("    link dropped at %llu ms, back at %llu ms (restart on '%s'); worst loop step %llu ms "
           "at %llu ms; floor %u B; network participants held together at most %u; "
           "undelivered %u\n",
           (unsigned long long)result.droppedAtMs, (unsigned long long)result.restoredAtMs,
           result.restartTask.c_str(), (unsigned long long)result.worstStepMs,
           (unsigned long long)result.worstStepAtMs, (unsigned)result.floorBytes,
           (unsigned)result.networkHeldTogether, (unsigned)result.undelivered);
    module.heap.printTape("the heap, from the moment the link was back", result.restoredAtMs,
                          result.windowEndMs);
}

/**
 * The red, both readings of it. With the restart on the control loop the loop stands still for
 * the association; with the MQTT machine on a task of its own the socket is rebuilt while the
 * handshake is made, and the floor is what the sum of everything that met leaves. And on the way
 * down, the telemetry spent its attempts on a link that was not there.
 */
void test_the_storm_with_the_restart_on_the_loop_stalls_the_loop_and_sums_the_heap(void)
{
    StormStand module(Shape::RECONNECT_ON_THE_LOOP);
    StormResult result;
    runTheStorm(module, result);

    printTheStorm(module, result, "network-offboard: the storm, with the restart on the loop");
    assertEveryParticipantTookPart(module, result, "reconnect-on-the-loop");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(CONTROL_TASK, result.restartTask.c_str(),
                                     "in this shape the restart has to run on the control loop");

    char message[256];
    snprintf(message, sizeof(message),
             "the worst control-loop step was %llu ms; the restart held the loop for %u ms",
             (unsigned long long)result.worstStepMs, (unsigned)RADIO_RESTART_MS);
    TEST_ASSERT_TRUE_MESSAGE(result.worstStepMs >= RADIO_RESTART_MS, message);

    // The three largest participants are down together: the handshake the returning link made
    // necessary, the socket being rebuilt, and the discovery's pass that fell on the same seconds.
    const uint32_t threeThatMeet = TLS_HANDSHAKE_BYTES + MQTT_SOCKET_BYTES + LADDER_PASS_BYTES;
    snprintf(message, sizeof(message),
             "the floor was %u B of %u B free; the handshake, the socket and the discovery's pass "
             "are %u B together",
             (unsigned)result.floorBytes, (unsigned)STEADY_FREE_BYTES, (unsigned)threeThatMeet);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(STEADY_FREE_BYTES - threeThatMeet, result.floorBytes,
                                             message);
    TEST_ASSERT_TRUE_MESSAGE(result.networkHeldTogether >= 2,
                             "the network participants never met - this is not the storm");
    TEST_ASSERT_TRUE_MESSAGE(result.heapLowLines > 0,
                             "a storm that deep left no HEAP_LOW line of its own");

    // The side reading of the recorded run: "kind 0 ticket 71 not delivered after 4 attempts".
    TEST_ASSERT_TRUE_MESSAGE(result.undelivered > 0,
                             "no request spent its attempts on the link that was down");
}

/**
 * The first reading, green: the control loop decides on the restart and the network task makes
 * it. The loop's worst step through the whole of it - the outage, the restart and the storm - is
 * the one the module keeps everywhere else.
 */
void test_the_storm_on_one_network_task_keeps_the_loop_stepping(void)
{
    StormStand module(Shape::ONE_NETWORK_TASK);
    StormResult result;
    runTheStorm(module, result);

    printTheStorm(module, result, "network-offboard: the same storm, the network on one task");
    assertEveryParticipantTookPart(module, result, "one-network-task");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(NETWORK_TASK, result.restartTask.c_str(),
                                     "the restart did not run on the network task");

    char message[256];
    snprintf(message, sizeof(message),
             "the worst control-loop step was %llu ms at %llu ms (limit %u ms) - the loop is "
             "still waiting on the network",
             (unsigned long long)result.worstStepMs, (unsigned long long)result.worstStepAtMs,
             (unsigned)MAX_ACCEPTED_STEP_MS);
    TEST_ASSERT_TRUE_MESSAGE(result.worstStepMs <= MAX_ACCEPTED_STEP_MS, message);

    // And the telemetry did not spend its attempts on the link that was down.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, result.undelivered,
                                     "a request spent its attempts while the link was down");
}

/**
 * The second reading, green: with every network participant on one task, the network task holds
 * one of them at a time, and the floor is what the largest of them leaves together with the two
 * that are not the network's - the discovery's pass on the acquisition task and the message the
 * control loop parses. The threshold is read off the participants, and the direct claim is read
 * off the tape: no two network participants ever held bytes at once.
 */
void test_the_storm_on_one_network_task_bottoms_out_at_its_largest_network_participant(void)
{
    StormStand module(Shape::ONE_NETWORK_TASK);
    StormResult result;
    runTheStorm(module, result);

    assertEveryParticipantTookPart(module, result, "one-network-task");

    char message[256];
    snprintf(message, sizeof(message),
             "%u network participants held bytes at once - the network task is not the only one "
             "talking to the network",
             (unsigned)result.networkHeldTogether);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)result.networkHeldTogether, message);

    const uint32_t largestNetworkParticipant =
        std::max(TLS_HANDSHAKE_BYTES, std::max(MQTT_SOCKET_BYTES, DIAGNOSTIC_BODY_BYTES));
    const uint32_t deepest = largestNetworkParticipant + LADDER_PASS_BYTES + PUSH_PARSE_BYTES;
    snprintf(message, sizeof(message),
             "the floor was %u B out of %u B free; the deepest one network participant can go "
             "with the discovery's pass and a parse beside it is %u B",
             (unsigned)result.floorBytes, (unsigned)STEADY_FREE_BYTES, (unsigned)deepest);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(STEADY_FREE_BYTES - deepest, result.floorBytes,
                                                message);
}

/**
 * The order the queue puts the link back in, read off the run: the radio first, the broker
 * socket second, the server third - and no REST call begins while a connect act is in progress.
 * The outbox says so on its own tape as well.
 */
void test_the_link_comes_back_in_order_radio_then_broker_then_server(void)
{
    StormStand module(Shape::ONE_NETWORK_TASK);
    StormResult result;
    runTheStorm(module, result);

    assertEveryParticipantTookPart(module, result, "one-network-task");

    char message[256];
    snprintf(message, sizeof(message),
             "restart ended at +%lld ms, socket at +%lld ms, handshake at +%lld ms",
             result.restartEndedAtMs - (long long)result.restoredAtMs,
             result.socketAtMs - (long long)result.restoredAtMs,
             result.handshakeAtMs - (long long)result.restoredAtMs);
    TEST_ASSERT_TRUE_MESSAGE(result.restartEndedAtMs <= result.socketAtMs, message);
    TEST_ASSERT_TRUE_MESSAGE(result.socketAtMs < result.handshakeAtMs, message);

    TEST_ASSERT_TRUE_MESSAGE(!module.link.acts.empty(), "the link machine never ran an act");
    for (size_t i = 0; i < module.server.calls.size(); i++)
    {
        const StormServer::Call &call = module.server.calls[i];
        snprintf(message, sizeof(message),
                 "a %s call began at %llu ms, inside an MQTT connect act",
                 call.who.c_str(), (unsigned long long)call.startedAtMs);
        TEST_ASSERT_FALSE_MESSAGE(module.link.actInProgressAt(call.startedAtMs), message);
    }

    const long long saidActStarted =
        module.logger.firstSaying("OUTBOX: mqtt connect act started", result.restoredAtMs);
    const long long saidActOver =
        module.logger.firstSaying("OUTBOX: mqtt connect act over", result.restoredAtMs);
    TEST_ASSERT_TRUE_MESSAGE(saidActStarted >= 0, "the outbox said nothing about the act");
    TEST_ASSERT_TRUE_MESSAGE(saidActStarted < saidActOver, "the act was over before it started");
    TEST_ASSERT_TRUE_MESSAGE(saidActOver <= result.handshakeAtMs,
                             "the handshake began before the outbox said the act was over");
}

/**
 * A command from a person waits for nothing but the queue's own order. The message the broker
 * held for the session is dispatched on the iteration after the CONNACK that let it through, and
 * the fetch it sets off reaches the server inside the window - after the act, because the act
 * holds every request, and not deferred by anything else.
 */
void test_a_push_from_a_person_is_dispatched_and_fetched_inside_the_storm(void)
{
    StormStand module(Shape::ONE_NETWORK_TASK);
    StormResult result;
    runTheStorm(module, result);

    assertEveryParticipantTookPart(module, result, "one-network-task");

    TEST_ASSERT_TRUE_MESSAGE(module.chargingProcessor.dispatched > 0,
                             "the message the broker held for the session was never dispatched");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, module.deviceCommands.executions,
                                     "the fallback executed a command the server never confirmed");
    TEST_ASSERT_TRUE_MESSAGE(!module.link.connackAtMs.empty(), "the broker never answered");

    const long long connack = (long long)module.link.connackAtMs.back();
    const long long dispatched = (long long)module.chargingProcessor.dispatchedAtMs.back();
    char message[256];
    snprintf(message, sizeof(message),
             "the CONNACK came at +%lld ms, the message was dispatched at +%lld ms, the fetch it "
             "set off went at +%lld ms",
             connack - (long long)result.restoredAtMs, dispatched - (long long)result.restoredAtMs,
             result.commandFetchAtMs - (long long)result.restoredAtMs);
    TEST_ASSERT_TRUE_MESSAGE(dispatched >= connack, message);
    TEST_ASSERT_TRUE_MESSAGE(dispatched - connack <= 2 * CONTROL_PERIOD_MS + PUSH_PARSE_MS, message);
    TEST_ASSERT_TRUE_MESSAGE(result.commandFetchAtMs >= 0, "the command produced no fetch at all");
    TEST_ASSERT_TRUE_MESSAGE(result.commandFetchAtMs <= (long long)result.windowEndMs, message);
}

// ================================================================================================
// The decision and the job
// ================================================================================================

/** @brief The diagnostics over the real outbox, with the control loop driving both. */
struct RestartStand
{
    VirtualClock clock;
    HostTaskScheduler scheduler{clock};
    SimArduino arduino{clock, &scheduler};
    SimHeap heap{clock, arduino};
    TapeLogger logger{clock};
    MockDebugSystem debug;
    MockSettings settings;
    MockModuleConfig config{&logger, &debug, &settings};
    MockNTPTimeAdapter timeAdapter;
    StormWifi wifi{scheduler, clock};
    MockRestAPIService restApi;
    MockMqttManager mqtt;
    ArduinoJsonConverter json;
    StormServer server{scheduler, heap, wifi, clock};
    HostMessageQueue<OutboxCompletion> completions{Outbox::COMPLETION_QUEUE_DEPTH};
    Outbox outbox{&server, &scheduler, &completions, &logger, &debug};
    LinkSupervisor supervisor{&restApi, &config, logger, debug, &wifi, &timeAdapter, &arduino, &mqtt};
    StepRecorder steps;

    RestartStand()
    {
        debug.featureEnabled = true;
        config.setSettings(&settings);
        config.setModuleUid(MODULE_UID);
        wifi.setConnected(true);
        mqtt.isConnectedReturnValue = true;
        outbox.setWifiAdapter(&wifi);
        supervisor.setOutbox(&outbox);

        // The control loop: the decision every period, and the drain of what the network task
        // finished, as LoopServiceConfigureDone does both.
        scheduler.createTask(CONTROL_TASK, 512 * 1024, 1, [this]() {
            while (true)
            {
                const uint64_t startedAt = clock.nowMs();
                OutboxCompletion completion;
                while (outbox.pollCompletion(completion))
                {
                    supervisor.onOutboxCompletion(completion);
                }
                supervisor.superviseOnce();
                steps.record(startedAt, clock.nowMs());
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }
};

/**
 * The decision is the control loop's and the restart is the network task's: one restart on the
 * link going down, made on outbox-net, the loop stepping meanwhile, and the outcome on the tape
 * when the restart is over - not when it was asked for.
 *
 * Mutation: call the adapter in requestWifiReconnect() whether or not there is an outbox.
 */
void test_a_restart_is_decided_on_the_loop_and_made_on_the_network_task(void)
{
    RestartStand stand;
    stand.scheduler.runForMs(1000);

    stand.wifi.setConnected(false);
    const uint64_t droppedAt = stand.clock.nowMs();
    stand.scheduler.runForMs(RADIO_RESTART_MS + 2000);

    TEST_ASSERT_FALSE(stand.scheduler.stepBudgetExhausted());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.wifi.restarts.size(),
                                     "the link went down and the radio was not restarted once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(NETWORK_TASK, stand.wifi.restarts[0].task.c_str(),
                                     "the restart ran on the wrong task");
    TEST_ASSERT_TRUE_MESSAGE(stand.wifi.isConnected(), "the restart did not bring the link back");

    const uint64_t worstStep = stand.steps.worstBetween(droppedAt, stand.clock.nowMs());
    char message[192];
    snprintf(message, sizeof(message),
             "the control loop's worst step during the restart was %llu ms (limit %u ms)",
             (unsigned long long)worstStep, (unsigned)MAX_ACCEPTED_STEP_MS);
    TEST_ASSERT_TRUE_MESSAGE(worstStep <= MAX_ACCEPTED_STEP_MS, message);

    const long long asked = stand.logger.firstSaying("WIFI_RECONNECT: restart handed to the network task");
    const long long answered = stand.logger.firstSaying("WIFI_RECONNECT: restart result=1");
    TEST_ASSERT_TRUE_MESSAGE(asked >= 0, "the decision left no line on the tape");
    TEST_ASSERT_TRUE_MESSAGE(answered >= 0, "the outcome never reached the tape");
    snprintf(message, sizeof(message),
             "asked at %lld ms, answered at %lld ms, the restart ended at %llu ms", asked, answered,
             (unsigned long long)stand.wifi.restarts[0].finishedAtMs);
    TEST_ASSERT_TRUE_MESSAGE(answered >= (long long)stand.wifi.restarts[0].finishedAtMs, message);
    TEST_ASSERT_TRUE_MESSAGE(answered - asked >= (long long)RADIO_RESTART_MS, message);
}

/**
 * One restart in flight at a time, whatever the loop decides meanwhile: the decision made on the
 * next tick, while the radio is still off for the restart, is not a second restart.
 *
 * Mutation: drop the in-flight check from requestWifiReconnect().
 */
void test_a_restart_in_flight_is_not_asked_for_twice(void)
{
    RestartStand stand;
    stand.wifi.restartMs = 5000;
    stand.scheduler.runForMs(1000);

    stand.wifi.setConnected(false);
    stand.scheduler.runForMs(2000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stand.wifi.reconnectCallCount, "the restart did not begin");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.wifi.restarts.size(), "the restart is over already");

    // The link "recovers" from the loop's point of view and drops again while the restart runs.
    // The edge is seen; the restart is not doubled.
    stand.wifi.connected = true;
    stand.scheduler.runForMs(CONTROL_PERIOD_MS);
    stand.wifi.connected = false;
    stand.scheduler.runForMs(CONTROL_PERIOD_MS);
    TEST_ASSERT_TRUE_MESSAGE(stand.logger.countSaying("WIFI_RECONNECT: restart already in flight") > 0,
                             "the second decision was not refused");

    stand.scheduler.runForMs(10000);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.wifi.restarts.size(),
                                     "a second restart was queued behind the one in flight");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.outbox.pendingCount(),
                                     "a restart is still waiting in the queue");
}

/**
 * The other decision that restarts the radio - a dead broker with the station associated and a
 * gateway that stays silent twice - is a job of the queue like the lost association: the probes and
 * the restart made on the network task, the loop stepping meanwhile, and the outcome on the tape when
 * the restart is over, not when it was asked for. The station is associated, so the restart is the
 * unconditional one - asked "for a lost link" it would be skipped by the network task.
 *
 * Mutation: ask for the restart for a lost link whatever the association.
 */
void test_a_dead_broker_behind_a_silent_gateway_restarts_the_radio_on_the_network_task(void)
{
    RestartStand stand;
    SimLinkProbe probe(stand.wifi, stand.scheduler);
    probe.routerAnswers = false;
    stand.outbox.setLinkProbe(&probe, boardLinkProbePlan());
    stand.mqtt.isConnectedReturnValue = false;

    // A minute of patience, a silent check, thirty seconds, a second silent check: the restart is
    // decided a little after 108 s - each check is 9 s of silence on the network task.
    stand.scheduler.runForMs(150000);
    const long long decided = stand.logger.firstSaying("LINK_CHECK: PROBING -> LINK_DOWN (gateway silent)");
    TEST_ASSERT_TRUE_MESSAGE(decided >= 0, "a dead broker behind a silent gateway led to no decision");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, probe.gatewayAsks, "the gateway was not asked twice before the restart");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stand.wifi.reconnectCallCount, "the restart did not begin");

    stand.scheduler.runForMs(RADIO_RESTART_MS + 2 * CONTROL_PERIOD_MS);

    TEST_ASSERT_FALSE(stand.scheduler.stepBudgetExhausted());
    // One restart, not more: the next would wait out a minute's pause and a gateway check after it.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.wifi.restarts.size(), "the radio was not restarted once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(NETWORK_TASK, stand.wifi.restarts[0].task.c_str(),
                                     "the restart ran on the wrong task");

    const long long answered = stand.logger.firstSaying("WIFI_RECONNECT: restart result=1");
    char message[192];
    snprintf(message, sizeof(message), "decided at %lld ms, answered at %lld ms, the restart ended at %llu ms",
             decided, answered, (unsigned long long)stand.wifi.restarts[0].finishedAtMs);
    TEST_ASSERT_TRUE_MESSAGE(answered >= 0, "the outcome never reached the tape");
    TEST_ASSERT_TRUE_MESSAGE(answered >= (long long)stand.wifi.restarts[0].finishedAtMs, message);

    const uint64_t worstStep = stand.steps.worstBetween(0, stand.clock.nowMs());
    snprintf(message, sizeof(message),
             "the control loop's worst step through the probes and the restart was %llu ms (limit %u ms)",
             (unsigned long long)worstStep, (unsigned)MAX_ACCEPTED_STEP_MS);
    TEST_ASSERT_TRUE_MESSAGE(worstStep <= MAX_ACCEPTED_STEP_MS, message);
}

// ================================================================================================
// The gate
// ================================================================================================

/** @brief The outbox alone, over a server behind a link that comes and goes. */
struct GateStand
{
    VirtualClock clock;
    HostTaskScheduler scheduler{clock};
    SimArduino arduino{clock, &scheduler};
    SimHeap heap{clock, arduino};
    TapeLogger logger{clock};
    MockDebugSystem debug;
    StormWifi wifi{scheduler, clock};
    StormServer server{scheduler, heap, wifi, clock};
    HostMessageQueue<OutboxCompletion> completions{Outbox::COMPLETION_QUEUE_DEPTH};
    Outbox outbox{&server, &scheduler, &completions, &logger, &debug};

    explicit GateStand(bool gated)
    {
        debug.featureEnabled = false;
        wifi.setConnected(true);
        if (gated)
        {
            outbox.setWifiAdapter(&wifi);
        }
    }

    std::vector<OutboxCompletion> drain()
    {
        std::vector<OutboxCompletion> collected;
        OutboxCompletion completion;
        while (outbox.pollCompletion(completion))
        {
            collected.push_back(completion);
        }
        return collected;
    }
};

/**
 * The recorded run's side reading: fourteen seconds of link down, and a telemetry request spent
 * its four attempts on it. With the gate the request waits, at no cost in attempts, and goes out
 * on its first attempt once the link is back.
 *
 * The ungated run is the shape before, and the red: an outbox that knows nothing of the radio.
 */
void test_a_request_does_not_spend_attempts_while_the_link_is_down(void)
{
    const uint64_t OUTAGE_MS = 14000;

    GateStand before(false);
    before.wifi.setConnected(false);
    before.outbox.submitSensorUpdate(UpdateDto());
    before.scheduler.runForMs(OUTAGE_MS);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(Outbox::DEFAULT_MAX_ATTEMPTS, (uint32_t)before.server.calls.size(),
                                     "without the gate the request spends its whole budget on the "
                                     "outage - this is the shape before");
    TEST_ASSERT_EQUAL_UINT32(1, before.outbox.undeliveredCount());

    GateStand after(true);
    after.wifi.setConnected(false);
    const uint32_t ticket = after.outbox.submitSensorUpdate(UpdateDto());
    after.scheduler.runForMs(OUTAGE_MS);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)after.server.calls.size(),
                                     "a request was attempted over a link that was down");
    TEST_ASSERT_EQUAL_UINT32(0, after.outbox.undeliveredCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)after.outbox.pendingCount(),
                                     "the request waiting for the link was dropped");

    after.wifi.setConnected(true);
    after.scheduler.runForMs(TLS_HANDSHAKE_MS + REQUEST_BODY_MS + 1000);

    std::vector<OutboxCompletion> completions = after.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_EQUAL_UINT32(ticket, completions[0].ticket);
    TEST_ASSERT_TRUE_MESSAGE(completions[0].delivered, "the request was not delivered once the link was back");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)completions[0].attempts,
                                     "the wait for the link cost the request attempts");
}

/**
 * The same for a request already in flight when the link drops: the attempt that failed is paid
 * for, the ones left are not spent on a radio that is off, and the request goes out when the link
 * is back rather than being given up on.
 *
 * Mutation: skip holdForLink() between attempts.
 */
void test_a_request_in_flight_when_the_link_drops_waits_for_it(void)
{
    GateStand stand(true);
    stand.server.failedCallMs = 100;

    // The link goes while the first attempt is on the wire: the attempt fails, as a socket does.
    const uint32_t ticket = stand.outbox.submitDeviceUpdate(UpdateDto());
    stand.scheduler.runForMs(50);
    stand.wifi.setConnected(false);
    stand.scheduler.runForMs(20000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.server.calls.size(),
                                     "an attempt was made over the link that had dropped");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.drain().size(),
                                     "the request in flight was given up on while the link was down");

    stand.wifi.setConnected(true);
    stand.scheduler.runForMs(TLS_HANDSHAKE_MS + REQUEST_BODY_MS + 1000);

    std::vector<OutboxCompletion> completions = stand.drain();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)completions.size());
    TEST_ASSERT_EQUAL_UINT32(ticket, completions[0].ticket);
    TEST_ASSERT_TRUE(completions[0].delivered);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)completions[0].attempts,
                                     "the wait for the link was charged as attempts");
}

/**
 * A restart asked for while a request waits, with the link up, goes first. The gate lets both
 * through here - the link is up - so the order is the queue's own: the restart is the link's job,
 * and a request made over a radio about to be restarted would only spend an attempt on it.
 *
 * Mutation: take the restart after the requests.
 */
void test_a_restart_asked_for_while_a_request_waits_goes_first(void)
{
    GateStand stand(true);

    // The same loop iteration submits a request and asks for the restart; the task sees both at
    // its next poll.
    const uint32_t request = stand.outbox.submitDiagnostic(DiagnosticDto());
    const uint32_t restart = stand.outbox.submitWifiReconnect();
    stand.scheduler.runForMs(RADIO_RESTART_MS + TLS_HANDSHAKE_MS + REQUEST_BODY_MS + 2000);

    TEST_ASSERT_FALSE(stand.scheduler.stepBudgetExhausted());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.wifi.restarts.size(), "the restart was not made");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.server.calls.size(), "the request was not made");
    char message[192];
    snprintf(message, sizeof(message),
             "the request went out at %llu ms, the restart ran %llu..%llu ms - the request went first",
             (unsigned long long)stand.server.calls[0].startedAtMs,
             (unsigned long long)stand.wifi.restarts[0].startedAtMs,
             (unsigned long long)stand.wifi.restarts[0].finishedAtMs);
    TEST_ASSERT_TRUE_MESSAGE(stand.wifi.restarts[0].finishedAtMs <= stand.server.calls[0].startedAtMs, message);

    std::vector<OutboxCompletion> completions = stand.drain();
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)completions.size());
    TEST_ASSERT_EQUAL_UINT32(restart, completions[0].ticket);
    TEST_ASSERT_EQUAL_UINT32(request, completions[1].ticket);
    TEST_ASSERT_TRUE(completions[1].delivered);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)completions[1].attempts,
                                     "the request spent an attempt on the radio being restarted");
}

// ================================================================================================
// The broker that says nothing
// ================================================================================================

/** @brief The outbox with the real link machine over a broker that never answers. */
struct DeadBrokerStand
{
    VirtualClock clock;
    HostTaskScheduler scheduler{clock};
    SimArduino arduino{clock, &scheduler};
    SimHeap heap{clock, arduino};
    TapeLogger logger{clock};
    MockDebugSystem debug;
    StormWifi wifi{scheduler, clock};
    StormServer server{scheduler, heap, wifi, clock};
    HostMessageQueue<OutboxCompletion> completions{Outbox::COMPLETION_QUEUE_DEPTH};
    Outbox outbox{&server, &scheduler, &completions, &logger, &debug};
    AsyncMqttClient client;
    std::recursive_mutex clientMutex;
    MqttManager mqtt{client, &logger, &debug, CLIENT_ID_PREFIX, IPAddress(116, 203, 235, 144), 1883,
                     "user",  "secret", nullptr, nullptr, &wifi, &clientMutex};
    ClockedLink link{mqtt, client, clock, &heap};

    DeadBrokerStand()
    {
        debug.featureEnabled = false;
        wifi.setConnected(true);
        outbox.setWifiAdapter(&wifi);
        outbox.setMqttLink(&link);
        // No broker attached: a CONNECT that nothing ever answers, which is what a dead broker
        // looks like to the client. The boot asks once, as setupMqttConnection() does.
        hostUptimeMs = 0;
        mqtt.setClientId(clientId().c_str());
        mqtt.connect();
    }

    /** The broker stayed dead: the client dialled, and nothing ever answered. */
    bool theBrokerNeverAnswered() const
    {
        return !client.connected() && client.connectAttempts > 0;
    }
};

/**
 * A broker that never answers costs the queue one act per backoff cycle and nothing more: the
 * close grace and the connect timeout, and then the requests go - the backoff between acts is
 * theirs. Every request is delivered while the broker is dead, and none waits longer than an act.
 *
 * The bound is the act's length as MqttManager defines it. It is not CONNECT_TIMEOUT_MS alone: a
 * socket that is already down settles by its close grace, not by an event, and that grace is part
 * of the act.
 */
void test_a_broker_that_says_nothing_delays_a_request_by_at_most_one_act(void)
{
    DeadBrokerStand stand;

    const uint32_t PERIOD_MS = 4000;
    const uint32_t RUN_MS = 120000;
    std::vector<uint64_t> submittedAtMs;

    for (uint64_t elapsed = 0; elapsed < RUN_MS; elapsed += PERIOD_MS)
    {
        char uid[16];
        snprintf(uid, sizeof(uid), "rule-%u", (unsigned)submittedAtMs.size());
        TEST_ASSERT_NOT_EQUAL(IOutbox::kRejected, stand.outbox.submitRuleStateByUid(uid, "{}"));
        submittedAtMs.push_back(stand.clock.nowMs());
        stand.scheduler.runForMs(PERIOD_MS);
    }
    stand.scheduler.runForMs(30000);

    TEST_ASSERT_FALSE(stand.scheduler.stepBudgetExhausted());
    TEST_ASSERT_TRUE_MESSAGE(stand.link.acts.size() >= 3, "the link machine did not cycle through its acts");
    TEST_ASSERT_TRUE_MESSAGE(stand.theBrokerNeverAnswered(), "the dead broker answered");

    // Events go out in the order they were submitted, so the i-th call answers the i-th submit.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)submittedAtMs.size(), (uint32_t)stand.server.calls.size(),
                                     "a request was lost or made twice");
    TEST_ASSERT_EQUAL_UINT32(0, stand.outbox.undeliveredCount());

    uint64_t worstDelayMs = 0;
    for (size_t i = 0; i < submittedAtMs.size(); i++)
    {
        TEST_ASSERT_TRUE(stand.server.calls[i].answered);
        const uint64_t delay = stand.server.calls[i].startedAtMs - submittedAtMs[i];
        worstDelayMs = std::max(worstDelayMs, delay);
    }

    const uint64_t oneAct = MqttManager::CLOSE_GRACE_MS + MqttManager::CONNECT_TIMEOUT_MS;
    const uint64_t bound = oneAct + Outbox::LINK_BEAT_MS + Outbox::IDLE_POLL_MS + 2 * REQUEST_BODY_MS;
    char message[256];
    snprintf(message, sizeof(message),
             "the worst wait behind the dead broker was %llu ms; one act is %llu ms (close grace + "
             "connect timeout), the bound with the beats and the requests ahead is %llu ms",
             (unsigned long long)worstDelayMs, (unsigned long long)oneAct, (unsigned long long)bound);
    printf("  network-offboard: %s\n", message);
    TEST_ASSERT_TRUE_MESSAGE(worstDelayMs <= bound, message);
    TEST_ASSERT_TRUE_MESSAGE(worstDelayMs > REQUEST_BODY_MS,
                             "no request ever waited behind an act - the act held nothing");
}

// ================================================================================================
// The watchdog
// ================================================================================================

/**
 * A radio restart asked for a lost link that the network task reaches with the station associated is
 * not run, and its completion says the station is associated.
 */
void test_a_restart_for_a_lost_link_is_not_run_on_a_link_that_is_up(void)
{
    DeadBrokerStand stand;
    stand.wifi.setConnected(true);

    const uint32_t ticket = stand.outbox.submitWifiReconnectForALostLink();
    stand.scheduler.runForMs(stand.wifi.restartMs + 5000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.wifi.restarts.size(),
                                     "the radio was restarted on a link that is up");
    OutboxCompletion completion;
    TEST_ASSERT_TRUE_MESSAGE(stand.completions.pop(completion), "the restart produced no completion");
    TEST_ASSERT_EQUAL_UINT32(ticket, completion.ticket);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)OutboxKind::WIFI_RECONNECT, (uint8_t)completion.kind);
    TEST_ASSERT_TRUE_MESSAGE(completion.delivered, "the completion did not say the station is associated");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, completion.attempts,
                                    "a restart that was not run was counted as an attempt");
}

/**
 * Asked for both ways while it waits - for a lost link and unconditionally, in either order - the
 * restart is one, and it is the unconditional one: it runs on a link that is up.
 */
void test_a_restart_asked_for_both_ways_is_the_unconditional_one(void)
{
    for (int order = 0; order < 2; order++)
    {
        DeadBrokerStand stand;
        stand.wifi.setConnected(true);

        if (order == 0)
        {
            stand.outbox.submitWifiReconnectForALostLink();
            stand.outbox.submitWifiReconnect();
        }
        else
        {
            stand.outbox.submitWifiReconnect();
            stand.outbox.submitWifiReconnectForALostLink();
        }
        stand.scheduler.runForMs(stand.wifi.restartMs + 5000);

        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, (uint32_t)stand.wifi.restarts.size(),
                                         order == 0 ? "lost link, then unconditional: not one restart"
                                                    : "unconditional, then lost link: not one restart");
    }
}

/**
 * The network task under the task watchdog through the longest legal link job: a restart that
 * fails after its whole minute and then scans what is in range, with the link machine's acts on
 * the same task. The restart feeds after every run() that fails with time left; the run() that
 * ends the minute and the scan after it are one unfed unit, and it is that unit - not the minute -
 * the outbox-net entry of the budget table is made of. The gap the model shows is asserted from
 * both sides, so a stand-in that feeds where the device does not is caught as well.
 *
 * Mutation: a scan of 200 s fires the model; a feed before the scan makes the gap the size of the
 * scan alone, under the unit the table names. The claim here is the entry's arithmetic.
 */
void test_a_minute_long_restart_keeps_the_network_task_under_a_quiet_watchdog(void)
{
    DeadBrokerStand stand;
    SimTaskWatchdog watchdog(stand.scheduler, stand.clock, (uint32_t)WDT_TIMEOUT_S * 1000UL);
    watchdog.start();

    stand.wifi.restartSucceeds = false;
    stand.wifi.restartMs = 60000;
    stand.wifi.attemptMs = WIFI_MULTI_RUN_MS;
    stand.wifi.scanAfterFailureMs = 10000; // the framework's scan wait, WiFiScanClass::scanNetworks

    stand.scheduler.runForMs(1000);
    stand.wifi.setConnected(false);
    const uint32_t ticket = stand.outbox.submitWifiReconnect();
    // Long enough for the watchdog to have fired had the job left a gap of the whole timeout
    // after its last feed - so a job that outgrows the budget is seen as the watchdog's verdict,
    // not as a restart that has not finished yet.
    stand.scheduler.runForMs(stand.wifi.restartMs + stand.wifi.scanAfterFailureMs +
                             (uint64_t)WDT_TIMEOUT_S * 1000UL);

    TEST_ASSERT_FALSE(stand.scheduler.stepBudgetExhausted());

    char message[256];
    snprintf(message, sizeof(message), "the watchdog fired for '%s' with a %llu ms gap",
             watchdog.firedTaskName().c_str(), (unsigned long long)watchdog.firedGapMs());
    TEST_ASSERT_FALSE_MESSAGE(watchdog.fired(), message);

    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)stand.wifi.restarts.size());
    TEST_ASSERT_EQUAL_STRING(NETWORK_TASK, stand.wifi.restarts[0].task.c_str());

    OutboxCompletion completion;
    TEST_ASSERT_TRUE_MESSAGE(stand.completions.pop(completion), "the restart produced no completion");
    TEST_ASSERT_EQUAL_UINT32(ticket, completion.ticket);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)OutboxKind::WIFI_RECONNECT, (uint8_t)completion.kind);
    TEST_ASSERT_FALSE_MESSAGE(completion.delivered, "a restart that failed was reported as associated");

    const uint64_t worstGap = watchdog.worstGapMs(NETWORK_TASK);
    const uint64_t unfedUnit = stand.wifi.attemptMs + stand.wifi.scanAfterFailureMs;
    snprintf(message, sizeof(message),
             "outbox-net showed a %llu ms feed gap through a minute-long restart; the unfed unit is "
             "the last run() (%u ms) and the scan after it (%u ms), and the budget the threshold is "
             "computed from is %u s",
             (unsigned long long)worstGap, (unsigned)stand.wifi.attemptMs,
             (unsigned)stand.wifi.scanAfterFailureMs, (unsigned)WDT_WORST_LEGAL_PAUSE_S);
    printf("  network-offboard: %s\n", message);
    TEST_ASSERT_TRUE_MESSAGE(worstGap <= unfedUnit + Outbox::LINK_BEAT_MS + SimTaskWatchdog::POLL_MS + 500,
                             message);
    // The watchdog model samples the gap, so it may see up to one poll less than the unit.
    TEST_ASSERT_TRUE_MESSAGE(worstGap + SimTaskWatchdog::POLL_MS >= unfedUnit, message);
    TEST_ASSERT_TRUE_MESSAGE(worstGap <= (uint64_t)WDT_WORST_LEGAL_PAUSE_S * 1000UL, message);
}

// ================================================================================================
// The completion queue
// ================================================================================================

/**
 * Every kind in flight at once - one of every state slot, the radio restart, and a full event
 * ring - and every outcome comes back. The depth the composition root uses is the one under test.
 *
 * Mutation: size the queue at MAX_IN_FLIGHT - 1.
 */
void test_every_kind_in_flight_at_once_loses_no_completion(void)
{
    GateStand stand(true);
    stand.server.failedCallMs = 0;

    // Nothing runs yet: the scheduler is cooperative and the test body is not a task, so what is
    // submitted here waits until runForMs() below.
    stand.outbox.submitSensorUpdate(UpdateDto());
    stand.outbox.submitDeviceUpdate(UpdateDto());
    stand.outbox.submitDiagnostic(DiagnosticDto());
    stand.outbox.submitRuleListRequest();
    stand.outbox.submitDeviceCommandFetch();
    stand.outbox.submitModuleCommandFetch();
    stand.outbox.submitSensorTypesRequest();
    stand.outbox.submitWifiReconnect();
    // The second act of the link. No roam radio is wired here, so it comes back undelivered - what
    // this test counts is the outcome, which is owed either way.
    const uint8_t elsewhere[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    stand.outbox.submitWifiRoam(elsewhere, 6);
    // The third act of the link. No probe is wired either: it comes back saying the gateway could not
    // be asked, which is an outcome all the same.
    stand.outbox.submitLinkProbe();
    for (size_t i = 0; i < Outbox::EVENT_RING_CAPACITY; i++)
    {
        TEST_ASSERT_NOT_EQUAL(IOutbox::kRejected, stand.outbox.submitRuleStateByUid("rule", "{}"));
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)Outbox::MAX_IN_FLIGHT, (uint32_t)stand.outbox.pendingCount(),
                                     "MAX_IN_FLIGHT does not count every kind that can wait");
    TEST_ASSERT_TRUE(stand.completions.capacity() >= Outbox::MAX_IN_FLIGHT);

    stand.scheduler.runForMs(120000);

    TEST_ASSERT_FALSE(stand.scheduler.stepBudgetExhausted());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stand.outbox.lostCompletionCount(),
                                     "an outcome was lost with every kind in flight");
    TEST_ASSERT_EQUAL_UINT32(0, stand.completions.overflowCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)Outbox::MAX_IN_FLIGHT, (uint32_t)stand.completions.size(),
                                     "not every request produced its completion");
}

/**
 * The bound on the outcomes owed at one drain, walked on its worst path: a request held for the
 * link inside dispatch(), the restart that hold takes and runs, and behind them every slot and the
 * whole ring filled again - MAX_IN_FLIGHT plus the two in dispatch, none of them drained, none
 * lost. The depth the composition root uses is the one under test, and the count reaches it exactly.
 *
 * Mutation: size the queue at MAX_IN_FLIGHT.
 */
void test_the_worst_path_owes_two_outcomes_beyond_every_kind_and_loses_none(void)
{
    GateStand stand(true);
    stand.server.failedCallMs = 100;

    // A request in flight when the link drops: its first attempt fails under the handshake, and
    // it waits for the link inside dispatch(), out of its slot.
    stand.outbox.submitDeviceUpdate(UpdateDto());
    stand.scheduler.runForMs(50);
    stand.wifi.setConnected(false);
    stand.scheduler.runForMs(TLS_HANDSHAKE_MS + 1000);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)stand.server.calls.size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.outbox.pendingCount(),
                                     "the request held for the link still counts as waiting");

    // Every slot and the ring, filled behind it - the slot the held request left included.
    stand.outbox.submitSensorUpdate(UpdateDto());
    stand.outbox.submitDeviceUpdate(UpdateDto());
    stand.outbox.submitDiagnostic(DiagnosticDto());
    stand.outbox.submitRuleListRequest();
    stand.outbox.submitDeviceCommandFetch();
    stand.outbox.submitModuleCommandFetch();
    stand.outbox.submitSensorTypesRequest();
    stand.outbox.submitWifiReconnect();
    const uint8_t elsewhere[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    stand.outbox.submitWifiRoam(elsewhere, 6);
    stand.outbox.submitLinkProbe();
    for (size_t i = 0; i < Outbox::EVENT_RING_CAPACITY; i++)
    {
        TEST_ASSERT_NOT_EQUAL(IOutbox::kRejected, stand.outbox.submitRuleStateByUid("rule", "{}"));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)Outbox::MAX_IN_FLIGHT, (uint32_t)stand.outbox.pendingCount());

    // The hold takes the restart and runs it; its slot is free again and is filled again.
    stand.scheduler.runForMs(200);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stand.wifi.reconnectCallCount, "the hold did not take the restart");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)stand.wifi.restarts.size(), "the restart is over already");
    TEST_ASSERT_NOT_EQUAL(IOutbox::kRejected, stand.outbox.submitWifiReconnect());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)Outbox::MAX_IN_FLIGHT, (uint32_t)stand.outbox.pendingCount());

    // Owed now: the held request, the restart in flight, and everything waiting. Nothing drains.
    stand.scheduler.runForMs(60000);

    TEST_ASSERT_FALSE(stand.scheduler.stepBudgetExhausted());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)stand.wifi.restarts.size());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)stand.outbox.pendingCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stand.outbox.lostCompletionCount(),
                                     "an outcome was lost on the worst path");
    TEST_ASSERT_EQUAL_UINT32(0, stand.completions.overflowCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)(Outbox::MAX_IN_FLIGHT + Outbox::JOBS_IN_DISPATCH),
                                     (uint32_t)stand.completions.size(),
                                     "the worst path did not owe every outcome it can");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)Outbox::COMPLETION_QUEUE_DEPTH, (uint32_t)stand.completions.size(),
                                     "the depth is not the bound: either room is wasted or an outcome can be lost");
}

// ================================================================================================
// The client without a task
// ================================================================================================

/**
 * MqttManager creates no task of its own any more: connect() configures the client and asks it to
 * dial, and the state machine is somebody else's to beat. The FreeRTOS stand-in records every
 * task the code under test asks for.
 *
 * Red on the revision before: connect() created "MqttRecovery".
 */
void test_the_mqtt_client_creates_no_task_of_its_own(void)
{
    hostTasksReset();

    SimMqttBroker broker;
    MockLogger logger;
    MockDebugSystem debug;
    MockWifiAdapter wifi;
    AsyncMqttClient client;
    std::recursive_mutex clientMutex;
    MqttManager mqtt(client, &logger, &debug, CLIENT_ID_PREFIX, IPAddress(116, 203, 235, 144), 1883,
                     "user", "secret", nullptr, nullptr, &wifi, &clientMutex);
    debug.featureEnabled = false;
    wifi.setConnected(true);
    client.attachBroker(&broker);

    mqtt.setClientId(clientId().c_str());
    TEST_ASSERT_TRUE(mqtt.connect());
    TEST_ASSERT_TRUE(mqtt.isConnected());

    // And through a reconnect, which is another place a task could be started from.
    client.disconnect();
    TEST_ASSERT_FALSE(mqtt.isConnected());
    for (int beat = 0; beat < 100 && !mqtt.isConnected(); beat++)
    {
        hostUptimeAdvance(MQTT_RECOVERY_PERIOD_MS);
        mqtt.serviceLinkOnce();
    }
    TEST_ASSERT_TRUE_MESSAGE(mqtt.isConnected(), "the machine did not reconnect when beaten");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)hostCreatedTasks.size(),
                                     "the MQTT client created a task of its own");
    hostTasksReset();
}

/**
 * The machine does nothing until the boot has asked for the broker once: beaten before connect(),
 * with the link up, it dials nothing - a CONNECT then would present the client id the boot has not
 * composed yet. After connect() the same beats drive a reconnect to its end.
 *
 * Mutation: drop the _configured check from serviceLinkOnce().
 */
void test_the_link_machine_is_idle_until_the_boot_asks_for_the_broker(void)
{
    SimMqttBroker broker;
    MockLogger logger;
    MockDebugSystem debug;
    MockWifiAdapter wifi;
    AsyncMqttClient client;
    std::recursive_mutex clientMutex;
    MqttManager mqtt(client, &logger, &debug, CLIENT_ID_PREFIX, IPAddress(116, 203, 235, 144), 1883,
                     "user", "secret", nullptr, nullptr, &wifi, &clientMutex);
    debug.featureEnabled = false;
    wifi.setConnected(true);
    client.attachBroker(&broker);

    hostUptimeMs = 100000;
    for (int beat = 0; beat < 50; beat++)
    {
        hostUptimeAdvance(MQTT_RECOVERY_PERIOD_MS);
        TEST_ASSERT_FALSE_MESSAGE(mqtt.serviceLinkOnce(), "an act began before the boot asked for the broker");
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, client.connectAttempts, "the client dialled before the boot asked it to");

    mqtt.setClientId(clientId().c_str());
    mqtt.connect();
    client.disconnect();

    bool sawAnAct = false;
    for (int beat = 0; beat < 100 && !mqtt.isConnected(); beat++)
    {
        hostUptimeAdvance(MQTT_RECOVERY_PERIOD_MS);
        sawAnAct = mqtt.serviceLinkOnce() || sawAnAct;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawAnAct, "no act was reported in progress during the reconnect");
    TEST_ASSERT_TRUE(mqtt.isConnected());
    TEST_ASSERT_EQUAL_STRING(clientId().c_str(), client.clientId());
}

} // namespace

extern "C" void run_network_offboard_tests(void)
{
    RUN_TEST(test_the_storm_with_the_restart_on_the_loop_stalls_the_loop_and_sums_the_heap);
    RUN_TEST(test_the_storm_on_one_network_task_keeps_the_loop_stepping);
    RUN_TEST(test_the_storm_on_one_network_task_bottoms_out_at_its_largest_network_participant);
    RUN_TEST(test_the_link_comes_back_in_order_radio_then_broker_then_server);
    RUN_TEST(test_a_push_from_a_person_is_dispatched_and_fetched_inside_the_storm);
    RUN_TEST(test_a_restart_is_decided_on_the_loop_and_made_on_the_network_task);
    RUN_TEST(test_a_restart_in_flight_is_not_asked_for_twice);
    RUN_TEST(test_a_dead_broker_behind_a_silent_gateway_restarts_the_radio_on_the_network_task);
    RUN_TEST(test_a_request_does_not_spend_attempts_while_the_link_is_down);
    RUN_TEST(test_a_request_in_flight_when_the_link_drops_waits_for_it);
    RUN_TEST(test_a_restart_asked_for_while_a_request_waits_goes_first);
    RUN_TEST(test_a_broker_that_says_nothing_delays_a_request_by_at_most_one_act);
    RUN_TEST(test_a_minute_long_restart_keeps_the_network_task_under_a_quiet_watchdog);
    RUN_TEST(test_a_restart_for_a_lost_link_is_not_run_on_a_link_that_is_up);
    RUN_TEST(test_a_restart_asked_for_both_ways_is_the_unconditional_one);
    RUN_TEST(test_every_kind_in_flight_at_once_loses_no_completion);
    RUN_TEST(test_the_worst_path_owes_two_outcomes_beyond_every_kind_and_loses_none);
    RUN_TEST(test_the_mqtt_client_creates_no_task_of_its_own);
    RUN_TEST(test_the_link_machine_is_idle_until_the_boot_asks_for_the_broker);
}
