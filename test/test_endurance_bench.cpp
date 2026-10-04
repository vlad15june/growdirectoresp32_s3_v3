// The endurance bench - two simulated weeks of the whole module against the host stand-ins.
//
// Every other scenario file asks what the module does in one situation over minutes. This one
// asks what it does over a fortnight: the scenarios prove a mechanism, endurance proves that
// nothing accumulates behind them. It is the cheapest test in the suite per simulated day and
// the only one that can see a slow drift at all.
//
// What is under test is production code, wired as globals.h wires it except where the last
// section of this comment says otherwise - that list is what a green run does not cover: the real
// ModuleSetupService, which waits for no network, and the real BootSequencer, whose gates the
// producers ask; the real LinkSupervisor with its troubleshooting machine, taking its looks at the
// link through the outbox; the real LoopServiceConfigureDone driving the real DeviceService,
// SensorService, RuleService, CommandService and DiagnosticService; the real SensorAcquisitionService on the real
// SensorReader/ModbusSensorReader over a simulated RS-485 line; the real Outbox with its network
// task; the real SyncRulesUseCase, EvaluateRulesUseCase, ModuleCommandExecutionUseCase and
// NetworkCommandRunner; the real CounterReport, including the memory columns. Replaced are the
// outside world (server, line, MQTT broker, OTA transport, the radio and what lies beyond it), the
// storage DAOs, the watchdog timers - and time.
//
// ### What the run asserts, and where
//
// Sampled every simulated hour and asserted over the whole series, not only at the end:
//   - the task watchdog never fired and the external-kick gate never latched;
//   - the worst control-loop step stayed under the limit - measured from outside by the bench,
//     and independently by the firmware itself, whose max_loop_step_ms column reports the worst
//     step of every diagnostic period;
//   - refusal and drop counters stayed at zero; the outbox and the completion queue came back
//     to empty every hour;
//   - registries and caches kept their size, and the live allocation balance of an hour equals
//     that of the same hour a day earlier, to the block and to the byte;
//   - the DIAG-COUNTERS line kept coming out on its own cadence;
//   - every hour ended on a module at rest: the boot sequence in STEADY with its gates open, the
//     link troubleshooting back at OK, and diagnostics reaching the server.
//
// ### What happens to the link, and what is asserted of every return
//
// A plan of nineteen windows a day, the same every day, opens faults of the four kinds the link
// troubleshooting tells apart: the server down, the internet down behind a router that answers, a
// router that does not answer behind an associated station, and an access point gone - some of the
// last coming back by themselves, some only through a radio restart. Two hundred and sixty-six
// returns in the fortnight. Each is judged once its allowance is over: the boot sequence is back
// in STEADY with every gate open and the troubleshooting at OK; the radio was restarted only on
// its side of the router, on the pauses the machine's own table names, and the module not at all;
// the structure went out once for a new address and not otherwise; the outage was recorded once,
// with its cause; and diagnostics reach the server again. Over the whole run the radio was started
// by nothing but those restarts and the boot's association.
//

// One thing happens to the module rather than around it: seven hours in, the account changes the
// sensor model its sensors are bound to, and the change moves a computed channel. The module
// stores the new catalog, updates its sensors and republishes its structure - the last outgoing
// HTTP call the control loop could still make - and the step limit above has to hold through it.
//
// And at the end of the fortnight, on the module that has been running all that time, the two
// steady-state allocation guards of the loop are re-measured: an idle control
// iteration and a steady rule evaluation tick must still allocate exactly nothing.
//
// ### What this bench cannot see
//
// Heap fragmentation - deliberately. The host allocator says nothing about the device heap, and
// a bench that pretended otherwise would be convincingly wrong; that question belongs to the
// hardware run. For the same reason the stack watermark columns of the DIAG line read "n/a"
// here: the host scheduler has no stack accounting to report and the line says so rather than
// printing a comforting zero. Both are asserted below as the honest absences they are.
//
// The live balance is measured in both blocks and bytes, and the two together answer more than
// either alone - a runaway container is few blocks and many bytes, a forgotten small object is
// many blocks and few bytes. What neither answers is the shape of the heap those bytes are
// spread across, which is the same absence as fragmentation above.
//
// The balance is also a snapshot at the hour boundary, so a peak that grows inside an hour and
// is released before the checkpoint is invisible by construction. On a device it is the peak
// that runs the heap out, not the settled level; that measurement belongs on the board.
//
// The first day of the run is outside both memory claims: the day-against-day comparison looks
// at the last three days, and the balance is drawn from hour 24 onwards, because everything
// before that is the module settling into its caches.
//
// The rate of heap traffic is reported and not claimed. An astronomical hour of the second week
// does not contain the same work as the same hour of the first - the recovery cycle a silent
// port runs is 105 seconds long and divides neither - so comparing the two costs measures where
// a boundary fell, not whether the module got more expensive. The claim a comparison would carry is
// carried by the live balance, exactly. The long note at the measurement says what was tried.
//
// The device's 32-bit millis() wraparound (49.7 days) is out of reach too: SimArduino::millis()
// returns the host's 64-bit unsigned long, so no simulated span reaches the wrap.
//
// Three of the eight rule types run here: SENSOR, SCHEDULE and LOOP. STEP does not, so the step
// cycle store is wired and never used; the remaining four - ph, ec, dimmer_auto, dimmer_manual -
// are validated and reported synchronized by this firmware and never evaluated at all, which is
// why no fortnight of running them would prove anything.
//
// ### What is not in the graph
//
// Not reachable on the host without a stand-in that would be a second copy of the thing under
// test: the WiFi and MQTT transports, SPIFFS behind the DAOs, the UDP paths, the web servers.
// Substituted rather than absent: the server, the RS-485 line, the MQTT broker, the OTA
// transport, the storage DAOs, the clock, the scheduler, the two watchdog timers, the logger,
// the OTA partition control and the OTA log.
//
// ### Where the wiring differs from globals.h, and what a green run therefore does not say
//
// The bench module is not a module of the board. It is a hydro module with four RS-485 sensor
// ports and three pumps, because that is the one type whose configuration has both halves of this
// bench: ModuleConfig::getSensorPinCount() answers four for a sensor and a hydro module, and only
// a hydro module has pumps that ProcessDeviceStateUseCase drives to a pin. On the board the two
// halves do not meet. The UART pins of the first line, 17 and 18, are the outputs of pumps 2 and 3
// on a hydro module, so its setup does not begin the line: ModuleSetupService::setupSensorBus()
// begins it for a sensor module only, and only for a sensor module does the setup power the ports
// of the stored sensors or arm the discovery. The bench begins the line itself, in wireTheGraph().
// So the sensor half of the run - the sweeps, the recovery of a silent port, the daily ping, the
// dark ports - is proven on a combination that exists in no module, and a sensor module, the one
// that has the line, is never taken through its own setup for the fortnight.
//
// Left out of the wiring although the host could build them, and what each costs:
//   - StartupSensorScanUseCase, which globals.h hands to the setup, the boot sequence, the
//     acquisition task, the command service and the structure republish. The boot's discovery
//     and preparation pass is never armed and never runs on the acquisition task between the
//     sweeps; the DISCOVERY gate is opened by the boot sequence with no producer asking it; a
//     catalog that settles after the boot arms nothing.
//   - commandService.setModuleConfig. With the configuration the command service answers a
//     PingPin, SetSensorModel or ScanSensors on a module that is not a sensor module with "No
//     sensor line on this module" and the command never reaches the bus - which is the answer a
//     hydro module gets on the board. The daily ping, and the assertions about the dark ports it
//     leaves behind, stand on this omission.
//   - outbox.setMqttLink. The outbox's network task never beats the MQTT connect machine, so a
//     return pays no MQTT connect act: requests do not wait for it and holdForLink() does not
//     serve it. The broker stand-in comes back together with the server.
//   - WifiRoamUseCase, outbox.setRoamRadio and the background act board. No roam is ever decided;
//     the board that says what each of the four background tasks is doing is written by none of
//     them and read by nobody, so the supervision's pause while the station moves between access
//     points, and the loop's heap-low trace naming the acts alive, never see an act.
//   - SensorTypeCatalogWriter. The synchronization stores the catalog it fetched inline, on the
//     control loop, instead of handing it to a storage task: there is no ticket, no pending write
//     and no task created and gone per write. What the bench asserts through the catalog change
//     - the loop step, the allocation balance - is about the inline write.
//   - FirmwareUpdateRecoveryUseCase. Nothing holds an update command back while the running
//     image is on probation, keeps one for the next boot when the HTTP connection is down, or
//     resumes one at boot; the update here is the plain path - command, download on the command
//     task, outcome.
//   - The collaborators of SensorService that globals.h gives it: the plausibility gate, the
//     virtual channels and the UDP manager. Every reading is accepted, the reading path values
//     and reports no computed channel (the structure still describes one), and no reading goes
//     out over UDP.
//   - The settings of the command service. It never learns that the module was registered when
//     the boot began, so the deletion check the boot owes its first STEADY is skipped
//     (CommandService::checkModuleDeletion()) and the stand-in's queryModuleDeletion() below is
//     never called.
//   - Every writer of ModuleStatusBoard except the outbox: the radio, the HTTP client and the TLS
//     client. "The server answered with a status" is never recorded, so the supervision always
//     reads it as false and the branch of the troubleshooting that takes the server's own answer
//     as proof of the internet does not run; and of the four faults of the module itself (TLS
//     without memory, a radio that would not start, refused sockets, a refused network task) the
//     first three can never be raised, and the fourth only if the scheduler refuses a task, which
//     this run never makes it do. "The module is not restarted" holds for a module that cannot
//     show them.
//   - RuleStateReportingService and, in SyncRulesUseCase, the settings. The rule evaluation has
//     no notifier, so the reports of a protective shutdown, of a sensor out of reach and of a
//     rule recovered are never made, and the rule sync leaves the account out of its report.
//   - The collaborators of the command service and the module-command executor that only other
//     commands use: the device and sensor command use cases and the rule repository (a rule
//     pushed over MQTT is dropped), the LED use cases, the remote command sender, the Modbus
//     address allocator, the structure use case and the catalog synchronization. Of the module
//     commands the run issues two kinds, PingPin and UpdateFirmware.
//   - OnChangeModuleModeUseCase and ClearModuleDataService, which the loop service factory gives
//     the loop. A mode the code under test requests - the restart after an update included - is
//     seen as the requested mode and never carried out, so the run goes on past it.
//   - PreventiveRestartUseCase - the nightly restart. This is one of the two ways the module
//     restarts itself, and it is not in this run: "no unexplained restarts" here means the
//     watchdog never fired, the gate never latched and nothing else called restart() - it does
//     not mean the nightly restart behaves.
//   - RemoteSensorReceiver - the real receiver of remote readings. The rule's sensor is fed
//     through the same two ISensorValueModel calls the receiver makes, with its own cadence,
//     so what is skipped is the transport and the parsing, not the effect - and the notices
//     of a changed rule set that the boot sequence, the rule sync and the command service give it.
//   - the button and LED use cases - neither is on the path of anything asserted here.
//   - bootEventLog and restartEventLog - the loop flushes them on the same five-second timer as
//     otaUpdateLog, which is wired, so the branch is exercised; what is skipped is two more
//     instances of the same call.

#include <unity.h>

#include "Mock/ArduinoMock.h"

#include "constants.h"

#include "Application/Services/Boot/BootSequencer.h"
#include "Application/Services/CommandService.h"
#include "Application/Services/CounterReport.h"
#include "Application/Services/DiagnosticService.h"
#include "Application/Services/ModuleSetupService.h"
#include "Application/Services/Net/LinkSupervisor.h"
#include "Application/Services/Devices/DeviceService.h"
#include "Application/Services/LoopServices/LoopServiceConfigureDone.h"
#include "Application/Services/MemoryCounters.h"
#include "Application/Services/Net/NetworkCommandRunner.h"
#include "Application/Services/Net/Outbox.h"
#include "Application/Services/Rules/RuleService.h"
#include "Application/Services/Sensors/SensorAcquisitionService.h"
#include "Application/Services/Sensors/SensorService.h"
#include "Core/Domain/Data/Models/SensorValueModel.h"
#include "Core/Domain/Diagnostics/AllocationFailureCounter.h"
#include "Core/Domain/Diagnostics/LoopStepMonitor.h"
#include "Core/Domain/Diagnostics/ModuleStatusBoard.h"
#include "Core/Domain/UseCases/DeviceControl/ProcessDeviceStateUseCase.h"
#include "Core/Domain/UseCases/FirmwareUpdate/ConfirmRunningFirmwareUseCase.h"
#include "Core/Domain/UseCases/ModuleCommand/ModuleCommandExecutionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/CheckModuleDeletionUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/OnChangeLocalIpUseCase.h"
#include "Core/Domain/UseCases/ModuleMode/OnConnectivityRestoredUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/RepublishModuleStructureUseCase.h"
#include "Core/Domain/UseCases/ModuleStructure/SensorStructureUseCase.h"
#include "Core/Domain/UseCases/Rules/EvaluateRulesUseCase.h"
#include "Core/Domain/UseCases/Rules/SyncRulesUseCase.h"
#include "Core/Domain/UseCases/SensorTypes/SyncSensorTypesUseCase.h"
#include "Core/Domain/UseCases/Sensors/VirtualSensorChannelsUseCase.h"
#include "Core/Domain/UseCases/Sensors/VpdChannelUseCase.h"
#include "Infrastructure/Adapters/ExternalWDT/LoopLivenessGate.h"
#include "Infrastructure/Adapters/Http/TlsMemoryPool.h"
#include "Infrastructure/Adapters/Json/ArduinoJsonConverter.h"
#include "Infrastructure/Adapters/Json/JsonMemoryPool.h"
#include "Infrastructure/Adapters/Rs485/RawBusAccessAdapter.h"
#include "Infrastructure/Adapters/Sensors/HardwareSensors/ModbusSensors/ModbusSensorReader.h"
#include "Infrastructure/Adapters/Sensors/SensorReader.h"
#include "Infrastructure/Configuration/StepCycleStore.h"
#include "Infrastructure/Controllers/MQTT/MqttManager.h"
#include "Infrastructure/Controllers/MQTT/TopicHandlers.h"
#include "Infrastructure/Repositories/RuleRepository.h"
#include "Infrastructure/Repositories/SensorRepository.h"

#include "Mock/Application/MockClearModuleDataService.h"
#include "Mock/Core/Domain/MockButtonUseCases.h"
#include "Mock/Core/Domain/MockOtaPartitionControl.h"
#include "Mock/Core/Domain/MockOtaUpdateLog.h"
#include "Mock/Core/Domain/Models/MockDeviceModel.h"
#include "Mock/Infrastructure/Api/MockHybridAPIService.h"
#include "Mock/Infrastructure/Configuration/MockModuleConfig.h"
#include "Mock/Infrastructure/Configuration/MockNTPTimeAdapter.h"
#include "Mock/Infrastructure/Configuration/MockSettings.h"
#include "Mock/Infrastructure/HTTP/MockRestAPIService.h"
#include "Mock/Infrastructure/Repositories/MockSensorRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSensorTypeRepository.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsRuleDao.h"
#include "Mock/Infrastructure/SPIFFS/MockSpiffsSensorDao.h"
#include "Mock/Infrastructure/Wifi/MockWiFiManager.h"
#include "Mock/Infrastructure/Wifi/MockWifiAdapter.h"

#include "Sim/CensusRelays.h"
#include "Sim/HostAllocationCounter.h"
#include "Sim/HostBusLock.h"
#include "Sim/HostMessageQueue.h"
#include "Sim/HostTaskScheduler.h"
#include "Sim/SimArduino.h"
#include "Sim/SimFirmwareUpdater.h"
#include "Sim/SimLinkProbe.h"
#include "Sim/SimNetwork.h"
#include "Sim/SimRs485Bus.h"
#include "Sim/SimTaskWatchdog.h"
#include "Sim/VirtualClock.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace census = HostAllocationCensus;

using hostsim::CensusCommandService;
using hostsim::CensusConfirmRunningFirmwareUseCase;
using hostsim::CensusDeviceService;
using hostsim::CensusDiagnosticService;
using hostsim::CensusJsonConverter;
using hostsim::CensusOnChangeLocalIpUseCase;
using hostsim::CensusOnConnectivityRestoredUseCase;
using hostsim::CensusOutbox;
using hostsim::CensusRepublishStructureUseCase;
using hostsim::CensusRuleService;
using hostsim::CensusSensorService;
using hostsim::CensusSyncRulesUseCase;
using hostsim::CensusSyncSensorTypesUseCase;
using hostsim::HostBusLock;
using hostsim::HostMessageQueue;
using hostsim::HostTaskScheduler;
using hostsim::SimArduino;
using hostsim::SimFirmwareUpdater;
using hostsim::SimNetwork;
using hostsim::SimRs485Bus;
using hostsim::SimTaskWatchdog;
using hostsim::VirtualClock;

namespace {

// --- how long, and how fast ---------------------------------------------------------------

const uint64_t MS_PER_HOUR = 3600ULL * 1000ULL;
const uint64_t MS_PER_DAY = 24ULL * MS_PER_HOUR;

/** The span the plan asks for: a fortnight of continuous operation. */
const uint64_t SIMULATED_DAYS = 14;
const uint64_t SIMULATED_SPAN_MS = SIMULATED_DAYS * MS_PER_DAY;

/**
 * How often the control loop comes back round, in this bench.
 *
 * The device's loop() is free-running - main.cpp calls it back to back - so this is a model of
 * its cadence, not a property read off the firmware, and every scenario file picks a value. The
 * short scenarios use 10 ms because they observe minutes; a fortnight at 10 ms is 121 million
 * iterations, which no host run finishes in the minutes this bench is meant to cost. 100 ms
 * keeps every service interval in the module (1 s rule tick, 15 s sensor sweep, 30 s
 * diagnostics, 5 min rule sync) exactly where it is and costs the resolution of the step
 * measurement, which is the honest thing to give up here: what a fortnight adds is duration,
 * and the sub-100 ms interleaving is what S1, S6, S7, S8, S9 and S12 already prove.
 */
const uint32_t CONTROL_PERIOD_MS = 100;

/**
 * The slowest control-loop step this bench accepts: one nominal period plus the 50 ms every
 * other scenario allows on top of its own period. Both halves are borrowed, neither invented -
 * 50 ms is the figure S1/S6/S7/S8/S9 are held to.
 */
const uint32_t MAX_ACCEPTED_SLACK_MS = 50;
const uint32_t MAX_ACCEPTED_STEP_MS = CONTROL_PERIOD_MS + MAX_ACCEPTED_SLACK_MS;

/** The per-task legal-pause budget the watchdog threshold is computed from (constants.h). */
const uint32_t LEGAL_PAUSE_BUDGET_MS = (uint32_t)WDT_WORST_LEGAL_PAUSE_S * 1000UL;

/** LoopServiceConfigureDone's diagnostic timer, and with it the DIAG-COUNTERS cadence. */
const uint32_t DIAGNOSTIC_INTERVAL_MS = 30000;

/** The external watchdog chip's kick cadence (ExternalWDT.h WDT_KICK_INTERVAL_MS). */
const uint32_t EXT_KICK_INTERVAL_MS = 100;

const char* const LOOP_TASK_NAME = "arduino-loop";
const char* const MODULE_UID = "endurance-module";

/**
 * The sensor type catalog, and the hour the account changes it.
 *
 * Hour seven, deliberately inside the first simulated day: everything the catalog change retains -
 * the extra channel on three sensors, the sensor records the republished structure builds - is
 * already in place by hour 24, where the memory claims of this run begin. So the change is a real
 * one-off in the middle of ordinary operation, and it costs the day-against-day comparisons
 * nothing.
 *
 * The hour is odd rather than a multiple of six, and that is not free: what asks for a catalog is
 * a reboot, a change of local IP, and a synchronization that did not succeed - and the last of
 * those waits for the module's offline decision to clear or for the six-hour interval under it.
 * A change scheduled on the hour a server outage opens would therefore be picked up hours late or
 * not at all, and the run would be measuring the outage instead of the change.
 */
const uint64_t CATALOG_CHANGE_HOUR = 7;
const char* const CATALOG_REVISION_1 = "2026-02-01T00:00:00.0000000Z";
const char* const CATALOG_REVISION_2 = "2026-02-11T09:01:35.4435200Z";

// --- the outside world, on a schedule -------------------------------------------------------

/**
 * What is wrong for the length of one window of the schedule. The four kinds are what the link
 * troubleshooting tells apart, by the two checks it makes on its own link - A, the gateway's answer
 * to ARP, and B, a public address's answer to an echo.
 */
enum class Fault : uint8_t {
    NONE = 0,
    /** The server and the broker are down; the router and the internet answer (A answered, B answered). */
    SERVER_DOWN,
    /** The internet is down behind a router that answers (A answered, B silent). */
    INTERNET_DOWN,
    /** The station stays associated and its router does not answer (A silent). */
    ROUTER_SILENT,
    /** The access point is gone: the station has no association. */
    WIFI_DROP
};

const size_t FAULT_KINDS = 5;

const char* faultName(Fault kind)
{
    switch (kind)
    {
    case Fault::SERVER_DOWN: return "server down";
    case Fault::INTERNET_DOWN: return "internet down";
    case Fault::ROUTER_SILENT: return "router silent";
    case Fault::WIFI_DROP: return "Wi-Fi drop";
    default: return "none";
    }
}

/** One window of the daily plan: when it opens, how long it lasts, and how the module comes back. */
struct PlannedFault {
    Fault kind;
    uint32_t hour;
    uint32_t minute;
    uint32_t lengthS;
    /**
     * WIFI_DROP: the station associates again by itself once the access point is back. Otherwise
     * only the radio restart of the troubleshooting brings it back, on its next pause.
     */
    bool returnsByItself;
    /** WIFI_DROP: the address the station holds after the return is not the one it held before. */
    bool newAddress;
};

/**
 * The plan every simulated day follows - nineteen windows, 266 over the fortnight.
 *
 * It is the same on every day, which the memory claims below depend on: they compare an hour with
 * the same hour a day earlier. No window crosses an hour boundary or the time its return is judged,
 * so every hourly checkpoint is taken on a module that is meant to be settled. The hours of the
 * daily bus command (03), of the catalog change (07) and of the two firmware downloads (09, 15) are
 * left free, so that none of them is judged together with a return. The server windows at 00, 06,
 * 12 and 18 are the six-hourly outage the bench always had, opening at twenty past so that the boot
 * of the first hour is not one of them.
 */
const PlannedFault DAILY_PLAN[] = {
    {Fault::SERVER_DOWN, 0, 20, 420, false, false},
    {Fault::WIFI_DROP, 1, 10, 45, true, true},
    {Fault::ROUTER_SILENT, 2, 10, 360, false, false},
    {Fault::INTERNET_DOWN, 4, 10, 540, false, false},
    {Fault::WIFI_DROP, 5, 10, 240, false, false},
    {Fault::SERVER_DOWN, 6, 20, 420, false, false},
    {Fault::WIFI_DROP, 8, 10, 840, true, true},
    {Fault::INTERNET_DOWN, 10, 10, 1500, false, false},
    {Fault::ROUTER_SILENT, 11, 10, 720, false, false},
    {Fault::SERVER_DOWN, 12, 20, 420, false, false},
    {Fault::WIFI_DROP, 13, 10, 90, true, false},
    {Fault::SERVER_DOWN, 14, 10, 900, false, false},
    {Fault::WIFI_DROP, 16, 10, 1800, false, true},
    {Fault::INTERNET_DOWN, 17, 10, 360, false, false},
    {Fault::SERVER_DOWN, 18, 20, 420, false, false},
    {Fault::ROUTER_SILENT, 19, 10, 1200, false, false},
    {Fault::WIFI_DROP, 20, 10, 180, true, false},
    {Fault::SERVER_DOWN, 22, 10, 660, false, false},
    {Fault::WIFI_DROP, 23, 10, 480, true, true},
};
const size_t PLAN_SIZE = sizeof(DAILY_PLAN) / sizeof(DAILY_PLAN[0]);

/** One window of the run: the plan's entry on its day, in milliseconds from the start. */
struct FaultWindow {
    uint64_t index = 0;
    Fault kind = Fault::NONE;
    uint64_t startMs = 0;
    uint64_t endMs = 0;
    bool returnsByItself = false;
    bool newAddress = false;
};

FaultWindow faultWindow(uint64_t index)
{
    const PlannedFault& entry = DAILY_PLAN[index % PLAN_SIZE];
    FaultWindow window;
    window.index = index;
    window.kind = entry.kind;
    window.startMs = (index / PLAN_SIZE) * MS_PER_DAY +
                     ((uint64_t)entry.hour * 60 + entry.minute) * 60ULL * 1000ULL;
    window.endMs = window.startMs + (uint64_t)entry.lengthS * 1000ULL;
    window.returnsByItself = entry.returnsByItself;
    window.newAddress = entry.newAddress;
    return window;
}

/** How many windows of this kind the plan opens in a span of whole days. */
uint64_t windowsOfKindIn(Fault kind, uint64_t days)
{
    uint64_t perDay = 0;
    for (size_t i = 0; i < PLAN_SIZE; i++)
    {
        perDay += (DAILY_PLAN[i].kind == kind) ? 1 : 0;
    }
    return perDay * days;
}

/**
 * How long after a window closes the module is given to be back to normal before it is judged.
 *
 * A request in flight when the fault clears is a dead one with its four attempts and their backoffs
 * (about two minutes), the supervision looks every thirty seconds, and the structure and the first
 * requests follow: five minutes. A station that only a radio restart brings back waits for the next
 * of them, at most a pause of ten minutes and the minute a failed restart takes before it.
 */
uint64_t returnAllowanceMs(const FaultWindow& window)
{
    const bool onlyARestartBringsItBack = window.kind == Fault::WIFI_DROP && !window.returnsByItself;
    return onlyARestartBringsItBack ? 15ULL * 60ULL * 1000ULL : 5ULL * 60ULL * 1000ULL;
}

/**
 * @brief What the world is doing to the module at this moment, read by every stand-in that talks
 *        to the backend.
 *
 * One state, read by everything that talks to the backend - the REST calls, the MQTT connection,
 * the failure counter the diagnostics watch. On a site there is one server behind all three, so a
 * fault that only the REST calls could see would be one this product does not have; and the
 * firmware's own offline decision is built out of exactly those inputs (LinkSupervisor:
 * routerDown || (!mqttConnected && httpDown)), so a bench that leaves any of them untouched leaves
 * that decision - and everything hanging off it - unreachable.
 */
struct LinkWorld {
    Fault active = Fault::NONE;
    /** The station: the backend is reached only through an association. */
    const MockWifiAdapter* radio = nullptr;
    /** When the station first associated in this run; 0 while it has not. */
    uint64_t firstAssociationAtMs = 0;

    bool backendReachable() const
    {
        return active == Fault::NONE && radio != nullptr && radio->connected;
    }
};

/** What a healthy request costs, and what a dead one costs before the transport gives up. */
const uint32_t HEALTHY_CALL_MS = 200;
const uint32_t DEAD_CALL_MS = 30000;

/** How often the neighbouring sensor module publishes its channel (its own UDP cadence). */
const uint32_t REMOTE_PUBLISH_INTERVAL_MS = 15000;

/**
 * How far either side of a one-off event the memory balance is read to price it.
 *
 * Four hours, and the number is not free: the balance swings with a four-hour period here (the
 * six-hour sensor cycle beating against the hourly irrigation one), so a window that is not a
 * whole number of periods prices the swing instead of the event. Two hours either side is one
 * period, and comfortably longer than the two minutes a download takes.
 */
const size_t RETENTION_WINDOW_HOURS = 2;

/**
 * When the two firmware downloads are started, in hours from the start of the run.
 *
 * Named because the memory arithmetic below needs them: a firmware update legitimately retains
 * memory (the command, its report, the log entry), and an accounting of what the module keeps
 * has to know which hours are allowed to add to the total.
 */
const size_t FIRST_DOWNLOAD_HOUR = 6 * 24 + 9;
const size_t SECOND_DOWNLOAD_HOUR = 10 * 24 + 15;

/** The sensor on the flapping port answers for this long, then goes quiet for this long. */
const uint64_t FLAP_PERIOD_MS = 4 * MS_PER_HOUR;

// UIDs longer than any small-string buffer, so a copy is always a real allocation and cannot
// hide inside the object (same reason as in the steady-state allocation tests).
const char* const SENSOR_HEALTHY_UID = "sensor-healthy-9c37-8d21e5a0b471-000001";
// The channel a neighbouring sensor module publishes every fifteen seconds, and the one the
// sensor rule watches. Rules read ISensorValueModel::getSensorValue(uid), and that is where a
// remote update lands (RemoteSensorClient::writeSensorsFromPayload); a locally polled
// multi-channel sensor stores its channels elsewhere, in the per-sensor channel map, and is
// therefore not what a rule can be driven from here.
const char* const REMOTE_CHANNEL_UID = "remote-air-temperature-7c31-9ab4e2f10d55.01";
const char* const SENSOR_FLAPPING_UID = "sensor-flapping-ad48-9e32f6b1c582-00002";
const char* const SENSOR_SILENT_UID = "sensor-silent-be59-af43a7c2d693-000003";

const char* const DEVICE_SENSOR_UID = "device-by-sensor-cf6a-ba54b8d3e7a4-001";
const char* const DEVICE_SCHEDULE_UID = "device-by-schedule-eb8c-dc76daf5a9c6-2";
const char* const DEVICE_LOOP_UID = "device-by-loop-fc9d-ed87eba6bad7-0003";

const char* const RULE_SENSOR_UID = "rule-sensor-aaaaaaaa-bbbb-cccc-000001";
const char* const RULE_SCHEDULE_UID = "rule-schedule-aaaaaaaa-bbbb-cccc-0002";
const char* const RULE_LOOP_UID = "rule-loop-aaaaaaaa-bbbb-cccc-dddd-003";

/** 2021-01-01 00:00:00 UTC: where the module's wall clock starts, so day boundaries line up. */
const unsigned long EPOCH_START = 1609459200UL;

// --- host stand-ins -------------------------------------------------------------------------

/**
 * A logger that keeps counters instead of text.
 *
 * MockLogger appends every line to a vector and to a file; over 40 000 diagnostic periods that
 * is both unbounded growth in the harness and heap traffic inside the windows this bench
 * measures. What endurance needs from the log is narrower: how many DIAG-COUNTERS lines came
 * out, when, and what the last one said.
 */
class CountingLogger : public ILogger {
public:
    explicit CountingLogger(VirtualClock& clock) : _clock(clock) { _lastCountersLine[0] = '\0'; }

    uint64_t countersLines() const { return _countersLines; }
    uint64_t firstCountersAtMs() const { return _firstCountersAtMs; }
    uint64_t lastCountersAtMs() const { return _lastCountersAtMs; }
    uint64_t worstCountersGapMs() const { return _worstGapMs; }

    /**
     * @brief The largest max_loop_step_ms any counters line reported, and where it came from.
     *
     * The bench measures the control loop from outside, with CadenceRecorder; this is the same
     * claim taken from the firmware's own instrument, which is what a field module would report
     * and the only one that will exist on the bench. Every line describes its own diagnostic
     * period, so reading the worst of them covers the whole fortnight, not only its end.
     */
    uint64_t worstReportedLoopStepMs() const { return _worstReportedLoopStepMs; }
    uint64_t worstReportedLoopStepAtMs() const { return _worstReportedLoopStepAtMs; }
    uint64_t linesWithLoopStep() const { return _linesWithLoopStep; }

    /**
     * @brief Count the silence between the last line seen and the end of the run.
     *
     * Without this, a line that stops coming out an hour before the end falls off the end of the
     * recording: every gap between two observed lines is still short, and the count is short by
     * too few lines to notice. Same reason CadenceRecorder has closeAt().
     */
    void closeCountersAt(uint64_t nowMs)
    {
        if (_countersLines > 0 && (nowMs - _lastCountersAtMs) > _worstGapMs)
        {
            _worstGapMs = nowMs - _lastCountersAtMs;
            _worstGapStartedAtMs = _lastCountersAtMs;
        }
    }
    uint64_t worstCountersGapStartedAtMs() const { return _worstGapStartedAtMs; }
    const char* lastCountersLine() const { return _lastCountersLine; }

    /** The lines of the boot sequence and of the link machines, counted by what they say. */
    enum Tape {
        OUTAGE_RECORDED,
        RESTART_ASKED,
        RESTART_NOT_RUN,
        PROBE_ASKED,
        STEADY_ENTERED,
        DIAGNOSTIC_OWED_SENT,
        MODULE_FAULT_HELD,
        TAPE_KINDS
    };
    uint64_t tape(Tape which) const { return _tape[which]; }

    void print(const char*) override {}
    void println(const char* message) override { observe(message); }
    void printf(const char*, ...) override {}

    void print(const char*, MqttLoggerMode) override {}
    void println(const char* message, MqttLoggerMode) override { observe(message); }
    void printf(const char*, MqttLoggerMode, ...) override {}

    void print(const char*, const char*) override {}
    void println(const char*, const char* message) override { observe(message); }
    void printf(const char*, const char*, ...) override {}

    void print(const char*, const char*, MqttLoggerMode) override {}
    void println(const char*, const char* message, MqttLoggerMode) override { observe(message); }
    void printf(const char*, const char*, MqttLoggerMode, ...) override {}

    void print(const char*, MqttLoggerMode, const char*) override {}
    void println(const char*, MqttLoggerMode, const char* message) override { observe(message); }
    void printf(const char*, MqttLoggerMode, const char*, ...) override {}

    void log(LogLevel, const char*) override {}
    void log(LogLevel, const char*, const char*) override {}

    void logJson(const char*, const char*, const char*) override {}
    void logJson(const char*, const char*, const char*, const char*) override {}

    void setModuleUid(const char*) override {}

private:
    void observe(const char* message)
    {
        if (message == nullptr)
        {
            return;
        }
        // Counted and not kept: a fortnight of the machines' lines is millions of bytes of text, and
        // what the run asks of them is how often each said its thing.
        if (strncmp(message, "LINK_CHECK: outage over", 23) == 0) _tape[OUTAGE_RECORDED]++;
        else if (strncmp(message, "LINK_CHECK: radio restart asked", 31) == 0) _tape[RESTART_ASKED]++;
        else if (strncmp(message, "WIFI_RECONNECT: restart not run", 31) == 0) _tape[RESTART_NOT_RUN]++;
        else if (strncmp(message, "LINK_CHECK: probe handed", 24) == 0) _tape[PROBE_ASKED]++;
        else if (strncmp(message, "LINK_CHECK: the fault of the module held", 40) == 0) _tape[MODULE_FAULT_HELD]++;
        else if (strncmp(message, "BOOT_SEQ: diagnostic owed", 25) == 0) _tape[DIAGNOSTIC_OWED_SENT]++;
        else if (strncmp(message, "BOOT_SEQ: ", 10) == 0 && strstr(message, "-> STEADY (") != nullptr) _tape[STEADY_ENTERED]++;

        if (strncmp(message, "DIAG-COUNTERS:", 14) != 0)
        {
            return;
        }

        const uint64_t nowMs = _clock.nowMs();
        if (_countersLines == 0)
        {
            _firstCountersAtMs = nowMs;
        }
        else
        {
            const uint64_t gap = nowMs - _lastCountersAtMs;
            if (gap > _worstGapMs)
            {
                _worstGapMs = gap;
                _worstGapStartedAtMs = _lastCountersAtMs;
            }
        }
        _lastCountersAtMs = nowMs;
        _countersLines++;

        // A fixed buffer, so that recording the line costs no heap inside a measured window.
        snprintf(_lastCountersLine, sizeof(_lastCountersLine), "%s", message);

        // Read out of the line rather than off the monitor: the column is what a field module
        // gives an engineer, so this is the number the bench holds to the limit. Parsed with
        // strtoull on the fixed buffer - no allocation inside a measured window.
        const char* column = strstr(_lastCountersLine, " max_loop_step_ms=");
        if (column == nullptr)
        {
            return;
        }
        _linesWithLoopStep++;
        const uint64_t reported = strtoull(column + strlen(" max_loop_step_ms="), nullptr, 10);
        if (reported > _worstReportedLoopStepMs)
        {
            _worstReportedLoopStepMs = reported;
            _worstReportedLoopStepAtMs = nowMs;
        }
    }

    VirtualClock& _clock;
    uint64_t _countersLines = 0;
    uint64_t _firstCountersAtMs = 0;
    uint64_t _lastCountersAtMs = 0;
    uint64_t _worstGapMs = 0;
    uint64_t _worstGapStartedAtMs = 0;
    uint64_t _worstReportedLoopStepMs = 0;
    uint64_t _worstReportedLoopStepAtMs = 0;
    uint64_t _linesWithLoopStep = 0;
    uint64_t _tape[TAPE_KINDS] = {0};
    char _lastCountersLine[CounterReport::LINE_BUFFER_CHARS];
};

/**
 * Diagnostic and setup logging on, everything else off - the shape a field module is shipped with
 * when someone is watching the counters line and the tape of the link. The setup feature is what
 * the boot sequence and the link machines write under. Leaving every feature on would put hundreds of
 * millions of formatted lines through the loop and measure the logger instead of the module.
 */
class DiagnosticOnlyDebugSystem : public IDebugSystem {
public:
    IDebugFeature* registerFeature(const char*, const char* = nullptr, bool = false,
                                   MqttLoggerMode = MqttLoggerMode::MqttAndSerial) override
    {
        return nullptr;
    }
    IDebugFeature* getFeature(const char*) override { return nullptr; }
    void setDebugFeatureState(const char*, bool) override {}
    void setFeatureLogChannel(const char*, MqttLoggerMode) override {}

    bool isFeatureEnabled(const char* featureName) override
    {
        return featureName != nullptr && (strcmp(featureName, DebugFeatures::DIAGNOSTIC_DEBUG) == 0 ||
                                          strcmp(featureName, DebugFeatures::SETUP_DEBUG) == 0);
    }
};

/** Wall-clock time derived from the virtual clock, so a fortnight of schedule rules is real. */
class SimNtpTimeAdapter : public MockNTPTimeAdapter {
public:
    /** How long after the station first associates SNTP has set the clock. */
    static const uint32_t SNTP_AFTER_ASSOCIATION_MS = 3000;

    SimNtpTimeAdapter(VirtualClock& clock, const LinkWorld& world) : _clock(clock), _world(world)
    {
        _buffer[0] = '\0';
    }

    bool wasSetBySntpThisBoot() override
    {
        return _world.firstAssociationAtMs != 0 &&
               _clock.nowMs() >= _world.firstAssociationAtMs + SNTP_AFTER_ASSOCIATION_MS;
    }

    unsigned long sntpSetAtMs() override
    {
        return (unsigned long)(_world.firstAssociationAtMs + SNTP_AFTER_ASSOCIATION_MS);
    }

    unsigned long getEpochTime() override
    {
        return EPOCH_START + (unsigned long)(_clock.nowMs() / 1000ULL);
    }

    const char* getFormattedTime() override
    {
        const unsigned long epoch = getEpochTime();
        const time_t asTime = (time_t)epoch;
        struct tm parts;
        gmtime_r(&asTime, &parts);
        strftime(_buffer, sizeof(_buffer), "%Y-%m-%dT%H:%M:%S.000Z", &parts);
        return _buffer;
    }

    bool getLocalTime(struct tm* out) override
    {
        if (out == nullptr)
        {
            return false;
        }
        const time_t asTime = (time_t)getEpochTime();
        gmtime_r(&asTime, out);
        return true;
    }

private:
    VirtualClock& _clock;
    const LinkWorld& _world;
    char _buffer[40];
};

/**
 * The backend, over the simulated socket.
 *
 * Two things it models that a plain mock cannot: every call costs the calling task its latency
 * (SimNetwork blocks the caller, as a synchronous socket does), and every call queues on one
 * lock, because RestApiService serialises them all on its _apiMutex. Outages are a function of
 * the clock rather than of a script, so the run stays a function of its constants.
 *
 * Every call is tagged as the census's "rest transport" row, which is where the response bodies
 * the module then works on are built. Read it as the seam and not as a device measurement: on the
 * board the same seam is RestAPIService plus HttpClientAdapter plus the TLS stack, none of which
 * is in this graph, and part of what is counted here is the mock's own record of the last call.
 */
class SimServer : public MockHybridAPIService {
public:
    SimServer(HostTaskScheduler& scheduler, SimNetwork& network, VirtualClock& clock, const LinkWorld& world)
        : _scheduler(scheduler), _network(network), _clock(clock), _world(world)
    {
    }

    class ApiLock {
    public:
        explicit ApiLock(SimServer& server) : _server(server)
        {
            const uint32_t ticket = _server._nextApiTicket++;
            while (_server._apiServing != ticket)
            {
                _server._scheduler.delayMs(10);
            }
        }
        ~ApiLock() { _server._apiServing++; }

    private:
        SimServer& _server;
    };

    /** True while the station is associated and no window of the plan hides the backend. */
    bool reachableNow() const { return _world.backendReachable(); }

    /**
     * @brief How many calls in a row the server has refused.
     *
     * What RestAPIService::getCountOffline() counts on the device, counted the same way here:
     * consecutive failures of real calls, reset by the first one that succeeds. Read by the
     * diagnostics to decide whether the module is offline, which is why it is measured from the
     * traffic rather than derived from the outage schedule - a schedule the firmware cannot see.
     */
    int consecutiveFailures() const { return _consecutiveFailures; }

    /** Where that counter lives, for the stand-in that reports it to the diagnostics. */
    const int* consecutiveFailuresAddress() const { return &_consecutiveFailures; }

    uint64_t outageCalls() const { return _outageCalls; }
    uint64_t servedCalls() const { return _servedCalls; }

    /** Diagnostics the server took, and structures it took: what a return is judged by. */
    uint64_t diagnosticsDelivered() const { return _diagnosticsDelivered; }
    uint64_t structuresDelivered() const { return (uint64_t)sendToServerModuleStructureCallCount; }

    /** How many times the module asked for the rule list - its periodic sync plus any forced. */
    uint64_t ruleListFetches() const { return _ruleListFetches; }

    std::vector<ModuleCommandDto> pendingModuleCommands;
    std::vector<Rule> serverRules;
    std::vector<SensorType> serverSensorTypes;

    /** Catalog fetches the module made, and how many of them fell outside an outage. */
    uint64_t sensorTypeFetches = 0;
    uint64_t sensorTypeFetchesOk = 0;

    // --- the calls the module makes ---------------------------------------------------------

    bool sendDiagnosticData(const DiagnosticDto& diagnosticData,
                            ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        const bool taken = spend("diagnostic") &&
                           MockHybridAPIService::sendDiagnosticData(diagnosticData, priority);
        _diagnosticsDelivered += taken ? 1 : 0;
        return taken;
    }

    /** The question the deletion check asks on the first STEADY: answered "not deleted" when the server is there. */
    ModuleDeletionQueryResult queryModuleDeletion() override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        const bool answered = spend("is-deleted");
        ModuleDeletionQueryResult result;
        result.received200 = answered;
        result.queryFailed = !answered;
        result.isDeletedFlag = answered ? 0 : -1;
        result.finalHttpCode = answered ? 200 : -1;
        return result;
    }

    bool sendModuleUpdateData(const UpdateDto& updateData,
                              ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        return spend("update") && MockHybridAPIService::sendModuleUpdateData(updateData, priority);
    }

    std::vector<Rule> getRuleList(bool* httpFailed = nullptr, bool* readWhole = nullptr) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        _ruleListFetches++;
        const bool ok = spend("rules");
        if (httpFailed != nullptr)
        {
            *httpFailed = !ok;
        }
        if (readWhole != nullptr)
        {
            *readWhole = ok;
        }
        return ok ? serverRules : std::vector<Rule>();
    }

    bool sendRuleStateCommand(const char* message,
                              ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        return spend("rule-state") &&
               MockHybridAPIService::sendRuleStateCommand(message, priority);
    }

    bool sendByUidRuleStateCommand(const char* uid, const char* message) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        return spend("rule-state-uid") &&
               MockHybridAPIService::sendByUidRuleStateCommand(uid, message);
    }

    std::vector<ModuleCommandDto> getModuleCommand(bool* httpFailed = nullptr) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        const bool ok = spend("module-commands");
        if (httpFailed != nullptr)
        {
            *httpFailed = !ok;
        }
        if (!ok)
        {
            return std::vector<ModuleCommandDto>();
        }
        std::vector<ModuleCommandDto> answer;
        answer.swap(pendingModuleCommands);
        return answer;
    }

    std::vector<DeviceCommandDto> getDeviceCommand(bool* httpFailed = nullptr) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        const bool ok = spend("device-commands");
        if (httpFailed != nullptr)
        {
            *httpFailed = !ok;
        }
        return std::vector<DeviceCommandDto>();
    }

    std::vector<SensorType> getSensorTypesList(bool* httpFailed = nullptr) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        const bool ok = spend("sensor-types");
        if (httpFailed != nullptr)
        {
            *httpFailed = !ok;
        }
        sensorTypeFetches++;
        sensorTypeFetchesOk += ok ? 1 : 0;
        return ok ? serverSensorTypes : std::vector<SensorType>();
    }

    bool sendModuleCommandExecutionResult(
        const std::vector<ModuleCommandUpdateDto>& updates) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        return spend("module-command-result") &&
               MockHybridAPIService::sendModuleCommandExecutionResult(updates);
    }

    bool sendCommandResult(const CommandResultDto& result,
                           ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        return spend("command-result") &&
               MockHybridAPIService::sendCommandResult(result, priority);
    }

    bool sendToServerModuleStructure(const ModuleStructure& structure,
                                     ApiPriority priority = ApiPriority::UNDEFINED) override
    {
        ApiLock lock(*this);
        census::Scope owner(census::kRest);
        countComputedChannel(structure);
        return spend("structure") &&
               MockHybridAPIService::sendToServerModuleStructure(structure, priority);
    }

    /**
     * @brief Structures that arrived carrying a computed channel record.
     *
     * The module has none until the catalog gives its sensors an air pair, so this is where the
     * republish becomes visible from the server's side - and it is visible there whichever task
     * made the call, which is what a soundness gate has to be.
     */
    uint64_t structuresWithComputedChannel() const { return _structuresWithComputedChannel; }

private:
    void countComputedChannel(const ModuleStructure& structure)
    {
        for (size_t node = 0; node < structure.nodes.size(); node++)
        {
            for (size_t sensor = 0; sensor < structure.nodes[node].sensors.size(); sensor++)
            {
                if (structure.nodes[node].sensors[sensor].type == "vpd")
                {
                    _structuresWithComputedChannel++;
                    return;
                }
            }
        }
    }

    /** Pay for one request in virtual time and answer according to the outage schedule. */
    bool spend(const char* label)
    {
        const bool reachable = reachableNow();
        _network.setReachable(reachable);
        _network.setResponseLatencyMs(reachable ? HEALTHY_CALL_MS : DEAD_CALL_MS);
        const SimNetwork::Response response = _network.send(label, "");
        if (response.ok)
        {
            _servedCalls++;
            _consecutiveFailures = 0;
        }
        else
        {
            _outageCalls++;
            _consecutiveFailures++;
        }
        return response.ok;
    }

    HostTaskScheduler& _scheduler;
    SimNetwork& _network;
    VirtualClock& _clock;
    const LinkWorld& _world;
    uint64_t _diagnosticsDelivered = 0;
    uint32_t _nextApiTicket = 0;
    uint32_t _apiServing = 0;
    uint64_t _servedCalls = 0;
    uint64_t _outageCalls = 0;
    uint64_t _ruleListFetches = 0;
    uint64_t _structuresWithComputedChannel = 0;
    int _consecutiveFailures = 0;
};

/** An MQTT broker that delivers what the scenario hands it, on the control loop. */
class ScriptedMqttManager : public IMqttManager {
public:
    explicit ScriptedMqttManager(const LinkWorld& world) : _world(world) {}

    bool connect() override { return true; }
    void disconnect() override {}

    /** The broker is the backend: it is unreachable exactly while the backend is. */
    bool isConnected() override { return _world.backendReachable(); }
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
        // The MQTT row of the census: this stand-in and, below it, the real CommandTopicHandler
        // that parses the payload and hands the command on. The transport itself
        // (AsyncMqttClient, its send and receive buffers) is device-only and is not in this graph
        // - the row is what the module does with a message, not what receiving one costs.
        census::Scope owner(census::kMqtt);
        if (!_hasPending)
        {
            return;
        }
        _hasPending = false;
        if (_handler != nullptr)
        {
            _handler->handleMessage("GrowDirectorMqttTopic/endurance-module",
                                    reinterpret_cast<const uint8_t*>(_pending),
                                    (unsigned int)strlen(_pending));
        }
        _delivered++;
    }

    /** One message at a time, in a fixed buffer: no growth, no heap inside a measured window. */
    void deliver(const char* payload)
    {
        snprintf(_pending, sizeof(_pending), "%s", payload);
        _hasPending = true;
    }

    void setHandler(ITopicHandler* handler) { _handler = handler; }
    uint64_t delivered() const { return _delivered; }

private:
    const LinkWorld& _world;
    ITopicHandler* _handler = nullptr;
    char _pending[384] = {0};
    bool _hasPending = false;
    uint64_t _delivered = 0;
};

/**
 * @brief The one thing the diagnostics read off the REST transport: how many calls in a row failed.
 *
 * LinkSupervisor decides the module is offline from `routerDown || (!mqttConnected &&
 * httpDown)`, and `httpDown` is this counter against its threshold. The bench has no REST
 * transport - every call goes through the hybrid service - so this stand-in carries the number
 * across from the server that actually refused the calls. Nothing else on it is used: the
 * diagnostic send goes to the outbox, and the outbox to the hybrid service.
 */
class SimOfflineCounter : public MockRestAPIService {
public:
    void setSource(const int* consecutiveFailures) { _source = consecutiveFailures; }
    int getCountOffline() const override { return (_source != nullptr) ? *_source : 0; }

private:
    const int* _source = nullptr;
};

/**
 * @brief The station as the board's adapter answers for it, and as the plan drives it.
 *
 * WifiAdapter::getLocalIP() answers 0.0.0.0 while the station is not associated, and its restart
 * takes the association down, waits for a new one in steps of 100 ms for up to a minute - feeding
 * the task watchdog at every step, on the caller's task - and answers whether it got one. Neither
 * is what MockWifiAdapter does on its own. The cold start of the boot's association is the first
 * restart and takes the time device logs give it; every later restart associates in seconds if
 * the access point is in range, and spends its whole minute if it is not.
 */
class SimRadio : public MockWifiAdapter {
public:
    static const uint32_t COLD_START_ASSOCIATION_MS = 12000;
    static const uint32_t RESTART_ASSOCIATION_MS = 3000;
    static const uint32_t RESTART_BUDGET_MS = 60000;
    static const uint32_t FEED_STEP_MS = 100;
    /** The restarts whose start is kept, from the first: a fortnight asks for a few hundred. */
    static const size_t MAX_RECORDED_RESTARTS = 4096;

    SimRadio(HostTaskScheduler& scheduler, VirtualClock& clock, LinkWorld& world)
        : _scheduler(scheduler), _clock(clock), _world(world)
    {
        _restartStartsMs.reserve(MAX_RECORDED_RESTARTS);
        strcpy(localIP, "10.0.0.100");
    }

    /** The access point is in range: a restart can associate, and the station can on its own. */
    bool accessPointInRange = true;

    const char* getLocalIP() override { return connected ? localIP : "0.0.0.0"; }

    /** The next association is given a different address. */
    void handOutANewAddressOnTheNextAssociation() { _newAddressPending = true; }

    /** The station associates by itself: what the framework does when the access point is back. */
    void associateByItself() { associate(); }

    bool reconnect() override
    {
        reconnectCalled = true;
        reconnectCallCount++;
        const bool coldStart = _world.firstAssociationAtMs == 0;
        if (!coldStart)
        {
            _restarts++;
            if (_restartStartsMs.size() < MAX_RECORDED_RESTARTS)
            {
                _restartStartsMs.push_back(_clock.nowMs());
            }
            else
            {
                _unrecordedRestarts++;
            }
        }
        connected = false;
        const uint32_t associationMs = coldStart ? COLD_START_ASSOCIATION_MS : RESTART_ASSOCIATION_MS;
        for (uint32_t waited = 0; waited < RESTART_BUDGET_MS;)
        {
            _scheduler.watchdogFeed();
            _scheduler.delayMs(FEED_STEP_MS);
            waited += FEED_STEP_MS;
            if (accessPointInRange && waited >= associationMs)
            {
                associate();
                return true;
            }
        }
        return false;
    }

    /** Restarts made after the boot's association, and when each began. */
    uint64_t restarts() const { return _restarts; }
    uint64_t startOfRestart(uint64_t number) const { return _restartStartsMs[(size_t)number]; }
    uint64_t unrecordedRestarts() const { return _unrecordedRestarts; }

private:
    void associate()
    {
        if (!connected && _newAddressPending)
        {
            _newAddressPending = false;
            _addresses++;
            snprintf(localIP, sizeof(localIP), "10.0.%u.%u", (unsigned)(_addresses / 250),
                     (unsigned)(1 + _addresses % 250));
        }
        connected = true;
        if (_world.firstAssociationAtMs == 0)
        {
            _world.firstAssociationAtMs = _clock.nowMs();
        }
    }

    HostTaskScheduler& _scheduler;
    VirtualClock& _clock;
    LinkWorld& _world;
    std::vector<uint64_t> _restartStartsMs;
    uint64_t _restarts = 0;
    uint64_t _unrecordedRestarts = 0;
    uint32_t _addresses = 0;
    bool _newAddressPending = false;
};

/**
 * @brief The real rule sync, with the forced syncs counted.
 *
 * A spy, not a stand-in: every call is forwarded to the real use case, which does its real work.
 * It exists because the effect of a forced sync - one more rule-list fetch - is indistinguishable
 * from the outbox retrying a fetch that an outage interrupted, and a premise that cannot tell
 * those apart proves nothing about the branch it claims to cover.
 */
class ForceSyncSpy : public ISyncRulesUseCase {
public:
    explicit ForceSyncSpy(ISyncRulesUseCase& real) : _real(real) {}

    bool init() override { return _real.init(); }
    void loop() override { _real.loop(); }
    void onOutboxCompletion(const OutboxCompletion& completion) override
    {
        _real.onOutboxCompletion(completion);
    }
    bool getLastSyncStatus() const override { return _real.getLastSyncStatus(); }
    time_t getLastSyncTime() const override { return _real.getLastSyncTime(); }

    bool forceSync() override
    {
        _forced++;
        return _real.forceSync();
    }

    uint64_t forcedCount() const { return _forced; }

private:
    ISyncRulesUseCase& _real;
    uint64_t _forced = 0;
};

/** The factory DeviceService::init() asks for the real relay state machine. */
class RealUseCaseFactory : public IProcessDeviceStateUseCaseFactory {
public:
    RealUseCaseFactory(ILogger* logger, IDebugSystem* debugSystem, IModuleConfig* moduleConfig,
                       IArduinoAdapter* arduinoAdapter, IDeviceModel* deviceModel)
        : _logger(logger), _debugSystem(debugSystem), _moduleConfig(moduleConfig),
          _arduinoAdapter(arduinoAdapter), _deviceModel(deviceModel)
    {
    }

    IProcessDeviceStateUseCase* create(int) override
    {
        return new ProcessDeviceStateUseCase(_logger, _debugSystem, _moduleConfig,
                                             _arduinoAdapter, _deviceModel);
    }

private:
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IModuleConfig* _moduleConfig;
    IArduinoAdapter* _arduinoAdapter;
    IDeviceModel* _deviceModel;
};

/** Worst silence between two control-loop iterations, and when it happened. */
class CadenceRecorder {
public:
    void tick(uint64_t nowMs)
    {
        if (_count > 0)
        {
            const uint64_t gap = nowMs - _lastMs;
            if (gap > _worstGapMs)
            {
                _worstGapMs = gap;
                _worstGapStartedAtMs = _lastMs;
            }
        }
        _lastMs = nowMs;
        _count++;
    }

    void closeAt(uint64_t nowMs)
    {
        if (_count > 0 && (nowMs - _lastMs) > _worstGapMs)
        {
            _worstGapMs = nowMs - _lastMs;
            _worstGapStartedAtMs = _lastMs;
        }
    }

    uint64_t count() const { return _count; }
    uint64_t worstGapMs() const { return _worstGapMs; }
    uint64_t worstGapStartedAtMs() const { return _worstGapStartedAtMs; }

private:
    uint64_t _count = 0;
    uint64_t _lastMs = 0;
    uint64_t _worstGapMs = 0;
    uint64_t _worstGapStartedAtMs = 0;
};

// --- fixture --------------------------------------------------------------------------------

SensorEntity makeSensor(const char* uid, int port, uint8_t address)
{
    SensorEntity sensor;
    sensor.uid = uid;
    sensor.protocol = "ModBus";
    sensor.model = "soil_moisture_temperature_ec_sensor";
    sensor.sensorType = "temperature";
    sensor.createdAt = "2026-08-22T10:00:00.0000000Z";
    sensor.isActive = true;
    sensor.isError = false;
    sensor.pin = port;
    sensor.sensorTypeUid = "type-1";
    sensor.modbusDeviceAddress = address;
    sensor.baudRate = 9600;

    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = 0x0000;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channel = 1;
    config.channelName = "temperature_channel";
    config.sensorType = "temperature";
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    sensor.modbusRegisterConfigs.push_back(config);

    return sensor;
}

/** One register channel of a catalog entry. */
ModbusRegisterConfig makeRegisterConfig(uint16_t startRegister, uint8_t channel,
                                        const char* channelType)
{
    ModbusRegisterConfig config;
    config.functionCode = 0x03;
    config.startRegister = startRegister;
    config.numRegisters = 1;
    config.multiplier = 0.1f;
    config.offset = 0.0f;
    config.channel = channel;
    config.channelName = std::string(channelType) + "_channel";
    config.sensorType = channelType;
    config.canConnectMinValue = 0.0f;
    config.canConnectMaxValue = 100.0f;
    return config;
}

/**
 * @brief The catalog entry the module's sensors are bound to, at one of its two revisions.
 *
 * It is also what a pin ping looks for on the line, which is why a configured module must carry
 * one at all.
 *
 * Revision one is the single temperature channel a configured module starts with. Revision two
 * adds the humidity channel next to it, which is the change worth simulating: with an air
 * temperature and a humidity channel on the same sensor the module computes a VPD channel it did
 * not have before, so the channel layout it published to the backend is out of date and the
 * structure has to go out again. That republish is the one outgoing HTTP call the control loop
 * could still make.
 *
 * `updatedAt` is what SyncSensorTypesUseCase compares, and it must never be empty here: an empty
 * one is taken for "unknown, sync anyway", and the module would rewrite its catalog on every
 * five-minute fetch for the whole fortnight.
 */
SensorType makeModbusSensorType(const char* uid, const char* name, bool withHumidity,
                                const char* updatedAt)
{
    SensorType type;
    type.uid = uid;
    type.name = name;
    type.protocol = SensorProtocol::MODBUS_PROTOCOL;
    type.canScan = true;
    type.modbusDeviceAddress = 1;
    type.baudRate = 9600;
    type.modbusPingAddress = 0x0000;
    type.createdAt = "2026-01-01T00:00:00.0000000Z";
    type.updatedAt = updatedAt;

    type.modbusRegisterConfig.push_back(
        makeRegisterConfig(0x0000, 1, withHumidity ? "air_temp" : "temperature"));
    if (withHumidity)
    {
        type.modbusRegisterConfig.push_back(makeRegisterConfig(0x0001, 2, "humidity"));
    }

    return type;
}

Rule baseRule(const char* uid, const char* deviceUid, RuleType type)
{
    Rule rule;
    rule.uid = uid;
    rule.version = 1;
    rule.deviceUid = deviceUid;
    rule.ruleType = type;
    rule.mode = RuleMode::ON;
    rule.state = RuleState::SYNCHRONIZED;
    return rule;
}

std::string moduleCommandJson(const char* command)
{
    char buffer[384];
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"ModuleCommandNotification\",\"growDirectorUid\":\"%s\","
             "\"command\":\"%s\",\"options\":{}}",
             MODULE_UID, command);
    return std::string(buffer);
}

ModuleCommandDto makeServerModuleCommand(const std::string& uid, const char* command,
                                         const std::string& options)
{
    ModuleCommandDto dto;
    dto.uid = uid;
    dto.command = command;
    dto.createdAt = "2026-08-22T10:00:00Z";
    dto.options = options;
    return dto;
}

/**
 * @brief The module, wired as globals.h wires it except for what the host cannot build and
 *        what the head of this file lists.
 *
 * Everything LoopServiceConfigureDone calls on an iteration is here: the four services, the
 * diagnostic timer, the MQTT pump, the local-IP check, the connectivity check, the OTA log
 * flush and the firmware confirmation. What is missing from the graph, and what it costs, is
 * listed at the top of this file - read it before concluding that a green run covers something.
 */
struct EnduranceModule {
    // --- who pays for the graph itself --------------------------------------------------------
    //
    // The markers below charge the members that follow them, up to the next marker, to an owner
    // of the census. Without them the whole construction lands on whoever opened the scope around
    // `new EnduranceModule()`, and that is not a bookkeeping detail: this firmware reserves its
    // bounded queues to their cap in constructors, so the completion rings of the command
    // service, the acquisition service and the network runner - tens of kilobytes - would be
    // filed under the harness and the module's total would answer a third less than the truth.
    //
    // The rule for reading them: a marker names the owner of the *following* declarations. What
    // is a stand-in for the outside world (the clock, the line, the mocks, the simulated server's
    // own state) stays with the harness; what is production code, or storage the device keeps in
    // RAM, goes to the subsystem that owns it on the board.

    census::PhaseMarker phaseHarness{census::kBench};
    VirtualClock clock;
    HostTaskScheduler scheduler;
    SimArduino arduino;
    HostBusLock busLock;
    SimNetwork network;
    LinkWorld world;

    SimRs485Bus port0;
    SimRs485Bus port1;
    SimRs485Bus port2;
    SimRs485Bus port3;
    std::vector<IModbusRS485*> ports;

    CountingLogger logger;
    DiagnosticOnlyDebugSystem debugSystem;
    MockSettings settings;
    MockModuleConfig moduleConfig;
    SimNtpTimeAdapter timeAdapter;

    census::PhaseMarker phaseJson{census::kJson};
    // The converter globals.h builds, not a second implementation of IJsonConverter: the
    // documents this one opens come from JsonMemoryPool, which is what makes the pool's traffic
    // and the cost of a parse in the table below the board's numbers.
    ArduinoJsonConverter jsonConverter;
    // Injected wherever the composition root injects the converter, so that what a parse or a
    // serialization costs shows up under its own name instead of under whoever called it.
    CensusJsonConverter censusJsonConverter;

    census::PhaseMarker phaseStructure{census::kStructure};
    ModuleStructure moduleStructure;

    // The two SPIFFS stand-ins keep what the board keeps in flash, so they stay with the harness;
    // the repositories above them are production code holding a cache in RAM on the board too.
    census::PhaseMarker phaseStorageStandIn{census::kBench};
    MockSpiffsSensorDao sensorDao;
    census::PhaseMarker phaseSensorRepository{census::kSensors};
    SensorRepository sensorRepository;
    census::PhaseMarker phaseRuleStandIn{census::kBench};
    MockSpiffsRuleDao ruleDao;
    census::PhaseMarker phaseRuleRepository{census::kRules};
    RuleRepository ruleRepository;
    census::PhaseMarker phaseCatalogStore{census::kCatalog};
    MockSensorTypeRepository sensorTypeRepository;
    census::PhaseMarker phaseCommandRepository{census::kBench};
    MockSensorRepository commandSensorRepository;

    census::PhaseMarker phaseSensors{census::kSensors};
    SensorValueModel sensorValueModel;

    ModbusSensorReader modbusSensorReader;
    RawBusAccessAdapter rawBusAccess;
    SensorReader sensorReader;
    SensorAcquisitionService acquisition;
    SensorService sensorService;

    census::PhaseMarker phaseDeviceModel{census::kBench};
    MockDeviceModel deviceModel;

    census::PhaseMarker phaseDevices{census::kDevices};
    DeviceDigitalOut deviceBySensor;
    DeviceDigitalOut deviceBySchedule;
    DeviceDigitalOut deviceByLoop;
    RealUseCaseFactory useCaseFactory;
    DeviceService deviceService;

    census::PhaseMarker phaseRules{census::kRules};
    StepCycleTable stepTable;
    StepCycleStore stepCycleStore;
    EvaluateRulesUseCase evaluateRules;
    SyncRulesUseCase syncRules;

    census::PhaseMarker phaseCatalog{census::kCatalog};
    SyncSensorTypesUseCase syncSensorTypes;
    // Declared here rather than with the other relays: the local-IP use case takes both of these
    // by reference in its constructor, so they have to exist before it does.
    CensusSyncSensorTypesUseCase censusSyncSensorTypes;
    CensusSyncRulesUseCase censusSyncRules;
    census::PhaseMarker phaseRuleService{census::kRules};
    ForceSyncSpy syncRulesSpy;
    RuleService ruleService;

    census::PhaseMarker phaseVirtualChannels{census::kSensors};
    VpdChannelUseCase vpdChannel;
    VirtualSensorChannelsUseCase virtualChannels;
    census::PhaseMarker phaseSensorStructure{census::kStructure};
    SensorStructureUseCase sensorStructure;

    census::PhaseMarker phaseServerStandIn{census::kRest};
    SimServer server;
    census::PhaseMarker phaseOutbox{census::kOutbox};
    HostMessageQueue<OutboxCompletion> completions;
    Outbox outbox;
    census::PhaseMarker phaseRepublish{census::kStructure};
    RepublishModuleStructureUseCase republishStructure;
    census::PhaseMarker phaseNetworkRunner{census::kOta};
    NetworkCommandRunner networkRunner;

    census::PhaseMarker phaseOutsideWorld{census::kBench};
    SimFirmwareUpdater firmwareUpdater;
    MockClearModuleDataService clearService;
    MockWiFiManager wifiManager;

    census::PhaseMarker phaseCommands{census::kCommands};
    CheckModuleDeletionUseCase checkDeletion;
    ModuleCommandExecutionUseCase moduleCommandUseCase;
    CommandService commandService;

    census::PhaseMarker phaseMqtt{census::kMqtt};
    CommandTopicHandler topicHandler;
    census::PhaseMarker phaseBrokerStandIn{census::kBench};
    ScriptedMqttManager mqttManager;
    SimOfflineCounter offlineCounter;

    census::PhaseMarker phaseDiagnostics{census::kDiagnostics};
    CounterReport counterReport;
    LinkSupervisor linkSupervisor;
    DiagnosticService diagnosticService;

    // The three the loop calls on every iteration besides the services, and the log it flushes
    // every five seconds. Wired rather than left null: two of them are how the module restarts
    // itself, and a run whose headline is "no unexplained restarts" has to contain those paths.
    census::PhaseMarker phaseWifiStandIn{census::kBench};
    SimRadio wifiAdapter;
    SimLinkProbe linkProbe;
    MockButtonUseCases buttonUseCases;
    census::PhaseMarker phaseStatusBoard{census::kDiagnostics};
    ModuleStatusBoard statusBoard;
    census::PhaseMarker phaseLocalIp{census::kStructure};
    OnChangeLocalIpUseCase onChangeLocalIp;
    census::PhaseMarker phaseConnectivity{census::kRules};
    OnConnectivityRestoredUseCase onConnectivityRestored;
    // The order the module comes up in and the setup that precedes it: both are production code,
    // and both are the boot's, so what they hold is the remainder the census is measured by.
    census::PhaseMarker phaseBoot{census::kOther};
    BootSequencer bootSequencer;
    ModuleSetupService setupService;
    census::PhaseMarker phaseOtaStandIn{census::kBench};
    MockOtaPartitionControl otaPartitionControl;
    MockOtaUpdateLog otaUpdateLog;
    census::PhaseMarker phaseOta{census::kOta};
    ConfirmRunningFirmwareUseCase confirmRunningFirmware;

    census::PhaseMarker phaseWatchdogStandIn{census::kBench};
    SimTaskWatchdog taskWatchdog;
    // The loop gate and the loop service itself have no subsystem of their own: what they hold
    // is the remainder the census is measured by.
    census::PhaseMarker phaseLoop{census::kOther};
    LoopLivenessGate loopGate;
    uint64_t extKickCount = 0;
    uint64_t extStarvedCount = 0;

    /** What the loop task sees of the boot sequence and of the module's own restart requests. */
    uint64_t needRestartRequests = 0;
    BootSequencer::Phase lastPhase = BootSequencer::Phase::NOT_STARTED;
    uint64_t phaseEntries[6] = {0};

    /** What a window opens with, to be read again when its return is judged. */
    struct WindowStart {
        uint64_t restarts = 0;
        uint64_t structures = 0;
        uint64_t outagesRecorded = 0;
        uint64_t needRestarts = 0;
        uint64_t diagnosticsAtEnd = 0;
    };

    /**
     * The verdicts of the returns, counted by kind, and the first failures kept with their numbers.
     * A fixed table: the verdict is given on the harness's task, where an assertion cannot be, and
     * the test body reads it from here.
     */
    struct FaultLedger {
        static const size_t MAX_FAILURES = 24;
        struct Failure {
            uint64_t window;
            Fault kind;
            const char* what;
            long long seen;
            long long wanted;
        };
        uint32_t judged[FAULT_KINDS] = {0};
        uint64_t radioRestarts[FAULT_KINDS] = {0};
        uint64_t structures[FAULT_KINDS] = {0};
        uint64_t outagesRecorded[FAULT_KINDS] = {0};
        uint64_t returnsToSteady = 0;
        uint64_t restartsInTheirWindows = 0;
        Failure failures[MAX_FAILURES];
        uint32_t failureCount = 0;

        void fail(const FaultWindow& window, const char* what, long long seen, long long wanted)
        {
            if (failureCount < MAX_FAILURES)
            {
                failures[failureCount].window = window.index;
                failures[failureCount].kind = window.kind;
                failures[failureCount].what = what;
                failures[failureCount].seen = seen;
                failures[failureCount].wanted = wanted;
            }
            failureCount++;
        }
    };
    FaultLedger ledger;

    /**
     * The owner tags of the control loop's fan-out.
     *
     * The loop reaches every service through an interface pointer the composition root sets, so
     * this is where a tag can be put without touching src/: each relay forwards its interface to
     * the real object and adds nothing but the scope. Everything the firmware starts on a task of
     * its own - the outbox network task, the acquisition task, the network command runner - is
     * tagged by task name instead, in wireTheGraph().
     */
    CensusSensorService censusSensorService;
    CensusDeviceService censusDeviceService;
    CensusRuleService censusRuleService;
    CensusCommandService censusCommandService;
    CensusDiagnosticService censusDiagnosticService;
    CensusRepublishStructureUseCase censusRepublishStructure;
    CensusOnChangeLocalIpUseCase censusOnChangeLocalIp;
    CensusOnConnectivityRestoredUseCase censusOnConnectivityRestored;
    CensusConfirmRunningFirmwareUseCase censusConfirmRunningFirmware;
    CensusOutbox censusOutbox;

    LoopServiceConfigureDone loopService;
    census::PhaseMarker phaseRecorder{census::kBench};
    CadenceRecorder cadence;

    EnduranceModule()
        : scheduler(clock),
          arduino(clock, &scheduler),
          busLock(scheduler, clock),
          network(clock, scheduler),
          port0(clock, "sim-port-0"),
          port1(clock, "sim-port-1"),
          port2(clock, "sim-port-2"),
          port3(clock, "sim-port-3"),
          ports{&port0, &port1, &port2, &port3},
          logger(clock),
          moduleConfig(&logger, &debugSystem, &settings),
          timeAdapter(clock, world),
          censusJsonConverter(jsonConverter),
          sensorRepository(&sensorDao),
          ruleRepository(&ruleDao, &timeAdapter),
          modbusSensorReader(ports, arduino, &logger, &debugSystem),
          rawBusAccess(ports, arduino, &logger, &debugSystem),
          sensorReader(nullptr, &modbusSensorReader, &arduino, &logger, &debugSystem),
          acquisition(&sensorReader, &arduino, &scheduler, &logger, &debugSystem),
          sensorService(&moduleConfig, &moduleStructure, &sensorRepository, &sensorReader,
                        &sensorValueModel, &timeAdapter, &arduino, nullptr, &logger,
                        &debugSystem),
          deviceBySensor(),
          deviceBySchedule(),
          deviceByLoop(),
          useCaseFactory(&logger, &debugSystem, &moduleConfig, &arduino, &deviceModel),
          deviceService(&moduleConfig, &moduleStructure, &deviceModel, &timeAdapter, &arduino,
                        &useCaseFactory, nullptr, &logger, &debugSystem),
          stepCycleStore(stepTable),
          evaluateRules(&deviceModel, &ruleRepository, &arduino, &timeAdapter, &sensorValueModel,
                        &logger, &debugSystem, nullptr, &stepCycleStore),
          syncRules(&server, &ruleRepository, &arduino, &timeAdapter, &censusJsonConverter, &logger,
                    &debugSystem),
          syncSensorTypes(&server, &sensorTypeRepository, &arduino, &timeAdapter,
                          &censusJsonConverter, &logger, &debugSystem, &sensorRepository),
          censusSyncSensorTypes(syncSensorTypes),
          censusSyncRules(syncRules),
          syncRulesSpy(syncRules),
          ruleService(&evaluateRules, &syncRules, &deviceModel, &logger, &debugSystem),
          virtualChannels(&vpdChannel),
          sensorStructure(&virtualChannels, &logger, &debugSystem),
          server(scheduler, network, clock, world),
          completions(16),
          outbox(&server, &scheduler, &completions, &logger, &debugSystem),
          republishStructure(&moduleStructure, &sensorRepository, &server, &sensorStructure,
                             &logger, &debugSystem, &arduino),
          networkRunner(&scheduler, &logger, &debugSystem),
          firmwareUpdater(scheduler),
          checkDeletion(&server, &logger, &debugSystem),
          moduleCommandUseCase(&logger, &debugSystem, &timeAdapter, &firmwareUpdater, &arduino,
                               &commandSensorRepository, &sensorTypeRepository, &sensorReader,
                               &moduleConfig, &settings, &server, &clearService,
                               &moduleStructure, &sensorValueModel, &wifiManager, nullptr,
                               &rawBusAccess, &checkDeletion),
          commandService(nullptr, &moduleCommandUseCase, nullptr, &moduleStructure, &logger,
                         &debugSystem, &timeAdapter, &censusJsonConverter),
          topicHandler(&logger, &debugSystem, MODULE_UID, &commandService, &censusJsonConverter),
          mqttManager(world),
          linkSupervisor(&offlineCounter, &moduleConfig, logger, debugSystem, &wifiAdapter, &timeAdapter,
                         &arduino, &mqttManager),
          diagnosticService(&offlineCounter, &moduleConfig, logger, debugSystem,
                            &censusJsonConverter, &wifiAdapter, &timeAdapter, &arduino,
                            &mqttManager, &server),
          wifiAdapter(scheduler, clock, world),
          linkProbe(wifiAdapter, scheduler),
          onChangeLocalIp(wifiAdapter, wifiManager, moduleStructure, moduleConfig, censusSyncRules,
                          censusSyncSensorTypes, logger, debugSystem),
          onConnectivityRestored(&linkSupervisor, &syncRulesSpy, &moduleConfig, &arduino,
                                 &logger, &debugSystem),
          bootSequencer(&arduino, &logger, &debugSystem),
          setupService(&logger, &debugSystem, &moduleConfig, nullptr, nullptr, nullptr, &buttonUseCases,
                       &wifiAdapter, &wifiManager, nullptr, nullptr, &mqttManager, &moduleStructure,
                       &censusDeviceService, &censusRuleService, &sensorRepository,
                       &sensorTypeRepository, &sensorReader, nullptr, nullptr, nullptr, nullptr, nullptr,
                       &arduino, &syncSensorTypes, nullptr, &onConnectivityRestored),
          confirmRunningFirmware(&otaPartitionControl, &moduleStructure, &arduino, &logger,
                                 &debugSystem, &otaUpdateLog),
          taskWatchdog(scheduler, clock, (uint32_t)WDT_TIMEOUT_S * 1000UL),
          loopGate((uint32_t)WDT_TIMEOUT_S * 1000UL),
          censusSensorService(sensorService),
          censusDeviceService(deviceService),
          censusRuleService(ruleService),
          censusCommandService(commandService),
          censusDiagnosticService(diagnosticService),
          censusRepublishStructure(republishStructure),
          censusOnChangeLocalIp(onChangeLocalIp),
          censusOnConnectivityRestored(onConnectivityRestored),
          censusConfirmRunningFirmware(confirmRunningFirmware),
          censusOutbox(outbox)
    {
        settings.setUser("endurance@example.com");
        settings.setAccountUid("endurance-account");
        moduleConfig.setSettings(&settings);
        moduleConfig.setModuleUid(MODULE_UID);
        // A hydro module: the one type that has both halves of this bench - RS-485 sensor ports
        // (getSensorPinCount() answers for SENSOR and HYDRO only) and pumps that
        // ProcessDeviceStateUseCase actually drives to a pin. On a sensor module the rule
        // decisions would stop short of the GPIO write; on a device module the bus commands
        // would have no port to run on.
        moduleConfig.setModuleType(MODULE_TYPE_HYDRO);
        moduleConfig.setSensorPinCount(4);
        moduleConfig.setDeviceCount(3);
        moduleConfig.setFirmwareVersion("endurance");

        // What the settings hold when the setup runs: a configured, registered module. The setup
        // fills the structure and starts nothing on the network; the station associates on the
        // first iteration of the loop, as the board's does. And what the per-iteration use cases
        // test before they do anything: the image on probation - a module that has just taken an
        // update and has to earn the confirmation by working, which is the case worth running for
        // a fortnight.
        world.radio = &wifiAdapter;
        moduleConfig.setWifiAdapter(&wifiAdapter);
        settings.setConfigured(ModuleModeEnum::CONFIGURED_DONE);
        settings.setWasInitialSyncCompleted(1);
        settings.setNoScanSensors(false);
        // The requested mode, which ModuleSetupService::handleModuleMode() sets from the stored one
        // on every boot. The mock records each setter in a map, so its first call allocates; made
        // here, that bookkeeping stays out of the hours the balance below compares.
        moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
        offlineCounter.setSource(server.consecutiveFailuresAddress());
        otaPartitionControl.runningState = OtaImageState::PENDING_VERIFY;

        // The RTC table is RTC_NOINIT_ATTR on the device and holds whatever was there; here it is
        // a plain member, and a member left to default initialization would carry stack garbage
        // that differs between a standalone run and a run after 941 other tests. Zeroed and then
        // swept exactly as the composition root does after a power cut.
        memset(&stepTable, 0, sizeof(stepTable));
        stepCycleStore.dropInvalidSlots();

        // Process-wide, like the allocation failure counter: whatever the test files before this
        // one left in it is not this run's worst control-loop step.
        LoopStepMonitor::reset();

        seedTheLine();
        seedTheRegistries();
        seedTheDevices();
        wireTheGraph();

        // The graph is built. Everything from here on is charged by the relays and by the task
        // tags; what the construction itself cost is read by the census run, by owner.
    }

    // --- the line ---------------------------------------------------------------------------

    void seedTheLine()
    {
        SimRs485Bus::Device healthy;
        healthy.address = 1;
        healthy.registerValues.push_back(250); // x 0.1 = 25.0 degrees
        port0.addDevice(healthy);

        SimRs485Bus::Device flapping;
        flapping.address = 1;
        flapping.registerValues.push_back(180);
        port1.addDevice(flapping);

        SimRs485Bus::Device silent;
        silent.address = 1;
        silent.silent = true;
        port2.addDevice(silent);

        // Ports 2 and 3 share a UART, as they do on the board (globals.h): a model scan of the
        // free port 3 takes the silent sensor's power for its length. Ports 0 and 1 are left a
        // line each - both their devices answer at 0x01, which on one wire would be a collision.
        port2.shareTheLineWith(port3);
    }

    /**
     * @brief Drop what the simulators themselves recorded, keeping only the module's own memory.
     *
     * SimRs485Bus keeps every frame the master ever wrote, and SimFirmwareUpdater every task name
     * that fed the watchdog during a download. Both are how a short scenario checks what went on
     * the wire, and both grow without limit by design - over 80 000 sweeps that is the simulator
     * accumulating, not the firmware. The live-allocation balance is about the module, so the
     * recorders are emptied at every hourly checkpoint before the balance is read.
     *
     * clear() releases the elements but keeps the vector's own capacity, which is why this
     * bounds the recorders rather than removing them: their capacity reaches its plateau inside
     * the first day, before the balance starts being accounted. If the memory budget below ever
     * goes red, check that plateau before looking for a leak in src/ - a rare event that made a
     * recorder grow later would arrive as unexplained bytes.
     */
    void clearSimulatorRecorders()
    {
        SimRs485Bus* allPorts[] = {&port0, &port1, &port2, &port3};
        for (size_t i = 0; i < sizeof(allPorts) / sizeof(allPorts[0]); i++)
        {
            allPorts[i]->sentFrames.clear();
            allPorts[i]->writes.clear();
        }
        firmwareUpdater.progressFeedTaskNames.clear();
    }

    /** The reading the healthy sensor reports; moved by the scenario to drive its rule. */
    void setHealthyReading(uint16_t rawValue) { port0.device(1)->registerValues[0] = rawValue; }

    /** The flapping sensor answers or does not, on its own schedule. */
    void setFlappingSilent(bool silent) { port1.device(1)->silent = silent; }

    // --- registries -------------------------------------------------------------------------

    void seedTheRegistries()
    {
        // Sensors and rules are seeded into the SPIFFS stand-ins, which model flash; the catalog
        // is seeded into the repository, which on the board holds it in RAM. Hence two owners.
        census::Scope owner(census::kBench);
        // Bound to the catalog entry below, the way a configured module's sensors are: without
        // the type uid the catalog sync skips them, and a catalog change would move nothing.
        sensorDao.saveSensor(makeSensor(SENSOR_HEALTHY_UID, 0, 1));
        sensorDao.saveSensor(makeSensor(SENSOR_FLAPPING_UID, 1, 1));
        sensorDao.saveSensor(makeSensor(SENSOR_SILENT_UID, 2, 1));

        // The catalog a configured module carries. Without it a pin ping has nothing to look
        // for and never reaches the line: the command asks the network for a catalog instead.
        // The account has the same revision, so the five-minute sync finds nothing to do until
        // driveTheWorld() changes it at CATALOG_CHANGE_HOUR.
        {
            census::Scope catalogOwner(census::kCatalog);
            sensorTypeRepository.addSensorType(
                makeModbusSensorType("type-1", "ENDURANCE-TEMP", false, CATALOG_REVISION_1));
        }
        server.serverSensorTypes.push_back(
            makeModbusSensorType("type-1", "ENDURANCE-TEMP", false, CATALOG_REVISION_1));

        std::vector<Rule> rules;

        // Held by a reading: the sensor rule switches its device with the temperature.
        Rule sensorRule = baseRule(RULE_SENSOR_UID, DEVICE_SENSOR_UID, RuleType::SENSOR);
        sensorRule.sensorUid = REMOTE_CHANNEL_UID;
        sensorRule.direction = RuleDirection::DOWN;
        sensorRule.valueLow = 10;
        sensorRule.valueHigh = 20;
        rules.push_back(sensorRule);

        // A daily window, 09:00 to 11:00. Only the time of day of startAt is read
        // (EvaluateRulesUseCase takes tm_hour/tm_min off it), so the window recurs every day of
        // the fortnight. Placed in the half of the day the sensor rule leaves its pump off: on a
        // hydro module a pump is admitted only while every other one is off, so a window that
        // overlapped the sensor rule's six-hour hold would simply never open.
        Rule scheduleRule = baseRule(RULE_SCHEDULE_UID, DEVICE_SCHEDULE_UID, RuleType::SCHEDULE);
        scheduleRule.schedule.push_back(ScheduleItem((time_t)(9 * 3600), 7200));
        rules.push_back(scheduleRule);

        // Irrigation: fifteen minutes on, forty-five off, for a fortnight - 336 full cycles of
        // relay switching, submissions and completions to accumulate behind.
        Rule loopRule = baseRule(RULE_LOOP_UID, DEVICE_LOOP_UID, RuleType::LOOP);
        loopRule.workingTime = 900;
        loopRule.waitingTime = 2700;
        rules.push_back(loopRule);

        ruleDao.saveAll(rules);
        server.serverRules = rules;
    }

    // --- devices ----------------------------------------------------------------------------

    static void setUpDevice(DeviceDigitalOut& device, const char* uid)
    {
        device.deviceUid = uid;
        device.deviceType = DEVICE_TYPE_DIGITAL;
        device.state = DEVICE_STATE_OFF;
        device.isError = false;
        device.isAuto = false;
        device.isManual = false;
        device.workByRule = false;
        device.workManual = false;
        device.needToSwitchBack = false;
        device.needReportingRuleUid = false;
        device.commandId = 0;
        device.ruleUid = nullptr;
        device.switchedAt = nullptr;
        device.ruleType = RuleType::SENSOR;
        device.ruleVersion = 0;
        device.workflowUid = nullptr;
        device.workflowVersion = 0;
        device.sensorUid = nullptr;
        device.ruleSensorStartAt = nullptr;
        device.ruleSensorStopAt = nullptr;
        device.ruleStopReason = nullptr;
        device.ruleScheduleStartAt = nullptr;
        device.isRuleTurnOnDevice = false;
    }

    void seedTheDevices()
    {
        setUpDevice(deviceBySensor, DEVICE_SENSOR_UID);
        setUpDevice(deviceBySchedule, DEVICE_SCHEDULE_UID);
        setUpDevice(deviceByLoop, DEVICE_LOOP_UID);
        deviceModel.addDevice(&deviceBySensor);
        deviceModel.addDevice(&deviceBySchedule);
        deviceModel.addDevice(&deviceByLoop);
    }

    // --- the graph --------------------------------------------------------------------------

    void wireTheGraph()
    {
        modbusSensorReader.setBusLock(&busLock);
        sensorReader.setBusLock(&busLock);
        rawBusAccess.setBusLock(&busLock);
        // The bench begins the line itself: the setup of this module does not. A line nobody began
        // carries nothing, and ModuleSetupService::setupSensorBus() begins the UARTs for a sensor
        // module only - on a hydro module they are the pins of its pumps. The head of this file
        // says what that leaves unproven.
        sensorReader.init();

        sensorService.setAcquisition(&acquisition);
        acquisition.setRawCommandExecutor(&moduleCommandUseCase);
        networkRunner.setExecutor(&moduleCommandUseCase);

        // What the firmware starts on its own: the census cannot put a relay between the loop and
        // a task nobody injects, so those are tagged by the name the firmware gives them. The
        // names are the ones in the code that creates the tasks, not new strings.
        scheduler.setTaskCensusOwner("outbox-net", census::kOutbox);
        scheduler.setTaskCensusOwner(SensorAcquisitionService::TASK_NAME, census::kSensors);
        scheduler.setTaskCensusOwner(NetworkCommandRunner::TASK_NAME, census::kOta);
        // The control loop's own task carries no owner: what it allocates outside the relays
        // below is exactly the remainder the census is measured by.
        scheduler.setTaskCensusOwner(LOOP_TASK_NAME, census::kOther);
        // Two tasks that belong to the bench and not to the module: the neighbouring sensor
        // module and the external watchdog chip.
        scheduler.setTaskCensusOwner("remote-sensor", census::kBench);
        scheduler.setTaskCensusOwner("ext-wdt-kicker", census::kBench);
        scheduler.setTaskCensusOwner("sim-task-wdt", census::kBench);
        scheduler.setTaskCensusOwner("link-world", census::kBench);

        sensorService.setOutbox(&censusOutbox);
        deviceService.setOutbox(&censusOutbox);
        syncRules.setOutbox(&censusOutbox);
        syncSensorTypes.setOutbox(&censusOutbox);
        // The consequence of a catalog that moved a computed channel: the structure goes out
        // again, through the outbox like everything else, and its outcome comes back to the
        // use case on the loop.
        syncSensorTypes.setVirtualSensorChannelsUseCase(&virtualChannels);
        syncSensorTypes.setRepublishModuleStructureUseCase(&censusRepublishStructure);
        syncSensorTypes.setSensorValueModel(&sensorValueModel);
        // The same offline decision OnConnectivityRestoredUseCase watches, read by the catalog sync
        // to re-arm a fetch the outage cost it.
        syncSensorTypes.setNetworkStatusProvider(&linkSupervisor);
        republishStructure.setOutbox(&censusOutbox);
        onChangeLocalIp.setOutbox(&censusOutbox);
        onChangeLocalIp.setClock(&arduino);
        commandService.setOutbox(&censusOutbox);
        diagnosticService.setOutbox(&censusOutbox);
        linkSupervisor.setOutbox(&censusOutbox);
        diagnosticService.setLinkSupervisor(&linkSupervisor);
        commandService.setBusCommandExecutor(&acquisition);
        commandService.setNetworkCommandRunner(&networkRunner);
        mqttManager.setHandler(&topicHandler);

        // The counters line, registered exactly as the composition root registers it, so that
        // what the run reads is the line the device prints. It is a copy of the block in
        // globals.h and will drift from it silently: the registration there names objects this
        // bench does not have (the FreeRTOS scheduler's untracked-task count has no host
        // counterpart and is a literal zero here). Compare the two if a column ever goes
        // missing from the device log and not from this run.
        {
        census::Scope counterOwner(census::kDiagnostics);
        counterReport.setTaskScheduler(&scheduler);
        counterReport.addCounter("max_loop_step_ms",
                                 [] { return (uint64_t)LoopStepMonitor::take(); });
        counterReport.addCounter("tls_fallbacks",
                                 [] { return (uint64_t)TlsMemoryPool::heapFallbacks(); });
        counterReport.addCounter("cmd_drop_signal",
                                 [this] { return (uint64_t)commandService.droppedSignalCount(); });
        counterReport.addCounter("cmd_drop_report",
                                 [this] { return (uint64_t)commandService.droppedReportCount(); });
        counterReport.addCounter("cmd_drop_batch",
                                 [this] { return (uint64_t)commandService.droppedBatchCount(); });
        counterReport.addCounter(
            "cmd_refuse_bus", [this] { return (uint64_t)commandService.refusedBusCommandCount(); });
        counterReport.addCounter("net_refused",
                                 [this] { return (uint64_t)networkRunner.refusedCount(); });
        counterReport.addCounter(
            "net_task_fail", [this] { return (uint64_t)networkRunner.taskStartFailureCount(); });
        counterReport.addCounter(
            "acq_refuse_bus", [this] { return (uint64_t)acquisition.refusedBusCommandCount(); });
        counterReport.addCounter(
            "acq_task_fail", [this] { return (uint64_t)acquisition.taskStartFailureCount(); });
        counterReport.addCounter("acq_drop_reading",
                                 [this] { return acquisition.droppedReadingCount(); });
        counterReport.addCounter("outbox_refused_events",
                                 [this] { return (uint64_t)outbox.refusedEventCount(); });
        counterReport.addCounter("outbox_lost_completions",
                                 [this] { return (uint64_t)outbox.lostCompletionCount(); });
        counterReport.addCounter("outbox_task_fail",
                                 [this] { return (uint64_t)outbox.taskStartFailureCount(); });
        counterReport.addCounter("sched_untracked_tasks", [] { return (uint64_t)0; });
        registerMemoryCounters(counterReport, arduino);
        counterReport.addTaskStack("stack_cmd_net", NetworkCommandRunner::TASK_NAME);
        counterReport.addTaskStack("stack_sensor_acq", SensorAcquisitionService::TASK_NAME);
        counterReport.addTaskStack("stack_outbox_net", "outbox-net");
        diagnosticService.setCounterReport(&counterReport);
        }

        loopService.setLogging(&logger, &debugSystem);
        loopService.setModuleConfig(&moduleConfig);
        loopService.setArduinoAdapter(&arduino);
        loopService.setOutbox(&censusOutbox);
        loopService.setCommandService(&censusCommandService);
        loopService.setDeviceService(&censusDeviceService);
        loopService.setSensorService(&censusSensorService);
        loopService.setRuleService(&censusRuleService);
        loopService.setMqttManager(&mqttManager);
        loopService.setDiagnosticService(&censusDiagnosticService);
        loopService.setLinkSupervisor(&linkSupervisor);
        loopService.setOnChangeLocalIpUseCase(&censusOnChangeLocalIp);
        loopService.setOnConnectivityRestoredUseCase(&censusOnConnectivityRestored);
        loopService.setConfirmRunningFirmwareUseCase(&censusConfirmRunningFirmware);
        loopService.setSyncSensorTypesUseCase(&censusSyncSensorTypes);
        loopService.setRepublishModuleStructureUseCase(&censusRepublishStructure);
        loopService.setOtaUpdateLog(&otaUpdateLog);

        // What globals.h adds for the link: the outbox's radio, the looks it takes at the link and
        // what the module's parts report about themselves, and the sequence the module comes up
        // in. The producers ask it before they act; it reads its events off their owners.
        outbox.setWifiAdapter(&wifiAdapter);
        outbox.setLinkProbe(&linkProbe, boardLinkProbePlan());
        outbox.setStatusBoard(&statusBoard);
        linkSupervisor.setStatusBoard(&statusBoard);

        bootSequencer.setWifiAdapter(&wifiAdapter);
        bootSequencer.setOnChangeLocalIpUseCase(&onChangeLocalIp);
        bootSequencer.setSensorService(&sensorService);
        bootSequencer.setSyncSensorTypesUseCase(&syncSensorTypes);
        bootSequencer.setMqttManager(&mqttManager);
        bootSequencer.setSyncRulesUseCase(&syncRules);
        bootSequencer.setNetworkListeners(&setupService);
        bootSequencer.setBootAssociation(&linkSupervisor);
        bootSequencer.setCommandService(&commandService);
        bootSequencer.setClock(&timeAdapter);
        {
            // The ceilings globals.h derives, from the same owners' constants.
            BootSequencer::CeilingInputs inputs;
            inputs.restAttemptMs = 15000 + 5000 + 120000 + 30000;
            inputs.attemptsPerJob = Outbox::DEFAULT_MAX_ATTEMPTS;
            inputs.firstBackoffMs = Outbox::DEFAULT_BACKOFF_BASE_MS;
            inputs.backoffFactor = Outbox::BACKOFF_FACTOR;
            inputs.mqttActMs = MqttManager::CLOSE_GRACE_MS + MqttManager::CONNECT_TIMEOUT_MS;
            inputs.jobsAheadOfTheCatalog = 6;
            inputs.portWakeUpMs = PortPower::WAKE_UP_MS;
            inputs.sweepIntervalMs = SensorAcquisitionService::SWEEP_INTERVAL_MS;
            inputs.channelReadMs = 3 * 300 + 2 * 100;
            bootSequencer.setCeilings(BootSequencer::ceilingsFrom(inputs));
        }
        sensorService.setBootGates(&bootSequencer);
        ruleService.setBootGates(&bootSequencer);
        deviceService.setBootGates(&bootSequencer);
        republishStructure.setBootGates(&bootSequencer);
        loopService.setBootSequencer(&bootSequencer);
        setupService.setSensorStructureUseCase(&sensorStructure);

        TEST_ASSERT_TRUE_MESSAGE(censusSensorService.init(),
                                 "sensor service failed to initialize");

        // setup() as main.cpp runs it. It waits for no network: it fills the structure, initializes
        // the device and rule services and the connectivity use case - the last of which decides in
        // init() whether this module type evaluates rules at all, so that without it perform()
        // returns on its first line for the whole run - and leaves the station to the loop.
        {
            census::Scope setupOwner(census::kStructure);
            setupService.setupModule();
        }
        TEST_ASSERT_EQUAL_STRING_MESSAGE(MODULE_UID, moduleStructure.growdirectorUid.c_str(),
                                         "the setup did not fill the module's structure");
        TEST_ASSERT_TRUE_MESSAGE(!moduleStructure.nodes.empty(), "the setup left the structure without a node");
        TEST_ASSERT_FALSE_MESSAGE(wifiAdapter.connected, "the setup brought the station up: it waited for the network");

        taskWatchdog.start();
    }

    // --- the tasks --------------------------------------------------------------------------

    /** One task, mirroring main.cpp: feed, beat, run the loop service, come back. */
    void startArduinoLoopTask()
    {
        scheduler.createTask(LOOP_TASK_NAME, 512 * 1024, 1, [this]() {
            scheduler.watchdogSubscribeCurrentTask();
            while (true)
            {
                cadence.tick(clock.nowMs());
                scheduler.watchdogFeed();
                loopGate.noteAlive((uint32_t)clock.nowMs());
                loopService.loop();
                observeTheLoop();
                scheduler.delayMs(CONTROL_PERIOD_MS);
            }
        });
    }

    /**
     * @brief A neighbouring sensor module, publishing one channel every fifteen seconds.
     *
     * On a real site the sensor a rule watches lives on another module and arrives over UDP or
     * MQTT; RemoteSensorClient writes exactly this pair into the value model for every channel
     * of the payload. Modelled as a task rather than as a nudge between hours because the rule
     * engine's freshness window is five minutes: a reading injected once an hour would leave
     * the rule stale for fifty-five of every sixty.
     *
     * The value follows a six-hour swing, so the rule holds its pump on for six hours and off
     * for six, all fortnight.
     */
    void startRemoteSensorTask()
    {
        scheduler.createTask("remote-sensor", 64 * 1024, 1, [this]() {
            while (true)
            {
                const uint64_t hour = clock.nowMs() / MS_PER_HOUR;
                const float value = ((hour / 6) % 2 == 0) ? 25.0f : 5.0f;
                {
                    // The task is the harness's, the entry it leaves in the value model is the
                    // module's - on the board RemoteSensorClient writes the same pair.
                    census::Scope owner(census::kSensors);
                    sensorValueModel.setSensorValue(REMOTE_CHANNEL_UID, value);
                    sensorValueModel.setLastReadTime(REMOTE_CHANNEL_UID,
                                                     (uint32_t)clock.nowMs());
                }
                scheduler.delayMs(REMOTE_PUBLISH_INTERVAL_MS);
            }
        });
    }

    /** The external kicker: ExternalWDT::wdtTask()'s decision, on the harness clock. */
    void startExternalKickerTask()
    {
        scheduler.createTask("ext-wdt-kicker", 64 * 1024, 2, [this]() {
            while (true)
            {
                if (loopGate.allowKick((uint32_t)clock.nowMs()))
                {
                    extKickCount++;
                }
                else
                {
                    extStarvedCount++;
                }
                scheduler.delayMs(EXT_KICK_INTERVAL_MS);
            }
        });
    }

    // --- the link world ---------------------------------------------------------------------

    void observeTheLoop()
    {
        const BootSequencer::Phase phase = bootSequencer.phase();
        if (phase != lastPhase)
        {
            phaseEntries[(size_t)phase]++;
            lastPhase = phase;
        }
        // What a restart of the module asks for is noted and taken back, so that the next one is
        // seen too: nothing on the host acts on it.
        if (moduleConfig.getRequestedModuleMode() == ModuleModeEnum::CONFIGURED_NEED_RESTART)
        {
            needRestartRequests++;
            moduleConfig.setRequestedModuleMode(ModuleModeEnum::CONFIGURED_DONE);
        }
    }

    /** Every gate the sequence holds producers behind is open. */
    bool allGatesAreOpen() const
    {
        return bootSequencer.allows(BootGate::TELEMETRY) && bootSequencer.allows(BootGate::RULE_LIST) &&
               bootSequencer.allows(BootGate::DEVICE_UPDATE) &&
               bootSequencer.allows(BootGate::STRUCTURE_REPUBLISH) &&
               bootSequencer.allows(BootGate::DIAGNOSTIC_SEND) && bootSequencer.allows(BootGate::DISCOVERY);
    }

    void sleepUntil(uint64_t atMs)
    {
        if (atMs > clock.nowMs())
        {
            scheduler.delayMs((uint32_t)(atMs - clock.nowMs()));
        }
    }

    WindowStart openTheWindow(const FaultWindow& window)
    {
        WindowStart start;
        start.restarts = wifiAdapter.restarts();
        start.structures = server.structuresDelivered();
        start.outagesRecorded = logger.tape(CountingLogger::OUTAGE_RECORDED);
        start.needRestarts = needRestartRequests;

        world.active = window.kind;
        switch (window.kind)
        {
        case Fault::ROUTER_SILENT:
            linkProbe.routerAnswers = false;
            break;
        case Fault::INTERNET_DOWN:
            linkProbe.internetAnswers = false;
            break;
        case Fault::WIFI_DROP:
            wifiAdapter.accessPointInRange = false;
            wifiAdapter.connected = false;
            if (window.newAddress)
            {
                wifiAdapter.handOutANewAddressOnTheNextAssociation();
            }
            break;
        default:
            break;
        }
        return start;
    }

    void closeTheWindow(const FaultWindow& window, WindowStart& start)
    {
        world.active = Fault::NONE;
        linkProbe.routerAnswers = true;
        linkProbe.internetAnswers = true;
        if (window.kind == Fault::WIFI_DROP)
        {
            wifiAdapter.accessPointInRange = true;
            if (window.returnsByItself)
            {
                // What the framework's reconnection does once the access point answers again.
                scheduler.delayMs(2000);
                wifiAdapter.associateByItself();
            }
        }
        start.diagnosticsAtEnd = server.diagnosticsDelivered();
    }

    /**
     * @brief The return of the module from one window, judged when its allowance is over.
     *
     * What a return has to look like is read off the module, not off a second copy of the algorithm:
     * the sequence is back in STEADY with every gate open and the troubleshooting back to OK; the
     * radio was restarted only for a fault on the radio's side of the router, on the pauses the
     * machine's own table names, and the module not at all; the structure went out once when the
     * address changed and not otherwise; the outage was recorded once, with the cause the plan gave
     * it; and the diagnostics reach the server again.
     */
    void judge(const FaultWindow& window, const WindowStart& start)
    {
        const size_t kind = (size_t)window.kind;
        ledger.judged[kind]++;
        const bool onTheRadiosSide = window.kind == Fault::ROUTER_SILENT || window.kind == Fault::WIFI_DROP;

        // 1. Where a module at rest is.
        if (bootSequencer.phase() != BootSequencer::Phase::STEADY)
        {
            ledger.fail(window, "the boot sequence did not end the return in STEADY",
                        (long long)bootSequencer.phase(), (long long)BootSequencer::Phase::STEADY);
        }
        else
        {
            ledger.returnsToSteady++;
        }
        if (!allGatesAreOpen())
        {
            ledger.fail(window, "a gate of the boot sequence stayed shut after the return", 0, 1);
        }
        if (linkSupervisor.troublePhase() != LinkTroubleshooting::Phase::OK)
        {
            ledger.fail(window, "the link troubleshooting did not come back to OK",
                        (long long)linkSupervisor.troublePhase(), (long long)LinkTroubleshooting::Phase::OK);
        }

        // 2. Restarts: of the radio, by the algorithm; of the module, never.
        const uint64_t restarts = wifiAdapter.restarts() - start.restarts;
        ledger.radioRestarts[kind] += restarts;
        if (needRestartRequests != start.needRestarts)
        {
            ledger.fail(window, "the module was asked to restart", (long long)(needRestartRequests - start.needRestarts), 0);
        }
        if (!onTheRadiosSide)
        {
            if (restarts != 0)
            {
                ledger.fail(window, "the radio was restarted for a fault past the router", (long long)restarts, 0);
            }
        }
        else
        {
            ledger.restartsInTheirWindows += restarts;
            if (window.kind == Fault::ROUTER_SILENT && restarts == 0)
            {
                ledger.fail(window, "a router out of reach was not answered with a radio restart", 0, 1);
            }
            if (restarts > 0)
            {
                const uint64_t first = wifiAdapter.startOfRestart(start.restarts);
                if (window.kind == Fault::ROUTER_SILENT &&
                    first < window.startMs + LinkTroubleshooting::SUSPECT_MS + LinkTroubleshooting::GATEWAY_RECHECK_MS)
                {
                    ledger.fail(window, "the radio was restarted before the gateway was found silent twice",
                                (long long)(first - window.startMs),
                                (long long)(LinkTroubleshooting::SUSPECT_MS + LinkTroubleshooting::GATEWAY_RECHECK_MS));
                }
                for (uint64_t i = 1; i < restarts; i++)
                {
                    const uint64_t gap = wifiAdapter.startOfRestart(start.restarts + i) -
                                         wifiAdapter.startOfRestart(start.restarts + i - 1);
                    const uint32_t pause = LinkTroubleshooting::restartPauseMs((uint8_t)(i - 1 < 3 ? i - 1 : 3));
                    if (gap < pause)
                    {
                        ledger.fail(window, "a radio restart came before its pause was over", (long long)gap,
                                    (long long)pause);
                    }
                    // And not long after it: the restart that ended the last one, a supervision tick, a
                    // look at the gateway and a request in flight ahead of it on the network task are
                    // what a pause can be stretched by - six minutes cover them with room to spare.
                    if (gap > pause + 6ULL * 60ULL * 1000ULL)
                    {
                        ledger.fail(window, "a radio restart was put off long past its pause", (long long)gap,
                                    (long long)pause);
                    }
                }
                // No more of them than the pauses leave room for between the first and the verdict.
                const uint64_t room = window.endMs + returnAllowanceMs(window) - first;
                uint64_t most = 1;
                uint64_t elapsed = 0;
                for (uint8_t i = 0;; i++)
                {
                    elapsed += LinkTroubleshooting::restartPauseMs(i < 3 ? i : 3);
                    if (elapsed > room)
                    {
                        break;
                    }
                    most++;
                }
                if (restarts > most)
                {
                    ledger.fail(window, "more radio restarts than the pauses allow", (long long)restarts, (long long)most);
                }
            }
        }

        // 3. The structure goes out once for an address that changed, and not for one that did not.
        const uint64_t structures = server.structuresDelivered() - start.structures;
        const uint64_t wanted = (window.kind == Fault::WIFI_DROP && window.newAddress) ? 1 : 0;
        ledger.structures[kind] += structures;
        if (structures != wanted)
        {
            ledger.fail(window, "the structure did not go out once per new address", (long long)structures, (long long)wanted);
        }

        // 4. The outage is recorded, once, with its cause.
        const uint64_t recorded = logger.tape(CountingLogger::OUTAGE_RECORDED) - start.outagesRecorded;
        ledger.outagesRecorded[kind] += recorded;
        LinkTroubleshooting::Outage outage;
        if (recorded != 1 || !linkSupervisor.lastOutage(outage))
        {
            ledger.fail(window, "the outage was not recorded exactly once", (long long)recorded, 1);
        }
        else
        {
            const LinkTroubleshooting::Cause cause = window.kind == Fault::SERVER_DOWN ? LinkTroubleshooting::Cause::SERVICES
                                                    : window.kind == Fault::INTERNET_DOWN ? LinkTroubleshooting::Cause::INTERNET
                                                                                          : LinkTroubleshooting::Cause::ROUTER;
            if (outage.cause != cause)
            {
                ledger.fail(window, "the outage was recorded with another cause", (long long)outage.cause, (long long)cause);
            }
            if (outage.startedAtMs < window.startMs ||
                outage.startedAtMs > window.startMs + LinkSupervisor::TICK_INTERVAL_MS + 1000)
            {
                ledger.fail(window, "the outage was recorded as started off the tick that saw it",
                            (long long)outage.startedAtMs - (long long)window.startMs, (long long)LinkSupervisor::TICK_INTERVAL_MS);
            }
            if ((uint64_t)outage.startedAtMs + outage.durationMs < window.endMs)
            {
                ledger.fail(window, "the outage was recorded as over before the window closed",
                            (long long)outage.startedAtMs + (long long)outage.durationMs, (long long)window.endMs);
            }
        }

        // 5. The diagnostics reach the server again.
        if (server.diagnosticsDelivered() <= start.diagnosticsAtEnd)
        {
            ledger.fail(window, "no diagnostic reached the server after the return", 0, 1);
        }
    }

    /** The plan, on a task of the harness: it opens and closes every window and judges every return. */
    void startLinkWorldTask()
    {
        scheduler.createTask("link-world", 64 * 1024, 1, [this]() {
            for (uint64_t index = 0;; index++)
            {
                const FaultWindow window = faultWindow(index);
                sleepUntil(window.startMs);
                WindowStart start = openTheWindow(window);
                sleepUntil(window.endMs);
                closeTheWindow(window, start);
                sleepUntil(window.endMs + returnAllowanceMs(window));
                judge(window, start);
            }
        });
    }
};

// --- what one hour of the run looks like from outside ---------------------------------------

struct HourSample {
    uint64_t atMs = 0;

    /** Heap allocations made by the whole binary during this hour. */
    uint64_t allocations = 0;

    /**
     * How many counted allocations the process still holds at the end of this hour.
     *
     * The hourly traffic above cannot see a leak whose rate is steady - a container that grows
     * by one element per iteration allocates almost nothing extra per hour once it has doubled
     * its way up - so a bench that only measured traffic would call an unbounded leak healthy.
     * This is the balance: every operator new ever made minus every operator delete.
     */
    long long liveAllocations = 0;

    /**
     * And how many bytes those allocations hold.
     *
     * The count alone is not enough: a container that doubles its way up to megabytes is a
     * single live allocation, so the classic runaway - one vector that takes an element every
     * iteration - is invisible to a count and obvious here.
     */
    long long liveBytes = 0;

    /** Worst control-loop step observed so far, and the moment it started. */
    uint64_t worstStepMs = 0;
    uint64_t worstStepAtMs = 0;

    /** Depths: what is waiting somewhere. These are what must not grow. */
    size_t outboxPending = 0;
    size_t completionsDepth = 0;
    size_t activeSensors = 0;
    size_t storedRules = 0;

    /** Refusals and drops: what the module threw away. These must stay at zero. */
    uint32_t outboxRefusedEvents = 0;
    uint32_t outboxLostCompletions = 0;
    uint32_t completionsOverflow = 0;
    uint32_t acqRefusedBus = 0;
    uint32_t acqTaskFail = 0;
    uint64_t acqDroppedReadings = 0;
    uint32_t cmdDropSignal = 0;
    uint32_t cmdDropReport = 0;
    uint32_t cmdDropBatch = 0;
    uint32_t cmdRefuseBus = 0;
    uint32_t netRefused = 0;
    uint32_t netTaskFail = 0;

    uint32_t outboxTaskFail = 0;

    // Deltas, not absolutes: both counters are process-global and the suite runs in one process,
    // so earlier files (the counters line, the memory columns) have deliberately cranked them
    // before this run starts.
    //
    // Kept out of the sum of refusals above, and reported rather than asserted, because on the
    // host neither can move: alloc_fails is written only by the ESP heap hook and by the TLS
    // pool, and nothing in this run enters the TLS pool at all. A zero here is the shape of the
    // seam, not a result, and listing it beside counters that could have moved would inflate
    // what the run proves.
    uint32_t allocFailsInRun = 0;
    uint32_t tlsFallbacksInRun = 0;

    /** Cumulative, and expected to move: what the module got done. */
    uint32_t outboxDelivered = 0;
    uint32_t outboxUndelivered = 0;
    uint64_t diagLines = 0;
    uint64_t loopIterations = 0;
    uint64_t sweeps = 0;
    /** Ports a step or a command left dark that the sweep gave their power back, so far. */
    uint64_t darkPortsPowered = 0;
    uint64_t extStarved = 0;

    bool watchdogFired = false;
    bool gateLatched = false;
    bool budgetExhausted = false;

    /** Where the module stands at the hour's end, which no window of the plan reaches. */
    bool steady = false;
    bool gatesOpen = false;
    bool troubleshootingOk = false;
    uint64_t diagnosticsDelivered = 0;
    uint32_t failedVerdicts = 0;
};

HourSample takeSample(EnduranceModule& module, uint64_t allocations, bool budgetExhausted,
                      uint32_t allocFailsAtStart, uint32_t tlsFallbacksAtStart)
{
    HourSample sample;
    sample.atMs = module.clock.nowMs();
    sample.allocations = allocations;
    sample.liveAllocations = HostAllocationCounter::liveCount();
    sample.liveBytes = HostAllocationCounter::liveBytes();

    sample.worstStepMs = module.cadence.worstGapMs();
    sample.worstStepAtMs = module.cadence.worstGapStartedAtMs();

    sample.outboxPending = module.outbox.pendingCount();
    sample.completionsDepth = module.completions.size();
    sample.activeSensors = module.sensorRepository.getActiveSensors().size();
    sample.storedRules = module.ruleDao.getStorage().size();

    sample.outboxRefusedEvents = module.outbox.refusedEventCount();
    sample.outboxLostCompletions = module.outbox.lostCompletionCount();
    sample.completionsOverflow = module.completions.overflowCount();
    sample.acqRefusedBus = module.acquisition.refusedBusCommandCount();
    sample.acqTaskFail = module.acquisition.taskStartFailureCount();
    sample.acqDroppedReadings = module.acquisition.droppedReadingCount();
    sample.cmdDropSignal = module.commandService.droppedSignalCount();
    sample.cmdDropReport = module.commandService.droppedReportCount();
    sample.cmdDropBatch = module.commandService.droppedBatchCount();
    sample.cmdRefuseBus = module.commandService.refusedBusCommandCount();
    sample.netRefused = module.networkRunner.refusedCount();
    sample.netTaskFail = module.networkRunner.taskStartFailureCount();
    sample.outboxTaskFail = module.outbox.taskStartFailureCount();
    sample.allocFailsInRun = AllocationFailureCounter::count() - allocFailsAtStart;
    sample.tlsFallbacksInRun = TlsMemoryPool::heapFallbacks() - tlsFallbacksAtStart;

    sample.outboxDelivered = module.outbox.deliveredCount();
    sample.outboxUndelivered = module.outbox.undeliveredCount();
    sample.diagLines = module.logger.countersLines();
    sample.loopIterations = module.cadence.count();
    sample.sweeps = module.acquisition.sweepCount();
    sample.darkPortsPowered = module.acquisition.darkPortPoweredCount();
    sample.extStarved = module.extStarvedCount;

    sample.watchdogFired = module.taskWatchdog.fired();
    sample.gateLatched = module.loopGate.latched();
    sample.budgetExhausted = budgetExhausted;

    sample.steady = module.bootSequencer.phase() == BootSequencer::Phase::STEADY;
    sample.gatesOpen = module.allGatesAreOpen();
    sample.troubleshootingOk = module.linkSupervisor.troublePhase() == LinkTroubleshooting::Phase::OK;
    sample.diagnosticsDelivered = module.server.diagnosticsDelivered();
    sample.failedVerdicts = module.ledger.failureCount;

    return sample;
}

/**
 * @brief What the world does to the module during hour `hour`.
 *
 * A pure function of the hour index, so the run is reproducible from its constants: the
 * greenhouse warms and cools on a six-hour swing, one sensor drops off the line and comes back,
 * a bus command arrives once a day, and two firmware downloads are started - one over a link
 * that dies mid-transfer, one that completes. Nothing here is random.
 */
void driveTheWorld(EnduranceModule& module, uint64_t hour)
{
    // The reading of the local sensor on the healthy port, moved on the same six-hour swing as
    // the neighbour's: a bus frame that changes is what keeps the reading path honest, from the
    // register through the reader to the update the outbox sends.
    module.setHealthyReading(((hour / 6) % 2 == 0) ? 250 : 50);

    // The flapping sensor: present and answering for two hours, present and silent for two.
    module.setFlappingSilent((hour % (FLAP_PERIOD_MS / MS_PER_HOUR)) >=
                             (FLAP_PERIOD_MS / MS_PER_HOUR / 2));

    // One bus command a day, at 03:00 - a pin ping, which walks the Modbus address range on a
    // port and costs the acquisition task real virtual seconds. On the free port 3: its model
    // scans take the power of port 2, which shares the wire, so once a day the sweep finds a port
    // a command has left dark, gives it its power back and leaves it alone for its warm-up - and
    // the silent sensor's port recovery starts again from the same point every day. Pin 1 in the
    // command is port 3 of the board (ModuleCommandExecutionUseCase::convertPinFromJson()).
    if (hour % 24 == 3)
    {
        module.server.pendingModuleCommands.push_back(
            makeServerModuleCommand("ping-" + std::to_string(hour), "PingPin", "{\"pin\":1}"));
        module.mqttManager.deliver(moduleCommandJson("PingPin").c_str());
    }

    // The account changes the sensor model once, seven hours in: the type gains a humidity channel
    // next to its air temperature one. The module's five-minute sync picks it up, stores the
    // catalog, updates the three sensors bound to that type - and finds that they now expose a
    // computed VPD channel they did not have, so the structure it published is out of date and
    // goes out again. That republish is a POST that must not block the control loop.
    if (hour == CATALOG_CHANGE_HOUR)
    {
        module.server.serverSensorTypes.clear();
        module.server.serverSensorTypes.push_back(
            makeModbusSensorType("type-1", "ENDURANCE-TEMP", true, CATALOG_REVISION_2));

        // And what makes the module go and look. A synchronization that succeeded re-arms nothing,
        // so a module whose last fetch worked - which is this one - asks for the catalog on
        // exactly two occasions: at setup, and when the local IP changes. A renewed DHCP lease is
        // therefore the honest trigger, and it is what a module gets in the field.
        snprintf(module.wifiAdapter.localIP, sizeof(module.wifiAdapter.localIP), "192.168.0.%d",
                 (int)(20 + hour));
    }

    // Two firmware downloads: the first over a link that dies, the second complete. Neither
    // installs anything - the host graph has no bootloader - so what is exercised is the entry:
    // the command, the network task that carries the download, its watchdog feeds, the retry of
    // the report and the settings write on the loop. Both fall outside the two whole-day
    // windows the heap-traffic comparison uses, so that it compares ordinary days with
    // ordinary days.
    if (hour == FIRST_DOWNLOAD_HOUR || hour == SECOND_DOWNLOAD_HOUR)
    {
        module.firmwareUpdater.updateResult = (hour > 8 * 24);
        module.firmwareUpdater.downloadMs = 120000;
        module.firmwareUpdater.progressEveryMs = 25000;
        module.firmwareUpdater.progressFeedTaskNames.clear();
        module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
            "fw-" + std::to_string(hour), "UpdateFirmware",
            "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\","
            "\"version\":\"9.9.9\"}"));
        module.mqttManager.deliver(moduleCommandJson("UpdateFirmware").c_str());
    }
}

// --- the census: which subsystem holds the RAM, and what it peaks at ------------------------
//
// Same module, same world, a shorter span: what changes is that every allocation is charged to an
// owner. The span is a simulated day plus two hours - long enough to contain the whole daily
// cycle (four server outages, the daily bus command, the catalog change and the republish it
// causes, a firmware download) and short enough to run twice inside one test, which is what makes
// the numbers checkable rather than merely printed.

/** A day plus two hours: the daily cycle, whole, with the hours around it. */
const size_t CENSUS_HOURS = 26;

/** The firmware download this run drives - the fortnight's own two fall far outside 26 hours. */
const size_t CENSUS_DOWNLOAD_HOUR = 20;

/**
 * One owner's row of the table: what it settles at, and the most it ever needed on top of that.
 *
 * The peak is measured per simulated hour and rearmed at the top of each one, so what it reports
 * is a transient - how much this owner had to have free at once, above the level it was already
 * holding when the hour began - and not the slope of a module still filling its caches. That is
 * the number a heap runs out of.
 */
struct CensusRow {
    long long liveBytes = 0;
    long long liveBlocks = 0;

    /**
     * How much of `liveBytes` this owner already held before the run started.
     *
     * Building the graph is not a preamble to the measurement: this firmware reserves its
     * bounded queues to their cap in constructors, and on the board those reservations come out
     * of the same heap at static-construction time, before the control loop exists. A census
     * that charged them to the harness would answer a third less than the module holds and would
     * hide the largest identifiable item of the boot from the very question it was asked.
     */
    long long constructionBytes = 0;

    /**
     * The first simulated hour, kept apart from the rest.
     *
     * Nothing about it is steady state: the module is filling every cache it will ever hold, so
     * the peak of that hour is boot cost and growth together, and averaging it into the rest
     * would hide both. It is here because the board's largest unattributed transient is a boot
     * one, and this is the row that can be compared with it.
     */
    long long bootTransient = 0;

    /** The worst hour after the first, which is the one a running module has to survive. */
    long long steadyTransient = 0;
    uint64_t transientAtMs = 0;
    const char* transientLabel = "";
};

struct CensusResult {
    CensusRow rows[census::kOwnerCount];
    long long moduleLiveBytes = 0;
    /** What the module's own objects cost to build, before a single loop iteration ran. */
    long long moduleConstructionBytes = 0;
    /** The same for the harness: stand-ins, fiber stacks, the fixture's own data. */
    long long benchConstructionBytes = 0;
    /** How many distinct module owners paid for part of the construction. */
    int constructionOwnersCharged = 0;
    long long moduleBootTransient = 0;
    long long moduleSteadyTransient = 0;
    uint64_t moduleTransientAtMs = 0;
    const char* moduleTransientLabel = "";
    uint64_t unpairedReleases = 0;
    uint64_t overflowedPushes = 0;

    // What the run actually did. Read before the table is believed: a census of a module that sat
    // still would be a table of zeroes and would pass every claim below.
    uint64_t diagLines = 0;
    uint64_t loopIterations = 0;
    uint64_t sweeps = 0;
    uint32_t delivered = 0;
    uint64_t catalogFetchesOk = 0;
    uint64_t structuresWithComputedChannel = 0;
    bool firmwareDownloadRan = false;
    uint64_t mqttDelivered = 0;

    /**
     * The JSON document pool, which no row of the table above can show.
     *
     * ArduinoJson documents come from JsonMemoryPool - static slots and malloc - and this meter
     * replaces operator new, so a document is invisible to it whether it was served from a slot
     * or fell back to the heap. The pool counts both itself, and the numbers matter to anyone
     * reading the table: a fallback is a heap block the size of a document (up to 16 KB) that
     * appears in no owner's row here and in no column of the device's diagnostic line either.
     */
    uint32_t jsonPooledServed = 0;
    uint32_t jsonHeapBypasses = 0;
    uint32_t jsonHeapFallbacks = 0;
    int jsonPeakSlotsInUse = 0;
};

/** What the world does in this hour, in the words the peak of that hour will be stamped with. */
const char* censusMomentLabel(size_t hour)
{
    if (hour == CATALOG_CHANGE_HOUR) return "the catalog changes and the local IP with it";
    if (hour == CENSUS_DOWNLOAD_HOUR) return "a firmware download";
    if (hour % 24 == 3) return "the daily bus command";
    for (size_t i = 0; i < PLAN_SIZE; i++)
    {
        if (DAILY_PLAN[i].hour == hour % 24) return "an hour with a fault of the link";
    }
    return "an ordinary hour";
}

/** The one event this run adds to driveTheWorld(): the fortnight's downloads are days away. */
void driveTheCensusExtras(EnduranceModule& module, size_t hour)
{
    if (hour != CENSUS_DOWNLOAD_HOUR) return;

    module.firmwareUpdater.updateResult = true;
    module.firmwareUpdater.downloadMs = 120000;
    module.firmwareUpdater.progressEveryMs = 25000;
    module.firmwareUpdater.progressFeedTaskNames.clear();
    module.server.pendingModuleCommands.push_back(makeServerModuleCommand(
        "fw-census", "UpdateFirmware",
        "{\"url\":\"https://api-test.growdirector.com/api/v1/Firmware/abc\","
        "\"version\":\"9.9.9\"}"));
    module.mqttManager.deliver(moduleCommandJson("UpdateFirmware").c_str());
}

/**
 * @brief One census run: build the module, live a simulated day and two hours, read the table.
 *
 * The module is built on the heap and inside a bench scope on purpose. The fixture is the
 * harness seeding registries, the line and the devices - work a real module does in its setup
 * service, from flash, and not on any of these owners - so it belongs in the harness row and not
 * in the module's total. What the module's own init() paths allocate is charged to them, because
 * the relays are already in place when wireTheGraph() calls them.
 *
 * The moment is advanced once a simulated minute, so a peak is located to the minute and labelled
 * with what the world was doing that hour.
 */
CensusResult runCensus()
{
    // The pool is process-wide and the files before this one have used it; only the movement
    // during this run says anything.
    const uint32_t jsonServedAtStart = JsonMemoryPool::instance().pooledServed();
    const uint32_t jsonBypassesAtStart = JsonMemoryPool::instance().heapBypasses();
    const uint32_t jsonFallbacksAtStart = JsonMemoryPool::instance().heapFallbacks();
    JsonMemoryPool::instance().resetPeakSlotsInUse();

    census::begin();
    census::setMoment(0, "the fixture is built");

    CensusResult result;

    EnduranceModule* module = nullptr;
    {
        // The scope names the default, not the whole: the phase markers inside the fixture's
        // member list re-tag it as the graph is built, so a member that is production code is
        // charged to its subsystem and only the stand-ins stay here.
        census::Scope harness(census::kBench);
        module = new EnduranceModule();
        module->startArduinoLoopTask();
        module->startExternalKickerTask();
        module->startRemoteSensorTask();
        module->startLinkWorldTask();
    }

    // Read before anything runs: this is the graph itself, by owner.
    for (int owner = 0; owner < (int)census::kOwnerCount; owner++)
    {
        const census::Owner which = (census::Owner)owner;
        result.rows[owner].constructionBytes = census::liveBytes(which);
        if (census::isModuleOwner(which) && result.rows[owner].constructionBytes > 0)
        {
            result.constructionOwnersCharged++;
        }
    }
    result.moduleConstructionBytes = census::moduleLiveBytes();
    result.benchConstructionBytes = census::liveBytes(census::kBench);

    for (size_t hour = 0; hour < CENSUS_HOURS; hour++)
    {
        {
            census::Scope harness(census::kBench);
            driveTheWorld(*module, hour);
            driveTheCensusExtras(*module, hour);
        }

        const char* label = censusMomentLabel(hour);
        census::setMoment(module->clock.nowMs(), label);

        // What every owner is holding as the hour opens. The peak of the hour is read against
        // this, so a row reports what the hour demanded and not what the module had accumulated.
        long long heldAtHourStart[census::kOwnerCount];
        for (int owner = 0; owner < (int)census::kOwnerCount; owner++)
        {
            heldAtHourStart[owner] = census::liveBytes((census::Owner)owner);
        }
        const long long moduleHeldAtHourStart = census::moduleLiveBytes();
        census::rearmPeaks();

        for (size_t minute = 0; minute < 60; minute++)
        {
            census::setMoment(module->clock.nowMs(), label);
            module->scheduler.runForMs(60000);
        }

        for (int owner = 0; owner < (int)census::kOwnerCount; owner++)
        {
            const census::Owner which = (census::Owner)owner;
            const long long transient = census::peakBytes(which) - heldAtHourStart[owner];
            if (hour == 0)
            {
                result.rows[owner].bootTransient = transient;
            }
            else if (transient > result.rows[owner].steadyTransient)
            {
                result.rows[owner].steadyTransient = transient;
                result.rows[owner].transientAtMs = census::peakAtMs(which);
                result.rows[owner].transientLabel = label;
            }
        }
        const long long moduleTransient = census::modulePeakBytes() - moduleHeldAtHourStart;
        if (hour == 0)
        {
            result.moduleBootTransient = moduleTransient;
        }
        else if (moduleTransient > result.moduleSteadyTransient)
        {
            result.moduleSteadyTransient = moduleTransient;
            result.moduleTransientAtMs = census::modulePeakAtMs();
            result.moduleTransientLabel = label;
        }

        {
            census::Scope harness(census::kBench);
            module->clearSimulatorRecorders();
        }
    }

    for (int owner = 0; owner < (int)census::kOwnerCount; owner++)
    {
        const census::Owner which = (census::Owner)owner;
        result.rows[owner].liveBytes = census::liveBytes(which);
        result.rows[owner].liveBlocks = census::liveBlocks(which);
    }
    result.moduleLiveBytes = census::moduleLiveBytes();
    result.unpairedReleases = census::unpairedReleases();
    result.overflowedPushes = census::overflowedPushes();

    result.diagLines = module->logger.countersLines();
    result.loopIterations = module->cadence.count();
    result.sweeps = module->acquisition.sweepCount();
    result.delivered = module->outbox.deliveredCount();
    result.catalogFetchesOk = module->server.sensorTypeFetchesOk;
    result.structuresWithComputedChannel = module->server.structuresWithComputedChannel();
    result.firmwareDownloadRan = module->firmwareUpdater.updateCalled;
    result.mqttDelivered = module->mqttManager.delivered();
    result.jsonPooledServed = JsonMemoryPool::instance().pooledServed() - jsonServedAtStart;
    result.jsonHeapBypasses = JsonMemoryPool::instance().heapBypasses() - jsonBypassesAtStart;
    result.jsonHeapFallbacks = JsonMemoryPool::instance().heapFallbacks() - jsonFallbacksAtStart;
    result.jsonPeakSlotsInUse = JsonMemoryPool::instance().peakSlotsInUse();

    {
        census::Scope harness(census::kBench);
        delete module;
    }
    census::end();
    return result;
}

void printCensusTable(const CensusResult& result)
{
    printf("\n  census: %u simulated hours. live = held in the steady state; boot = what the "
           "first hour needed at once; worst = the worst any later hour needed on top of what it "
           "opened with\n",
           (unsigned)CENSUS_HOURS);
    printf("  %-22s %10s %7s %9s %9s %9s %11s  %s\n", "owner", "live B", "blocks", "of which",
           "boot B", "worst B", "at ms", "during");
    printf("  %-22s %10s %7s %9s %9s %9s %11s\n", "", "", "", "built", "", "", "");
    for (int owner = 0; owner < (int)census::kOwnerCount; owner++)
    {
        const census::Owner which = (census::Owner)owner;
        const CensusRow& row = result.rows[owner];
        printf("  %-22s %10lld %7lld %9lld %9lld %9lld %11llu  %s%s\n", census::ownerName(which),
               row.liveBytes, row.liveBlocks, row.constructionBytes, row.bootTransient,
               row.steadyTransient, (unsigned long long)row.transientAtMs, row.transientLabel,
               census::isModuleOwner(which) ? "" : "   [not the module]");
    }
    printf("  %-22s %10lld %7s %9lld %9lld %9lld %11llu  %s\n", "MODULE TOTAL",
           result.moduleLiveBytes, "-", result.moduleConstructionBytes,
           result.moduleBootTransient, result.moduleSteadyTransient,
           (unsigned long long)result.moduleTransientAtMs, result.moduleTransientLabel);
    printf("  census: building the graph cost the module %lld B across %d owners; the harness's "
           "own share of it was %lld B\n",
           result.moduleConstructionBytes, result.constructionOwnersCharged,
           result.benchConstructionBytes);
    printf("  census: unpaired releases %llu, lost tags %llu\n",
           (unsigned long long)result.unpairedReleases,
           (unsigned long long)result.overflowedPushes);
    printf("  census: the run did %llu loop iterations, %llu sweeps, %llu diag lines, "
           "%u deliveries, %llu catalog fetches, %llu mqtt commands, firmware download %s\n",
           (unsigned long long)result.loopIterations, (unsigned long long)result.sweeps,
           (unsigned long long)result.diagLines, result.delivered,
           (unsigned long long)result.catalogFetchesOk, (unsigned long long)result.mqttDelivered,
           result.firmwareDownloadRan ? "ran" : "none");
    printf("  census: json pool (invisible to every row above) served=%u bypasses=%u "
           "fallbacks=%u peak slots in use=%d of 10\n",
           result.jsonPooledServed, result.jsonHeapBypasses, result.jsonHeapFallbacks,
           result.jsonPeakSlotsInUse);
}

} // namespace

// --------------------------------------------------------------------------------------------
// The run itself.
// --------------------------------------------------------------------------------------------

static void test_endurance_a_fortnight_of_the_whole_module(void)
{
    EnduranceModule module;
    module.startArduinoLoopTask();
    module.startExternalKickerTask();
    module.startRemoteSensorTask();
    module.startLinkWorldTask();

    const size_t hours = (size_t)(SIMULATED_SPAN_MS / MS_PER_HOUR);

    std::vector<HourSample> samples;
    samples.reserve(hours + 1); // reserved up front: sampling must not be what allocates

    bool budgetExhausted = false;

    // Where the two process-global counters stood before the fortnight started.
    const uint32_t allocFailsAtStart = AllocationFailureCounter::count();
    const uint32_t tlsFallbacksAtStart = TlsMemoryPool::heapFallbacks();

    for (size_t hour = 0; hour < hours; hour++)
    {
        driveTheWorld(module, hour);

        const unsigned long long before = HostAllocationCounter::newCount();
        module.scheduler.runForMs(MS_PER_HOUR);
        const unsigned long long after = HostAllocationCounter::newCount();
        module.clearSimulatorRecorders();

        budgetExhausted = budgetExhausted || module.scheduler.stepBudgetExhausted();
        samples.push_back(takeSample(module, (uint64_t)(after - before), budgetExhausted,
                                     allocFailsAtStart, tlsFallbacksAtStart));
    }

    module.cadence.closeAt(module.clock.nowMs());
    module.logger.closeCountersAt(module.clock.nowMs());

    // --- the run has to be sound before its verdict means anything --------------------------

    TEST_ASSERT_FALSE_MESSAGE(budgetExhausted,
                              "harness gave up: a task kept running without letting time advance");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)hours, (uint32_t)samples.size(),
                                     "the run did not produce one sample per simulated hour");

    // The module really ran: iterations, sweeps, deliveries and relay writes all happened, and
    // the scheduled world really happened to it.
    const HourSample& last = samples.back();
    TEST_ASSERT_TRUE_MESSAGE(last.loopIterations > hours * 30000,
                             "the control loop barely ran - the harness, not the firmware, is "
                             "what this run would be measuring");
    TEST_ASSERT_TRUE_MESSAGE(last.sweeps > hours * 100,
                             "the acquisition task did not sweep the line at its cadence");
    TEST_ASSERT_TRUE_MESSAGE(last.outboxDelivered > hours * 10,
                             "almost nothing was delivered - the outbox never really ran");
    TEST_ASSERT_TRUE_MESSAGE(module.server.outageCalls() > 0,
                             "no request ever met the scheduled outage");
    TEST_ASSERT_TRUE_MESSAGE(module.firmwareUpdater.updateCalled,
                             "no firmware download ever started");
    TEST_ASSERT_TRUE_MESSAGE(module.acquisition.busCommandCount() >= SIMULATED_DAYS,
                             "the daily bus command never reached the acquisition task");

    // And it darkened the port sharing its wire, so the sweep gave a dark port its power back and
    // waited out its warm-up - on every day the memory balance below compares, and the day before
    // each. A path that did not run there is a path whose leak the balance cannot see.
    for (size_t day = samples.size() / 24 - 4; day < samples.size() / 24; day++)
    {
        const uint64_t before = samples[day * 24 - 1].darkPortsPowered;
        const uint64_t after = samples[(day + 1) * 24 - 1].darkPortsPowered;
        char darkMessage[200];
        snprintf(darkMessage, sizeof(darkMessage),
                 "on day %lu the sweep gave no port a command had left dark its power back: the "
                 "daily ping took no stored sensor's power",
                 (unsigned long)day);
        TEST_ASSERT_TRUE_MESSAGE(after > before, darkMessage);
    }

    // The catalog really changed under the module, and the whole chain it sets off really ran:
    // the sync stored it, the sensors followed it into a computed channel they did not have, and
    // the structure went out again. Without this the loop-step verdict below would be green for
    // the one path this bench covers, simply because it never happened.
    char catalogMessage[288];
    snprintf(catalogMessage, sizeof(catalogMessage),
             "the catalog change of hour %lu never reached the server as a republished structure "
             "(%llu of %llu catalog fetches answered, %llu structures carried a computed channel): "
             "the sync found nothing to do, or nothing was bound to the type it changed",
             (unsigned long)CATALOG_CHANGE_HOUR,
             (unsigned long long)module.server.sensorTypeFetchesOk,
             (unsigned long long)module.server.sensorTypeFetches,
             (unsigned long long)module.server.structuresWithComputedChannel());
    TEST_ASSERT_TRUE_MESSAGE(module.server.structuresWithComputedChannel() >= 1, catalogMessage);

    // The three use cases the loop calls on every iteration did something, not just got called.
    // Being wired is not the same as being executed: each of them tests a precondition on its
    // first line - WiFi up, the module uid on the structure, the rule capability decided in
    // init() - and with any of those missing the loop drives them twelve million times to no
    // effect, which is what this bench looked like before these assertions existed.
    char reachedMessage[224];
    snprintf(reachedMessage, sizeof(reachedMessage),
             "the local-IP check never submitted a module structure in %lu simulated days: it "
             "returns on its first line while the WiFi adapter reports disconnected",
             (unsigned long)SIMULATED_DAYS);
    TEST_ASSERT_TRUE_MESSAGE(module.server.sendToServerModuleStructureCallCount > 0,
                             reachedMessage);

    snprintf(reachedMessage, sizeof(reachedMessage),
             "the running image was never confirmed in %lu simulated days (%d attempts, %u "
             "healthy iterations): the gate has three ways not to be reached - no module uid on "
             "the structure, not enough uptime, or an image that was not on probation",
             (unsigned long)SIMULATED_DAYS, module.otaPartitionControl.markValidCalls,
             (unsigned)module.confirmRunningFirmware.healthyIterations());
    TEST_ASSERT_TRUE_MESSAGE(module.otaPartitionControl.markValidCalls > 0, reachedMessage);
    TEST_ASSERT_TRUE_MESSAGE(module.confirmRunningFirmware.healthyIterations() > 0,
                             "the confirmation counted no healthy iterations at all");

    // The connectivity check forces a rule sync on the edge from offline back to online, and the
    // server goes out every six hours here. Counted at the branch, not at its trace: a rule-list
    // fetch more than the five-minute cadence would make is indistinguishable from the outbox
    // retrying a fetch the outage interrupted, and a premise counted at the trace stays green
    // with the use case's own capability flag forced to false.
    snprintf(reachedMessage, sizeof(reachedMessage),
             "the connectivity check forced no rule sync in %lu simulated days across %llu "
             "server outages: the offline decision it watches never went true",
             (unsigned long)SIMULATED_DAYS,
             (unsigned long long)windowsOfKindIn(Fault::SERVER_DOWN, SIMULATED_DAYS));
    TEST_ASSERT_TRUE_MESSAGE(module.syncRulesSpy.forcedCount() > 0, reachedMessage);
    // Each rule to its own pin, not "some pin was written": the sensor rule holds pump 0 on its
    // six-hour swing, the schedule rule opens pump 1 daily, the loop rule cycles pump 2 hourly.
    // A future change that quietly kills one of the three would otherwise leave this green - and
    // a rule that stops reaching its pin is exactly the failure this bench exists to catch.
    const uint8_t* pumpPins = module.moduleConfig.getPumpPins();
    const char* ruleNames[] = {"the sensor rule", "the schedule rule", "the loop rule"};
    for (int channel = 0; channel < 3; channel++)
    {
        const uint8_t pin = pumpPins[channel];
        const int writes = module.arduino.digitalWriteCounts.count(pin) > 0
                               ? module.arduino.digitalWriteCounts[pin]
                               : 0;
        char message[224];
        snprintf(message, sizeof(message),
                 "%s drove pin %d %d times in %lu simulated days: its decisions stopped short of "
                 "the GPIO write",
                 ruleNames[channel], (int)pin, writes, (unsigned long)SIMULATED_DAYS);
        TEST_ASSERT_TRUE_MESSAGE(writes >= (int)SIMULATED_DAYS, message);
    }

    // --- 1. no unexplained restarts ---------------------------------------------------------

    for (size_t i = 0; i < samples.size(); i++)
    {
        char message[224];
        snprintf(message, sizeof(message),
                 "the task watchdog fired for '%s' with a %lu ms gap, by hour %lu of the run",
                 module.taskWatchdog.firedTaskName().c_str(),
                 (unsigned long)module.taskWatchdog.firedGapMs(), (unsigned long)i);
        TEST_ASSERT_FALSE_MESSAGE(samples[i].watchdogFired, message);

        snprintf(message, sizeof(message),
                 "the external-kick gate latched by hour %lu: the loop had been silent past the "
                 "%d s stall limit and the chip would have reset the module",
                 (unsigned long)i, (int)WDT_TIMEOUT_S);
        TEST_ASSERT_FALSE_MESSAGE(samples[i].gateLatched, message);

        snprintf(message, sizeof(message),
                 "%lu kicks were withheld by hour %lu although the loop never stalled",
                 (unsigned long)samples[i].extStarved, (unsigned long)i);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)samples[i].extStarved, message);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, module.arduino.restartCount,
                                  "the module restarted itself during the run");
    TEST_ASSERT_TRUE_MESSAGE(module.extKickCount > 0, "the external kicker never ran");

    // Every task that put itself under the watchdog stayed inside the legal-pause budget the
    // threshold is computed from - over a fortnight, not over one scenario.
    const char* watchedTasks[] = {LOOP_TASK_NAME, SensorAcquisitionService::TASK_NAME,
                                  "outbox-net", NetworkCommandRunner::TASK_NAME};
    const std::vector<std::string>& subscribed = module.scheduler.watchdogSubscribedTaskNames();
    for (size_t i = 0; i < sizeof(watchedTasks) / sizeof(watchedTasks[0]); i++)
    {
        char message[224];

        // Before the budget: worstGapMs() answers 0 for a name it has never seen, so a task that
        // never started - or a name misspelled here - would otherwise sail through the budget
        // assertion below as a perfect zero. A task that never put itself under the watchdog is
        // a failure of the run, not a task with no gaps.
        bool everSubscribed = false;
        for (size_t j = 0; j < subscribed.size(); j++)
        {
            everSubscribed = everSubscribed || subscribed[j] == watchedTasks[i];
        }
        snprintf(message, sizeof(message),
                 "task '%s' never subscribed to the watchdog in %lu simulated days - either it "
                 "never started or this name does not match the one it registers",
                 watchedTasks[i], (unsigned long)SIMULATED_DAYS);
        TEST_ASSERT_TRUE_MESSAGE(everSubscribed, message);

        snprintf(message, sizeof(message),
                 "task '%s' showed a %lu ms feed gap over the fortnight - over the %lu ms budget "
                 "the watchdog threshold is computed from",
                 watchedTasks[i], (unsigned long)module.taskWatchdog.worstGapMs(watchedTasks[i]),
                 (unsigned long)LEGAL_PAUSE_BUDGET_MS);
        TEST_ASSERT_TRUE_MESSAGE(module.taskWatchdog.worstGapMs(watchedTasks[i]) <=
                                     LEGAL_PAUSE_BUDGET_MS,
                                 message);
    }

    // --- 1b. the boot sequence and the link machines, over every return ------------------------
    //
    // The plan opens nineteen windows a day - a server down, an internet down behind a router that
    // answers, a router that does not answer behind an associated station, an access point gone -
    // and every one of them is judged when its allowance is over (EnduranceModule::judge()). What
    // is asserted here is that every verdict was given and none of them failed, and then what the
    // verdicts cannot say by themselves: the whole run, and the hours between the windows.

    {
        uint64_t judgedTotal = 0;
        for (size_t kind = 1; kind < FAULT_KINDS; kind++)
        {
            judgedTotal += module.ledger.judged[kind];
            char message[160];
            snprintf(message, sizeof(message), "%lu returns from '%s' were judged in %lu simulated days; the plan opens %lu",
                     (unsigned long)module.ledger.judged[kind], faultName((Fault)kind), (unsigned long)SIMULATED_DAYS,
                     (unsigned long)windowsOfKindIn((Fault)kind, SIMULATED_DAYS));
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(windowsOfKindIn((Fault)kind, SIMULATED_DAYS), module.ledger.judged[kind], message);
        }
        char message[512];
        if (module.ledger.failureCount > 0)
        {
            const EnduranceModule::FaultLedger::Failure& first = module.ledger.failures[0];
            snprintf(message, sizeof(message),
                     "%lu of %lu returns failed; the first, window %lu (%s, day %lu): %s - %lld seen, %lld wanted",
                     (unsigned long)module.ledger.failureCount, (unsigned long)judgedTotal, (unsigned long)first.window,
                     faultName(first.kind), (unsigned long)(faultWindow(first.window).startMs / MS_PER_DAY), first.what,
                     first.seen, first.wanted);
            TEST_FAIL_MESSAGE(message);
        }
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(SIMULATED_DAYS * PLAN_SIZE, judgedTotal, "not every window of the plan was judged");

        // No restart outside a window that calls for one: every one the radio made is in the ledger
        // of a window on its side of the router, and the boot's association is the only other call.
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.wifiAdapter.unrecordedRestarts(),
                                         "the radio stand-in ran out of room for the starts of its restarts");
        snprintf(message, sizeof(message),
                 "the radio was restarted %lu times and %lu of them fall in a window that calls for it",
                 (unsigned long)module.wifiAdapter.restarts(), (unsigned long)module.ledger.restartsInTheirWindows);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(module.wifiAdapter.restarts(), module.ledger.restartsInTheirWindows, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)module.wifiAdapter.restarts() + 1, module.wifiAdapter.reconnectCallCount,
                                      "the radio was started by something that is neither a restart nor the boot's association");
        // The one restart the module asks for is the second firmware update's, into the image it
        // installed; the link machine asks for none (it would be on the tape as a fault that held).
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, module.needRestartRequests,
                                         "the module asked for a restart that is not the installed update's");
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, module.logger.tape(CountingLogger::MODULE_FAULT_HELD),
                                         "the link machine held a fault of the module");

        // The diagnostic the boot owed is the boot's alone: a return does not make the module owe one.
        TEST_ASSERT_TRUE_MESSAGE(module.logger.tape(CountingLogger::DIAGNOSTIC_OWED_SENT) <= 1,
                                 "the module owed its diagnostic again after the boot");

        // The runs of the machines were real: the sequence left STEADY and came back as often as
        // the radio's side of the plan asked, and the probe and the restarts went out.
        TEST_ASSERT_TRUE_MESSAGE(module.phaseEntries[(size_t)BootSequencer::Phase::STEADY] >
                                     windowsOfKindIn(Fault::WIFI_DROP, SIMULATED_DAYS),
                                 "the boot sequence was not back in STEADY after every drop of the access point");
        TEST_ASSERT_TRUE_MESSAGE(module.logger.tape(CountingLogger::PROBE_ASKED) >=
                                     windowsOfKindIn(Fault::SERVER_DOWN, SIMULATED_DAYS) +
                                         windowsOfKindIn(Fault::INTERNET_DOWN, SIMULATED_DAYS),
                                 "the link was not probed for the faults past the router");
    }

    // And the hours between the windows: none of them ends inside a window or its allowance, so
    // every checkpoint is a module at rest - the sequence in STEADY, its gates open, the
    // troubleshooting back to OK - and diagnostics keep reaching the server at their own cadence.
    for (size_t i = 0; i < samples.size(); i++)
    {
        char message[200];
        snprintf(message, sizeof(message),
                 "hour %lu ended with the boot sequence %s STEADY, its gates %s and the link troubleshooting %s",
                 (unsigned long)i, samples[i].steady ? "in" : "not in", samples[i].gatesOpen ? "open" : "not all open",
                 samples[i].troubleshootingOk ? "at OK" : "not at OK");
        TEST_ASSERT_TRUE_MESSAGE(samples[i].steady && samples[i].gatesOpen && samples[i].troubleshootingOk, message);

        const uint64_t deliveredThisHour =
            samples[i].diagnosticsDelivered - (i > 0 ? samples[i - 1].diagnosticsDelivered : 0);
        snprintf(message, sizeof(message),
                 "%lu diagnostics reached the server in hour %lu; one every 30 s is at most %lu, and a return leaves at least one",
                 (unsigned long)deliveredThisHour, (unsigned long)i, (unsigned long)(MS_PER_HOUR / DIAGNOSTIC_INTERVAL_MS + 2));
        TEST_ASSERT_TRUE_MESSAGE(deliveredThisHour >= 1 && deliveredThisHour <= MS_PER_HOUR / DIAGNOSTIC_INTERVAL_MS + 2, message);
    }

    // --- 2. the loop always completed on time -----------------------------------------------

    char stepMessage[256];
    snprintf(stepMessage, sizeof(stepMessage),
             "worst control-loop step over %lu simulated days was %lu ms starting at t=%lu ms "
             "(limit %lu ms = %lu ms period + %lu ms slack)",
             (unsigned long)SIMULATED_DAYS, (unsigned long)module.cadence.worstGapMs(),
             (unsigned long)module.cadence.worstGapStartedAtMs(),
             (unsigned long)MAX_ACCEPTED_STEP_MS, (unsigned long)CONTROL_PERIOD_MS,
             (unsigned long)MAX_ACCEPTED_SLACK_MS);
    TEST_ASSERT_TRUE_MESSAGE(module.cadence.worstGapMs() <= MAX_ACCEPTED_STEP_MS, stepMessage);

    // The same claim from the firmware's own instrument, and it is the stricter one: the cadence
    // above measures the span between two iterations - the work plus the period the harness
    // sleeps - while max_loop_step_ms measures the iteration itself, so its budget is the slack
    // alone. This is the number a module in the field reports, and every counters line describes
    // its own diagnostic period, so the worst of them covers the fortnight and not only its end.
    TEST_ASSERT_TRUE_MESSAGE(module.logger.linesWithLoopStep() == module.logger.countersLines(),
                             "some counters lines came out without the max_loop_step_ms column");
    snprintf(stepMessage, sizeof(stepMessage),
             "the module itself reported a worst control-loop step of %lu ms in the counters line "
             "at t=%lu ms, over the %lu ms this bench allows the work of one iteration",
             (unsigned long)module.logger.worstReportedLoopStepMs(),
             (unsigned long)module.logger.worstReportedLoopStepAtMs(),
             (unsigned long)MAX_ACCEPTED_SLACK_MS);
    TEST_ASSERT_TRUE_MESSAGE(module.logger.worstReportedLoopStepMs() <= MAX_ACCEPTED_SLACK_MS,
                             stepMessage);

    // After the verdict, because these are the consequences of the change and not the measurement:
    // the republish above went through the outbox, exactly once for one catalog change, and its
    // outcome came back to the use case on the control loop.
    char republishMessage[224];
    snprintf(republishMessage, sizeof(republishMessage),
             "the structure was handed to the outbox %lu times in %lu simulated days for one "
             "catalog change (%lu delivered, %lu refused)",
             (unsigned long)module.republishStructure.submittedCount(),
             (unsigned long)SIMULATED_DAYS,
             (unsigned long)module.republishStructure.deliveredCount(),
             (unsigned long)module.republishStructure.refusedCount());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.republishStructure.submittedCount(),
                                     republishMessage);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, module.republishStructure.deliveredCount(),
                                     republishMessage);

    // --- 3. nothing grew without limit ------------------------------------------------------

    for (size_t i = 0; i < samples.size(); i++)
    {
        const HourSample& sample = samples[i];
        char message[256];

        // The outbox is drained, not merely within its cap: pendingCount() cannot exceed the
        // cap by construction (an event ring of 8 plus seven one-slot kinds), so a "<= cap"
        // assertion would be a sentence that cannot be falsified. What can be falsified is that
        // the queue comes back to empty every hour - a dispatcher that fell behind, or a job
        // that never completed, shows up here and nowhere else.
        snprintf(message, sizeof(message),
                 "hour %lu: the outbox still holds %lu jobs at the end of the hour",
                 (unsigned long)i, (unsigned long)sample.outboxPending);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)sample.outboxPending, message);

        snprintf(message, sizeof(message),
                 "hour %lu: %lu completions were still waiting to be applied by the loop",
                 (unsigned long)i, (unsigned long)sample.completionsDepth);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)sample.completionsDepth, message);

        snprintf(message, sizeof(message),
                 "hour %lu: the sensor registry holds %lu active sensors, not the 3 it was "
                 "seeded with",
                 (unsigned long)i, (unsigned long)sample.activeSensors);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, (uint32_t)sample.activeSensors, message);

        snprintf(message, sizeof(message),
                 "hour %lu: the rule registry holds %lu rules, not the 3 it was seeded with",
                 (unsigned long)i, (unsigned long)sample.storedRules);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, (uint32_t)sample.storedRules, message);

        // Refusals and drops: never silent on the device, and never at all here.
        snprintf(message, sizeof(message),
                 "hour %lu: refusals and drops - outbox_refused_events=%lu "
                 "outbox_lost_completions=%lu completions_overflow=%lu acq_refuse_bus=%lu "
                 "acq_task_fail=%lu acq_drop_reading=%lu cmd_drop_signal=%lu cmd_drop_report=%lu "
                 "cmd_drop_batch=%lu cmd_refuse_bus=%lu net_refused=%lu net_task_fail=%lu "
                 "outbox_task_fail=%lu",
                 (unsigned long)i, (unsigned long)sample.outboxRefusedEvents,
                 (unsigned long)sample.outboxLostCompletions,
                 (unsigned long)sample.completionsOverflow, (unsigned long)sample.acqRefusedBus,
                 (unsigned long)sample.acqTaskFail, (unsigned long)sample.acqDroppedReadings,
                 (unsigned long)sample.cmdDropSignal, (unsigned long)sample.cmdDropReport,
                 (unsigned long)sample.cmdDropBatch, (unsigned long)sample.cmdRefuseBus,
                 (unsigned long)sample.netRefused, (unsigned long)sample.netTaskFail,
                 (unsigned long)sample.outboxTaskFail);
        const uint64_t refusals =
            (uint64_t)sample.outboxRefusedEvents + sample.outboxLostCompletions +
            sample.completionsOverflow + sample.acqRefusedBus + sample.acqTaskFail +
            sample.acqDroppedReadings + sample.cmdDropSignal + sample.cmdDropReport +
            sample.cmdDropBatch + sample.cmdRefuseBus + sample.netRefused + sample.netTaskFail +
            sample.outboxTaskFail;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)refusals, message);
    }

    // Heap traffic per hour, reported and not asserted on.
    //
    // Requiring the busiest hour of the last three days not to cost more than the
    // busiest hour of days two to four is not a property of a healthy module, because an
    // astronomical hour of the second week does not contain the same work as the same hour of the
    // first. A port whose sensor never answers runs the recovery cycle of SensorRecoveryUseCase -
    // power off, power on, then POLL_WINDOW_MS of polling - which at one sweep every fifteen
    // seconds is seven sweeps, or 105 seconds, and 105 divides neither an hour nor a day. The
    // cycle therefore lands differently in every hour, and an hour costs a whole sweep more or
    // less than the same hour a day earlier depending on where the boundary cut the cycle.
    //
    // Measured on this run: 64 of the 72 compared hours differ from the same hour a day earlier
    // under GCC and 46 of 72 under clang, by ±27 to ±71 allocations, with no trend - the twelfth
    // day costs exactly what the first, fourth and eighth cost. Comparing the two maxima passed
    // with a margin of exactly zero on both platforms, so it was not measuring growth, it was
    // measuring which side of an hour boundary a recovery cycle happened to fall on. No window
    // and no lag makes it exact: hourly, daily and weekly windows, and windows anchored to the
    // sweep count rather than the clock, were all tried and none repeats. Normalising by work
    // does not help either - over the two windows the sweep count, the RS-485 frame count, the
    // readAvailable count and the delivery count are equal to the unit while the allocation
    // totals differ.
    //
    // What carries "nothing grows" instead is the live balance below, which is exact on both
    // platforms and strictly stronger: it compares every hour of the last three days with the
    // same hour a day earlier and requires equality to the block and to the byte.
    uint64_t earlyPeak = 0;
    uint64_t latePeak = 0;
    for (size_t i = 24; i < 4 * 24; i++)
    {
        earlyPeak = (samples[i].allocations > earlyPeak) ? samples[i].allocations : earlyPeak;
    }
    for (size_t i = samples.size() - 3 * 24; i < samples.size(); i++)
    {
        latePeak = (samples[i].allocations > latePeak) ? samples[i].allocations : latePeak;
    }

    // The one thing the traffic figure still has to do is prove the meter was reading at all:
    // a dead counter would report zero traffic and zero growth, and every allocation claim in
    // this file would go quietly green.
    TEST_ASSERT_TRUE_MESSAGE(earlyPeak > 0,
                             "no allocation at all in a whole day - the meter is not reading");

    // Traffic is a rate, and a rate says nothing about what is kept: a container that takes one
    // more element every iteration allocates almost nothing extra per hour once it has doubled
    // its way up, so a bench that only watched traffic would call an unbounded leak healthy.
    // What is kept is the live balance - every counted allocation made minus every one released.
    //
    // The claim is exact rather than approximate, and that is what the run turned out to
    // support: by the second week each hour holds precisely as many allocations as the same
    // hour of the previous day. Comparing an hour with the same hour a day earlier is what
    // makes it exact - every periodic event of the module (1 s, 15 s, 30 s, 5 min, hourly,
    // six-hourly, daily) has the same phase in both - and it stays true across a one-off that
    // legitimately retains memory, such as the first firmware download, because that shifts
    // both sides of every later comparison equally. One cycle of the module does not divide a
    // day - the silent sensor's port recovery, about 105 s - and it has the same phase in both
    // because the daily ping takes that port's power and the recovery starts again from it.
    for (size_t i = samples.size() - 3 * 24; i < samples.size(); i++)
    {
        const long long todayCount = samples[i].liveAllocations;
        const long long yesterdayCount = samples[i - 24].liveAllocations;
        const long long todayBytes = samples[i].liveBytes;
        const long long yesterdayBytes = samples[i - 24].liveBytes;

        char liveMessage[256];
        snprintf(liveMessage, sizeof(liveMessage),
                 "hour %lu holds %lld live allocations against %lld at the same hour a day "
                 "earlier: %+lld blocks a day is memory the module never gives back",
                 (unsigned long)i, todayCount, yesterdayCount, todayCount - yesterdayCount);
        TEST_ASSERT_TRUE_MESSAGE(todayCount == yesterdayCount, liveMessage);

        snprintf(liveMessage, sizeof(liveMessage),
                 "hour %lu holds %lld live bytes against %lld at the same hour a day earlier: "
                 "%+lld bytes a day, or %+lld over a fortnight",
                 (unsigned long)i, todayBytes, yesterdayBytes, todayBytes - yesterdayBytes,
                 (todayBytes - yesterdayBytes) * (long long)SIMULATED_DAYS);
        TEST_ASSERT_TRUE_MESSAGE(todayBytes == yesterdayBytes, liveMessage);
    }

    // Day against day is exact, and blind in one direction: a container that doubles its way up
    // holds the same bytes on both sides of any comparison that contains no doubling, and the
    // doublings of a runaway get exponentially rarer. So the run is also accounted end to end.
    //
    // The books have to balance: everything the module holds at the end that it did not hold
    // once settled must be explained by the events allowed to retain memory. Here that is the
    // two firmware updates - a command, its report, a log entry - and each one's retention is
    // measured, not assumed, by reading the balance an hour either side of it. Anything left
    // over is memory that accumulated with nothing to show for it.
    //
    // Both ends of the account are taken a whole number of days apart, and that matters: the
    // balance swings by a couple of blocks with the six-hour sensor cycle, so two checkpoints at
    // different phases of that cycle differ by the swing and not by anything kept. Day-aligned
    // checkpoints are at the same phase of every cycle the module has.
    const size_t budgetFrom = 24;                  // the end of the first settled day
    const size_t budgetTo = samples.size() - 24;   // a whole number of days later
    const long long settledCount = samples[budgetFrom].liveAllocations;
    const long long settledBytes = samples[budgetFrom].liveBytes;
    long long allowedCount = 0;
    long long allowedBytes = 0;
    const size_t downloadHours[] = {FIRST_DOWNLOAD_HOUR, SECOND_DOWNLOAD_HOUR};
    for (size_t i = 0; i < sizeof(downloadHours) / sizeof(downloadHours[0]); i++)
    {
        const size_t before = downloadHours[i] - RETENTION_WINDOW_HOURS;
        const size_t after = downloadHours[i] + RETENTION_WINDOW_HOURS;
        allowedCount += samples[after].liveAllocations - samples[before].liveAllocations;
        allowedBytes += samples[after].liveBytes - samples[before].liveBytes;
    }

    // The allowance is measured either side of each update, which means whatever those hours
    // retain is subtracted - including, in principle, an update that started retaining megabytes.
    // The second update is what closes that: the first one legitimately keeps state a module
    // keeps once (the boot record, the persisted update status), and the second must add nothing
    // to it. An update that leaked would show here, where nothing is self-calibrating.
    const size_t secondBefore = SECOND_DOWNLOAD_HOUR - RETENTION_WINDOW_HOURS;
    const size_t secondAfter = SECOND_DOWNLOAD_HOUR + RETENTION_WINDOW_HOURS;
    const long long secondRetainedCount =
        samples[secondAfter].liveAllocations - samples[secondBefore].liveAllocations;
    const long long secondRetainedBytes =
        samples[secondAfter].liveBytes - samples[secondBefore].liveBytes;
    char secondMessage[256];
    snprintf(secondMessage, sizeof(secondMessage),
             "the second firmware update kept %+lld blocks and %+lld bytes; the first one has "
             "already paid for everything a module keeps once, so the second must keep nothing",
             secondRetainedCount, secondRetainedBytes);
    TEST_ASSERT_TRUE_MESSAGE(secondRetainedCount == 0 && secondRetainedBytes == 0, secondMessage);

    const long long grewCount = samples[budgetTo].liveAllocations - settledCount;
    const long long grewBytes = samples[budgetTo].liveBytes - settledBytes;

    char budgetMessage[320];
    snprintf(budgetMessage, sizeof(budgetMessage),
             "between hour %lu and hour %lu the module ended up holding %+lld blocks and "
             "%+lld bytes more than before; the two firmware updates account for %+lld blocks "
             "and %+lld bytes of that, leaving %+lld blocks and %+lld bytes unexplained",
             (unsigned long)budgetFrom, (unsigned long)budgetTo, grewCount, grewBytes,
             allowedCount, allowedBytes, grewCount - allowedCount, grewBytes - allowedBytes);
    TEST_ASSERT_TRUE_MESSAGE(grewBytes == allowedBytes, budgetMessage);
    TEST_ASSERT_TRUE_MESSAGE(grewCount == allowedCount, budgetMessage);

    // --- 4. the DIAG-COUNTERS line kept its cadence ------------------------------------------

    const uint64_t worstDiagPeriodMs = (uint64_t)DIAGNOSTIC_INTERVAL_MS + MAX_ACCEPTED_STEP_MS;

    char diagMessage[256];
    snprintf(diagMessage, sizeof(diagMessage),
             "the counters line went missing for %lu ms starting at t=%lu ms; its period is "
             "%lu ms and one worst loop step is %lu ms",
             (unsigned long)module.logger.worstCountersGapMs(),
             (unsigned long)module.logger.worstCountersGapStartedAtMs(),
             (unsigned long)DIAGNOSTIC_INTERVAL_MS, (unsigned long)MAX_ACCEPTED_STEP_MS);
    TEST_ASSERT_TRUE_MESSAGE(module.logger.worstCountersGapMs() <= worstDiagPeriodMs, diagMessage);

    const uint64_t minimumLines = SIMULATED_SPAN_MS / worstDiagPeriodMs - 1;
    snprintf(diagMessage, sizeof(diagMessage),
             "%lu counters lines came out over %lu simulated days; at one per %lu ms there had "
             "to be at least %lu",
             (unsigned long)module.logger.countersLines(), (unsigned long)SIMULATED_DAYS,
             (unsigned long)worstDiagPeriodMs, (unsigned long)minimumLines);
    TEST_ASSERT_TRUE_MESSAGE(module.logger.countersLines() >= minimumLines, diagMessage);

    // And what it carries is still the whole line: the watermark columns the host cannot measure
    // are reported as unknown to the last hour, never as a zero that would read like an alarm.
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(module.logger.lastCountersLine(), "alloc_fails="),
                                 "the last counters line lost its memory columns");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(module.logger.lastCountersLine(), "stack_outbox_net=n/a"),
                                 "a stack watermark the host cannot measure was reported as a "
                                 "number instead of as unknown");

    // --- 5. the steady-state allocation guards still hold after a fortnight -------------------
    //
    // The same two measurements the loop and the rule engine are held to on a fresh fixture,
    // re-taken on a module that has been running for two simulated weeks. A cache that leaked a
    // little on every invalidation, or a snapshot that stopped settling once some counter grew
    // past a threshold, shows up here and nowhere else.

    // Twenty stretches of fifty idle iterations, 10 ms apart - ten simulated seconds in total.
    // At most one sensor tick (15 s) and one device tick (30 s) can fall in that span, so at
    // most two of the twenty stretches may be anything other than empty.
    const int STRETCHES = 20;
    const int ITERATIONS_PER_STRETCH = 50;
    const uint32_t IDLE_STEP_MS = 10;

    for (int i = 0; i < 20; i++)
    {
        module.sensorService.loop();
        module.deviceService.loop();
        module.clock.advanceMs(IDLE_STEP_MS);
    }

    int emptyStretches = 0;
    uint64_t busiestStretch = 0;
    for (int stretch = 0; stretch < STRETCHES; stretch++)
    {
        HostAllocationCounter::reset();
        for (int i = 0; i < ITERATIONS_PER_STRETCH; i++)
        {
            module.sensorService.loop();
            module.deviceService.loop();
            module.clock.advanceMs(IDLE_STEP_MS);
        }
        const unsigned long long allocations = HostAllocationCounter::newCount();
        if (allocations == 0)
        {
            emptyStretches++;
        }
        busiestStretch = (allocations > busiestStretch) ? allocations : busiestStretch;
    }

    char idleMessage[256];
    snprintf(idleMessage, sizeof(idleMessage),
             "after %lu simulated days only %d of %d idle stretches allocated nothing (busiest "
             "made %lu allocations); at most two may carry a periodic tick",
             (unsigned long)SIMULATED_DAYS, emptyStretches, STRETCHES,
             (unsigned long)busiestStretch);
    TEST_ASSERT_TRUE_MESSAGE(emptyStretches >= STRETCHES - 2, idleMessage);

    // And the rule engine's steady tick, the second guard: the same twenty-second look, one
    // evaluation per simulated second.
    int emptyTicks = 0;
    uint64_t busiestTick = 0;
    for (int tick = 0; tick < 20; tick++)
    {
        module.clock.advanceMs(1000);
        HostAllocationCounter::reset();
        module.evaluateRules.loop();
        const unsigned long long allocations = HostAllocationCounter::newCount();
        if (allocations == 0)
        {
            emptyTicks++;
        }
        busiestTick = (allocations > busiestTick) ? allocations : busiestTick;
    }

    char tickMessage[256];
    snprintf(tickMessage, sizeof(tickMessage),
             "after %lu simulated days only %d of 20 steady rule ticks allocated nothing "
             "(busiest made %lu allocations)",
             (unsigned long)SIMULATED_DAYS, emptyTicks, (unsigned long)busiestTick);
    TEST_ASSERT_TRUE_MESSAGE(emptyTicks >= 18, tickMessage);

    // --- what the run cost, for the record ---------------------------------------------------

    printf("\n  endurance: %lu simulated days, %llu loop iterations, %llu sweeps, "
           "%llu counters lines\n",
           (unsigned long)SIMULATED_DAYS, (unsigned long long)module.cadence.count(),
           (unsigned long long)module.acquisition.sweepCount(),
           (unsigned long long)module.logger.countersLines());
    printf("  endurance: worst loop step %llu ms (module reported %llu ms at t=%llu ms), "
           "worst feed gap %llu ms, delivered %lu / undelivered %lu\n",
           (unsigned long long)module.cadence.worstGapMs(),
           (unsigned long long)module.logger.worstReportedLoopStepMs(),
           (unsigned long long)module.logger.worstReportedLoopStepAtMs(),
           (unsigned long long)module.taskWatchdog.worstGapMsOverall(),
           (unsigned long)module.outbox.deliveredCount(),
           (unsigned long)module.outbox.undeliveredCount());
    // Reported, not asserted on - see the note above the measurement. Worth having when a change
    // moves it by thousands rather than by the tens the recovery cycle accounts for.
    printf("  endurance: hourly heap traffic peak %llu (days 2-4) vs %llu (days 12-14), "
           "not compared\n",
           (unsigned long long)earlyPeak, (unsigned long long)latePeak);
    printf("  endurance: live allocations %lld -> %lld, live bytes %lld -> %lld "
           "(hour 24 to the last hour)\n",
           (long long)samples[24].liveAllocations, (long long)samples.back().liveAllocations,
           (long long)samples[24].liveBytes, (long long)samples.back().liveBytes);
    printf("  endurance: relay writes");
    for (std::map<uint8_t, int>::const_iterator it = module.arduino.digitalWriteCounts.begin();
         it != module.arduino.digitalWriteCounts.end(); ++it)
    {
        printf(" pin%d=%d", (int)it->first, it->second);
    }
    printf(", bus commands %llu, firmware downloads %s\n",
           (unsigned long long)module.acquisition.busCommandCount(),
           module.firmwareUpdater.updateCalled ? "ran" : "none");

    uint64_t returnsToSteady = module.ledger.returnsToSteady;
    printf("  endurance: %llu returns judged, %llu of them ended in STEADY with every gate open, none failed\n",
           (unsigned long long)(SIMULATED_DAYS * PLAN_SIZE), (unsigned long long)returnsToSteady);
    for (size_t kind = 1; kind < FAULT_KINDS; kind++)
    {
        printf("  endurance: %-14s %3lu returns, %4lu radio restarts, %3lu structures, %3lu outages recorded\n",
               faultName((Fault)kind), (unsigned long)module.ledger.judged[kind],
               (unsigned long)module.ledger.radioRestarts[kind], (unsigned long)module.ledger.structures[kind],
               (unsigned long)module.ledger.outagesRecorded[kind]);
    }
    printf("  endurance: boot sequence entered LINK %llu, NET_STRUCTURE %llu, NET_CATALOG %llu, STEADY %llu times; "
           "%llu probes, %llu radio restarts asked, %llu not run, %llu run, module restarts %llu\n",
           (unsigned long long)module.phaseEntries[(size_t)BootSequencer::Phase::LINK],
           (unsigned long long)module.phaseEntries[(size_t)BootSequencer::Phase::NET_STRUCTURE],
           (unsigned long long)module.phaseEntries[(size_t)BootSequencer::Phase::NET_CATALOG],
           (unsigned long long)module.phaseEntries[(size_t)BootSequencer::Phase::STEADY],
           (unsigned long long)module.logger.tape(CountingLogger::PROBE_ASKED),
           (unsigned long long)module.logger.tape(CountingLogger::RESTART_ASKED),
           (unsigned long long)module.logger.tape(CountingLogger::RESTART_NOT_RUN),
           (unsigned long long)module.wifiAdapter.restarts(), (unsigned long long)module.needRestartRequests);
}

// --------------------------------------------------------------------------------------------
// The census: who holds the RAM.
// --------------------------------------------------------------------------------------------

static void test_ram_census_of_a_simulated_day_by_owner(void)
{
    const CensusResult first = runCensus();
    const CensusResult second = runCensus();

    printCensusTable(first);

    // --- the run has to have done something before its table means anything ------------------

    TEST_ASSERT_TRUE_MESSAGE(first.loopIterations > 20000,
                             "the control loop barely ran: a census of a module standing still");
    TEST_ASSERT_TRUE_MESSAGE(first.sweeps > 1000, "the line was never swept in the census run");
    TEST_ASSERT_TRUE_MESSAGE(first.diagLines > 2000, "the diagnostics never reported");
    TEST_ASSERT_TRUE_MESSAGE(first.delivered > 100, "nothing was ever delivered to the server");
    TEST_ASSERT_TRUE_MESSAGE(first.catalogFetchesOk > 0,
                             "the catalog was never fetched, so its row prices nothing");
    TEST_ASSERT_TRUE_MESSAGE(first.structuresWithComputedChannel > 0,
                             "the catalog change never reached a republished structure");
    TEST_ASSERT_TRUE_MESSAGE(first.firmwareDownloadRan,
                             "no firmware download ran, so the ota row prices nothing");
    TEST_ASSERT_TRUE_MESSAGE(first.mqttDelivered > 0,
                             "no MQTT message was handled, so the mqtt row prices nothing");
    // The converter is the one place where a substitution would not show up as a missing feature:
    // anything implementing IJsonConverter lets the graph build and the scenario pass. What says
    // the production one ran is the pool - only ArduinoJson documents come out of it - so a run
    // whose pool served nothing converted with something the board does not have, and every JSON
    // number in the table below would be that other thing's.
    TEST_ASSERT_TRUE_MESSAGE(first.jsonPooledServed > 0,
                             "the JSON pool served nothing in a whole simulated day: the bench is "
                             "converting with something other than the converter globals.h builds");

    // --- the attribution has to be sound ------------------------------------------------------

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        0, first.overflowedPushes,
        "the tag stack ran out of room: some allocations were charged to the wrong owner");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        0, first.unpairedReleases,
        "the first census saw releases of blocks it never handed out");

    char message[288];

    // --- 1. building the graph is charged to the module, not to the harness --------------------
    //
    // The bounded queues this firmware reserves to their cap live in constructors, and on the
    // board they come out of the heap at static construction, before the loop exists. A census
    // that filed them under the harness would answer a third less than the module holds - and
    // would hide the largest identifiable item of the boot from a table written to explain the
    // boot. Two claims, because two ways of getting it wrong: charging the lot to the harness,
    // and charging the lot to one lump owner.

    snprintf(message, sizeof(message),
             "building the graph charged the module %lld bytes: the constructors of src/ - the "
             "reserved completion rings above all - are being filed under the harness",
             first.moduleConstructionBytes);
    TEST_ASSERT_TRUE_MESSAGE(first.moduleConstructionBytes > 0, message);

    snprintf(message, sizeof(message),
             "the construction was charged to %d module owners: a single lump owner says the "
             "graph costs something but not who pays",
             first.constructionOwnersCharged);
    TEST_ASSERT_TRUE_MESSAGE(first.constructionOwnersCharged >= 4, message);

    // --- 2. the remainder is small: the census names the owners that matter --------------------

    const long long other = first.rows[census::kOther].liveBytes;
    const long long moduleTotal = first.moduleLiveBytes;
    TEST_ASSERT_TRUE_MESSAGE(moduleTotal > 0, "the module holds nothing at all - nothing to split");
    // Basis points rather than percent: at four tenths of a percent an integer percentage prints
    // "0 %" and says nothing about how much room is left before the threshold.
    snprintf(message, sizeof(message),
             "unattributed bytes are %lld of %lld held by the module (%lld.%02lld%%): the owners "
             "are not enough to account for what the module holds",
             other, moduleTotal, (other * 10000 / moduleTotal) / 100,
             (other * 10000 / moduleTotal) % 100);
    TEST_ASSERT_TRUE_MESSAGE(other * 10 < moduleTotal, message);

    // --- 3. the same day costs the same twice --------------------------------------------------
    //
    // Two runs of the same scenario in one process, each from a module built from scratch. If a
    // row differed between them the table would be a measurement of the process, not of the
    // module, and none of the numbers in the report could be quoted.

    for (int owner = 0; owner < (int)census::kOwnerCount; owner++)
    {
        const census::Owner which = (census::Owner)owner;
        snprintf(message, sizeof(message),
                 "owner %s cost %lld bytes to build in the first run and %lld in the second",
                 census::ownerName(which), first.rows[owner].constructionBytes,
                 second.rows[owner].constructionBytes);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(first.rows[owner].constructionBytes,
                                        second.rows[owner].constructionBytes, message);

        snprintf(message, sizeof(message),
                 "owner %s peaked at a different moment in the two runs: %llu ms against %llu ms",
                 census::ownerName(which), (unsigned long long)first.rows[owner].transientAtMs,
                 (unsigned long long)second.rows[owner].transientAtMs);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(first.rows[owner].transientAtMs,
                                         second.rows[owner].transientAtMs, message);

        snprintf(message, sizeof(message),
                 "owner %s held %lld bytes in the first run and %lld in the second: the census is "
                 "not reproducible",
                 census::ownerName(which), first.rows[owner].liveBytes,
                 second.rows[owner].liveBytes);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(first.rows[owner].liveBytes, second.rows[owner].liveBytes,
                                        message);

        snprintf(message, sizeof(message),
                 "owner %s needed %lld bytes at once in the first run and %lld in the second",
                 census::ownerName(which), first.rows[owner].steadyTransient,
                 second.rows[owner].steadyTransient);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(first.rows[owner].steadyTransient,
                                        second.rows[owner].steadyTransient, message);

        snprintf(message, sizeof(message),
                 "owner %s needed %lld bytes in the first hour of the first run and %lld in the "
                 "second",
                 census::ownerName(which), first.rows[owner].bootTransient,
                 second.rows[owner].bootTransient);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(first.rows[owner].bootTransient,
                                        second.rows[owner].bootTransient, message);

        snprintf(message, sizeof(message), "owner %s held %lld blocks in the first run and %lld in "
                                           "the second",
                 census::ownerName(which), first.rows[owner].liveBlocks,
                 second.rows[owner].liveBlocks);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(first.rows[owner].liveBlocks, second.rows[owner].liveBlocks,
                                        message);
    }

    TEST_ASSERT_EQUAL_INT64_MESSAGE(first.moduleLiveBytes, second.moduleLiveBytes,
                                    "the module total moved between two runs of the same day");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(first.moduleBootTransient, second.moduleBootTransient,
                                    "the first hour cost differently the second time round");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(first.moduleSteadyTransient, second.moduleSteadyTransient,
                                    "the worst transient moved between two runs of the same day");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(first.moduleTransientAtMs, second.moduleTransientAtMs,
                                     "the worst transient happened at a different moment the "
                                     "second time round");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(first.moduleConstructionBytes, second.moduleConstructionBytes,
                                    "the graph cost a different amount to build the second time");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(first.benchConstructionBytes, second.benchConstructionBytes,
                                    "the harness cost a different amount to build the second time");

    // --- 4. the two absences are absences, not measurements ------------------------------------
    //
    // Both rows are zero because neither subsystem is in this graph, and saying so here keeps a
    // future reader from quoting the zero as a result. The web servers and the TLS stack name
    // device-only headers; what TLS costs is measured on the board, by the churn meter.

    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, first.rows[census::kWebServers].liveBytes,
                                    "the web servers are not in this graph, yet something was "
                                    "charged to them");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(0, first.rows[census::kTls].liveBytes,
                                    "the TLS stack is not in this graph, yet something was "
                                    "charged to it");
}

extern "C" void run_endurance_bench_tests(void)
{
    printf("\n----- Running Endurance Bench Tests -----\n");

    RUN_TEST(test_ram_census_of_a_simulated_day_by_owner);
    RUN_TEST(test_endurance_a_fortnight_of_the_whole_module);

    printf("\n----- Endurance Bench Tests Complete -----\n");
}
