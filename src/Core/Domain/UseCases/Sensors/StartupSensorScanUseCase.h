#ifndef STARTUP_SENSOR_SCAN_USE_CASE_H
#define STARTUP_SENSOR_SCAN_USE_CASE_H

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "constants.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Entities/Sensor/SensorTypeCatalog.h"
#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Enums/SensorStatusEnum.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/IRawBusAccess.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/PortPower.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/UseCases/IModbusAddressAllocator.h"
#include "Core/Domain/Interfaces/UseCases/IRepublishModuleStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/ISensorStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Boot/IBootGates.h"
#include "Core/Domain/Interfaces/UseCases/IVirtualSensorChannelsUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Sensors/IStartupSensorScan.h"

/**
 * @brief What a module finds out about its sensors after a restart, off the boot path.
 *
 * A restart answers four questions about its sensors, all of them on the
 * RS-485 line: is every entity of the registry still there, is
 * there anything new on the free ports, do two devices hold the same ModBus address, and do the
 * per-model registers still say what they should. Together they are minutes of line time, and inside `setup()`
 * they would leave the module with no loop - no telemetry, no rules, no relays, no button.
 *
 * So they are not asked in `setup()`. The answers are collected step by step by the task that
 * owns the line, between the
 * sweeps of the sensors the registry already knows, so the module is live from its first second
 * and the discovery catches up behind it.
 *
 * ### Which half runs where
 *
 * The repositories and the module structure belong to the control loop - `Repository<T>` is an
 * unordered_map cache plus a SPIFFS write-through with no lock of its own - so planning a step and
 * applying its outcome happen here, on the loop, and only `runStepOnBus()` crosses to the
 * acquisition task. That is the same division the complex module commands use.
 *
 * ### Why a step is this small
 *
 * One step is one model tried on one port, or one entity pinged, or one address written. A port
 * scanned whole would be minutes of uninterrupted line time and the sweep would wait all of it;
 * one model on one port is seconds to tens of seconds, and the sweep gets the line back between
 * two of them. The transactions inside a step are exactly the ones the boot scan made, in the same
 * order. A port's window on the restart's pass is several such steps - one per model - with its
 * wire held dark from the first of them to the last; see buildWindowStep().
 *
 * ### What a free port is asked, and in what order
 *
 * Most free ports are free: nothing is plugged into them, and the whole of what the module spends
 * on such a port is spent proving a negative. So the questions are ordered by what they cost and
 * by how likely they are to be answered.
 *
 *   1. **the cheap rung.** One request per catalog model, at that model's own factory address -
 *      and, on the restart's pass and on the periodic one, at every address of the module's range
 *      first, see below - with the reader's ordinary read budget and no retries - the same single
 *      transaction the sweep makes four times a minute on every stored sensor. A device that is plugged in and
 *      still holds the address it left the factory with answers this, and that is the common case
 *      of a person adding a sensor. An answer here is confirmed by the full model scan before
 *      anything is adopted, because the frame may have come from the port sharing this one's UART:
 *      the scan isolates, and what answers with the rest of the wire dark is on this port. That
 *      confirmation costs the other port of the wire a power cycle, so it is paid once for an
 *      address rather than at every pass - a port that was answered by its neighbour is not asked
 *      that address again while the neighbour holds it, and the neighbour is moved onto its own
 *      port's address, which frees the factory address for whatever is plugged in next. This rung
 *      is also walked periodically, for ever - see FREE_PORT_PROBE_INTERVAL_MS - which is what
 *      makes a sensor plugged in after the restart appear without another restart. Every free
 *      port is asked it, including one the module has given up identifying: it names an address,
 *      so it costs the ports sharing its wire nothing, and a sensor put in the place of a device
 *      nobody could name is found by it like any other;
 *   2. **presence.** One broadcast poll of the port - the wildcard query frames, the
 *      ones the address write already puts on the line - asking whether anything at all is there.
 *      A wildcard is answered by every device that can hear it, and two ports of this board share
 *      one UART, so this is the one question that has to isolate: the ports on the same wire lose
 *      their power for the length of the poll and get it straight back, the way the
 *      address write isolates before it puts the same frames on the line. Being the one rung that
 *      costs a neighbour, it is also the one a periodic pass rations - see
 *      presencePollIsWorthAsking().
 *
 *      Only the models those frames were written for answer them, so on the restart's own pass
 *      silence is not taken for an empty port: the poll opens a window, the wire stays dark, and
 *      the window goes on to ask every scannable ModBus model at every address of the module's
 *      range - a device the module once gave an address answers there, and nowhere else - and,
 *      when nothing answers that either, writes the port's address the way the identification procedure
 *      writes it, to 0x00 (see buildWindowStep()). What that finds is adopted like any other
 *      find. Only what is silent to all of it is an empty port. While a window is open the sweep
 *      leaves the ports of its wire alone and no bus command is served - either would give those
 *      ports their power back in the middle of it (see holdsAWindowOpen(), keepsThePortDark());
 *   3. **identification.** The full procedure of the boot scan - the broadcast re-address,
 *      the per-port address, the model's own, then the module's whole ModBus range - for a port
 *      that answered the presence poll and matched no model. It is what finds a device whose
 *      address has been scrambled, it costs tens of seconds per model, and it isolates: every
 *      model scan of it takes the power off the other port of its wire, a working sensor
 *      included. So a port gets exactly one pass of it - a device that answers the line and
 *      matches nothing would otherwise be chased for as long as the module runs - and no pass this
 *      class makes on its own ever runs it twice on the same port. The way back is rescan(), and
 *      what that asks for is the two cheap rungs on the one port a person named: the procedure
 *      itself is run by the ScanSensors command that calls it, on that same port, and the line
 *      serves the command before the pass.
 *
 * On the restart's pass the order runs across the ports as well as down the ladder: every free port
 * is asked the cheap rung before any port is asked the rest. A device at its factory address is
 * found in seconds, a port's window is a minute and more, and a sensor on the last port walked
 * would otherwise wait for the window of every port before it. The ports the cheap rung named are
 * done with; the others go on down the ladder, in the order of the ports. The cheap rung itself asks
 * first the ports that share a wire with a stored sensor whose address it has to leave out - see
 * askThePortsThatLeaveAnAddressOutFirst(); a periodic pass orders its ports the same way. A pass
 * after the restart asks one port at a time: most ports get nothing from it but the cheap rung.
 *
 * The cheap rung of the restart's pass and of the periodic one asks the module's own range too,
 * before the factory addresses - see buildRangeStepOfTheCheapRung(): a device the module once gave
 * an address is what a free port most often has on it after a restart, or when a sensor is moved
 * from another port or another module, and it answers there and nowhere else. And on the restart's
 * pass a port whose wire had a sensor found by the rest of the ladder is asked the cheap rung once
 * more before its own window - see askThePortsOfItsWireTheCheapRungAgain().
 *
 * The address a newly found device ends up on is not part of finding it: a device answering at its
 * factory address is adopted there and moved into the module's own range afterwards, by the
 * address phase, with the same writeSensorAddress() sequence as any other move. One kind of move is
 * made at once rather than afterwards: the one that parts a device the restart's pass adopts from a
 * stored row of its wire on the same address - see partItFromTheRowOfItsWire() - and when that
 * device will not be parted, it is not kept - see dropTheFindThatCannotBeParted().
 *
 * ### Why no row is left on a factory address
 *
 * A factory address belongs to a model and not to a row: every device of that model answers at it,
 * so two of them on one wire cannot be told apart, and a free port of that wire is answered by a
 * device that is not on it. An address of the module's own range belongs to one row. So a row is
 * only ever passing through a factory address, and a pass over the free ports checks the registry
 * for rows that are still on one - a row the sweep is reading, on the address its model leaves the
 * factory with - and owes each of them the same address write the restart's address phase would
 * make. It needs no free port, no command and no restart; see noteTheRowsStillOnTheirFactoryAddress().
 *
 * ### What the ports cost while all that runs
 *
 * A free port is dark: nothing is plugged in, or what is plugged in has no power, and the boot
 * leaves it that way (ModbusRS485 drives SENSOR_POWER_n low, and the preparation switches off
 * every port no stored sensor uses). A question put to a dark port is answered by nobody, so a
 * pass over the free ports opens by raising their power and closes by putting it back down -
 * POWER_FREE_PORTS and RELEASE_FREE_PORTS, two steps per pass, no frames in either. Between two
 * passes a free port is as dark as the boot left it, which is what keeps whatever is plugged into
 * it off the wire its paired port works on. A free port may be taken down after its cheap rung for
 * the same reason - on the restart's pass until its window, on a periodic pass for the rest of the
 * pass - see buildTheStepThatTakesDown().
 *
 * Power is not enough on its own: a sensor needs seconds of it before it answers anything, and
 * the module's own belief about how many is ten (SensorReader::scanModbusSensor and
 * ModbusSensorReader::isolatePortForAddressWrite both wait that long after they move a power pin).
 * So a pass that had to raise a port's power leaves the ports alone for PORT_WAKE_UP_MS before it
 * asks them anything. The wait costs no line time - the sweep has the line throughout. A periodic
 * pass pays it once; the restart's pass pays it again before each port that a window or a model
 * scan of the port before it has taken the power of.
 *
 * ### What it holds
 *
 * The plan is not a list of steps: a step carries a whole `SensorEntity`, and a module with four
 * ports and a forty-model catalog would be a hundred and sixty of them. What is held instead is
 * the registry's uids, the ports left to scan, and a handle to the catalog - the same handle every
 * other catalog reader takes, not a copy - and the step of the moment is built from those.
 */
class StartupSensorScanUseCase : public IStartupSensorScan
{
public:
    StartupSensorScanUseCase(ILogger *logger = nullptr,
                             IDebugSystem *debugSystem = nullptr,
                             IModuleConfig *moduleConfig = nullptr,
                             ISensorRepository *sensorRepository = nullptr,
                             ISensorTypeRepository *sensorTypeRepository = nullptr,
                             ISensorReader *sensorReader = nullptr,
                             ModuleStructure *moduleStructure = nullptr,
                             ISensorStructureUseCase *sensorStructure = nullptr,
                             IModbusAddressAllocator *modbusAddressAllocator = nullptr,
                             ISensorValueModel *sensorValues = nullptr,
                             IVirtualSensorChannelsUseCase *virtualChannels = nullptr,
                             IArduinoAdapter *arduino = nullptr)
        : _logger(logger),
          _debugSystem(debugSystem),
          _moduleConfig(moduleConfig),
          _sensorRepository(sensorRepository),
          _sensorTypeRepository(sensorTypeRepository),
          _sensorReader(sensorReader),
          _moduleStructure(moduleStructure),
          _sensorStructure(sensorStructure),
          _modbusAddressAllocator(modbusAddressAllocator),
          _sensorValues(sensorValues),
          _virtualChannels(virtualChannels),
          _arduino(arduino)
    {
    }

    /**
     * @brief Sweeps a stored row may miss before the structure describes its port as empty.
     *
     * The error flag the sweep keeps is up after a single failed read, and one failed read is the
     * commonest thing on this line: a collision with a discovery step or a raw command from the
     * backend, a device that answered late, a frame lost to noise. A structure built in that
     * second would carry the row as an empty port, and the backend then drops every reading that
     * arrives for its channels until a structure says otherwise.
     *
     * Three misses at the sweep's own cadence, so a row has to be quiet for three quarters of a
     * minute before the structure gives up on it - and still less than the minute of unbroken
     * failure after which the port recovery cuts the port's power and brings it back.
     */
    static const uint32_t SILENT_AFTER_MISSED_SWEEPS = 3;

    /**
     * @brief Answered sweeps in a row before a port the structure gave up on becomes a sensor again.
     *
     * The way back has to be as slow as the way out, and for a reason the module makes itself: the
     * port recovery cuts the power of a port that has been failing for a minute and brings it back,
     * and one read that answers is all it takes to call the port recovered. A device with a bad
     * contact therefore lives in a cycle of about a minute of silence and a single answer, and a
     * module that believed that answer would describe itself twice per cycle for as long as the
     * contact is bad.
     *
     * Two answers in a row - the first one starts the clock, the second one lands a sweep later -
     * so a single answer in the middle of the silence settles nothing, and a device that is
     * genuinely back is in the app again within half a minute of its second reading.
     */
    static const uint32_t ANSWERING_SWEEPS_TO_RETURN = 2;

    /** @brief The interval at which the sweep reads a row, and the unit the sweeps are counted in. */
    static const uint32_t SWEEP_INTERVAL_MS = 15000;

    /**
     * @brief The shortest gap between two structures sent because a row's liveness changed.
     *
     * The hysteresis above answers the flap the module's own port recovery produces; it cannot
     * answer every flap, because for any pair of thresholds there is a period that resonates with
     * them. This is the bound that does not depend on the period: whatever the line does, liveness
     * costs the server at most one structure every five minutes, and the ones it holds back are
     * counted rather than dropped - the difference stays, and the next review past the interval
     * sends the state as it is then.
     *
     * Five minutes because a liveness change is not urgent to the second - the app shows a sensor
     * and its last reading, and a five-minute-old snapshot of which sensors exist is honest enough
     * - while a resonating flap must not cost more than a handful of structures an hour.
     */
    static const uint32_t LIVENESS_REPUBLISH_MIN_INTERVAL_MS = 300000;

    /**
     * @brief How long between two cheap passes over the free ports, once the restart's is over.
     *
     * Read off the sweep: the acquisition task serves a discovery step only between two sweeps, so
     * a period equal to SWEEP_INTERVAL_MS would put a pass into every gap the sweep leaves and the
     * ladder would be on the line whenever the sweep is not. Two sweeps leaves every other gap to
     * the commands the backend sends and to the module's own quiet.
     *
     * The other end of the choice is the person holding the sensor they have just plugged in. One
     * cheap probe is a read budget (300 ms), and the cheap rung asks five of them per scannable
     * model on every free port - the four addresses of the module's range and the model's own - so
     * a pass costs free ports x scannable models x 5 x that: 72 s on a stand's four free ports and
     * twelve models, 240 s against a field catalog of forty, with every probe unanswered and no
     * address left out. What they wait for is this interval plus the walk, 102 s and 270 s on
     * those - without the restart the app asks them for today.
     *
     * Measured from the end of the last pass rather than its start (see markPassEnded), so two
     * passes are always this far apart whatever the walk between them cost.
     */
    static const uint32_t FREE_PORT_PROBE_INTERVAL_MS = 30000;

    /**
     * @brief The longest the ladder leaves the line alone after the boot arms it.
     *
     * A restart's expensive moments are all in its first minute and they are the module's own: the
     * TLS session and the handshake that sets it up, the first synchronizations, the first
     * structure. Every one of them wants tens of kilobytes at once on a heap that has tens of
     * kilobytes free. A pass of this ladder wants its own - the registry's uids, a handle on the
     * whole sensor-type catalog, the entity of the step on the line - and starting it in those
     * same seconds is what makes the two into one peak.
     *
     * What the ladder actually waits for is the boot sequence: its DISCOVERY gate opens once the
     * stored sensors have had their first outcome and the boot's structure and catalog have
     * settled - or, with no link, once the first outcomes are in and the catalog the module holds
     * is the one there is. That is the end of the boot's own work, which on device logs is
     * twelve to thirty-three seconds in. This window is the bound on that wait, not the wait itself: on a
     * slow or silent server the sequence can sit in its network phases for minutes, and the ladder
     * does not wait longer than this for it - it starts here, beside whatever the network is still
     * doing. A module wired without a sequence waits exactly this long.
     *
     * Nothing is lost by waiting. The sensors the module already knows come from the registry on
     * flash, are described in the structure the boot sends and are read by the sweep from the
     * first second; what a pass looks for is what is *not* in the registry, and there is nothing
     * urgent about that.
     *
     * A minute, because that is the length of the boot's own work as the module logs it - the loop
     * starts around ten seconds in, the first telemetry and the first synchronizations follow it,
     * and by the end of the minute they are over.
     *
     * A pass a person asked for is not held: see theLadderMayStepNow().
     */
    static const uint32_t BOOT_SETTLE_MS = 60000;

    /**
     * @brief The wait for an answer to the broadcast presence poll of a port.
     *
     * The address-discovery wildcard query waits 30 polls of 10 ms for the same frames
     * (ModbusSensorReader::queryAddressViaWildcard), and the reader's ordinary read budget for any
     * transaction is the same 300 ms. Neither number is chosen here; this is the one they agree on.
     */
    static const uint32_t PRESENCE_POLL_TIMEOUT_MS = 300;

    /**
     * @brief How long a port that has just been given its power is left alone before it is asked.
     *
     * Not a number of this ladder's own: it is the wait the module already spends whenever it
     * moves a port's power pin and then talks to what is behind it - SensorReader::scanModbusSensor
     * after it arranges the ports for a scan, ModbusSensorReader::isolatePortForAddressWrite and
     * powerCyclePort before an address write. Ten seconds is what this firmware believes a sensor
     * takes to come up, and a cheap probe is 300 ms, so a port asked before this has elapsed
     * answers nothing whatever is plugged into it.
     *
     * It is waited out between steps, not inside one: nothing holds the line while it passes, the
     * sweep goes on reading the stored sensors, and the ports being waited for are the ones this
     * pass has just switched on - a pass that found them switched on already waits for nothing.
     *
     * It is also the bound on what a pass of cheap questions can find, and the bound is real: a
     * pass raises the ports' power, waits this long and asks; a device needing longer answers
     * nothing and its port is described as empty. That is the price of a pass that costs seconds
     * instead of minutes, and it is paid by the boot's pass and by every periodic one. The way to
     * a slower device is the identification procedure, which holds a port's power for as long as it walks
     * the catalog: a ScanSensors command runs it on the port it names, and the pass that follows
     * that command asks the cheap questions of that port again - by then the port has had power
     * for the whole length of the procedure, minutes rather than these ten seconds, and whatever
     * needed that long to come up answers them.
     */
    static const uint32_t PORT_WAKE_UP_MS = PortPower::WAKE_UP_MS;

    /**
     * @brief Does the restart's window on a silent port also write the port's address?
     *
     * The identification procedure wrote an address into every free port at every restart - the model's
     * own address write sent to 0x00, then the blind register 0/1 sequence - and that is what
     * finds a device holding an address nobody knows, which neither the range nor a wildcard
     * reaches. It is also most of what the window costs: a model's writes are a step of about
     * 20 s, the blind sequence 17 s of it, and the ports of the wire stay dark across the steps of
     * every model.
     *
     * The one switch for both writes: false leaves the window the poll and the range and nothing
     * that writes.
     */
    static const bool THE_WINDOW_WRITES_THE_PORT_ADDRESS = true;

    /**
     * @brief How long a port the restart's pass has taken down is kept out of the sweep's hands.
     *
     * The sweep reads from a list of sensors, not from the registry: SensorService hands it the
     * registry's ModBus rows once per SensorService::SENSOR_READ_INTERVAL, fifteen seconds. A row the
     * pass removes is in that list until the next hand-over, and a read of it into a dark port gives
     * the port its power (SensorAcquisitionService::thePortIsNotUpYet()) - the very power the pass
     * took down. Two intervals cover the one hand-over with room to spare, and after them the list
     * says what the registry says: a sensor added on that port later is read like any other.
     */
    static const uint32_t THE_SWEEPS_LIST_LAGS_THE_REGISTRY_BY_MS = 2 * SWEEP_INTERVAL_MS;

    /**
     * @brief Tell the server what the discovery adopted: on the restart's pass as each sensor is
     *        adopted and once more when the pass is over, on a later pass once it is over.
     *
     * Optional: without it the sensors found by a restart reach the backend on the next structure
     * the module sends for any other reason.
     */
    void setRepublishModuleStructureUseCase(IRepublishModuleStructureUseCase *useCase)
    {
        _republish = useCase;
    }

    /**
     * @brief The boot sequence, whose DISCOVERY gate lets the ladder onto the line before the end
     *        of BOOT_SETTLE_MS.
     *
     * Optional: without it the window alone decides.
     */
    void setBootGates(IBootGates *gates) { _bootGates = gates; }

    /**
     * @brief The way onto the line that can ask a port as a whole rather than an address.
     *
     * The presence rung asks "is anything on this port", and no answer to that question can come
     * through the sensor reader: every one of its entry points is about one device at one address
     * and rejects a frame that came from another. This is the same adapter the raw ModbusRead of
     * the backend uses, and the frames it is given here are the wildcard queries of the address discovery.
     *
     * Optional. Without it a port that answered no model is handed to the identification rung
     * anyway - the ladder does not cheapen an answer it cannot get, so a module wired without this
     * discovers exactly what a full scan discovers, at the full price.
     */
    void setRawBusAccess(IRawBusAccess *rawBusAccess) { _rawBus = rawBusAccess; }

    /**
     * @brief Which UART each port of the module is wired to. One entry per port.
     *
     * Two ports of this board share one HardwareSerial and differ only in which SENSOR_POWER pin
     * feeds what is plugged into them (globals.h), so they are one electrical line: a frame put on
     * either is heard by the devices of both. Everything that names an address survives that - a
     * reader rejects an answer from a responder it did not ask - but the presence poll names no
     * address on purpose, and that is the one question a device on the paired port would answer
     * for its neighbour. Given this, the poll takes that neighbour's power down for its own
     * length; without it, the poll goes out as it stands and a shared line answers it. The model
     * scan and the address write take down the same ports, by the same wiring, in the reader -
     * globals.h hands this the reader's own account of it (ModbusSensorReader::portLines()).
     *
     * Optional, and empty on a stand that does not say: a module wired by hand is treated as four
     * independent lines.
     */
    void setPortLines(const std::vector<int> &lineOfPort)
    {
        _lineOfPort = lineOfPort;
        _isolatedPorts.reserve(_lineOfPort.size());
        _portsComingBack.reserve(_lineOfPort.size());
        _portsTakenDown.reserve(_lineOfPort.size());
        _portsLeftDark.reserve(_lineOfPort.size());
    }

    // --- IStartupSensorScan --------------------------------------------------------------------

    /**
     * @brief The one place that answers "how is this stored row described".
     *
     * The question the answer turns on is not "is anybody going to check" - that is a guess about
     * the future, and the two structures a boot produces would disagree over it. It is "is the
     * line answering for this row", which is a fact, and it is the fact the boot scan decides on:
     *
     *   - a sensor the person has accepted is described as itself, answering or not. That is what
     *     the boot scan did for an ADDED sensor whether its ping succeeded or failed;
     *   - any other stored sensor is described as itself too - unless the module has asked the
     *     line about it and nothing has been coming back for several sweeps, and then it is an
     *     empty port.
     *
     * The fact is read where it is kept up to date - see lineIsSilentFor() - and not from a note
     * the restart made, because a structure can be built at any time and a sensor that was silent
     * during the restart may well be answering by then.
     *
     * A boot that skips the discovery asks nothing, so nothing is ever found silent and the whole
     * registry is described as it stands - byte for byte the noScanSensors path of the boot scan.
     *
     * A registry that did not read back describes no row, and the server takes such a structure for
     * a module whose ports are all empty. That is remembered here, and the structure is sent again
     * once the registry reads back - see reviewSensorLiveness().
     */
    void describeStoredSensors(Node &node) override
    {
        if (_sensorRepository == nullptr)
        {
            return;
        }

        // Asked before the read below, which then holds what this answer saw.
        _describedARegistryThatDidNotReadBack = theRegistryDidNotReadBack();
        const std::vector<SensorEntity> stored = _sensorRepository->getAll();

        // Updated in place rather than rebuilt: a note carries the liveness its row has settled on,
        // which outlives any one structure, and rebuilding would free and allocate the uid of every
        // row on every build. Capacity is taken once, for the registry as it stands.
        _describedLiveness.reserve(stored.size());
        for (size_t i = 0; i < _describedLiveness.size(); i++)
        {
            _describedLiveness[i].describedInThisPass = false;
        }

        for (size_t i = 0; i < stored.size(); i++)
        {
            describeStoredSensor(node, stored[i]);
        }

        dropNotesOfRowsThatLeftTheRegistry();

        // A structure built from the registry is what the placeholders of a boot without a catalog
        // were standing in for.
        _placeholdersReported = false;
    }

    /**
     * @brief What became of the structure this describer last wrote. Control loop only.
     *
     * A note says what the *server* was told, so it may only be believed once the structure has
     * been taken. Until this is called the note holds what the last build wrote; a structure that
     * never arrived restores the notes to what they said before it, which leaves the difference
     * standing and lets the next review offer it again - a change of liveness that happened while
     * the uplink was down would otherwise be spent on a structure nobody received.
     *
     * A structure whose fate nobody reports - the one the boot builds - keeps the behaviour it has
     * always had: the note is taken at its word.
     */
    void onDescribedStructureSettled(bool delivered) override
    {
        if (delivered)
        {
            return;
        }

        for (size_t i = 0; i < _describedLiveness.size(); i++)
        {
            _describedLiveness[i].describedEmptyPort = _describedLiveness[i].emptyPortBeforeTheBuild;
        }

        _livenessUndeliveredCount++;
        log("STARTUP_SCAN: the structure that was described did not reach the server - the module still owes it");
    }

    /**
     * @brief Send the structure again when a row is no longer what the last one said it was.
     *
     * Control loop only, and cheap enough for the loop to call it every iteration: the fact can
     * only move when the sweep reads a row, so the comparison runs once per sweep interval and
     * over the cached active view, which copies nothing.
     *
     * The flag alone is not enough to act on - it is up after a single missed read, and a single
     * read that answers does not mean a device is back - so what is compared is the settled fact,
     * the one lineIsSilentFor() answers with, against the answer the describer gave for that same
     * row when the last structure was built. The two part in both directions, and both matter to
     * the person looking at the app: a sensor that was silent while the structure was built and
     * has since come back is absent from the app until something else sends a structure, and a
     * sensor that has really gone stays in it as a live reading that never changes.
     *
     * Nothing is compared while the discovery is still running: it describes the rows it pings as
     * it goes and sends the structure itself - for every sensor the restart's pass adopts, and once
     * more when a pass finishes. A structure that pass owes, because the one before it was still on
     * its way, goes out from here once that one has settled; it is built from the registry when it
     * goes, so it carries every row's liveness too, and nothing is compared until it has gone.
     *
     * A structure described from a registry that did not read back is not a row that changed: no
     * row was described, so no note says what the server was told. It is sent again as soon as the
     * registry reads back, the way a pass sends its structure - after the one on its way, if any -
     * and not held for a discovery that may run for minutes.
     */
    void reviewSensorLiveness() override
    {
        // Here, on every iteration of the loop, so that the end of the pass is let go of within an
        // iteration of the moment it stops counting, whether anything looks at the rows then or not:
        // it must not outlive the clock.
        forgetTheEndOfTheRestartsPassOnceItDecidesNothing();

        if (_structureOwed)
        {
            sendTheOwedStructureOnceTheLastOneHasSettled();
            return;
        }

        if (_describedARegistryThatDidNotReadBack && !theRegistryDidNotReadBack())
        {
            _describedARegistryThatDidNotReadBack = false;
            log("STARTUP_SCAN: the registry read back - the structure that described it empty is sent again");
            sendTheStructureOfTheRestartsPass("registry_read_back");
            return;
        }

        if (_republish == nullptr || _sensorRepository == nullptr || _arduino == nullptr ||
            _describedLiveness.empty() || isRunning())
        {
            return;
        }

        if (_republish->isSendPending())
        {
            // A structure is on its way, and one submitted now would go out beside it rather than
            // after it. The rows are compared once it has settled, against the notes it leaves.
            return;
        }

        const uint32_t now = static_cast<uint32_t>(_arduino->millis());
        if (_livenessReviewedAtMs != 0 && (now - _livenessReviewedAtMs) < SWEEP_INTERVAL_MS)
        {
            return;
        }
        _livenessReviewedAtMs = (now == 0) ? 1 : now;

        // By reference: the repository hands out its active view rather than a copy, and this runs
        // on the loop. Nothing below writes the registry, so the view outlives the walk.
        //
        // Every row is asked, not only up to the first difference: asking is what moves a row
        // between the two settled answers, and a row skipped because another one was waiting for
        // the interval below would stop settling for as long as that lasted.
        const std::vector<SensorEntity> &polled = _sensorRepository->getActiveSensors();
        const SensorEntity *changed = nullptr;
        for (size_t i = 0; i < polled.size(); i++)
        {
            LivenessNote *note = describedLivenessOf(polled[i].uid);
            if (note == nullptr)
            {
                // Never described, so there is nothing the server was told to compare with.
                continue;
            }

            if (describedAsEmptyPort(polled[i], *note) != note->describedEmptyPort &&
                changed == nullptr)
            {
                changed = &polled[i];
            }
        }

        if (changed == nullptr)
        {
            return;
        }

        if (_livenessRepublishedAtMs != 0 &&
            (now - _livenessRepublishedAtMs) < LIVENESS_REPUBLISH_MIN_INTERVAL_MS)
        {
            // Held, not dropped: the note still disagrees with the row, so the first review past
            // the interval offers whatever the rows say then.
            _livenessRepublishHeldCount++;
            log("STARTUP_SCAN: sensor %s changed again too soon - the structure is held back (held %u time(s))",
                changed->uid.c_str(), (unsigned)_livenessRepublishHeldCount);
            return;
        }

        log("STARTUP_SCAN: sensor %s is no longer what the last structure said - sending the structure again",
            changed->uid.c_str());
        _livenessRepublishedAtMs = (now == 0) ? 1 : now;
        _livenessRepublishCount++;
        // Rebuilds every record through describeStoredSensors() above, which is also what brings
        // the notes in step with what has just been sent - and what puts them back if it turns out
        // that nothing was.
        _republish->execute("sensor_liveness_changed");
    }

    void arm() override
    {
        _pass = Pass::WHOLE_MODULE;
        _phase = Phase::PLAN_VERIFY;
        _finished = false;
        _adoptedInThisPass = 0;
        _stepsInThisPass = 0;
        _didDiscover = true;
        _armCount++;
        startTheSettleWindow();
        log("STARTUP_SCAN: armed");
    }

    void armPreparationOnly() override
    {
        _pass = Pass::WHOLE_MODULE;
        _phase = Phase::PREPARE;
        _finished = false;
        _didDiscover = false;
        _preparationArmCount++;
        startTheSettleWindow();
        log("STARTUP_SCAN: armed for preparation only");
    }

    void armWhenTheCatalogArrives(bool preparationOnly) override
    {
        _armOwed = true;
        _armOwedPreparationOnly = preparationOnly;
        _placeholdersReported = true;
        log("STARTUP_SCAN: no catalog to scan with - %s once one arrives",
            preparationOnly ? "the preparation is armed" : "the discovery is armed");
    }

    /**
     * @brief A catalog synchronization has finished: arm what the boot owes, if there is now a
     *        catalog to arm it with. Control loop only.
     *
     * Asks storage how many types there are only while something is owed - once per finished
     * synchronization, and on a module that armed at boot never.
     */
    void onCatalogSettled() override
    {
        if (!_armOwed || _sensorTypeRepository == nullptr || _sensorTypeRepository->count() <= 0)
        {
            return;
        }

        _armOwed = false;
        _lateArmCount++;
        log("STARTUP_SCAN: the catalog has arrived - arming what the boot owed");
        // noScanSensors skips the discovery around the sensors a module holds; the preparation is
        // what it still owes them. A module that holds none has nothing to prepare, and the boot
        // that left this arm owed had no catalog to discover anything with - the placeholder path
        // of setup uses the flag up so that the next chance to scan does scan
        // (ModuleSetupService::createPlaceholderSensors()). The catalog arriving is that chance;
        // skipped, the module would go on with no sensor and the server with placeholders.
        if (_armOwedPreparationOnly && _sensorRepository != nullptr && _sensorRepository->count() > 0)
        {
            armPreparationOnly();
            return;
        }
        arm();
    }

    /**
     * @brief Recorded here and acted on between two steps, never in the middle of one.
     *
     * The caller is a command from the server arriving on the control loop, which is also the task
     * that plans the steps - but it can arrive while one is on the line, and cutting that one in
     * half would leave the answer of a step nobody is expecting any more.
     *
     * One port, the one the command named. A pass costs the ports it does not walk their power -
     * the poll and every model scan of the identification procedure isolate the port's wire - so a
     * pass over ports nobody asked about is a working sensor taken off the line for the length of
     * it.
     */
    void rescan(int pin) override
    {
        _rescanRequestCount++;
        if (_rescanRequested && _rescanPin != pin)
        {
            // One request is held, not a queue of them: a second command arriving before the
            // first pass has started takes its place. Both commands still run their own procedure
            // on their own port - that is the command's work and this is only what follows it -
            // so what the replaced request costs is the cheap questions asked again on that port.
            log("STARTUP_SCAN: a pass over pin %d was still owed when pin %d was asked about - "
                "the first is dropped",
                _rescanPin, pin);
        }
        _rescanRequested = true;
        _rescanPin = pin;
        log("STARTUP_SCAN: pin %d was asked about by hand - a pass over it is owed", pin);
    }

    bool isRunning() const override { return aPassIsRunning() && _pass == Pass::WHOLE_MODULE; }

    /**
     * @brief The one pass somebody is standing in front of the module waiting for: the one a
     *        ScanSensors command asks for.
     *
     * It counts from the moment the command asked rather than from the moment the pass starts,
     * because between the two the line is running that same command's identification procedure on
     * that same port - one entry as far as anyone watching the module is concerned.
     *
     * Neither the pass a restart arms nor the periodic walk over the free ports is one. The
     * periodic walk never ends, and a module indicating it would indicate for the rest of its
     * life. The restart's pass does end, but nobody asked for it: it runs on every boot, it
     * outlives the boot by half a minute or more, and what it would put on the front panel is a
     * blink nobody can act on while the module is otherwise working normally.
     */
    bool isScanEntryRunning() const override { return aPersonAskedForThisPass(); }

    bool takeNextStep(StartupScanStep &step) override
    {
        if (_stepInFlight)
        {
            return false;
        }

        if (!theLadderMayStepNow())
        {
            return false;
        }

        // A registry that did not read back holds no row, and every port would look free to a pass
        // planned on it: the ports of the stored sensors would be asked as free ones are - given
        // an address and their power taken down where nothing answers - and a sensor that answered
        // would be adopted over the row it already has once storage reads again. No pass begins and
        // no step is handed out until it reads back.
        if (theRegistryDidNotReadBack())
        {
            if (!_heldForTheRegistry)
            {
                _heldForTheRegistry = true;
                log("STARTUP_SCAN: the registry did not read back - the discovery waits for it");
            }
            return false;
        }
        _heldForTheRegistry = false;

        if (!_stepPending)
        {
            beginTheNextPassIfOneIsDue();
        }

        if (!aPassIsRunning())
        {
            return false;
        }

        // A step that was built and then given back is offered again as it was, not replaced by
        // the one after it: the cursor moves when an answer is applied, and a queue that had no
        // room must not cost the module a port. Unless the port stopped being free while the step
        // waited - then it is dropped, for the same reason it would not have been built now.
        if (_stepPending && stepBelongsToAFreePort(_inFlight.kind) &&
            portClaimedSinceThePassBegan(_inFlight.pin))
        {
            log("STARTUP_SCAN: pin %d was claimed while a step for it waited - the step is dropped",
                _inFlight.pin);
            _stepPending = false;
            settleCurrentPin();
        }

        if (!_stepPending)
        {
            if (!buildNextStep(_inFlight))
            {
                return false;
            }
            _stepPending = true;
        }

        step = _inFlight;
        _stepInFlight = true;
        _takenStepCount++;
        return true;
    }

    void returnStep() override { _stepInFlight = false; }

    /**
     * @brief The bus half. Acquisition task only - it reads no repository and writes none.
     *
     * The transactions are the boot scan's, unchanged: a ping of the entity as stored; a broadcast
     * address write followed by the per-port, the model's own and then the whole ModBus range for
     * a candidate; the address write of a device that has to move; the port power arrangement and
     * per-model register writes of the preparation.
     */
    StartupScanOutcome runStepOnBus(const StartupScanStep &step) override
    {
        StartupScanOutcome outcome;

        if (_sensorReader == nullptr)
        {
            return outcome;
        }

        switch (step.kind)
        {
        case StartupScanStep::VERIFY_KNOWN:
            outcome.answered = _sensorReader->scanSensor(step.entity);
            return outcome;

        case StartupScanStep::PROBE_CANDIDATE:
            return probeCandidateOnBus(step);

        case StartupScanStep::PROBE_PRESENCE:
            return probePresenceOnBus(step);

        case StartupScanStep::SCAN_CANDIDATE:
            return scanCandidateOnBus(step);

        case StartupScanStep::WRITE_ADDRESS:
            outcome.answered = _sensorReader->writeSensorAddress(step.entity, step.targetAddress);
            return outcome;

        case StartupScanStep::PREPARE_KNOWN:
            _sensorReader->prepareSensorsForReading(step.entities);
            outcome.answered = true;
            return outcome;

        case StartupScanStep::POWER_FREE_PORTS:
            return powerTheFreePortsOnBus(step);

        case StartupScanStep::RELEASE_FREE_PORTS:
            return releaseTheFreePortsOnBus(step);

        case StartupScanStep::PROBE_RANGE:
            return askTheRangeOnBus(step);

        case StartupScanStep::WRITE_PORT_ADDRESS:
            return writeThePortAddressOnBus(step);

        case StartupScanStep::RELEASE_LINE:
            closeTheWindowOnBus();
            return outcome;

        default:
            return outcome;
        }
    }

    bool holdsAWindowOpen() const override { return _windowPin >= 0; }

    bool keepsThePortDark(int pin) const override
    {
        if (_windowPin >= 0 && pin != _windowPin && lineOfPort(pin) >= 0 &&
            lineOfPort(pin) == lineOfPort(_windowPin))
        {
            return true;
        }
        if (_arduino != nullptr && isAmongPins(_portsTakenDown, pin) &&
            (nowMs() - _portsTakenDownAtMs) < THE_SWEEPS_LIST_LAGS_THE_REGISTRY_BY_MS)
        {
            return true;
        }
        if (_arduino == nullptr || !isAmongPins(_portsComingBack, pin))
        {
            return false;
        }
        return (nowMs() - _portsCameBackAtMs) < PORT_WAKE_UP_MS;
    }

    void applyStepOutcome(const StartupScanOutcome &outcome) override
    {
        if (!_stepInFlight)
        {
            return;
        }

        const StartupScanStep step = _inFlight;
        _stepInFlight = false;
        _stepPending = false;
        _appliedStepCount++;
        _stepsInThisPass++;

        switch (step.kind)
        {
        case StartupScanStep::VERIFY_KNOWN:
            applyVerify(step, outcome);
            break;
        // Counted here rather than where the transactions are made: the bus half runs on the
        // acquisition task, and a counter the control loop reads may only be written by it.
        case StartupScanStep::PROBE_CANDIDATE:
            _cheapProbeCount++;
            // The cheap read costs the other ports nothing; the model scan it runs when something
            // answered costs the other port of its wire its power, and that is what has to be
            // remembered.
            if (outcome.answered || outcome.heardBeforeTheWireWasIsolated)
            {
                noteTheOtherPortsLostTheirPower();
                noteThePortsAheadMayBeDark();
            }
            applyCandidate(step, outcome);
            break;
        case StartupScanStep::SCAN_CANDIDATE:
            _identifyStepCount++;
            noteTheOtherPortsLostTheirPower();
            noteThePortsAheadMayBeDark();
            applyCandidate(step, outcome);
            break;
        case StartupScanStep::PROBE_PRESENCE:
            _presencePollCount++;
            noteTheOtherPortsLostTheirPower();
            if (!step.catalog && _rawBus != nullptr && aPortAheadSharesTheWireOf(step.pin))
            {
                // A poll outside a window gave the rest of its wire its power back moments ago.
                noteThePortsAheadMayBeDark();
            }
            applyPresence(step, outcome);
            break;
        case StartupScanStep::PROBE_RANGE:
        case StartupScanStep::WRITE_PORT_ADDRESS:
            applyWindowStep(step, outcome);
            break;
        case StartupScanStep::RELEASE_LINE:
            closeTheWindow(step);
            break;
        case StartupScanStep::WRITE_ADDRESS:
            applyAddressWrite(step, outcome);
            break;
        case StartupScanStep::PREPARE_KNOWN:
            // The last step of the sequence, and the only one whose answer ends it.
            _phase = Phase::DONE;
            break;
        case StartupScanStep::POWER_FREE_PORTS:
            // Only the ports whose power had to be raised owe a wake-up; ports that were up
            // already are asked straight away, the distinction isolatePortForAddressWrite() makes
            // between a target port it switched on and one it found on.
            _freePortsOweAWakeUp = outcome.answered;
            _freePortsPoweredAtMs = nowMs();
            _portsOweTheirPowerBack = false;
            break;
        case StartupScanStep::RELEASE_FREE_PORTS:
            break;
        default:
            break;
        }

        if (_phase == Phase::DONE)
        {
            finish();
        }
    }

    // --- Observation ---------------------------------------------------------------------------

    /** @brief Times the discovery was armed - once per boot that is allowed to scan. */
    uint32_t armCount() const { return _armCount; }

    /** @brief Times the boot's arming was owed to a catalog and made when one arrived. */
    uint32_t lateArmCount() const { return _lateArmCount; }

    /** @brief Times only the preparation was armed - once per boot that skips the discovery. */
    uint32_t preparationArmCount() const { return _preparationArmCount; }

    /** @brief Steps handed to the line. */
    uint32_t takenStepCount() const { return _takenStepCount; }

    /** @brief Steps whose answer was applied to the registry and the structure. */
    uint32_t appliedStepCount() const { return _appliedStepCount; }

    /** @brief Sensors the discovery wrote into the registry as NEW. */
    uint32_t adoptedCount() const { return _adoptedCount; }

    /** @brief Cheap probes of one model at one address on a free port. */
    uint32_t cheapProbeCount() const { return _cheapProbeCount; }

    /** @brief Broadcast presence polls of a free port. */
    uint32_t presencePollCount() const { return _presencePollCount; }

    /**
     * @brief Steps of the full identification procedure.
     *
     * The expensive rung, and the one the ladder exists to ration: this is the count that must not
     * grow while a module sits with an unidentifiable device on a port.
     */
    uint32_t identifyStepCount() const { return _identifyStepCount; }

    /** @brief Periodic cheap passes over the free ports, after the one the restart armed. */
    uint32_t freePortPassCount() const { return _freePortPassCount; }

    /** @brief Ports that answered the presence poll and matched no model of the catalog. */
    uint32_t unidentifiedOccupantCount() const { return _unidentifiedOccupantCount; }

    /** @brief Rows a pass of the module's own moved off their model's factory address. */
    uint32_t factoryAddressMoveCount() const { return _factoryAddressMoveCount; }

    /**
     * @brief Rows whose device answered the module and would not take the address it was written.
     *
     * The count that must not grow while a module sits with a sensor of such a model: one attempt
     * per row - the first one, whichever phase made it - and then no address write of any pass
     * goes near that row again. The two ways back are a person asking for its port by hand and a
     * restart, which is where the note lives.
     *
     * A write into a port with nothing on it is not one of these. It is refused in exactly the same
     * way - the ping that ends the write is answered by nobody - and counting it here would leave
     * the note on a row whose sensor its owner has not plugged in yet.
     */
    uint32_t factoryAddressMoveRefusedCount() const { return _factoryAddressMoveRefusedCount; }

    /** @brief Full passes asked for from outside - the ScanSensors command. */
    uint32_t rescanRequestCount() const { return _rescanRequestCount; }

    /** @brief Ports currently held out of the identification rung as unidentifiable. */
    size_t unidentifiedPinCount() const { return _unidentifiedPins.size(); }

    /**
     * @brief Is any pass still under way - the restart's own or a cheap one?
     *
     * isRunning() answers the narrower question the control loop asks ("has the discovery got the
     * line"), and a cheap pass deliberately does not count as that. This is the whole answer, and
     * what it is for is telling "the free ports are dark" from "the free ports are dark for the
     * moment because the pass that powered them has not put them back yet".
     */
    bool aPassIsInFlight() const { return aPassIsRunning(); }

    /**
     * @brief Entries of the module's own state this use case is still holding on to.
     *
     * Zero once the discovery is over. Not a diagnostic curiosity: this object outlives its own
     * work by the whole life of the module, so anything still counted here is memory the module
     * never gets back.
     */
    size_t retainedEntryCount() const
    {
        return _verifyUids.size() + _scanPins.size() + _pinsLeftToTheLadder.size() +
               _pinsOwedTheCheapRungAgain.size() + _pinsHeldDarkUntilTheirWalk.size() + _addressWrites.size() +
               _rowsOnAFactoryAddress.size() + _rowsAdoptedThisPass.size() +
               _rowsThatAnsweredThePing.size() + (_rowToPartFromItsWire.empty() ? (size_t)0 : (size_t)1) +
               (_storedRowToMoveOffItsWire.empty() ? (size_t)0 : (size_t)1) +
               _inFlight.entities.size() + _inFlight.pins.size() +
               (_catalog ? _catalog->size() : (size_t)0);
    }

    /**
     * @brief Rows whose only source of the fact is still the discovery's ping.
     *
     * Counted separately from retainedEntryCount(), which is what the discovery holds to do its
     * work: this is what it found out, and it is held on purpose - until the sweep reads the row
     * and takes the answer over, which is when it is dropped. Bounded by the registry, and empty
     * on a module whose sensors are all answering.
     */
    size_t silentToThePingCount() const { return _silentToThePing.size(); }

    /**
     * @brief Structures sent because a row's settled liveness stopped matching the last one.
     *
     * One per settled change, not one per sweep: a row that flaps between answering and not never
     * settles, so it never produces one at all.
     */
    uint32_t livenessRepublishCount() const { return _livenessRepublishCount; }

    /** @brief Differences found while the minimum interval between two of them was still running. */
    uint32_t livenessRepublishHeldCount() const { return _livenessRepublishHeldCount; }

    /** @brief Described structures that never reached the server, and whose notes were put back. */
    uint32_t livenessUndeliveredCount() const { return _livenessUndeliveredCount; }

    /**
     * @brief Rows the describer is holding a liveness note for.
     *
     * Counted apart from retainedEntryCount(), which is what the discovery holds to do its work
     * and gives back when it is over - like silentToThePingCount(), this is memory the module
     * keeps for as long as it runs, because the note is what a later fact is compared against.
     * One entry per row of the registry: the describer is the only writer, it writes what the
     * registry gives it, and a row that leaves the registry loses its note on the next structure.
     */
    size_t describedLivenessCount() const { return _describedLiveness.size(); }

private:
    enum class Phase
    {
        IDLE,
        PLAN_VERIFY,
        VERIFY,
        PLAN_SCAN,
        SCAN,
        PLAN_ADDRESSES,
        ADDRESSES,
        PREPARE,
        DONE
    };

    /**
     * @brief What the pass in progress is for.
     *
     * WHOLE_MODULE is the restart's own: every stored entity pinged, every free port taken down
     * the whole ladder, the addresses resolved, the registry prepared. FREE_PORTS is the cheap
     * pass that goes on for ever afterwards - the first rung only, and the phases after it only if
     * it found something. NAMED_PORT is what a ScanSensors command leaves behind it: the cheap
     * rung and the presence poll on the one port the command named, and the phases after them only
     * if that port answered.
     */
    enum class Pass
    {
        WHOLE_MODULE,
        FREE_PORTS,
        /** One port, because a person named it: see rescan(). */
        NAMED_PORT
    };

    /** @brief Which question of the ladder the port at the cursor is on. */
    enum class Rung
    {
        /**
         * One request per model, at the model's own factory address - on the restart's pass and the
         * periodic one at the addresses of the module's range first.
         */
        CHEAP,
        /** The broadcast poll: is anything on this port at all. */
        PRESENCE,
        /** The restart's window: the module's range, model by model. */
        RANGE,
        /** The restart's window: the port's address written, model by model. */
        WRITE,
        /** The restart's window is over and its wire is owed its power back. */
        CLOSE,
        /** The full identification procedure, model by model. */
        IDENTIFY
    };

    /**
     * @brief An address a free port hears answered by a sensor of the port sharing its wire.
     *
     * What the module paid one confirming scan to find out: the frame came from the neighbour, not
     * from anything on this port. The sensor is not named here - the registry is asked again every
     * time the note is read, and a wire that no longer carries a sensor at this address no longer
     * has a note.
     */
    struct WireAnswer
    {
        int pin = -1;
        int address = 0;
    };

    /**
     * @brief What the module holds about one row's liveness: what it told the server, and where
     *        the row is between the two settled answers.
     */
    struct LivenessNote
    {
        std::string uid;
        /** How the row was described in the last structure - what the server is taken to hold. */
        bool describedEmptyPort = false;
        /** The same, before the last build: restored when that structure never arrived. */
        bool emptyPortBeforeTheBuild = false;
        /** The answer the describer gives, which changes only when a fact has held long enough. */
        bool settledEmptyPort = false;
        /** When the row started answering against a settled empty port; 0 while it is not. */
        uint32_t answeringSinceMs = 0;
        /** Written by the pass over the whole registry, so rows that left it can be dropped. */
        bool describedInThisPass = false;
    };

    // --- Which pass runs, and when --------------------------------------------------------------

    bool aPassIsRunning() const { return _phase != Phase::IDLE && _phase != Phase::DONE; }

    /** @brief Is the pass in flight - or the one still owed - the pass a person asked for? */
    bool aPersonAskedForThisPass() const
    {
        return _rescanRequested || (aPassIsRunning() && _pass == Pass::NAMED_PORT);
    }

    /** @brief Begin the window the ladder leaves the line alone for. See BOOT_SETTLE_MS. */
    void startTheSettleWindow()
    {
        _armedAtMs = nowMs();
        _settled = false;
    }

    /**
     * @brief May the ladder put a step on the line at this moment?
     *
     * Two things are allowed through the window: a pass a person asked for, because they are
     * standing in front of the module and the command that asked for it has already had the line;
     * and everything at all on a stand assembled without a clock, which has no window because it
     * has nothing to measure one with - the same answer freePortPassIsDue() gives such a stand.
     *
     * Latched once the window is over, so that the module does not fall back into it when the
     * millisecond counter wraps around, seven weeks in.
     */
    bool theLadderMayStepNow()
    {
        if (_settled || aPersonAskedForThisPass() || _arduino == nullptr)
        {
            return true;
        }

        // The boot sequence may let the ladder go before the window is over, never after it.
        if (_bootGates != nullptr && _bootGates->allows(BootGate::DISCOVERY))
        {
            _settled = true;
            sayTheDiscoveryRuns("boot sequence");
            return true;
        }

        _settled = (nowMs() - _armedAtMs) >= BOOT_SETTLE_MS;
        if (_settled)
        {
            sayTheDiscoveryRuns("boot window over");
        }
        return _settled;
    }

    /** @brief One line when an armed pass is let onto the line, and why - read off a boot's tape. */
    void sayTheDiscoveryRuns(const char *why)
    {
        if (!aPassIsRunning() || _logger == nullptr || _debugSystem == nullptr ||
            !_debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            return;
        }

        // On the serial port only, as every line of the boot sequence is.
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "BOOT_SEQ: DISCOVERY WAIT -> RUN (%s) at %lu ms", why,
                 (unsigned long)nowMs());
        _logger->println(DebugFeatures::SETUP_DEBUG, buffer, MqttLoggerMode::SerialOnly);
    }

    static bool stepBelongsToAFreePort(StartupScanStep::Kind kind)
    {
        return kind == StartupScanStep::PROBE_CANDIDATE ||
               kind == StartupScanStep::PROBE_PRESENCE ||
               kind == StartupScanStep::SCAN_CANDIDATE ||
               kind == StartupScanStep::PROBE_RANGE ||
               kind == StartupScanStep::WRITE_PORT_ADDRESS;
    }

    /**
     * @brief Start a pass over the free ports when one is owed, between two steps and never inside.
     *
     * Two things ask for one: the ScanSensors command, which asks about the one port it names, and
     * the clock, which asks for the cheap rung on every free port at its interval. Neither
     * interrupts a pass in progress - a request is honoured at the first moment the discovery has
     * nothing on the line.
     */
    void beginTheNextPassIfOneIsDue()
    {
        if (aPassIsRunning())
        {
            return;
        }

        if (_rescanRequested)
        {
            _rescanRequested = false;
            beginNamedPortPass(_rescanPin);
            return;
        }

        if (!freePortPassIsDue())
        {
            return;
        }

        beginFreePortPass();
    }

    /**
     * @brief Is the cheap rung owed a walk over the free ports?
     *
     * Only on a module that was allowed to discover: `noScanSensors` decides whether this module
     * asks the line anything at all on this boot, and a periodic pass would be exactly the
     * scanning traffic the flag exists to keep off the line. A restart that skips the discovery
     * therefore skips this too, until the next one that does not.
     *
     * Without a clock there is no period to measure, and the pass never comes due - which is what
     * a caller assembled by hand, such as a test, gets; globals.h wires one.
     */
    bool freePortPassIsDue()
    {
        // _didDiscover is the whole gate: it is false on a module nobody has armed at all and on
        // the boot that consumed the flag, and it is set by arm() alone.
        if (!_didDiscover || _arduino == nullptr)
        {
            return false;
        }

        // The flag rather than a zero time: millis() really is 0 for the first moments of a
        // module's life, and a zero read as "never yet" would put the first pass on the line at
        // every iteration of the control loop until the clock moved off it.
        const uint32_t now = static_cast<uint32_t>(_arduino->millis());
        if (_freePortPassRan && (now - _freePortPassEndedAtMs) < FREE_PORT_PROBE_INTERVAL_MS)
        {
            return false;
        }
        return true;
    }

    void beginFreePortPass()
    {
        _pass = Pass::FREE_PORTS;
        _phase = Phase::PLAN_SCAN;
        _finished = false;
        _adoptedInThisPass = 0;
        _stepsInThisPass = 0;
        _freePortPassCount++;
        // Asked once per pass, on the control loop, before a single step is built: the answer is
        // the registry's and not the line's, and the write it asks for rides the address phase this
        // pass already has - between two sweeps, like every other step of the discovery.
        noteTheRowsStillOnTheirFactoryAddress();
    }

    /**
     * @brief The pass a ScanSensors command leaves behind it, on the port that command named.
     *
     * What the port has just had is the full identification procedure - the command runs it
     * itself, on this pin, and the line serves a queued command before any step of the discovery.
     * So this pass does not run that procedure again: what it adds is the questions the procedure
     * does not ask twice. The cheap rung, because the port has been under power for the whole
     * length of the procedure and a device too slow to answer any pass of the module's own is
     * awake by now; and the presence poll, because the answer to "is anything there at all" is
     * what tells an empty port from one with something on it nobody can name.
     *
     * A second procedure here would cost the other port of its wire its power for the length of it
     * - the isolation of a model scan takes that port down - and would find what the one that just
     * ran did not.
     */
    void beginNamedPortPass(int pin)
    {
        _pass = Pass::NAMED_PORT;
        _phase = Phase::PLAN_SCAN;
        _finished = false;
        _adoptedInThisPass = 0;
        _stepsInThisPass = 0;
        _namedPin = pin;
        // The way back for a port the module gave up identifying: whatever the last pass concluded
        // about this one is forgotten, and the ladder starts from the top on it. What a sensor of
        // its wire was answering for is not forgotten but not consulted either - the note holds
        // off the cheap pass and nothing else, so a pass a person asked for puts every question
        // again whatever it says. See theWireAlreadyAnsweredFor().
        forgetPortGaveUpIdentification(pin);
        // And the same way back for a device that would not take an address of the module: the
        // person asking about this port is the one thing that makes the module try it again.
        forgetTheRowsOfThePortWouldNotMove(pin);
    }

    /**
     * @brief End a cheap pass that found nothing, where it started.
     *
     * No address to hand out, nothing new to prepare and no structure to send: what the module
     * knows after such a pass is what it knew before it, and the only thing that must not survive
     * it is the catalog handle the pass was holding.
     */
    void endFreePortPassWithNothingFound()
    {
        _phase = Phase::DONE;
        _finished = true;
        releaseHoldings();
        markPassEnded();
    }

    /**
     * @brief Start the interval of the cheap rung from the end of a pass, not from its beginning.
     *
     * A pass is line time - seconds of it for the cheap rung, minutes when the identification
     * procedure runs - and an interval measured from where a pass began would put the next one on
     * the line the moment a long one ended.
     */
    void markPassEnded()
    {
        _freePortPassEndedAtMs = (_arduino != nullptr) ? static_cast<uint32_t>(_arduino->millis()) : 0;
        _freePortPassRan = true;
    }

    /** @brief The clock, or zero on a stand assembled without one. */
    uint32_t nowMs() const
    {
        return (_arduino != nullptr) ? static_cast<uint32_t>(_arduino->millis()) : 0;
    }

    /** @brief What the pass in progress is, for the log. */
    const char *passName() const
    {
        if (_pass == Pass::FREE_PORTS)
        {
            return "cheap";
        }
        return (_pass == Pass::NAMED_PORT) ? "hand-asked" : "full";
    }

    // --- Planning, on the control loop ----------------------------------------------------------

    /**
     * @brief The next step of the sequence, or false when the sequence is over.
     *
     * The phases run in the order the boot scan ran them: verify what is stored, scan what is
     * free, resolve the addresses that collide, then prepare. Each phase is planned when it is
     * reached and from the registry as it is then, because the phase before it may have adopted
     * a sensor.
     */
    bool buildNextStep(StartupScanStep &step)
    {
        while (true)
        {
            switch (_phase)
            {
            case Phase::PLAN_VERIFY:
                planVerify();
                _phase = Phase::VERIFY;
                break;

            case Phase::VERIFY:
                if (buildVerifyStep(step))
                {
                    return true;
                }
                _phase = Phase::PLAN_SCAN;
                break;

            case Phase::PLAN_SCAN:
                planScan();
                _phase = Phase::SCAN;
                break;

            case Phase::SCAN:
                if (buildCandidateStep(step))
                {
                    return true;
                }
                if (_scanWaitingForWakeUp)
                {
                    // The ports have their power and nothing is asked of them yet. The pass is
                    // still running - it owes the ports their questions and their power back -
                    // so the phase stays where it is and the next call finds the wait over.
                    return false;
                }
                if (_pass != Pass::WHOLE_MODULE && _adoptedInThisPass == 0 &&
                    _neighboursToMove.empty() && _rowsOnAFactoryAddress.empty())
                {
                    endFreePortPassWithNothingFound();
                    return false;
                }
                _phase = Phase::PLAN_ADDRESSES;
                break;

            case Phase::PLAN_ADDRESSES:
                if (_pass != Pass::WHOLE_MODULE && _adoptedInThisPass == 0)
                {
                    planTheMovesThisPassOwes();
                }
                else
                {
                    planAddresses();
                }
                _phase = Phase::ADDRESSES;
                break;

            case Phase::ADDRESSES:
                if (_pinToTakeDown >= 0)
                {
                    // A write of this phase was refused and the device found on that port is not
                    // kept: its port goes down before the next write or the preparation - see
                    // dropTheFindThatCannotBeParted().
                    const int pin = _pinToTakeDown;
                    _pinToTakeDown = -1;
                    return buildTheStepThatTakesDown(pin, step);
                }
                if (_addressCursor < _addressWrites.size() && thePortsAreStillComingBack())
                {
                    // The rungs before this one take the power off the ports sharing the wire of
                    // the one they ask, and a device that has just had its power back cannot
                    // answer anything for ten seconds. An address write is a write followed by a
                    // ping of the new address, and a ping nobody answers is a write the module
                    // gives up on and a sensor left where it was. So the phase waits, exactly as
                    // the scan waits for the ports it switched on.
                    return false;
                }
                if (buildAddressStep(step))
                {
                    return true;
                }
                _phase = Phase::PREPARE;
                break;

            case Phase::PREPARE:
                // The phase stays PREPARE while the preparation is on the line: isRunning() is
                // read as "the discovery still has the line", and the preparation is part of it.
                // DONE is reached when its answer comes back, in applyStepOutcome().
                if (buildPrepareStep(step))
                {
                    return true;
                }
                _phase = Phase::DONE;
                finish();
                return false;

            default:
                return false;
            }
        }
    }

    void planVerify()
    {
        _verifyUids.clear();
        _rowsThatAnsweredThePing.clear();
        _verifyCursor = 0;

        if (_sensorRepository == nullptr)
        {
            return;
        }

        // Uids and not entities: the discovery outlives the boot now, and holding the whole
        // registry for the length of it would be a second copy of it for minutes. The entity is
        // read back one at a time, when its turn comes.
        const std::vector<SensorEntity> stored = _sensorRepository->getAll();
        _verifyUids.reserve(stored.size());
        _rowsThatAnsweredThePing.reserve(stored.size());
        for (size_t i = 0; i < stored.size(); i++)
        {
            _verifyUids.push_back(stored[i].uid);
        }
        log("STARTUP_SCAN: verifying %zu stored sensor(s)", _verifyUids.size());
    }

    bool buildVerifyStep(StartupScanStep &step)
    {
        while (_verifyCursor < _verifyUids.size())
        {
            const std::string uid = _verifyUids[_verifyCursor];
            _verifyCursor++;

            if (_sensorRepository == nullptr)
            {
                continue;
            }

            Utils::Optional<SensorEntity> stored = _sensorRepository->get(uid.c_str());
            if (!stored)
            {
                // Removed while the discovery was walking the registry - by a command, or by the
                // step before this one. Nothing to ping.
                continue;
            }

            step = StartupScanStep();
            step.kind = StartupScanStep::VERIFY_KNOWN;
            step.entity = stored.value();
            step.pin = step.entity.pin;
            return true;
        }
        return false;
    }

    /**
     * @brief The ports nothing occupies, and the catalog to try on them.
     *
     * The occupancy is read from the registry as the verification left it, which is what the boot
     * scan did too: a port whose sensor answered is not scanned again.
     */
    void planScan()
    {
        _scanPins.clear();
        _scanPinCursor = 0;
        _pinsLeftToTheLadder.clear();
        _pinsOwedTheCheapRungAgain.clear();
        _pinsHeldDarkUntilTheirWalk.clear();
        _pinToTakeDown = -1;
        _cheapRungAskedEverywhere = false;
        _catalog.reset();
        _freePortsPowered = false;
        _freePortsReleased = false;
        _freePortsOweAWakeUp = false;
        _freePortsPoweredAtMs = 0;
        _scanWaitingForWakeUp = false;
        _portsOweTheirPowerBack = false;
        _portsAheadWentDark = false;
        _windowOpen = false;
        _windowHeard = false;
        _windowScanned = false;

        if (_moduleConfig == nullptr || _sensorTypeRepository == nullptr || _sensorRepository == nullptr)
        {
            _scanTypeCursor = 0;
            _pinSettled = false;
            return;
        }

        const std::vector<SensorEntity> existing = _sensorRepository->getAll();
        const int sensorPinCount = _moduleConfig->getSensorPinCount();
        // One entry per port of the module and no more, taken once: the list is what the pass
        // walks, and on a cheap pass it is rebuilt every interval. A pass a person asked for walks
        // the one port they named and no other - the ports they did not name have working sensors
        // on them as often as not, and every question this pass asks costs them their power.
        _scanPins.reserve((_pass == Pass::NAMED_PORT || sensorPinCount <= 0)
                              ? (size_t)1
                              : (size_t)sensorPinCount);
        for (int pin = 0; pin < sensorPinCount; pin++)
        {
            if (_pass == Pass::NAMED_PORT && pin != _namedPin)
            {
                continue;
            }
            if (isPinOccupied(pin, existing, "Analog") || isPinOccupied(pin, existing, "I2C") ||
                isPinOccupied(pin, existing, "ModBus"))
            {
                logPassProgress("STARTUP_SCAN: pin %d is occupied by an existing sensor", pin);
                continue;
            }
            if (_pass == Pass::FREE_PORTS && thePortIsLeftDark(pin))
            {
                continue;
            }
            _scanPins.push_back(pin);
        }
        if (_pass == Pass::WHOLE_MODULE)
        {
            // The ports the cheap rung leaves unnamed, collected while every port is asked it, those
            // of them asked it again, and those held dark until then: at most every port the pass
            // walks, taken once.
            _pinsLeftToTheLadder.reserve(_scanPins.size());
            _pinsOwedTheCheapRungAgain.reserve(_scanPins.size());
            _pinsHeldDarkUntilTheirWalk.reserve(_scanPins.size());
        }
        if (_pass != Pass::NAMED_PORT)
        {
            askThePortsThatLeaveAnAddressOutFirst();
        }

        // Held for the whole scan, which is minutes on the line: the catalog this pass walks is
        // the one it started from, whole, whatever the module stores in the meantime.
        _catalog = _sensorTypeRepository->catalogSnapshot();
        _moduleUid = _moduleConfig->getModuleUid();
        startPinAtTheCursor();
        logPassProgress("STARTUP_SCAN: %s pass over %zu free pin(s) against %zu sensor type(s)",
                        passName(), _scanPins.size(), _catalog ? _catalog->size() : (size_t)0);
    }

    /**
     * @brief Put the port at the cursor at the top of the ladder, which is where every port starts.
     *
     * Every free port of every pass, whatever an earlier pass concluded about it: the cheap rung
     * names an address and costs the ports sharing its wire nothing, so there is no port it is not
     * worth asking. What an earlier conclusion decides is the rung *after* it - see
     * presencePollIsWorthAsking(). A port the restart's pass comes back to, once every free port has
     * been asked the cheap rung, starts on the rung after it.
     */
    void startPinAtTheCursor()
    {
        _scanTypeCursor = 0;
        _cheapRangeCursor = 0;
        _cheapRungLeftAnAddressOut = false;
        _pinSettled = false;
        _rung = theRungAPortStartsOn();
    }

    /**
     * @brief The cheap rung - or, once the restart's pass has asked it of every free port, the poll,
     *        unless the port is owed the cheap rung again (see askThePortsOfItsWireTheCheapRungAgain()).
     */
    Rung theRungAPortStartsOn() const
    {
        if (!_cheapRungAskedEverywhere)
        {
            return Rung::CHEAP;
        }
        const bool askedAgain = _scanPinCursor < _pinsLeftToTheLadder.size() &&
                                isAmongPins(_pinsOwedTheCheapRungAgain, _pinsLeftToTheLadder[_scanPinCursor]);
        return askedAgain ? Rung::CHEAP : Rung::PRESENCE;
    }

    /**
     * @brief The ports the walk goes over: every free port - and, on the restart's pass once each
     *        has been asked the cheap rung, the ones it left unnamed.
     */
    const std::vector<int> &pinsOfTheWalk() const
    {
        return _cheapRungAskedEverywhere ? _pinsLeftToTheLadder : _scanPins;
    }

    /**
     * @brief Is a port left to walk? On the restart's pass the end of the first walk is where the
     *        second one begins: back to the first port the cheap rung left unnamed.
     */
    bool aPortIsLeftToWalk()
    {
        if (_scanPinCursor < pinsOfTheWalk().size())
        {
            return true;
        }
        if (_pass != Pass::WHOLE_MODULE || _cheapRungAskedEverywhere || _pinsLeftToTheLadder.empty())
        {
            return false;
        }

        _cheapRungAskedEverywhere = true;
        // The rest of the ladder goes over the ports in their own order, whatever order the cheap
        // rung asked them in: see askThePortsThatLeaveAnAddressOutFirst().
        std::sort(_pinsLeftToTheLadder.begin(), _pinsLeftToTheLadder.end());
        _scanPinCursor = 0;
        startPinAtTheCursor();
        logPassProgress("STARTUP_SCAN: every free pin has been asked the cheap questions - %zu pin(s) "
                        "go on to the rest",
                        _pinsLeftToTheLadder.size());
        return true;
    }

    /**
     * @brief Put the ports whose cheap rung will leave an address out at the front of the cheap
     *        rung, in the order of the ports, the others after them in theirs.
     *
     * Such a port shares its wire with a stored sensor that holds an address of the module's range,
     * which the cheap rung does not ask on the live wire (see buildRangeStepOfTheCheapRung()) - on
     * the restart's pass one that answered the pass's ping, on a periodic pass any. Whatever answers
     * there on the port collides with that sensor from the moment the pass has given the free ports
     * their power and they have come up, and it goes on until the port is taken down after its own
     * cheap rung (buildTheStepThatTakesDown()) - so the earlier that rung, the shorter the collision.
     * Asked last, the port waits for every other port's cheap rung, and a cheap find among them adds
     * its confirming scan and the warm-up after it. Asked first, it waits for none of them. The ports
     * asked after it lose that one port's cheap rung - seconds.
     *
     * Moved in place, with no other storage: a port is rotated to the end of the ones moved before it.
     */
    void askThePortsThatLeaveAnAddressOutFirst()
    {
        size_t moved = 0;
        for (size_t i = 0; i < _scanPins.size(); i++)
        {
            if (!aStoredSensorOfItsWireHoldsAnAddressTheCheapRungLeavesOut(_scanPins[i]))
            {
                continue;
            }
            std::rotate(_scanPins.begin() + moved, _scanPins.begin() + i, _scanPins.begin() + i + 1);
            moved++;
        }
    }

    /**
     * @brief Does a stored sensor of this port's wire hold an address of the module's range that the
     *        cheap rung leaves out - see theCheapRungLeavesOut()? The question
     *        buildRangeStepOfTheCheapRung() asks of an address before it leaves it out; it asks it
     *        only of the addresses it has a scannable model to ask at, and with none in the catalog
     *        the order of the ports is all this moves.
     */
    bool aStoredSensorOfItsWireHoldsAnAddressTheCheapRungLeavesOut(int pin)
    {
        for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
        {
            Utils::Optional<SensorEntity> holder = storedNeighbourHoldingTheAddress(pin, address);
            if (holder && theCheapRungLeavesOut(holder.value()))
            {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Does the cheap rung leave out the address this stored sensor of the paired port holds?
     *
     * On the restart's pass the address of a sensor that answered the pass's ping: it would answer
     * for itself, and a silent one may have its device anywhere. On a periodic pass the address of
     * any such sensor - nothing has pinged the stored rows, and the confirming scan of an answer
     * would take a working sensor's power at every pass.
     */
    bool theCheapRungLeavesOut(const SensorEntity &holder) const
    {
        return _pass == Pass::FREE_PORTS || answeredThisPass(holder.uid);
    }

    /**
     * @brief The free ports the restart's pass has still to ask something: the one at the cursor,
     *        the ones after it, and - while the cheap rung is being walked - the ones it has left to
     *        the rest of the ladder. A port somebody has claimed since is not one of them, and
     *        neither is one the pass holds dark until its own walk comes - see
     *        buildTheStepThatTakesDown().
     */
    void addThePortsStillToBeAsked(std::vector<int> &pins)
    {
        const std::vector<int> &walk = pinsOfTheWalk();
        pins.reserve(walk.size() + (_cheapRungAskedEverywhere ? 0 : _pinsLeftToTheLadder.size()));
        for (size_t i = _scanPinCursor; i < walk.size(); i++)
        {
            if (i == _scanPinCursor ||
                (!portClaimedSinceThePassBegan(walk[i]) && !isAmongPins(_pinsHeldDarkUntilTheirWalk, walk[i])))
            {
                pins.push_back(walk[i]);
            }
        }
        for (size_t i = 0; !_cheapRungAskedEverywhere && i < _pinsLeftToTheLadder.size(); i++)
        {
            if (!portClaimedSinceThePassBegan(_pinsLeftToTheLadder[i]) &&
                !isAmongPins(_pinsHeldDarkUntilTheirWalk, _pinsLeftToTheLadder[i]))
            {
                pins.push_back(_pinsLeftToTheLadder[i]);
            }
        }
    }

    /**
     * @brief Take one free port's power down, and nothing else: no frames.
     *
     * Two kinds of port are taken down this way, and both for the same reason - a stored sensor of
     * their wire answers at an address a device on them may hold too, and while both are up every
     * read of that sensor is answered twice at once, which is no answer at all:
     *
     *  - a port whose cheap rung had to leave out an address a stored sensor of its wire holds (see
     *    buildRangeStepOfTheCheapRung()), when anything is asked after it. On the restart's pass that
     *    is anything asked before its window - the rest of the cheap rung, or the windows of the
     *    ports left to the ladder before it. Whatever answers there on this port can only be found
     *    by the port's window, which asks it with the stored sensor's port dark; until then the port
     *    is dark itself, and it gets its power back - and its warm-up - when the walk comes back to
     *    it, not with the ports that the steps before it give their power back to. On a periodic
     *    pass, which opens no window, it is any port asked after it, and the port stays dark to the
     *    end of the pass;
     *  - a port whose device the restart's pass will not keep: see dropTheFindThatCannotBeParted().
     */
    bool buildTheStepThatTakesDown(int pin, StartupScanStep &step)
    {
        logPassProgress("STARTUP_SCAN: pin %d is taken down - a sensor of its wire answers at an address "
                        "a device on it may hold too",
                        pin);
        step = StartupScanStep();
        step.kind = StartupScanStep::RELEASE_FREE_PORTS;
        step.pin = pin;
        step.pins.reserve(1);
        step.pins.push_back(pin);
        return true;
    }

    static void forgetPin(std::vector<int> &pins, int pin)
    {
        for (size_t i = 0; i < pins.size(); i++)
        {
            if (pins[i] == pin)
            {
                pins.erase(pins.begin() + i);
                return;
            }
        }
    }

    /**
     * @brief Is the broadcast poll worth what it costs, for this port and this pass?
     *
     * The poll is the one question of the ladder that names no address, so it is the one that has
     * to isolate: the ports sharing this one's wire lose their power for its length. A restart
     * pays that once per free port and a person's ScanSensors once for the port it named. A cheap
     * pass, which comes round twice a minute for ever, asks it of one port only - the one it gave
     * up identifying,
     * to find out whether the thing it could not name is still there - and not even that one when
     * the wire carries a sensor the module reads: power-cycling a working sensor twice a minute to
     * keep a note about a device nobody can name is not a trade this module makes.
     */
    bool presencePollIsWorthAsking(int pin)
    {
        if (_pass != Pass::FREE_PORTS)
        {
            return true;
        }
        if (!portGaveUpIdentification(pin))
        {
            return false;
        }
        return !theLineCarriesAStoredSensor(pin);
    }

    void advanceToNextPin()
    {
        _scanPinCursor++;
        startPinAtTheCursor();
    }

    /** @brief This port has its answer for this pass: no further rung, no empty-port record. */
    void settleCurrentPin()
    {
        _pinSettled = true;
        _rung = Rung::IDENTIFY;
        _scanTypeCursor = _catalog ? _catalog->size() : 0;
    }

    bool buildCandidateStep(StartupScanStep &step)
    {
        if (!_catalog)
        {
            return false;
        }

        if (!_freePortsPowered)
        {
            // Nothing is asked of a dark port. The whole pass opens with this, once, and the
            // ports it switches on are given back at the end of it.
            _freePortsPowered = true;
            if (!_scanPins.empty())
            {
                _scanWaitingForWakeUp = true;
                step = StartupScanStep();
                step.kind = StartupScanStep::POWER_FREE_PORTS;
                step.pins = _scanPins;
                return true;
            }
            _freePortsReleased = true;
        }

        if (!_rowToPartFromItsWire.empty())
        {
            // Before a window closes and before the free ports get their power back: see
            // partItFromTheRowOfItsWire().
            const std::string uid = _rowToPartFromItsWire;
            std::string().swap(_rowToPartFromItsWire);
            if (buildTheMoveOf(uid, step))
            {
                return true;
            }
        }

        if (_pinToTakeDown >= 0)
        {
            // Before a window closes too: see dropTheFindThatCannotBeParted().
            const int pin = _pinToTakeDown;
            _pinToTakeDown = -1;
            return buildTheStepThatTakesDown(pin, step);
        }

        if (!_storedRowToMoveOffItsWire.empty() && !_windowOpen)
        {
            // Once the window that found the device has given the stored row's port back: see
            // partItFromTheRowOfItsWire().
            const std::string uid = _storedRowToMoveOffItsWire;
            std::string().swap(_storedRowToMoveOffItsWire);
            if (buildTheMoveOf(uid, step))
            {
                return true;
            }
        }

        if (theFreePortsAreStillWakingUp())
        {
            _scanWaitingForWakeUp = true;
            return false;
        }
        _scanWaitingForWakeUp = false;

        const std::vector<SensorType> &types = *_catalog;

        while (aPortIsLeftToWalk())
        {
            const int pin = pinsOfTheWalk()[_scanPinCursor];

            if (_windowOpen && (_rung == Rung::CLOSE || portClaimedSinceThePassBegan(pin)))
            {
                // The port's window is closed before anything else happens to the port: its wire
                // has been dark since the window opened, and whatever the walk does next - moves on,
                // identifies, calls the port empty - is done with the wire given back.
                step = StartupScanStep();
                step.kind = StartupScanStep::RELEASE_LINE;
                step.pin = pin;
                return true;
            }

            // Asked again before every single step, not once when the pass was planned. The
            // discovery takes minutes now, and it shares them with the control loop: a person
            // adding a sensor through the app, or a ScanSensors command, can claim this port
            // while the walk is still on it. Carrying on would put a broadcast re-address on a
            // device somebody has just configured and overwrite its row with a scan's guess.
            if (portClaimedSinceThePassBegan(pin))
            {
                // Said only when somebody else did the claiming. A port this pass adopted a sensor
                // on is claimed too, and reading "leaving it alone" under the line that says the
                // sensor was saved is how a person on site is told the opposite of what happened.
                if (!thisPassClaimedThePortItself(pin))
                {
                    log("STARTUP_SCAN: pin %d was claimed while the discovery walked it - leaving it alone",
                        pin);
                }
                _pinSettled = true;
                advanceToNextPin();
                continue;
            }

            if (_rung == theRungAPortStartsOn() && _scanTypeCursor == 0 && _cheapRangeCursor == 0)
            {
                // A port is not asked anything until it has had its power for PORT_WAKE_UP_MS,
                // and the steps before this one may have taken it away: the window of the port
                // sharing its wire darkens it for the length of the window, the presence poll of
                // that port for the length of the poll, and a model scan of that port darkens it
                // and leaves it so. So once a step has done any of it, every port the pass
                // has still to ask gets its power back - a step with no frames in it - and the walk
                // is left alone until whatever is on them has come up. Every one of them and not
                // only this one: the cheap rung asks the ports one after the other with nothing
                // between them that would give the next one its power, and the ports it leaves to
                // the rest are asked after all of them.
                // The same at the start of both walks of a port, the cheap rung's and the rest's -
                // and for a port the pass has held dark since its cheap rung, whatever the steps
                // before this one did: see buildTheStepThatTakesDown().
                const bool heldDark = isAmongPins(_pinsHeldDarkUntilTheirWalk, pin);
                if (_portsOweTheirPowerBack || heldDark)
                {
                    if (heldDark)
                    {
                        forgetPin(_pinsHeldDarkUntilTheirWalk, pin);
                    }
                    step = StartupScanStep();
                    step.kind = StartupScanStep::POWER_FREE_PORTS;
                    addThePortsStillToBeAsked(step.pins);
                    return true;
                }
                if (thePortsAheadAreStillComingBack())
                {
                    _scanWaitingForWakeUp = true;
                    return false;
                }
            }

            if (_rung == Rung::PRESENCE)
            {
                // One poll, and what it answers decides between an empty port and the
                // identification procedure - see applyPresence(), which is what moves this port
                // off this rung. Until then the step is the one thing this port has to offer: a
                // step already built is re-offered as it stands rather than rebuilt, so asking
                // twice is not a thing that can happen.
                step = StartupScanStep();
                step.kind = StartupScanStep::PROBE_PRESENCE;
                step.pin = pin;
                if (_pass == Pass::WHOLE_MODULE && _rawBus != nullptr)
                {
                    // The restart's pass makes the poll's isolation the window for the rest of
                    // what a silent port is asked - see buildWindowStep(). The catalog travels as
                    // the handle this pass holds.
                    step.catalog = _catalog;
                }
                return true;
            }

            if (_rung == Rung::RANGE || _rung == Rung::WRITE)
            {
                if (buildWindowStep(step, pin))
                {
                    return true;
                }
                continue;
            }

            if (_rung == Rung::CHEAP && buildRangeStepOfTheCheapRung(step, pin))
            {
                return true;
            }

            while (_scanTypeCursor < types.size())
            {
                const SensorType &sensorType = types[_scanTypeCursor];
                _scanTypeCursor++;

                if (!sensorType.canScan || !sensorType.isSupported)
                {
                    logPassProgress("STARTUP_SCAN: skipping sensor type %s (canScan=%s, isSupported=%s)",
                                    sensorType.name.c_str(),
                                    sensorType.canScan ? "true" : "false",
                                    sensorType.isSupported ? "true" : "false");
                    continue;
                }

                if (!sensorType.isI2CSensor() && !sensorType.isModbusSensor())
                {
                    continue;
                }

                if (_rung == Rung::CHEAP && sensorType.isModbusSensor() &&
                    theWireAlreadyAnsweredFor(pin, sensorType.modbusDeviceAddress))
                {
                    // The sensor of the paired port answers at this address, and the module has
                    // paid the confirming scan once to find that out. Asking again would cost
                    // that sensor its power for the same answer.
                    logPassProgress("STARTUP_SCAN: pin %d is not asked for %s at 0x%02X - the sensor "
                                    "sharing its wire answers there",
                                    pin, sensorType.name.c_str(), sensorType.modbusDeviceAddress);
                    continue;
                }

                step = StartupScanStep();
                step.kind = (_rung == Rung::CHEAP) ? StartupScanStep::PROBE_CANDIDATE
                                                   : StartupScanStep::SCAN_CANDIDATE;
                step.pin = pin;
                step.entity = buildCandidateEntity(sensorType, pin);
                return true;
            }

            if (_rung == Rung::CHEAP && presencePollIsWorthAsking(pin))
            {
                if (_pass == Pass::WHOLE_MODULE && !_cheapRungAskedEverywhere)
                {
                    // The restart's pass asks every free port this rung before it asks any port
                    // the next one: the rest is a window, a minute and more of a port, and a
                    // sensor at its factory address on a port walked later would wait for all of
                    // them. This port comes back for the rest once every free port has been asked.
                    _pinsLeftToTheLadder.push_back(pin);
                    const bool holdItDark = _cheapRungLeftAnAddressOut;
                    advanceToNextPin();
                    // Only when something is asked before its window: the rest of the cheap rung, or
                    // the window of a port left to the ladder before it.
                    if (holdItDark && (_scanPinCursor < _scanPins.size() || _pinsLeftToTheLadder.size() > 1))
                    {
                        _pinsHeldDarkUntilTheirWalk.push_back(pin);
                        return buildTheStepThatTakesDown(pin, step);
                    }
                    continue;
                }
                // No model answered at its own address - nor, on a pass that asks the range, at the
                // range. Whether anything is there at all is the next question, and it is one
                // question rather than one per model.
                _scanTypeCursor = 0;
                _rung = Rung::PRESENCE;
                continue;
            }

            if (_rung == Rung::CHEAP && _pass == Pass::FREE_PORTS && _cheapRungLeftAnAddressOut)
            {
                // A periodic pass has no window and no rest of the ladder to ask the port, so nothing
                // keeps it up: it goes dark at once, as the restart's pass does with a port it holds
                // for its window. Left up until the end of the pass, whatever answers on it at the
                // held address would collide with the stored sensor of its wire for as long as the
                // ports after it are asked. With none after it, the end of the pass takes it down.
                const bool somethingIsAskedAfterIt = _scanPinCursor + 1 < _scanPins.size();
                advanceToNextPin();
                if (somethingIsAskedAfterIt)
                {
                    return buildTheStepThatTakesDown(pin, step);
                }
                continue;
            }

            // Only a port that was heard is one the module gave up on, and this rung is reached
            // only by a port the presence poll heard - see applyPresence(). A port that settled on
            // an earlier rung leaves the loop through settleCurrentPin() instead.
            if (_rung == Rung::IDENTIFY && !_pinSettled)
            {
                rememberPortGaveUpIdentification(pin);
            }

            if (_pass == Pass::WHOLE_MODULE)
            {
                // Only the pass that took the port down the whole ladder may say the port is
                // empty. A cheap pass asked one cheap question and got no answer, which is what
                // an empty port and a device that was not ready both look like - and the record
                // it would add is one the structure already carries.
                closePin(pin);
            }
            advanceToNextPin();
        }

        return buildReleaseStep(step);
    }

    /**
     * @brief The cheap rung's range, before the factory addresses: the module's range, address by
     *        address, every scannable ModBus model at each.
     *
     * A device the module once gave an address answers there and nowhere else, and that is what a
     * free port most often has on it: a sensor moved from another port or another module, or one
     * whose row went with the registry. On the restart's pass the port's window asks the same
     * addresses, but only once the cheap rung has been asked of every free port and the windows of
     * the ports before it are over; a pass after it opens no window at all. Asked here, the device
     * is adopted in the seconds the cheap rung takes, by the same transaction as a factory address -
     * one read, then the confirming scan, then the adoption at the address it answered at - and its
     * port gets no window and no write.
     *
     * The wire is live here, so an address a sensor of the paired port holds is not asked when that
     * sensor answered this pass - the ping of the stored rows, or the scan that adopted it: it would
     * answer for itself, and the confirming scan that proves it takes that sensor's power for
     * nothing. The window asks it, with the paired port dark. A stored row that was silent
     * to its ping is a row whose device may be anywhere - moved onto this very port, most often - so
     * its address is asked like any other; should the row's own device answer after all, that is the
     * echo the confirming scan finds, at the price of one scan.
     *
     * A pass after the restart has no ping of the stored rows behind it, and its confirming scan
     * would take the power of a working sensor at every pass: it leaves out every address a stored
     * sensor of the paired port holds, whether or not that sensor has answered. What is plugged into
     * this port at such an address is found by the next restart's ping and window. Until then it
     * answers the stored sensor's reads too, so the periodic pass asks such a port first and takes
     * it down as soon as its cheap rung is over - see askThePortsThatLeaveAnAddressOutFirst().
     *
     * Not on the pass a person asked for: the ScanSensors command that leaves it behind has just
     * walked the module's range on that port with the identification procedure.
     *
     * @return true with a step to hand out; false once the range is over for this port
     */
    bool buildRangeStepOfTheCheapRung(StartupScanStep &step, int pin)
    {
        if (_pass == Pass::NAMED_PORT || !_catalog || _catalog->empty())
        {
            return false;
        }

        const std::vector<SensorType> &types = *_catalog;
        const size_t addresses = static_cast<size_t>(MODBUS_ADDRESS_MAX - MODBUS_ADDRESS_MIN + 1);
        while (_cheapRangeCursor < addresses * types.size())
        {
            const size_t offset = _cheapRangeCursor / types.size();
            const size_t model = _cheapRangeCursor % types.size();
            const int address = MODBUS_ADDRESS_MIN + static_cast<int>(offset);
            _cheapRangeCursor++;
            if (!isAScannableModbusModel(types[model]))
            {
                continue;
            }

            Utils::Optional<SensorEntity> holder = storedNeighbourHoldingTheAddress(pin, address);
            if (holder && theCheapRungLeavesOut(holder.value()))
            {
                logPassProgress("STARTUP_SCAN: pin %d is not asked 0x%02X on the live wire - sensor %s "
                                "of pin %d holds it; the window asks it",
                                pin, address, holder.value().uid.c_str(), holder.value().pin);
                // The rest of this address's models too: the same sensor answers each of them.
                _cheapRangeCursor = (offset + 1) * types.size();
                _cheapRungLeftAnAddressOut = true;
                continue;
            }
            if (holder && isTheFirstScannableModel(types, model))
            {
                logPassProgress("STARTUP_SCAN: pin %d is asked 0x%02X on the live wire - sensor %s of pin "
                                "%d holds it and was silent to this pass's ping",
                                pin, address, holder.value().uid.c_str(), holder.value().pin);
            }

            step = StartupScanStep();
            step.kind = StartupScanStep::PROBE_CANDIDATE;
            step.pin = pin;
            step.entity = buildCandidateEntity(types[model], pin);
            step.entity.modbusDeviceAddress = address;
            return true;
        }
        return false;
    }

    /**
     * @brief The next step of the open window of this port, one catalog model at a time.
     *
     * A window is the poll that opened it, then every scannable ModBus model of the catalog read
     * at every address of the module's range (PROBE_RANGE), then - on a port nothing has answered
     * at all - every such model's address writes (WRITE_PORT_ADDRESS), and the step that closes
     * it. One model per step, which is what bounds the line time of a step whatever the size of
     * the catalog: the sweep and the other wire get the line back between two of them. What the
     * steps do not get back between them is this wire - it stays dark from the poll to the close,
     * so that nothing but what is on this port can hear a question or a write.
     *
     * @return true with a step to hand out; false when the rung is over and the walk has moved it
     *         on (the caller goes round again)
     */
    bool buildWindowStep(StartupScanStep &step, int pin)
    {
        const std::vector<SensorType> &types = *_catalog;
        while (_scanTypeCursor < types.size())
        {
            const size_t model = _scanTypeCursor;
            _scanTypeCursor++;
            if (!isAScannableModbusModel(types[model]))
            {
                continue;
            }

            step = StartupScanStep();
            step.kind = (_rung == Rung::RANGE) ? StartupScanStep::PROBE_RANGE
                                               : StartupScanStep::WRITE_PORT_ADDRESS;
            step.pin = pin;
            step.catalog = _catalog;
            step.model = static_cast<int>(model);
            step.entity.uid = candidateUid(pin);
            step.entity.pin = pin;
            return true;
        }

        _scanTypeCursor = 0;
        if (_rung == Rung::RANGE && !_windowHeard && THE_WINDOW_WRITES_THE_PORT_ADDRESS)
        {
            // Nothing answered the poll or any address of the range: the port's address is
            // written, model by model. A port that did answer goes to the identification rung
            // after the window, and that procedure makes these same writes and the rest of the
            // identification procedure's.
            _rung = Rung::WRITE;
            return false;
        }
        _rung = Rung::CLOSE;
        return false;
    }

    /**
     * @brief Give the free ports back the darkness the pass found them in.
     *
     * Every port the pass walked, except one somebody has claimed since - a port with a sensor on
     * it is not free any more, and the preparation is what arranges its power. A pass that walked
     * no port owes nothing.
     */
    bool buildReleaseStep(StartupScanStep &step)
    {
        if (!_freePortsPowered || _freePortsReleased)
        {
            return false;
        }
        _freePortsReleased = true;

        std::vector<int> released = _scanPins;
        for (size_t i = released.size(); i > 0; i--)
        {
            if (portClaimedSinceThePassBegan(released[i - 1]))
            {
                released.erase(released.begin() + (i - 1));
            }
        }

        if (released.empty())
        {
            return false;
        }

        step = StartupScanStep();
        step.kind = StartupScanStep::RELEASE_FREE_PORTS;
        step.pins = released;
        return true;
    }

    /**
     * @brief Is the pass still owed the seconds the ports it switched on need to come up?
     *
     * Only the pass that had to switch a port on waits: _freePortsPoweredAtMs is left at zero when
     * every port already had its power, and there is nothing to wait for on a stand without a
     * clock either.
     */
    bool theFreePortsAreStillWakingUp() const
    {
        if (!_freePortsOweAWakeUp || _arduino == nullptr)
        {
            return false;
        }
        return (nowMs() - _freePortsPoweredAtMs) < PORT_WAKE_UP_MS;
    }

    /**
     * @brief Remember that a step of this pass has just taken the power of the ports sharing the
     *        wire of the port it asked.
     *
     * The model scan and the presence poll both isolate the ports of one wire, and the poll gives
     * the power straight back. What neither can give back is the warm-up: a sensor needs its ten
     * seconds of power before it answers anything, and until then it is a sensor nothing can be
     * written to or verified.
     */
    void noteTheOtherPortsLostTheirPower()
    {
        _portsLostTheirPowerAtMs = nowMs();
        _portsLostTheirPower = true;
    }

    /**
     * @brief Remember that a step of this pass may have left the ports it has not walked yet dark,
     *        or only just lit again - see the start of a port's walk in buildCandidateStep().
     */
    void noteThePortsAheadMayBeDark()
    {
        _portsOweTheirPowerBack = true;
        _portsAheadWentDark = true;
        _portsAheadWentDarkAtMs = nowMs();
    }

    /** @brief Are the ports this pass has not walked yet still owed the warm-up of their power? */
    bool thePortsAheadAreStillComingBack() const
    {
        if (!_portsAheadWentDark || _arduino == nullptr)
        {
            return false;
        }
        return (nowMs() - _portsAheadWentDarkAtMs) < PORT_WAKE_UP_MS;
    }

    /** @brief Is a port this walk has still to ask wired to the UART of the one at its cursor? */
    bool aPortAheadSharesTheWireOf(int pin) const
    {
        const int line = lineOfPort(pin);
        const std::vector<int> &walk = pinsOfTheWalk();
        for (size_t i = _scanPinCursor + 1; line >= 0 && i < walk.size(); i++)
        {
            if (lineOfPort(walk[i]) == line)
            {
                return true;
            }
        }
        return false;
    }

    /** @brief Is another port of the module wired to this one's UART - one a window darkens? */
    bool theWireHasAnotherPort(int pin) const
    {
        const int line = lineOfPort(pin);
        for (size_t other = 0; line >= 0 && other < _lineOfPort.size(); other++)
        {
            if (static_cast<int>(other) != pin && _lineOfPort[other] == line)
            {
                return true;
            }
        }
        return false;
    }

    /** @brief Are the ports this pass took down still owed their warm-up? */
    bool thePortsAreStillComingBack() const
    {
        if (!_portsLostTheirPower || _arduino == nullptr)
        {
            return false;
        }
        return (nowMs() - _portsLostTheirPowerAtMs) < PORT_WAKE_UP_MS;
    }

    /**
     * @brief Does the registry hold what a read of storage that did not come back left - nothing -
     *        rather than the stored rows? Control loop only, as every read of the registry here.
     */
    bool theRegistryDidNotReadBack()
    {
        return _sensorRepository != nullptr && _sensorRepository->storedSensorsUnread();
    }

    /**
     * @brief Does anything hold this port now, that did not hold it when the pass was planned?
     *
     * The same question `scanEmptyPinsForSensors` asked once before the boot scan, asked again -
     * against the registry as it is at this moment, because between two steps the control loop
     * has had hundreds of iterations to write it.
     */
    bool portClaimedSinceThePassBegan(int pin)
    {
        if (_sensorRepository == nullptr)
        {
            return false;
        }

        const std::vector<SensorEntity> stored = _sensorRepository->getAll();
        return isPinOccupied(pin, stored, "Analog") || isPinOccupied(pin, stored, "I2C") ||
               isPinOccupied(pin, stored, "ModBus");
    }

    /**
     * @brief Is what holds this port the row this same pass has just written for it?
     *
     * A pass that adopts a sensor makes the port it adopted it on an occupied one, so the walk
     * finds it claimed when it comes back round. Nobody claimed it: the pass is reading its own
     * work, and the log has already said what happened on that port.
     */
    bool thisPassClaimedThePortItself(int pin)
    {
        if (_sensorRepository == nullptr || _rowsAdoptedThisPass.empty())
        {
            return false;
        }

        const std::vector<SensorEntity> stored = _sensorRepository->getAll();
        for (size_t i = 0; i < stored.size(); i++)
        {
            if (stored[i].pin == pin && isAmong(_rowsAdoptedThisPass, stored[i].uid))
            {
                return true;
            }
        }
        return false;
    }

    /** @brief The temp entity a scan of one model on one port tries, as the boot scan built it. */
    SensorEntity buildCandidateEntity(const SensorType &sensorType, int pin) const
    {
        return candidateEntity(sensorType, pin, candidateUid(pin));
    }

    /** @brief The uid a sensor found on this port is adopted under. */
    std::string candidateUid(int pin) const
    {
        char pinStr[3];
        snprintf(pinStr, sizeof(pinStr), "%02d", pin);
        return _moduleUid + "." + pinStr;
    }

    /**
     * @brief The same entity, for the bus half: the uid comes with the step, because the module's
     *        own is the control loop's to read.
     */
    static SensorEntity candidateEntity(const SensorType &sensorType, int pin, const std::string &uid)
    {
        SensorEntity tempSensor;
        tempSensor.sensorType = sensorType.name;
        tempSensor.model = sensorType.name;
        tempSensor.protocol = sensorType.getProtocolString();
        tempSensor.sensorTypeUid = sensorType.uid;
        tempSensor.isActive = true;
        tempSensor.pin = pin;
        tempSensor.uid = uid;
        tempSensor.status = SensorStatusEnum::SENSOR_STATUS_NEW;

        if (sensorType.isI2CSensor())
        {
            tempSensor.i2cAddress = sensorType.i2cAddress;
            tempSensor.chipType = sensorType.chipType;
            tempSensor.i2cRegisterConfigs.clear();
            for (size_t i = 0; i < sensorType.i2cRegisterConfig.size(); i++)
            {
                tempSensor.i2cRegisterConfigs.push_back(sensorType.i2cRegisterConfig[i]);
            }
        }
        else
        {
            tempSensor.baudRate = sensorType.baudRate;
            tempSensor.modbusRegisterConfigs = sensorType.modbusRegisterConfig;
            tempSensor.modbusPingAddress = sensorType.modbusPingAddress;
            tempSensor.registerToStoreModbusDeviceAddress = sensorType.registerToStoreModbusDeviceAddress;
            tempSensor.writeRegistersCommand = sensorType.writeRegistersCommand;
            tempSensor.applyAddressAfterPowerCycle = sensorType.applyAddressAfterPowerCycle;
            tempSensor.modbusDeviceAddress = sensorType.modbusDeviceAddress;
        }

        return tempSensor;
    }

    /** @brief Record a port on which no model answered, the way the boot scan recorded it. */
    void closePin(int pin)
    {
        if (_pinSettled || _moduleStructure == nullptr || _moduleStructure->nodes.empty())
        {
            return;
        }

        // Asked here too, and not only before each model: a port whose last model was tried before
        // somebody claimed it reaches this line with nothing having re-checked it, and calling an
        // occupied port empty is the one thing this record must never say.
        if (portClaimedSinceThePassBegan(pin))
        {
            log("STARTUP_SCAN: pin %d is occupied now - no empty-port record for it", pin);
            return;
        }

        Node &node = _moduleStructure->nodes[0];

        Sensor sensor;
        sensor.pin = pin;
        sensor.type = "none";

        char sensorId[20];
        snprintf(sensorId, sizeof(sensorId), ".%02d", pin);
        sensor.uid = node.uid + sensorId;

        node.sensors.push_back(sensor);

        log("STARTUP_SCAN: added placeholder sensor group %d, type: %s, pin: %d, uid: %s",
            pin, sensor.type.c_str(), sensor.pin, sensor.uid.c_str());
    }

    /**
     * @brief The entities holding an address they cannot keep, and where each of them goes.
     *
     * Same two rules as before: an address outside the ModBus range of the module has to change,
     * and of the entities sharing one address only one keeps it - the one whose address is its
     * port's own, or else the first the registry lists.
     */
    void planAddresses()
    {
        _addressWrites.clear();
        _addressCursor = 0;

        if (_sensorRepository == nullptr || _sensorReader == nullptr || _modbusAddressAllocator == nullptr)
        {
            log("STARTUP_SCAN: skipping ModBus address management - a collaborator is missing");
            return;
        }

        std::vector<SensorEntity> allSensorEntities = _sensorRepository->getAll();
        if (allSensorEntities.empty())
        {
            return;
        }

        std::map<int, std::vector<SensorEntity *>> addressMap;
        std::vector<SensorEntity *> sensorsNeedingNewAddress;
        collectModbusSensorsAndAddresses(allSensorEntities, addressMap, sensorsNeedingNewAddress);
        findDuplicateAddresses(addressMap, sensorsNeedingNewAddress);

        if (sensorsNeedingNewAddress.empty())
        {
            log("STARTUP_SCAN: no sensors need address changes");
            return;
        }

        _addressWrites.reserve(sensorsNeedingNewAddress.size());
        for (size_t i = 0; i < sensorsNeedingNewAddress.size(); i++)
        {
            _addressWrites.push_back(sensorsNeedingNewAddress[i]->uid);
        }
        log("STARTUP_SCAN: assigning new addresses to %zu sensor(s)", _addressWrites.size());
    }

    /**
     * @brief The address phase of a pass that adopted nothing: the rows this pass found out are on
     *        an address that is not theirs to keep, and no others.
     *
     * planAddresses() resolves the whole registry, which is the question a restart asks and the
     * wrong one to ask twice a minute: a cheap pass has found out one thing and may act on that
     * one thing. Two things put a row here - a free port of its wire kept hearing it at its factory
     * address, or the row is simply still on that address and the sweep has read it - and the move
     * is the same either way: buildAddressStep() asks the allocator, preferring the address of the
     * row's own port.
     */
    void planTheMovesThisPassOwes()
    {
        _addressWrites.clear();
        _addressCursor = 0;

        if (_neighboursToMove.empty() && _rowsOnAFactoryAddress.empty())
        {
            return;
        }

        _addressWrites.reserve(_neighboursToMove.size() + _rowsOnAFactoryAddress.size());
        for (size_t i = 0; i < _neighboursToMove.size(); i++)
        {
            _addressWrites.push_back(_neighboursToMove[i]);
        }
        for (size_t i = 0; i < _rowsOnAFactoryAddress.size(); i++)
        {
            // One write per row however many reasons this pass has to move it: the wire may have
            // been answered for the very row the registry has just found on a factory address, and
            // a second write would be aimed at an address the first one has already left.
            if (!isAmong(_neighboursToMove, _rowsOnAFactoryAddress[i]))
            {
                _addressWrites.push_back(_rowsOnAFactoryAddress[i]);
            }
        }
        log("STARTUP_SCAN: moving %zu sensor(s) off an address that is not theirs to keep",
            _addressWrites.size());
    }

    /**
     * @brief The rows the module reads that are still on the address their model left the factory
     *        with, and the write each of them is owed.
     *
     * An address of the module's own range belongs to one row; a factory address belongs to a model,
     * and every device of that model answers at it. So a row left on one is a row that a second
     * device of the same model - plugged into the port sharing its UART, where a wildcard and a
     * cheap probe both reach it - cannot be told apart from. The restart moves such a row in its
     * address phase; this is the same move, made when the module finds out rather than at the next
     * restart, and it needs neither a free port nor a command.
     *
     * Two conditions, and both are about not spending line time on a guess. The sweep must have
     * read the row: an address write is minutes of the identification procedure and it ends in a ping of
     * the new address, so writing to a row nothing has answered for is a cost paid for a failure.
     * And the row must not already be on the address of its own port - then the factory address is
     * that address, and there is nothing to move it to.
     */
    void noteTheRowsStillOnTheirFactoryAddress()
    {
        if (_sensorRepository == nullptr || _sensorTypeRepository == nullptr ||
            _sensorValues == nullptr)
        {
            return;
        }

        SensorTypeCatalog catalog = _sensorTypeRepository->catalogSnapshot();
        if (!catalog)
        {
            return;
        }

        const std::vector<SensorEntity> stored = _sensorRepository->getAll();
        for (size_t i = 0; i < stored.size(); i++)
        {
            const SensorEntity &row = stored[i];
            if (!row.isModbusSensor() || row.pin < 0)
            {
                continue;
            }
            if (row.status != SENSOR_STATUS_NEW && row.status != SENSOR_STATUS_ADDED)
            {
                continue;
            }
            if (row.modbusDeviceAddress == MODBUS_ADDRESS_MIN + row.pin)
            {
                continue;
            }
            if (isAmong(_rowsThatWouldNotMove, row.uid) || isAmong(_rowsOnAFactoryAddress, row.uid))
            {
                continue;
            }
            // The sweep's own fact, read where lineIsSilentFor() reads it: a row with a read behind
            // it and no error standing is a row the line is answering for right now.
            if (_sensorValues->getLastReadTime(row.uid) == 0 || row.isError)
            {
                continue;
            }
            if (!theAddressIsTheFactoryAddressOfTheModel(*catalog, row))
            {
                continue;
            }

            log("STARTUP_SCAN: sensor %s answers on pin %d at 0x%02X, the address its model leaves "
                "the factory on - it is owed one of the module's own",
                row.uid.c_str(), row.pin, row.modbusDeviceAddress);
            _rowsOnAFactoryAddress.push_back(row.uid);
        }
    }

    /** @brief Does this row hold exactly the address the catalog gives its model? */
    bool theAddressIsTheFactoryAddressOfTheModel(const std::vector<SensorType> &types,
                                                 const SensorEntity &row) const
    {
        // By uid, which is what the row was built from and what the catalog is keyed by: a row
        // whose model the catalog does not carry is one the module knows nothing about, and
        // guessing its factory address from a name would move a device on a guess.
        if (row.sensorTypeUid.empty())
        {
            return false;
        }

        for (size_t i = 0; i < types.size(); i++)
        {
            if (types[i].uid != row.sensorTypeUid)
            {
                continue;
            }
            return types[i].isModbusSensor() &&
                   types[i].modbusDeviceAddress == row.modbusDeviceAddress;
        }
        return false;
    }

    /**
     * @brief Did the line answer for this row in this pass - its ping, or the scan that adopted it?
     *
     * The ping comes before any port is asked, and it is asked with the other port of the row's
     * wire dark: a row that answered it has its device on its own port. Silence says nothing of
     * where the device is.
     */
    bool answeredThisPass(const std::string &uid) const
    {
        return isAmong(_rowsThatAnsweredThePing, uid) || isAmong(_rowsAdoptedThisPass, uid);
    }

    /** @brief Is this model the first one of the catalog the cheap rung asks at an address? */
    static bool isTheFirstScannableModel(const std::vector<SensorType> &types, size_t model)
    {
        for (size_t i = 0; i < model; i++)
        {
            if (isAScannableModbusModel(types[i]))
            {
                return false;
            }
        }
        return true;
    }

    static bool isAmong(const std::vector<std::string> &uids, const std::string &uid)
    {
        for (size_t i = 0; i < uids.size(); i++)
        {
            if (uids[i] == uid)
            {
                return true;
            }
        }
        return false;
    }

    bool buildAddressStep(StartupScanStep &step)
    {
        while (_addressCursor < _addressWrites.size())
        {
            const std::string uid = _addressWrites[_addressCursor];
            _addressCursor++;

            if (buildTheMoveOf(uid, step))
            {
                return true;
            }
        }
        return false;
    }

    /** @brief The address write that moves this row onto a free address of the module, its own port's first. */
    bool buildTheMoveOf(const std::string &uid, StartupScanStep &step)
    {
        if (_sensorRepository == nullptr || _modbusAddressAllocator == nullptr)
        {
            return false;
        }

        Utils::Optional<SensorEntity> stored = _sensorRepository->get(uid.c_str());
        if (!stored)
        {
            return false;
        }

        SensorEntity entity = stored.value();

        if (isAmong(_rowsThatWouldNotMove, uid))
        {
            // This device has been written to once and stayed where it was. Every planner that
            // gets here asks for the same identification sequence - the isolation of its wire included -
            // and would be told the same thing; the full resolution of the registry,
            // which a restart makes and so does any pass that adopted a sensor, is no exception.
            log("STARTUP_SCAN: sensor %s has already refused an address of the module - it is "
                "left where it is",
                uid.c_str());
            return false;
        }

        // Read now, from the registry, and not from a map built when the phase was planned.
        // nextFreeAddress() only knows what it is handed, and between two address steps the
        // control loop can have given a port to somebody - a person accepting a sensor, a
        // ScanSensors command adopting one. Handing out an address a live row already holds
        // is the very collision this phase exists to resolve, and it would stand until the
        // next boot. The entity being moved is left out of its own occupancy, the way
        // SetSensorModel leaves it out.
        const ModbusAddressOccupancy occupied =
            _modbusAddressAllocator->occupiedAddresses(entity.uid);
        const int newAddress =
            _modbusAddressAllocator->nextFreeAddress(occupied, MODBUS_ADDRESS_MIN + entity.pin);
        if (newAddress == -1)
        {
            log("STARTUP_SCAN: no available addresses in range 0x%02X-0x%02X for sensor %s",
                MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX, entity.uid.c_str());
            return false;
        }

        log("STARTUP_SCAN: attempting to set new address 0x%02X for sensor %s (current: 0x%02X)",
            newAddress, entity.uid.c_str(), entity.modbusDeviceAddress);

        step = StartupScanStep();
        step.kind = StartupScanStep::WRITE_ADDRESS;
        step.entity = entity;
        step.pin = entity.pin;
        step.targetAddress = newAddress;
        return true;
    }

    /**
     * @brief The preparation of the whole registry, once the addresses are final.
     *
     * Last, as it was last in the boot sequence: the per-model register writes have to reach the
     * addresses the entities ended up on, not the ones they booted with.
     */
    bool buildPrepareStep(StartupScanStep &step)
    {
        if (_sensorRepository == nullptr || _sensorReader == nullptr)
        {
            return false;
        }

        step = StartupScanStep();
        step.kind = StartupScanStep::PREPARE_KNOWN;
        step.entities = _sensorRepository->getAll();
        return true;
    }

    /** @brief Once, whether the sequence ended on a step or on having none left to give. */
    void finish()
    {
        if (_finished)
        {
            return;
        }
        _finished = true;

        releaseHoldings();
        markPassEnded();
        if (_pass == Pass::WHOLE_MODULE)
        {
            _restartsPassEndedAtMs = nowMs();
            _restartsPassEnded = true;
        }

        // What this pass did, not what the module has done since it started: the line reads as the
        // account of the pass that is ending. The lifetime totals stay in the counters this class
        // keeps. Three of them are columns of the DIAG-COUNTERS line - adoptedCount(),
        // factoryAddressMoveRefusedCount() and identifyStepCount(); the rest are reachable from a
        // test and from a debugger and nowhere else.
        log("STARTUP_SCAN: finished - %u step(s), %u sensor(s) adopted",
            (unsigned)_stepsInThisPass, (unsigned)_adoptedInThisPass);

        // The structure the module sent at boot describes the registry as it stood before a single
        // question had been put to the line. What the discovery then found out - a sensor adopted,
        // a stored sensor that did not answer, one that did - lives only in the module until it is
        // sent again. Unconditional, and not only when something was adopted: the commonest boot
        // adopts nothing and still learns which stored sensors are really there, and that is
        // exactly the boot in which the person is waiting to see their sensor.
        //
        // The restart's pass has already sent one for every sensor it adopted, as it adopted it
        // (applyCandidate()). This one is still owed after them: it is built after the address
        // phase, so every record carries the address the pass left its row on, and it carries what
        // the ping found out about the stored rows. It follows the one in flight like any other
        // structure of that pass, and when one is already owed the two are one.
        //
        // A cheap pass sends one only when it adopted something, so the structure it sends is a
        // sensor appearing, and the module stays as quiet between two of them as it was before
        // they existed. A pass that only moved a sensor off a factory address has nothing to say:
        // the address travels only in the record's group, which the server does not read, and the
        // row keeps the uid it was described under.
        const bool thePassHasSomethingToSay =
            (_pass == Pass::WHOLE_MODULE) || _adoptedInThisPass > 0;
        if (_didDiscover && thePassHasSomethingToSay && _republish != nullptr)
        {
            sendTheStructureOfTheRestartsPass(_pass == Pass::WHOLE_MODULE ? "startup_scan"
                                                                          : "free_port_probe");
        }
        else if (_placeholdersReported && _republish != nullptr)
        {
            // What the server holds of this module is the placeholders its boot reported, having no
            // catalog to describe the stored sensors by, and no structure built from the registry
            // has gone out since. The catalog that armed this pass is there now; a pass armed for
            // the preparation only finds nothing out and would end in silence, leaving the stored
            // sensors described as empty ports for as long as nothing else sends a structure.
            sendTheStructureOfTheRestartsPass("placeholders_replaced");
        }
    }

    /**
     * @brief Send a structure of a pass - now, or once the one in flight has settled.
     *
     * A structure is the heaviest thing the module sends - the boot's lowest heap is taken while one
     * is on its way - and one submitted while another is still in flight is not merged with it:
     * both go out. So the passes have at most one structure on its way and owe at most one more.
     * What they owe is not a snapshot: it is built from the registry when it goes, so two sensors
     * adopted while the first structure travels go out together in the next one, and the structure
     * a pass ends with, owed the same way, carries whatever the pass has settled by then. See
     * sendTheOwedStructureOnceTheLastOneHasSettled().
     *
     * "In flight" includes the boot's own structure not having settled yet (the republish answers
     * isSendPending() until it has): a pass that runs before the module has reported itself - a boot
     * with no link - owes its structure until then, and it goes out after the boot's, carrying the
     * address the boot's reported and every sensor the pass found.
     */
    void sendTheStructureOfTheRestartsPass(const char *reason)
    {
        if (_republish == nullptr)
        {
            return;
        }

        if (_republish->isSendPending())
        {
            if (!_structureOwed)
            {
                log("STARTUP_SCAN: a structure is still on its way - the next one (%s) follows it", reason);
            }
            _structureOwed = true;
            _owedStructureReason = reason;
            return;
        }

        _structureOwed = false;
        _republish->execute(reason);
    }

    /** @brief The structure the restart's pass owes, once nothing is in flight. Control loop only. */
    void sendTheOwedStructureOnceTheLastOneHasSettled()
    {
        if (!_structureOwed || _republish == nullptr || _republish->isSendPending())
        {
            return;
        }

        _structureOwed = false;
        _republish->execute(_owedStructureReason);
    }

    /**
     * @brief Let go of everything the discovery was holding to do its work.
     *
     * This object lives as long as the module does and runs once in that lifetime, so what it
     * holds after it is over it holds for ever. The catalog handle is the one that matters: the
     * repository hands out a shared list precisely so that readers do not each carry a copy, and
     * a reader that never lets go pins 1.4-2 KB per sensor type - 17-24 KB on a stand's twelve
     * types, around 55 KB on a field account's forty - on a heap that has tens of KB free. It also
     * defeats SensorTypeRepository::adoptStoredCatalog(), which lets the old list go before taking
     * the new one exactly so that the two are never both whole at once.
     *
     * The rest is the same mistake in smaller amounts: the last step held a copy of the whole
     * registry (the set the preparation was applied to), and the plan's cursors held a uid per
     * stored sensor.
     */
    void releaseHoldings()
    {
        _catalog.reset();
        _inFlight = StartupScanStep();

        // swap-with-empty, not clear(): clear() keeps the capacity, and the capacity is the
        // memory this is about.
        std::vector<std::string>().swap(_verifyUids);
        std::vector<std::string>().swap(_addressWrites);
        std::vector<std::string>().swap(_neighboursToMove);
        std::vector<std::string>().swap(_rowsOnAFactoryAddress);
        std::vector<std::string>().swap(_rowsAdoptedThisPass);
        std::vector<std::string>().swap(_rowsThatAnsweredThePing);
        std::string().swap(_rowToPartFromItsWire);
        std::string().swap(_storedRowToMoveOffItsWire);
        _pinToTakeDown = -1;
        std::vector<int>().swap(_scanPins);
        std::vector<int>().swap(_pinsLeftToTheLadder);
        std::vector<int>().swap(_pinsOwedTheCheapRungAgain);
        std::vector<int>().swap(_pinsHeldDarkUntilTheirWalk);
        std::string().swap(_moduleUid);

        // _silentToThePing is deliberately NOT released: it is not a snapshot taken to do the work,
        // it is what the ping found out, and it is what describes a row the sweep has not reached
        // yet. It is dropped a uid at a time, as the sweep takes each row over. One uid per stored
        // sensor that did not answer, so it is bounded by the registry and empty on a module whose
        // sensors are all there.
        //
        // _unidentifiedPins is not released either, for the same kind of reason: it is what the
        // pass concluded about a port, and it is what keeps the next pass off the expensive rung.
        // One entry per port at most, dropped when the port goes quiet or is identified.
        //
        // _wireAnswers is the same kind of thing and is kept for the same reason: what one pass
        // paid to find out about a port keeps the passes after it off the same question. One entry
        // per port at most, dropped when the sensor that answered stops holding the address.
        //
        // _rowsThatWouldNotMove is the third: it is what an address write found out about a device,
        // and it is what keeps the next pass from writing to it again. One uid per row at most.
        //
        // _portsLeftDark is the fourth: it is what keeps the cheap passes after this one from putting
        // a device back on the address of a stored sensor of its wire. One entry per port at most.
    }

    // --- Applying an outcome, on the control loop -----------------------------------------------

    /**
     * @brief What a ping of a stored entity means for the structure.
     *
     * An entity that answered, and an entity the module had already accepted, are both described
     * as themselves. An entity that did not answer and nobody has accepted yet is described as an
     * empty port - but it stays in the registry: the model and the ModBus address already found
     * for it must not be lost because the device happened to be silent during one restart, after
     * which its owner would have to add it from scratch. The step from NEW to ADDED is the
     * person's, made through SetSensorModel, and nothing here makes it or unmakes it.
     */
    void applyVerify(const StartupScanStep &step, const StartupScanOutcome &outcome)
    {
        // Kept whatever becomes of the row below: the cheap rung reads it before it asks the
        // row's address on the live wire. One ping per row of the ping's plan, and the list was
        // reserved for all of them.
        if (outcome.answered)
        {
            _rowsThatAnsweredThePing.push_back(step.entity.uid);
        }

        if (_moduleStructure == nullptr || _moduleStructure->nodes.empty() ||
            _sensorRepository == nullptr)
        {
            return;
        }

        // The row as it is now, not as the step carried it. The ping took a second or more, and
        // the control loop has been writing the registry all that time: the person may have
        // accepted this very sensor while its ping was on the line, and answering out of the
        // snapshot would describe an accepted sensor as an empty port. Symmetric with
        // applyCandidate(), which re-reads for the same reason.
        Utils::Optional<SensorEntity> live = _sensorRepository->get(step.entity.uid.c_str());
        if (!live)
        {
            // Removed while its ping was on the line. Nothing to describe.
            return;
        }

        const SensorEntity &sensorEntity = live.value();
        Node &node = _moduleStructure->nodes[0];

        if (outcome.answered)
        {
            log("STARTUP_SCAN: sensor %s is online", sensorEntity.uid.c_str());
            forgetSilent(sensorEntity.uid);
        }
        else
        {
            log("STARTUP_SCAN: sensor %s is offline", sensorEntity.uid.c_str());
            rememberSilent(sensorEntity.uid);
        }

        describeStoredSensor(node, sensorEntity);
    }

    void applyCandidate(const StartupScanStep &step, const StartupScanOutcome &outcome)
    {
        if (!outcome.answered)
        {
            if (outcome.heardBeforeTheWireWasIsolated)
            {
                accountForTheAnswerTheWireGave(step);
            }
            return;
        }

        // Asked once more, because the answer took seconds to come back and the loop has been
        // writing the registry all that time. A port claimed since the step was handed over keeps
        // what it was given: a scan's guess must not replace a decision somebody made.
        if (portClaimedSinceThePassBegan(step.pin))
        {
            log("STARTUP_SCAN: pin %d was claimed while the line was being asked about it - the answer is dropped",
                step.pin);
            settleCurrentPin();
            return;
        }

        // And while the registry has not read back nobody can say whether the port is claimed: the
        // answer is dropped as one from a claimed port is, rather than adopted over a row the
        // registry may hold.
        if (theRegistryDidNotReadBack())
        {
            log("STARTUP_SCAN: pin %d answered while the registry did not read back - the answer is dropped",
                step.pin);
            settleCurrentPin();
            return;
        }

        // The entity the scan tried, at the address it answered on: the bus half changes nothing
        // else about it, so the address is all that has to travel back.
        SensorEntity detected = step.entity;
        detected.modbusDeviceAddress = outcome.answeredAddress;

        log("STARTUP_SCAN: detected %s sensor on pin %d", detected.sensorType.c_str(), step.pin);

        if (_moduleStructure != nullptr && !_moduleStructure->nodes.empty())
        {
            addSensorToStructure(_moduleStructure->nodes[0], detected);
        }

        if (_sensorRepository != nullptr)
        {
            if (_sensorRepository->createOrUpdateSensor(detected))
            {
                // It answered, whatever an earlier pass thought of the uid.
                forgetSilent(detected.uid);
                // And this pass is the one that heard it. The address phase of this same pass may
                // write to this row - a pass that adopted resolves the whole registry - and nothing
                // else it can ask has an answer about a port whose row is this new.
                _rowsAdoptedThisPass.push_back(detected.uid);
                _adoptedCount++;
                _adoptedInThisPass++;
                log("STARTUP_SCAN: successfully saved detected sensor %s to repository with NEW status",
                    detected.uid.c_str());

                if (_pass == Pass::WHOLE_MODULE)
                {
                    partItFromTheRowOfItsWire(step.pin, detected);
                }

                if (_pass == Pass::WHOLE_MODULE && _didDiscover)
                {
                    // The sweep reads the sensor from this moment and its readings go to the
                    // server, but the app shows a sensor only once a structure has described it -
                    // and the rest of the restart's pass can be minutes of windows.
                    sendTheStructureOfTheRestartsPass("startup_scan_found");
                }
                if (_pass == Pass::WHOLE_MODULE && _cheapRungAskedEverywhere)
                {
                    askThePortsOfItsWireTheCheapRungAgain(step.pin);
                }
            }
            else
            {
                log("STARTUP_SCAN: failed to save detected sensor %s to repository",
                    detected.uid.c_str());
            }
        }

        // The port is answered: the rest of the catalog is not tried on it, and no empty-port
        // record is produced for it. A port that has just been identified is not one the module
        // gave up on, whatever an earlier pass concluded about it.
        forgetPortGaveUpIdentification(step.pin);
        settleCurrentPin();
    }

    /**
     * @brief A device the restart's pass adopted at the address a stored row of the paired port holds
     *        as its own port's: the two rows are parted there and then rather than at the end of the
     *        pass.
     *
     * Whichever rung found it - the cheap rung, which asks such an address only when that row was
     * silent to its ping; the window, which asks it with that row's port dark; the identification
     * after the window - adopted it on a model scan that passed with the rest of its wire dark, so
     * it is on this port: two rows on one address of one wire. The address phase would part them -
     * the row whose port the address belongs to keeps it, and this one moves - but only once the
     * pass is over, and until then a read of the stored row on the wire the two ports share reaches
     * this device too: alone it answers for the row, beside the row's own device the two answers
     * collide. So the write is the next step: this port is up and has just answered, the scan has
     * left the rest of its wire dark, and a window that found it still holds its wire dark - the
     * write goes out before the step that closes it.
     *
     * The other way round - the address is this port's own and the stored row holds it as a foreign one
     * - the address phase keeps it with this device and moves the stored row. When that row answered
     * this pass's ping its device is on its own port, and the two collide just the same, so that move is
     * made at once too: as the next step, or - when a window found the device - once the window has
     * given the stored row's port back, since that port is the window's to keep dark until then.
     */
    void partItFromTheRowOfItsWire(int pin, const SensorEntity &adopted)
    {
        Utils::Optional<SensorEntity> holder = storedNeighbourHoldingTheAddress(pin, adopted.modbusDeviceAddress);
        if (!holder)
        {
            return;
        }
        if (holder.value().modbusDeviceAddress == MODBUS_ADDRESS_MIN + holder.value().pin)
        {
            _rowToPartFromItsWire = adopted.uid;
            log("STARTUP_SCAN: sensor %s on pin %d answers at 0x%02X, the address of sensor %s of pin %d on "
                "its wire - it is moved before anything else is asked",
                adopted.uid.c_str(), pin, adopted.modbusDeviceAddress, holder.value().uid.c_str(),
                holder.value().pin);
            return;
        }
        if (adopted.modbusDeviceAddress == MODBUS_ADDRESS_MIN + pin && answeredThisPass(holder.value().uid))
        {
            _storedRowToMoveOffItsWire = holder.value().uid;
            log("STARTUP_SCAN: sensor %s on pin %d answers at 0x%02X, its own port's address, which sensor %s "
                "of pin %d on its wire holds - that sensor is moved before anything else is asked",
                adopted.uid.c_str(), pin, adopted.modbusDeviceAddress, holder.value().uid.c_str(),
                holder.value().pin);
        }
    }

    /**
     * @brief A find on the rest of the ladder of the restart's pass: every port of the same wire that
     *        is still waiting for its window is asked the cheap rung once more before it.
     *
     * That port's cheap questions went out while the device just found was on the wire as it stood
     * then. Two devices of one model at their factory address answer one request together, a
     * collision is no answer at all, and so neither port was named. The find changes that. A device
     * found by a write - the window's, or the identification's after it - has been moved onto this
     * port's own address; a device found at an address of the module's range stays there, as a
     * stored row whose address the cheap rung does not ask on the live wire. Either way the address
     * the two collided on has one device behind it now, and the port still waiting is asked the cheap
     * rung - seconds - before a window of a minute and more that would find its device by writing it
     * an address. Its power and its warm-up come first, as at the start of any walk of a port on this
     * pass; a port the cheap rung does not name goes on to its window as it would have.
     */
    void askThePortsOfItsWireTheCheapRungAgain(int pin)
    {
        const int line = lineOfPort(pin);
        for (size_t i = _scanPinCursor + 1; line >= 0 && i < _pinsLeftToTheLadder.size(); i++)
        {
            const int other = _pinsLeftToTheLadder[i];
            if (other == pin || lineOfPort(other) != line || isAmongPins(_pinsOwedTheCheapRungAgain, other))
            {
                continue;
            }
            _pinsOwedTheCheapRungAgain.push_back(other);
            logPassProgress("STARTUP_SCAN: a sensor was found on pin %d - pin %d, on its wire, is asked the "
                            "cheap questions again before its window",
                            pin, other);
        }
    }

    /**
     * @brief Put a name to the device that answered for a free port, and take the address back.
     *
     * The cheap rung heard a model's factory address on a free port and the confirming scan, with
     * the rest of the wire dark, heard nothing at all: what answered is on another port of this
     * wire.
     * The registry says which - a stored sensor of the same wire holding exactly the address that
     * was asked for, and there is nothing else it can be.
     *
     * Two things follow from knowing that, and the first one alone would not be enough. The port
     * is not asked that address again while that sensor holds it: the confirmation costs that
     * sensor a power cycle, and paying it twice a minute for an answer that is already accounted
     * for is what this module was doing. And the sensor is moved onto its own
     * port's address there and then - the same act the address phase performs after a restart,
     * through the same writeSensorAddress() sequence. That is what makes the note temporary: once
     * the factory address is nobody's, the free port hears nothing at it, and a device of that
     * model plugged into that port is found by the cheap rung like any other.
     *
     * A module that cannot move the sensor - a model whose address the write never takes - keeps
     * the note instead, and then the port is asked again when a person asks for it by hand.
     */
    void accountForTheAnswerTheWireGave(const StartupScanStep &step)
    {
        const int address = step.entity.modbusDeviceAddress;
        Utils::Optional<SensorEntity> neighbour = storedNeighbourHoldingTheAddress(step.pin, address);
        if (!neighbour)
        {
            // Nothing the registry knows explains it. A device that needed longer to come up than
            // the confirmation gave it looks exactly like this, so the question is asked again at
            // the next pass rather than written off.
            log("STARTUP_SCAN: pin %d answered at address 0x%02X and then went quiet under the "
                "confirming scan - no stored sensor of its wire holds that address",
                step.pin, address);
            return;
        }

        const SensorEntity &entity = neighbour.value();
        rememberTheWireAnswersFor(step.pin, address);
        log("STARTUP_SCAN: the answer pin %d gave at address 0x%02X came from sensor %s on pin %d, "
            "which shares its wire",
            step.pin, address, entity.uid.c_str(), entity.pin);

        if (entity.modbusDeviceAddress == MODBUS_ADDRESS_MIN + entity.pin)
        {
            // Already on the address its own port gives it: there is nowhere to move it that would
            // free this one, so the note is the whole of what this pass can do.
            return;
        }
        rememberNeighbourToMove(entity.uid);
    }

    /**
     * @brief The stored sensor of this port's wire that holds this address, if there is one.
     *
     * A port of the same UART and not this one, a row the module reads (the same statuses
     * isPinOccupied counts), and the address exactly as asked. On a module nobody wired the lines
     * of there is no such thing as a shared wire, and nothing is found.
     */
    Utils::Optional<SensorEntity> storedNeighbourHoldingTheAddress(int pin, int address)
    {
        const int line = lineOfPort(pin);
        if (line < 0 || _sensorRepository == nullptr)
        {
            return Utils::Optional<SensorEntity>();
        }

        const std::vector<SensorEntity> stored = _sensorRepository->getAll();
        for (size_t i = 0; i < stored.size(); i++)
        {
            const SensorEntity &row = stored[i];
            if (row.protocol != "ModBus" || row.pin == pin || row.modbusDeviceAddress != address)
            {
                continue;
            }
            if (row.status != SENSOR_STATUS_NEW && row.status != SENSOR_STATUS_ADDED)
            {
                continue;
            }
            if (lineOfPort(row.pin) != line)
            {
                continue;
            }
            return Utils::Optional<SensorEntity>(row);
        }
        return Utils::Optional<SensorEntity>();
    }

    /**
     * @brief Is this address on this port a question the module has already answered?
     *
     * Only for the cheap pass, and only while the sensor that answered still holds the address:
     * the registry is asked every time, so a sensor that has moved or left takes the note with it
     * and the port is asked again. A pass a person asked for pays the question whatever the note
     * says - that is what asking by hand is for.
     */
    bool theWireAlreadyAnsweredFor(int pin, int address)
    {
        if (_pass != Pass::FREE_PORTS)
        {
            return false;
        }

        for (size_t i = 0; i < _wireAnswers.size(); i++)
        {
            if (_wireAnswers[i].pin != pin || _wireAnswers[i].address != address)
            {
                continue;
            }
            if (storedNeighbourHoldingTheAddress(pin, address))
            {
                return true;
            }
            _wireAnswers.erase(_wireAnswers.begin() + i);
            return false;
        }
        return false;
    }

    /** @brief One note per port, replaced rather than added to: a port has one wire. */
    void rememberTheWireAnswersFor(int pin, int address)
    {
        for (size_t i = 0; i < _wireAnswers.size(); i++)
        {
            if (_wireAnswers[i].pin == pin)
            {
                _wireAnswers[i].address = address;
                return;
            }
        }

        WireAnswer note;
        note.pin = pin;
        note.address = address;
        _wireAnswers.push_back(note);
    }

    /**
     * @brief Is this free port one the cheap pass leaves dark?
     *
     * While a stored sensor of its wire holds the address its device answers at: power on the port
     * would put the two on one address of one wire again, for the length of every pass, twice a
     * minute, and the cheap rung of that pass asks the module's range - less the addresses a stored
     * sensor of its wire holds - and then the models' factory addresses. The registry is asked
     * every time, so a sensor that has moved or left takes the note with it. The pass a
     * ScanSensors command leaves behind does not read the note: it asks the port it names whatever
     * the note says, which is how a person gets a sensor put there found.
     */
    bool thePortIsLeftDark(int pin)
    {
        for (size_t i = 0; i < _portsLeftDark.size(); i++)
        {
            if (_portsLeftDark[i].pin != pin)
            {
                continue;
            }
            if (storedNeighbourHoldingTheAddress(pin, _portsLeftDark[i].address))
            {
                return true;
            }
            _portsLeftDark.erase(_portsLeftDark.begin() + i);
            return false;
        }
        return false;
    }

    void forgetThePortIsLeftDark(int pin)
    {
        for (size_t i = 0; i < _portsLeftDark.size(); i++)
        {
            if (_portsLeftDark[i].pin == pin)
            {
                _portsLeftDark.erase(_portsLeftDark.begin() + i);
                return;
            }
        }
    }

    /** @brief Owe one address write to this sensor, once, however many ports heard it. */
    void rememberNeighbourToMove(const std::string &uid)
    {
        if (isAmong(_neighboursToMove, uid))
        {
            return;
        }
        if (isAmong(_rowsThatWouldNotMove, uid))
        {
            // The module has already written to this device and it stayed where it was. Knowing
            // that is what the note is for; writing again would cost the rest of its wire its
            // power to find out the same thing.
            log("STARTUP_SCAN: sensor %s answers for a free port of its wire and has already "
                "refused an address of the module - it is left where it is",
                uid.c_str());
            return;
        }
        _neighboursToMove.push_back(uid);
    }

    /**
     * @brief What a broadcast poll of a port means for the rest of the ladder.
     *
     * Silence is the answer this ladder is built for: nothing on the port answered its own address
     * and nothing answered a question addressed to nobody in particular, so the port is empty and
     * the expensive procedure is not worth its line time. Anything at all - and the port goes to
     * the identification rung, which is what the discovery has always done to every free port.
     *
     * Except on the pass a ScanSensors command leaves behind it: that procedure has just run on
     * this port, in the command itself, so a port that answers here is a port nothing could name -
     * which is exactly what the note is for.
     */
    void applyPresence(const StartupScanStep &step, const StartupScanOutcome &outcome)
    {
        if (step.catalog)
        {
            // The restart's pass: the poll opened the port's window, and its answer is the first
            // of what the window learns. The range comes next, model by model - see
            // buildWindowStep() - and what the whole window heard decides the port when it closes.
            _windowOpen = true;
            _windowHeard = outcome.answered;
            _rung = Rung::RANGE;
            _scanTypeCursor = 0;
            return;
        }

        concludeThePresence(step, outcome.answered);
    }

    /**
     * @brief What a port's answer to the presence question means for the rest of the ladder: the
     *        poll's own answer, or on the restart's pass that of the whole window.
     */
    void concludeThePresence(const StartupScanStep &step, bool answered)
    {
        if (!answered)
        {
            log("STARTUP_SCAN: nothing answered the presence poll of pin %d - the port is empty",
                step.pin);
            // A port that has gone quiet is an ordinary free port again: the ladder does not
            // remember a device that is no longer there.
            forgetPortGaveUpIdentification(step.pin);
            _pinSettled = false;

            if (_pass == Pass::WHOLE_MODULE)
            {
                closePin(step.pin);
            }
            advanceToNextPin();
            return;
        }

        if (_pass == Pass::FREE_PORTS)
        {
            // The cheap pass never escalates: the device that is there was already given its one
            // identification pass, and hearing it again says nothing new.
            logPassProgress("STARTUP_SCAN: pin %d still answers a presence poll and is still unidentified",
                            step.pin);
            _pinSettled = true;
            advanceToNextPin();
            return;
        }

        if (_pass == Pass::NAMED_PORT)
        {
            // The command that asked for this pass has just walked the whole catalog on this port
            // and named nothing; the port answers all the same. That is the one fact the ladder
            // keeps about it, and it is what holds the expensive rung off it until somebody asks
            // again.
            log("STARTUP_SCAN: pin %d answers a presence poll and the scan asked for it named "
                "nothing - the port has something on it nobody can identify",
                step.pin);
            rememberPortGaveUpIdentification(step.pin);
            _pinSettled = true;
            advanceToNextPin();
            return;
        }

        log("STARTUP_SCAN: something answered the presence poll of pin %d - identifying it", step.pin);
        _rung = Rung::IDENTIFY;
        _scanTypeCursor = 0;
    }

    /** @brief One model's answer inside a port's window: adopted, heard, or nothing. */
    void applyWindowStep(const StartupScanStep &step, const StartupScanOutcome &outcome)
    {
        if (outcome.answered)
        {
            // A model scan ran: it takes the rest of this wire down, which the window holds dark
            // already.
            noteTheOtherPortsLostTheirPower();
            _windowScanned = true;
        }

        if (outcome.answeredModel >= 0 && step.catalog &&
            static_cast<size_t>(outcome.answeredModel) < step.catalog->size())
        {
            // The same adoption as a find of any other rung: that model, at the address its scan
            // passed at - one of the module's range, or the port's own once it was written.
            StartupScanStep found = step;
            found.entity = buildCandidateEntity((*step.catalog)[outcome.answeredModel], step.pin);
            log("STARTUP_SCAN: pin %d answered as %s at 0x%02X inside its window",
                step.pin, found.entity.sensorType.c_str(), outcome.answeredAddress);
            applyCandidate(found, outcome);
            // The port is settled, and its window is the one thing left to do on it.
            _rung = Rung::CLOSE;
            return;
        }

        if (outcome.answered)
        {
            _windowHeard = true;
        }
    }

    /**
     * @brief The port's window is over: the wire has its power back, and the port is decided.
     *
     * A port the window adopted a sensor on is settled already. Otherwise it is decided the way the
     * poll alone decided it before there was a window, on what the whole window heard: anything at
     * all - the poll, a read of the range, a write that was verified - and the port goes to the
     * identification rung; nothing, and it is an empty port.
     */
    void closeTheWindow(const StartupScanStep &step)
    {
        _windowOpen = false;
        noteTheOtherPortsLostTheirPower();
        if (theWireHasAnotherPort(step.pin) || _windowScanned)
        {
            // The window darkened the rest of its wire, and so did any model scan inside it - by
            // the reader's own account of the wiring, which a stand that told this class nothing
            // has all the same: the ports still to be walked get their power back first.
            noteThePortsAheadMayBeDark();
        }
        const bool heard = _windowHeard || _windowScanned;
        _windowHeard = false;
        _windowScanned = false;

        if (_pinSettled || portClaimedSinceThePassBegan(step.pin))
        {
            // Adopted inside the window, or claimed by somebody else while it was open: the walk
            // finds the port claimed and moves on, saying which of the two it was.
            _rung = Rung::IDENTIFY;
            _scanTypeCursor = _catalog ? _catalog->size() : 0;
            return;
        }

        concludeThePresence(step, heard);
    }

    void applyAddressWrite(const StartupScanStep &step, const StartupScanOutcome &outcome)
    {
        // A move off a factory address, chosen by a pass of the module's own rather than by the
        // restart's address phase: either the registry found the row on one, or a free port of its
        // wire kept hearing it there.
        const bool aPassChoseThisMove =
            _pass != Pass::WHOLE_MODULE && (isAmong(_rowsOnAFactoryAddress, step.entity.uid) ||
                                            isAmong(_neighboursToMove, step.entity.uid));

        if (!outcome.answered)
        {
            log("STARTUP_SCAN: failed to write new address 0x%02X to sensor %s",
                step.targetAddress, step.entity.uid.c_str());

            // One attempt per row, whichever phase made it, and the module stops asking: the write
            // is the whole identification procedure and it takes the power of the rest of its wire. The
            // restart's address phase reaches the device through that same sequence, so a refusal
            // it is given is the answer to the question the passes after it ask - noting it there
            // too is what keeps one device from costing its wire its power twice in one boot.
            // What the row keeps instead is the address it had.
            //
            // But only where there was a device to say no. A write ends in a ping of the new
            // address, and a port with nothing on it is silent to that ping exactly as a device
            // that stayed where it was: the two are one answer on the line and only the module's
            // own facts tell them apart. A pass that chose this move chose it on such a fact - it
            // heard the device on the wire or the sweep is reading the row - and for the rest
            // there is theLineAnswersForTheRow().
            if (aPassChoseThisMove || theLineAnswersForTheRow(step.entity))
            {
                rememberTheRowWouldNotMove(step.entity.uid);
            }
            else
            {
                log("STARTUP_SCAN: nothing answers for sensor %s - the write went into a port with "
                    "nothing on it, and the module tries again once something does",
                    step.entity.uid.c_str());
            }
            dropTheFindThatCannotBeParted(step.entity);
            return;
        }

        if (aPassChoseThisMove)
        {
            _factoryAddressMoveCount++;
        }

        if (_sensorRepository == nullptr)
        {
            return;
        }

        Utils::Optional<SensorEntity> stored = _sensorRepository->get(step.entity.uid.c_str());
        if (!stored)
        {
            return;
        }

        SensorEntity entity = stored.value();
        const int oldAddress = entity.modbusDeviceAddress;
        entity.modbusDeviceAddress = step.targetAddress;

        if (_sensorRepository->createOrUpdateSensor(entity))
        {
            log("STARTUP_SCAN: successfully changed sensor %s address from 0x%02X to 0x%02X",
                entity.uid.c_str(), oldAddress, step.targetAddress);

            if (_moduleStructure != nullptr && !_moduleStructure->nodes.empty())
            {
                addSensorToStructure(_moduleStructure->nodes[0], entity);
            }
        }
        else
        {
            log("STARTUP_SCAN: failed to update sensor %s in repository after address change",
                entity.uid.c_str());
        }
    }

    /**
     * @brief A write that was to part two rows of one wire on one address has been refused: the row
     *        the restart's pass adopted is not kept, and its port goes dark.
     *
     * Both devices are there - the stored one answered this pass's ping and the adopted one passed a
     * model scan, and each of those asks its port with the rest of its wire dark - and one address of
     * one wire cannot carry two of them: from the moment both ports are up, every read at that address
     * is answered twice at once and neither row is read again. The write was the module's way to part
     * them, and it was refused. So the stored sensor - the one the module was reading before the
     * restart - keeps the wire, and the device the pass found is left as though its port were empty:
     * its row goes, and so do its records in the structure the module holds and its readings, and its
     * port is taken down by the next step - inside the window that found it, when one did; before the
     * next write or the preparation, when the address phase made the write - and neither the walk nor
     * the cheap passes after it give it its power back (see thePortIsLeftDark()). Only for as long as
     * the module runs: the next restart finds it, writes to it once and leaves it the same way, and a
     * ScanSensors command on its port asks it again.
     *
     * Which row the write was aimed at does not matter: the device the pass adopted may be the one
     * that refused, or the stored one may have refused to leave an address the rule of the address
     * phase keeps for the adopted device - the row that goes is the adopted one either way.
     */
    void dropTheFindThatCannotBeParted(const SensorEntity &refused)
    {
        if (_pass != Pass::WHOLE_MODULE || _sensorRepository == nullptr)
        {
            return;
        }
        Utils::Optional<SensorEntity> other =
            storedNeighbourHoldingTheAddress(refused.pin, refused.modbusDeviceAddress);
        if (!other || !answeredThisPass(refused.uid) || !answeredThisPass(other.value().uid))
        {
            return;
        }

        const SensorEntity found = isAmong(_rowsAdoptedThisPass, refused.uid) ? refused : other.value();
        const SensorEntity &kept = (found.uid == refused.uid) ? other.value() : refused;
        if (!isAmong(_rowsAdoptedThisPass, found.uid) || !_sensorRepository->remove(found.uid.c_str()))
        {
            return;
        }

        for (size_t i = 0; i < _rowsAdoptedThisPass.size(); i++)
        {
            if (_rowsAdoptedThisPass[i] == found.uid)
            {
                _rowsAdoptedThisPass.erase(_rowsAdoptedThisPass.begin() + i);
                break;
            }
        }
        forgetTheRowsOfThePortWouldNotMove(found.pin);
        if (_moduleStructure != nullptr)
        {
            // A structure sent as the module holds it, without the describer - the one a change of
            // the local IP sends - would otherwise still carry the records its adoption added.
            _moduleStructure->removeSensorsByPin(found.pin);
        }
        if (_sensorValues != nullptr)
        {
            // The uid is the port's: a device adopted on this port later in this run is adopted under
            // it, and would otherwise start with the readings of this one.
            _sensorValues->removeSensor(found.uid);
        }
        _pinToTakeDown = found.pin;
        forgetThePortIsLeftDark(found.pin);
        WireAnswer note;
        note.pin = found.pin;
        note.address = refused.modbusDeviceAddress;
        _portsLeftDark.push_back(note);
        log("STARTUP_SCAN: sensor %s on pin %d and sensor %s of pin %d answer at 0x%02X and cannot be parted - "
            "%s is not kept, and its port is taken down",
            found.uid.c_str(), found.pin, kept.uid.c_str(), kept.pin, refused.modbusDeviceAddress,
            found.uid.c_str());
    }

    // --- The bus half ---------------------------------------------------------------------------

    /**
     * @brief The cheap rung: does this model answer at the step's address on this port - its own
     *        factory address, or on the restart's pass and the periodic one an address of the
     *        module's range?
     *
     * One request and one read budget - the reader's ordinary single-register read, the same
     * transaction the sweep makes on every stored sensor four times a minute, with the retry loops
     * of the scan and of the ping left out. Against a port with nothing on it that is 300 ms
     * rather than the tens of seconds the identification procedure spends proving the same thing.
     *
     * A read that answers is not enough to adopt on, and it is not what adopts: the answer may
     * have come from the paired port of the same UART, and the value it carries has not been
     * checked against the range the model declares for it. So the model scan is run there and
     * then, at the same address - the isolation, the warm-up and the per-channel range checks
     * included - and adoption keeps the exact meaning it has always had: `scanSensor()` said yes.
     */
    StartupScanOutcome probeCandidateOnBus(const StartupScanStep &step)
    {
        StartupScanOutcome outcome;

        SensorEntity tempSensor = step.entity;

        if (tempSensor.protocol != "ModBus")
        {
            // Nothing cheap to try: the reader answers the other protocols from the scan alone.
            if (_sensorReader->scanSensor(tempSensor))
            {
                outcome.answered = true;
                outcome.answeredAddress = tempSensor.modbusDeviceAddress;
            }
            return outcome;
        }

        float value = 0.0f;
        if (!_sensorReader->readSensor(tempSensor, value))
        {
            return outcome;
        }

        if (_sensorReader->scanSensor(tempSensor))
        {
            outcome.answered = true;
            outcome.answeredAddress = tempSensor.modbusDeviceAddress;
            return outcome;
        }

        // Heard at the address with the whole wire live, silent at the same address with the rest
        // of the wire dark. Nothing on this port answered either question, so what
        // answered the first one is on another port of this wire.
        outcome.heardBeforeTheWireWasIsolated = true;
        return outcome;
    }

    /**
     * @brief The presence rung: is anything at all on this port?
     *
     * The two frames are the address-discovery queries, unchanged - the ones
     * `ModbusSensorReader::writeSensorAddress()` puts on the line when it has to find a device
     * whose address it does not know. What is different is what is done with the answer: nothing
     * is read out of it and nothing is written back, only the fact that a frame came at all.
     *
     * That fact cannot be had through the sensor reader: every entry point it has is about one
     * device at one address, and it rejects - rightly - a frame whose responder address is not the
     * one it asked. So this goes through the same adapter a raw ModbusRead from the backend uses.
     *
     * The identification procedure never puts these frames on a shared line, and neither does this: the address
     * write isolates the port first, and so does the poll - for its own length, and only the ports
     * that share this one's wire. Without that, "is anything on port 1" is answered by the sensor
     * working on port 0, and a free port paired with an occupied one would go down the whole
     * identification procedure on every pass.
     */
    StartupScanOutcome probePresenceOnBus(const StartupScanStep &step)
    {
        StartupScanOutcome outcome;

        if (_rawBus == nullptr)
        {
            // No way to ask the port as a whole. The ladder does not cheapen an answer it cannot
            // get: the port goes to the identification rung, exactly as it did before the ladder.
            outcome.answered = true;
            return outcome;
        }

        isolateTheLineForThePoll(step.pin);

        outcome.answered = somethingAnswersOnPort(step.pin, WILDCARD_QUERY_FC, WILDCARD_QUERY_REGISTER) ||
                           somethingAnswersOnPort(step.pin, VENDOR_QUERY_FC, VENDOR_QUERY_REGISTER);

        if (step.catalog)
        {
            // The restart's pass: the isolation is the port's window, and it stays until the step
            // that closes the window gives it back - see closeTheWindowOnBus().
            _windowPin = step.pin;
            logPassProgress("STARTUP_SCAN: pin %d window open - the range 0x%02X-0x%02X for every "
                            "scannable ModBus model%s",
                            step.pin, MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX,
                            THE_WINDOW_WRITES_THE_PORT_ADDRESS ? ", then the port's address written"
                                                               : "");
            return outcome;
        }

        giveTheLineBack();
        return outcome;
    }

    /**
     * @brief One model of the catalog, read at every address of the module's range, inside the
     *        port's window.
     *
     * A device the module once gave an address answers there and nowhere else - not at its
     * model's factory address, not to a wildcard - and costs no write at all to find. The
     * addresses held by the sensors of the paired port are asked too: that port is dark, so
     * whatever answers is on this one. One read each, the cheap rung's own transaction; an answer
     * is adopted on the meaning adoption has always had - the model's scan passed at that address
     * (the isolation of the rest of its wire, the warm-up, the range of every channel).
     */
    StartupScanOutcome askTheRangeOnBus(const StartupScanStep &step)
    {
        StartupScanOutcome outcome;
        if (!theStepNamesAModel(step))
        {
            return outcome;
        }
        const SensorType &type = (*step.catalog)[step.model];
        feedTheTaskWatchdog();

        SensorEntity candidate = candidateEntity(type, step.pin, step.entity.uid);
        for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
        {
            candidate.modbusDeviceAddress = address;
            float value = 0.0f;
            if (!_sensorReader->readSensor(candidate, value))
            {
                continue;
            }
            outcome.answered = true;
            if (_sensorReader->scanSensor(candidate))
            {
                log("STARTUP_SCAN: pin %d window - %s answered at 0x%02X and its scan passed",
                    step.pin, type.name.c_str(), address);
                outcome.answeredModel = step.model;
                outcome.answeredAddress = address;
                return outcome;
            }
        }
        return outcome;
    }

    /**
     * @brief One model's address writes into the port's window, the way the identification procedure wrote
     *        an address it could not direct.
     *
     * The model's own write sent to 0x00, then the blind register 0/1 sequence, each verified by a
     * ping of the port's address (ISensorReader::writeSensorAddressByBroadcast). What takes it is
     * adopted when that model's scan passes at the port's address. Only on a port that has answered
     * nothing at all - see buildWindowStep().
     */
    StartupScanOutcome writeThePortAddressOnBus(const StartupScanStep &step)
    {
        StartupScanOutcome outcome;
        if (!theStepNamesAModel(step))
        {
            return outcome;
        }
        const SensorType &type = (*step.catalog)[step.model];
        feedTheTaskWatchdog();

        const int portAddress = MODBUS_ADDRESS_MIN + step.pin;
        SensorEntity candidate = candidateEntity(type, step.pin, step.entity.uid);
        if (!_sensorReader->writeSensorAddressByBroadcast(candidate, portAddress))
        {
            return outcome;
        }
        outcome.answered = true;
        candidate.modbusDeviceAddress = portAddress;
        if (_sensorReader->scanSensor(candidate))
        {
            log("STARTUP_SCAN: pin %d window - %s took address 0x%02X and its scan passed",
                step.pin, type.name.c_str(), portAddress);
            outcome.answeredModel = step.model;
            outcome.answeredAddress = portAddress;
        }
        return outcome;
    }

    /**
     * @brief Close the window: the ports it took down get their power back, and the sweep leaves
     *        them alone until what is plugged into them has come up - see keepsThePortDark().
     */
    void closeTheWindowOnBus()
    {
        giveTheLineBack();
        logPassProgress("STARTUP_SCAN: pin %d window closed", _windowPin);
        _windowPin = -1;
    }

    /**
     * @brief Give the ports the poll took down their power back, and have the sweep leave them
     *        alone until what is plugged into them has come up - see keepsThePortDark(). The same
     *        whether the poll opened a window or asked its one question and let go.
     */
    void giveTheLineBack()
    {
        _portsComingBack.clear();
        for (size_t i = 0; i < _isolatedPorts.size(); i++)
        {
            _portsComingBack.push_back(_isolatedPorts[i]);
        }
        _portsCameBackAtMs = nowMs();
        letTheRestOfTheLineBack();
    }

    static bool theStepNamesAModel(const StartupScanStep &step)
    {
        return step.catalog && step.model >= 0 &&
               static_cast<size_t>(step.model) < step.catalog->size();
    }

    static bool isAmongPins(const std::vector<int> &pins, int pin)
    {
        for (size_t i = 0; i < pins.size(); i++)
        {
            if (pins[i] == pin)
            {
                return true;
            }
        }
        return false;
    }

    /** @brief What the catalog walk of a free port tries and the window asks: canScan, supported, ModBus. */
    static bool isAScannableModbusModel(const SensorType &type)
    {
        return type.canScan && type.isSupported && type.isModbusSensor();
    }

    /** @brief Vouch for the task that owns the line, between two bounded pieces of a long step. */
    void feedTheTaskWatchdog()
    {
        if (_arduino != nullptr)
        {
            _arduino->feedTaskWatchdog();
        }
    }

    /**
     * @brief Take the power off the ports that share this one's wire, for the length of one poll.
     *
     * The same act as `ModbusSensorReader::isolatePortForAddressWrite()`, and for the same reason -
     * a frame addressed to nobody in particular is answered by everybody that can hear it - the
     * ports on the same UART, and only those, except that this gives them back the moment the two
     * query frames are out and answered. What it costs the port it takes
     * down is a power cycle: the module pays it once per port per full pass, which is the same
     * thing it pays whenever it writes a device's address.
     */
    void isolateTheLineForThePoll(int pin)
    {
        _isolatedPorts.clear();

        const int line = lineOfPort(pin);
        if (line < 0)
        {
            return;
        }

        for (size_t other = 0; other < _lineOfPort.size(); other++)
        {
            if (static_cast<int>(other) == pin || _lineOfPort[other] != line)
            {
                continue;
            }
            if (_sensorReader->setModbusPortPower(static_cast<int>(other), false))
            {
                _isolatedPorts.push_back(static_cast<int>(other));
            }
        }

        if (!_isolatedPorts.empty())
        {
            logPassProgress("STARTUP_SCAN: pin %d shares its line - %zu port(s) taken down for the "
                            "presence poll",
                            pin, _isolatedPorts.size());
        }
    }

    /** @brief Give back the power of every port the poll took down, before anything else runs. */
    void letTheRestOfTheLineBack()
    {
        for (size_t i = 0; i < _isolatedPorts.size(); i++)
        {
            _sensorReader->setModbusPortPower(_isolatedPorts[i], true);
        }
        _isolatedPorts.clear();
    }

    /** @brief The UART a port is wired to, or -1 when nobody said. */
    int lineOfPort(int pin) const
    {
        if (pin < 0 || static_cast<size_t>(pin) >= _lineOfPort.size())
        {
            return -1;
        }
        return _lineOfPort[pin];
    }

    /** @brief Does a port sharing this one's wire carry a sensor the module reads? */
    bool theLineCarriesAStoredSensor(int pin)
    {
        const int line = lineOfPort(pin);
        if (line < 0 || _sensorRepository == nullptr)
        {
            return false;
        }

        const std::vector<SensorEntity> stored = _sensorRepository->getAll();
        for (size_t other = 0; other < _lineOfPort.size(); other++)
        {
            if (static_cast<int>(other) == pin || _lineOfPort[other] != line)
            {
                continue;
            }
            if (isPinOccupied(static_cast<int>(other), stored, "ModBus"))
            {
                return true;
            }
        }
        return false;
    }

    /** @brief Give the ports this pass is about to ask their power. */
    StartupScanOutcome powerTheFreePortsOnBus(const StartupScanStep &step)
    {
        StartupScanOutcome outcome;

        for (size_t i = 0; i < step.pins.size(); i++)
        {
            forgetPin(_portsTakenDown, step.pins[i]);
            if (_sensorReader->setModbusPortPower(step.pins[i], true))
            {
                // At least one port was dark, so whatever is plugged into it is only now coming
                // up: the pass owes it PORT_WAKE_UP_MS before it asks anything.
                outcome.answered = true;
                logPassProgress("STARTUP_SCAN: pin %d had no power - it has it now", step.pins[i]);
            }
        }

        return outcome;
    }

    /**
     * @brief Put the power of the ports the pass found nothing on back down - or of the one port the
     *        pass takes down (the step's `pin`), which the sweep is then kept off for a while: see
     *        THE_SWEEPS_LIST_LAGS_THE_REGISTRY_BY_MS.
     */
    StartupScanOutcome releaseTheFreePortsOnBus(const StartupScanStep &step)
    {
        StartupScanOutcome outcome;

        if (step.pin >= 0)
        {
            if (!isAmongPins(_portsTakenDown, step.pin))
            {
                _portsTakenDown.push_back(step.pin);
            }
            _portsTakenDownAtMs = nowMs();
        }
        for (size_t i = 0; i < step.pins.size(); i++)
        {
            if (_sensorReader->setModbusPortPower(step.pins[i], false))
            {
                outcome.answered = true;
                logPassProgress("STARTUP_SCAN: pin %d has nothing on it - its power goes back down",
                                step.pins[i]);
            }
        }

        return outcome;
    }

    bool somethingAnswersOnPort(int pin, uint8_t functionCode, uint16_t startRegister)
    {
        const uint8_t request[6] = {
            WILDCARD_SLAVE_ADDRESS,
            functionCode,
            (uint8_t)(startRegister >> 8),
            (uint8_t)(startRegister & 0xFF),
            0x00,
            0x01
        };

        std::vector<uint8_t> answer;
        if (!_rawBus->modbusRequest(pin, request, sizeof(request), true, PRESENCE_POLL_TIMEOUT_MS,
                                    answer))
        {
            return false;
        }

        if (answer.size() < MIN_MODBUS_FRAME_BYTES)
        {
            // Fewer bytes than the shortest frame that can exist: a fragment of noise, not a
            // device. Saying "somebody is here" on the strength of it would send the module down
            // the expensive rung on every port with a long cable.
            logPassProgress("STARTUP_SCAN: pin %d answered the presence poll with %zu byte(s) - not a frame",
                            pin, answer.size());
            return false;
        }

        logPassProgress("STARTUP_SCAN: pin %d answered the presence poll (fc 0x%02X): %zu bytes, responder %d",
                        pin, functionCode, answer.size(), (int)answer[0]);
        return true;
    }

    StartupScanOutcome scanCandidateOnBus(const StartupScanStep &step)
    {
        StartupScanOutcome outcome;

        SensorEntity tempSensor = step.entity;

        if (tempSensor.protocol == "I2C")
        {
            if (_sensorReader->scanSensor(tempSensor))
            {
                outcome.answered = true;
                outcome.answeredAddress = tempSensor.modbusDeviceAddress;
            }
            return outcome;
        }

        const int modelAddress = tempSensor.modbusDeviceAddress;
        const int scanTargetAddress = MODBUS_ADDRESS_MIN + step.pin;

        _sensorReader->writeSensorAddress(tempSensor, scanTargetAddress);

        tempSensor.modbusDeviceAddress = scanTargetAddress;
        if (_sensorReader->scanSensor(tempSensor))
        {
            outcome.answered = true;
            outcome.answeredAddress = scanTargetAddress;
            return outcome;
        }

        tempSensor.modbusDeviceAddress = modelAddress;
        if (_sensorReader->scanSensor(tempSensor))
        {
            outcome.answered = true;
            outcome.answeredAddress = modelAddress;
            return outcome;
        }

        for (int address = MODBUS_ADDRESS_MIN; address <= MODBUS_ADDRESS_MAX; address++)
        {
            tempSensor.modbusDeviceAddress = address;
            if (_sensorReader->scanSensor(tempSensor))
            {
                outcome.answered = true;
                outcome.answeredAddress = address;
                return outcome;
            }
        }

        return outcome;
    }

    // --- The pieces the boot scan used, unchanged ------------------------------------------------

    static bool isPinOccupied(int pin, const std::vector<SensorEntity> &sensors,
                              const std::string &protocol)
    {
        for (size_t i = 0; i < sensors.size(); i++)
        {
            const SensorEntity &sensor = sensors[i];
            if (sensor.protocol != protocol ||
                (sensor.status != SENSOR_STATUS_ADDED && sensor.status != SENSOR_STATUS_NEW))
            {
                continue;
            }

            if (sensor.pin == pin)
            {
                return true;
            }
        }
        return false;
    }

    void collectModbusSensorsAndAddresses(std::vector<SensorEntity> &allSensorEntities,
                                          std::map<int, std::vector<SensorEntity *>> &addressMap,
                                          std::vector<SensorEntity *> &sensorsNeedingNewAddress)
    {
        for (size_t i = 0; i < allSensorEntities.size(); i++)
        {
            SensorEntity &sensorEntity = allSensorEntities[i];

            if (sensorEntity.protocol != "ModBus")
            {
                continue;
            }

            if (sensorEntity.status != SENSOR_STATUS_NEW && sensorEntity.status != SENSOR_STATUS_ADDED)
            {
                continue;
            }

            const int address = sensorEntity.modbusDeviceAddress;
            if (address < MODBUS_ADDRESS_MIN || address > MODBUS_ADDRESS_MAX)
            {
                sensorsNeedingNewAddress.push_back(&sensorEntity);
                log("STARTUP_SCAN: sensor %s has address 0x%02X outside valid range 0x%02X-0x%02X, needs new address",
                    sensorEntity.uid.c_str(), address, MODBUS_ADDRESS_MIN, MODBUS_ADDRESS_MAX);
            }
            else
            {
                addressMap[address].push_back(&sensorEntity);
            }
        }
    }

    void findDuplicateAddresses(const std::map<int, std::vector<SensorEntity *>> &addressMap,
                                std::vector<SensorEntity *> &sensorsNeedingNewAddress)
    {
        for (std::map<int, std::vector<SensorEntity *>>::const_iterator pair = addressMap.begin();
             pair != addressMap.end(); ++pair)
        {
            const int address = pair->first;
            const std::vector<SensorEntity *> &sensors = pair->second;

            if (sensors.size() <= 1)
            {
                continue;
            }

            size_t ownerIndex = 0;
            size_t perPortMatches = 0;
            for (size_t i = 0; i < sensors.size(); i++)
            {
                if (sensors[i]->modbusDeviceAddress == MODBUS_ADDRESS_MIN + sensors[i]->pin)
                {
                    if (perPortMatches == 0)
                    {
                        ownerIndex = i;
                    }
                    perPortMatches++;
                }
            }

            const char *rule = "per_port_match";
            if (perPortMatches != 1)
            {
                ownerIndex = 0;
                rule = "first_in_repository";
            }

            log("STARTUP_SCAN: keeping sensor %s by rule %s, marking others for address change",
                sensors[ownerIndex]->uid.c_str(), rule);

            std::string movedUids;
            for (size_t i = 0; i < sensors.size(); i++)
            {
                if (i == ownerIndex)
                {
                    continue;
                }

                sensorsNeedingNewAddress.push_back(sensors[i]);
                if (!movedUids.empty())
                {
                    movedUids += ",";
                }
                movedUids += "\"" + sensors[i]->uid + "\"";
            }

            if (_logger)
            {
                char addressText[8];
                snprintf(addressText, sizeof(addressText), "0x%02X", address);
                std::string details = "{\"address\":\"" + std::string(addressText) +
                                      "\",\"keptUid\":\"" + sensors[ownerIndex]->uid +
                                      "\",\"rule\":\"" + rule +
                                      "\",\"movedUids\":[" + movedUids + "]}";
                _logger->logJson("SENSOR_ADDRESS", "duplicate_owner_chosen", details.c_str());
            }
        }
    }

    /** @brief The empty-port records of a sensor that did not answer, as the boot scan built them. */
    void addOfflinePlaceholders(Node &node, const SensorEntity &sensorEntity)
    {
        if (sensorEntity.isMultiChannel())
        {
            if (sensorEntity.protocol == "ModBus")
            {
                for (size_t i = 0; i < sensorEntity.modbusRegisterConfigs.size(); i++)
                {
                    pushOfflineChannel(node, sensorEntity, sensorEntity.modbusRegisterConfigs[i].channel);
                }
            }
            else if (sensorEntity.protocol == "I2C")
            {
                for (size_t i = 0; i < sensorEntity.i2cRegisterConfigs.size(); i++)
                {
                    pushOfflineChannel(node, sensorEntity, sensorEntity.i2cRegisterConfigs[i].channel);
                }
            }
            else
            {
                log("STARTUP_SCAN: unsupported protocol for offline multi-channel sensor: %s",
                    sensorEntity.protocol.c_str());
            }

            addOfflineVirtualChannels(node, sensorEntity);
            return;
        }

        Sensor sensor;
        sensor.uid = sensorEntity.uid + ".01";
        sensor.type = "none";
        sensor.pin = sensorEntity.pin;

        upsertRecord(node, sensor);

        log("STARTUP_SCAN: added offline single-channel sensor placeholder %s, type: none, protocol: %s, pin: %d",
            sensor.uid.c_str(), sensorEntity.protocol.c_str(), sensor.pin);

        addOfflineVirtualChannels(node, sensorEntity);
    }

    /**
     * @brief The computed channels of an offline sensor, as empty ports like the rest of it.
     *
     * A computed channel has no register config behind it, so the placeholders above do not reach
     * it, and it takes two records to leave a structure saying both that a channel is gone and
     * that the value computed from it is still there. Leaving the record out instead would be
     * worse: the backend drops readings that arrive for a channel uid its structure does not
     * carry, and this sensor still has a record in every update packet the module sends.
     */
    void addOfflineVirtualChannels(Node &node, const SensorEntity &sensorEntity)
    {
        if (_virtualChannels == nullptr)
        {
            return;
        }

        const std::vector<IVirtualSensorChannelsUseCase::Channel> channels =
            _virtualChannels->channels(sensorEntity);
        for (size_t i = 0; i < channels.size(); i++)
        {
            pushOfflineChannel(node, sensorEntity, channels[i].channel);
        }
    }

    void pushOfflineChannel(Node &node, const SensorEntity &sensorEntity, int channel)
    {
        Sensor sensor;
        sensor.uid = sensorEntity.uid + "." + (channel < 10 ? "0" : "") + std::to_string(channel);
        sensor.type = "none";
        sensor.pin = sensorEntity.pin;
        sensor.group = 0;

        upsertRecord(node, sensor);

        log("STARTUP_SCAN: added offline %s sensor placeholder %s, type: none, channel: %d, pin: %d",
            sensorEntity.protocol.c_str(), sensor.uid.c_str(), channel, sensor.pin);
    }

    /**
     * @brief Write one record of the structure, replacing the one that carries the same uid.
     *
     * A stored entity is already described in the structure the boot built - from the registry,
     * before anything was asked of the line. When the discovery finds it silent, its record is
     * *replaced* by the empty-port one; pushing a second record with the same uid would leave the
     * structure saying two things about one channel.
     */
    static void upsertRecord(Node &node, const Sensor &sensor)
    {
        for (size_t i = 0; i < node.sensors.size(); i++)
        {
            if (node.sensors[i].uid == sensor.uid)
            {
                node.sensors[i] = sensor;
                return;
            }
        }
        node.sensors.push_back(sensor);
    }

    /** @brief One stored row, described by what the line is answering for it. */
    void describeStoredSensor(Node &node, const SensorEntity &sensorEntity)
    {
        LivenessNote &note = livenessNoteFor(sensorEntity.uid);
        const bool emptyPort = describedAsEmptyPort(sensorEntity, note);

        note.emptyPortBeforeTheBuild = note.describedEmptyPort;
        note.describedEmptyPort = emptyPort;
        note.describedInThisPass = true;

        if (emptyPort)
        {
            log("STARTUP_SCAN: sensor %s is not answering - described as an empty port",
                sensorEntity.uid.c_str());
            addOfflinePlaceholders(node, sensorEntity);
            return;
        }

        addSensorToStructure(node, sensorEntity);
    }

    /**
     * @brief The whole rule, in one place: is this row's port described as empty?
     *
     * Both the describer and reviewSensorLiveness() ask it, and they have to ask the same
     * question: a review that answered it differently would find a difference where the structure
     * has none and send one every time it looked.
     */
    bool describedAsEmptyPort(const SensorEntity &sensorEntity, LivenessNote &note)
    {
        return sensorEntity.status != SENSOR_STATUS_ADDED && lineIsSilentFor(sensorEntity, note);
    }

    /**
     * @brief Is the line failing to answer for this row - now, not at some point in the past?
     *
     * Two sources, and the fresher one wins:
     *
     *   - the sweep. A row it has read since the restart has a read time in the value model, which
     *     lives in RAM and is empty after a restart; from that moment its error flag is this
     *     module's live health for that row - false while the reads succeed, true from the first
     *     failure. That is the same flag the telemetry state and the port recovery run on;
     *   - the discovery's ping, for the rows the sweep has not read yet. A restart that skips the
     *     discovery pings nothing, so this is empty and every stored row is described as it stands.
     *
     * What is deliberately not a source is the error flag of a row nothing has read yet: that one
     * came out of the registry file, where it was left by whatever happened before the restart,
     * and a structure shaped by it would report a healthy sensor as an empty port on the strength
     * of an old failure.
     *
     * The sweep's flag is up after a single failed read and down after a single one that answered;
     * this answer waits on both edges - SILENT_AFTER_MISSED_SWEEPS to give a row up,
     * ANSWERING_SWEEPS_TO_RETURN to take it back. Only this answer waits: the flag itself is
     * untouched, and the telemetry, the rules and the port recovery go on reading it as they
     * always have.
     *
     * Waiting on both edges is what makes the pair of thresholds a hysteresis rather than a delay.
     * With only the first, a device that answers once a minute - which is what a bad contact and
     * the port recovery produce together - settles as gone, then as present, then as gone, and
     * every one of those is a structure.
     */
    bool lineIsSilentFor(const SensorEntity &sensorEntity, LivenessNote &note)
    {
        const uint32_t lastReadMs =
            (_sensorValues != nullptr) ? _sensorValues->getLastReadTime(sensorEntity.uid) : 0;

        if (lastReadMs == 0)
        {
            // Nothing has read the row since the restart: the discovery's ping is the only fact
            // there is, and a ping is one question and one answer - there is nothing to settle.
            note.answeringSinceMs = 0;
            note.settledEmptyPort = wasFoundSilent(sensorEntity.uid);
            return note.settledEmptyPort;
        }

        // The ping's answer is spent: from here the sweep is the fact, and the uid is not worth
        // holding on to.
        forgetSilent(sensorEntity.uid);

        if (_arduino == nullptr)
        {
            // No clock, no waiting on either edge: the flag as it stands, which is what a caller
            // assembled by hand, such as a test, gets. globals.h wires one.
            note.answeringSinceMs = 0;
            note.settledEmptyPort = sensorEntity.isError;
            return note.settledEmptyPort;
        }

        if (sensorEntity.isError)
        {
            // Whatever recovery had begun is over; it starts again from the next answer.
            note.answeringSinceMs = 0;
            // Not while the restart's pass runs, and not for the silence it caused: its model
            // scans take the power of the rest of the wire they ask and leave it so, its
            // windows and address writes take the power of others, and the sweep leaves such a
            // port unread until it has had its power for PORT_WAKE_UP_MS again - time with no
            // read in it, which the count below would take for missed sweeps. A row that answered
            // the pass is described as a sensor in the structures it sends as it goes, and an
            // empty port in the one it ends with would be a sensor shown and then taken back. So
            // the misses are counted from the end of the pass: a row that is really gone is given
            // up on that many sweeps after it.
            if (!note.settledEmptyPort && !isRunning() &&
                missedSweepsSince(silenceCountedFrom(lastReadMs)) >= SILENT_AFTER_MISSED_SWEEPS)
            {
                note.settledEmptyPort = true;
            }
            return note.settledEmptyPort;
        }

        if (!note.settledEmptyPort)
        {
            note.answeringSinceMs = 0;
            return false;
        }

        // Answering, against a port the structure has given up on. The first answer starts the
        // clock and settles nothing; the row is taken back once it has been answering for the
        // sweeps in between.
        const uint32_t now = static_cast<uint32_t>(_arduino->millis());
        if (note.answeringSinceMs == 0)
        {
            note.answeringSinceMs = (now == 0) ? 1 : now;
            return true;
        }

        if ((now - note.answeringSinceMs) >= (ANSWERING_SWEEPS_TO_RETURN - 1) * SWEEP_INTERVAL_MS)
        {
            note.answeringSinceMs = 0;
            note.settledEmptyPort = false;
        }
        return note.settledEmptyPort;
    }

    /**
     * @brief How many sweeps have gone by since this row was last read successfully.
     *
     * Counted from the time of the last good read rather than from the failures themselves: a
     * failed read leaves no mark of its own anywhere the control loop can see - it raises a flag
     * that is already up - while every successful one is stamped in the value model. Between two
     * stamps the sweep is what passes, at its own cadence, so the elapsed time divided by that
     * cadence is the number of reads this row did not answer.
     *
     * Without a clock the flag is taken as it stands, which is what a caller assembled by hand,
     * such as a test, gets; globals.h wires one.
     */
    uint32_t missedSweepsSince(uint32_t lastReadMs)
    {
        if (_arduino == nullptr)
        {
            return SILENT_AFTER_MISSED_SWEEPS;
        }

        const uint32_t silentForMs = static_cast<uint32_t>(_arduino->millis()) - lastReadMs;
        return silentForMs / SWEEP_INTERVAL_MS;
    }

    /**
     * @brief The last good read of a row, or the end of the restart's pass if that came later.
     *
     * Which came later is read off their ages - how long ago each was - and not off the two moments:
     * millis() wraps after 49.7 days, and the difference of two moments of it says which is the later
     * one only while they are less than half of that apart. The end of the pass stops being one of
     * the two as soon as it can no longer decide anything; see
     * forgetTheEndOfTheRestartsPassOnceItDecidesNothing().
     */
    uint32_t silenceCountedFrom(uint32_t lastReadMs)
    {
        forgetTheEndOfTheRestartsPassOnceItDecidesNothing();
        if (!_restartsPassEnded)
        {
            return lastReadMs;
        }
        const uint32_t now = nowMs();
        return ((now - _restartsPassEndedAtMs) < (now - lastReadMs)) ? _restartsPassEndedAtMs : lastReadMs;
    }

    /**
     * @brief Let go of the end of the restart's pass once SILENT_AFTER_MISSED_SWEEPS sweeps have gone
     *        by since it.
     *
     * From that moment counting a row's silence from the later of its last read and the end of the
     * pass gives up on exactly the rows counting from its last read does - the end of the pass is
     * itself that many sweeps back - so the moment decides nothing any more. Kept, it would be an age
     * the clock wraps round 49.7 days on.
     */
    void forgetTheEndOfTheRestartsPassOnceItDecidesNothing()
    {
        if (_restartsPassEnded && _arduino != nullptr &&
            (nowMs() - _restartsPassEndedAtMs) >= SILENT_AFTER_MISSED_SWEEPS * SWEEP_INTERVAL_MS)
        {
            _restartsPassEnded = false;
        }
    }

    /**
     * @brief The note of a row, made on the spot if this is the first time the row is described.
     *
     * One entry per stored row and no more: entries are written only from the describer, which is
     * driven by the registry, and a row that leaves the registry loses its entry on the next pass
     * over the whole of it.
     */
    LivenessNote &livenessNoteFor(const std::string &uid)
    {
        LivenessNote *existing = describedLivenessOf(uid);
        if (existing != nullptr)
        {
            return *existing;
        }

        LivenessNote note;
        note.uid = uid;
        _describedLiveness.push_back(note);
        return _describedLiveness.back();
    }

    LivenessNote *describedLivenessOf(const std::string &uid)
    {
        for (size_t i = 0; i < _describedLiveness.size(); i++)
        {
            if (_describedLiveness[i].uid == uid)
            {
                return &_describedLiveness[i];
            }
        }
        return nullptr;
    }

    void dropNotesOfRowsThatLeftTheRegistry()
    {
        for (size_t i = _describedLiveness.size(); i > 0; i--)
        {
            if (!_describedLiveness[i - 1].describedInThisPass)
            {
                _describedLiveness.erase(_describedLiveness.begin() + (i - 1));
            }
        }
    }

    /**
     * @brief Record that a port answered the line and matched no model of the catalog.
     *
     * One line in the log and one entry per port - at most as many as the module has ports, and
     * the entry is dropped the moment the port either goes quiet or is identified. What it buys is
     * the promise the ladder makes about the expensive rung: a device that answers a broadcast and
     * matches nothing gets one identification pass and is then left alone, instead of being
     * chased once per pass for as long as the module runs.
     */
    void rememberPortGaveUpIdentification(int pin)
    {
        if (portGaveUpIdentification(pin))
        {
            return;
        }

        _unidentifiedPins.push_back(pin);
        _unidentifiedOccupantCount++;
        log("STARTUP_SCAN: pin %d has something on it that matches no sensor type - it is described "
            "as an empty port and no further identification is attempted (%u so far); "
            "a ScanSensors command tries again",
            pin, (unsigned)_unidentifiedOccupantCount);
    }

    bool portGaveUpIdentification(int pin) const
    {
        for (size_t i = 0; i < _unidentifiedPins.size(); i++)
        {
            if (_unidentifiedPins[i] == pin)
            {
                return true;
            }
        }
        return false;
    }

    void forgetPortGaveUpIdentification(int pin)
    {
        for (size_t i = 0; i < _unidentifiedPins.size(); i++)
        {
            if (_unidentifiedPins[i] == pin)
            {
                _unidentifiedPins.erase(_unidentifiedPins.begin() + i);
                return;
            }
        }
    }

    /**
     * @brief Record that a device did not take the address the module wrote it.
     *
     * One entry per row and the same promise the note about an unidentifiable port makes: what
     * failed once at a cost the rest of its wire pays is not attempted again by the module on its
     * own. The row keeps the address it had, the sweep goes on reading it there, and the way
     * back is a person asking for the port by hand.
     */
    void rememberTheRowWouldNotMove(const std::string &uid)
    {
        if (isAmong(_rowsThatWouldNotMove, uid))
        {
            return;
        }

        _rowsThatWouldNotMove.push_back(uid);
        _factoryAddressMoveRefusedCount++;
        log("STARTUP_SCAN: sensor %s did not take an address of the module - it stays where it is "
            "and is not written to again (%u so far); a ScanSensors command tries again",
            uid.c_str(), (unsigned)_factoryAddressMoveRefusedCount);
    }

    /**
     * @brief Is there something on this row's port answering the module, as far as it knows?
     *
     * The same two sources lineIsSilentFor() reads, and the fresher one wins: the sweep once it has
     * read the row, and the discovery's ping before that. Without either the answer is no - the
     * module has no fact about the port and must not act as though something had refused it.
     *
     * The ping counts only on the restart's own pass, because that is the pass that puts it: every
     * stored row is pinged there minutes before the address phase of the same pass writes to it. On
     * a later pass the ping belongs to a pass long over, and what is left of it says only which
     * rows nothing has answered for since.
     */
    bool theLineAnswersForTheRow(const SensorEntity &row) const
    {
        if (isAmong(_rowsAdoptedThisPass, row.uid))
        {
            // This pass put the question itself, on this port, and the model scan answered it.
            return true;
        }

        const uint32_t lastReadMs =
            (_sensorValues != nullptr) ? _sensorValues->getLastReadTime(row.uid) : 0;
        if (lastReadMs != 0)
        {
            // Read since the restart: from there the sweep's flag is the fact, exactly as it is for
            // the structure. The row is the one buildAddressStep() took out of the registry a step
            // ago - the sweep cannot have run since, because this pass has held the line all along.
            return !row.isError;
        }
        return _pass == Pass::WHOLE_MODULE && !wasFoundSilent(row.uid);
    }

    /** @brief Let the rows of one port be written to again, because a person asked about it. */
    void forgetTheRowsOfThePortWouldNotMove(int pin)
    {
        if (_sensorRepository == nullptr)
        {
            return;
        }

        for (size_t i = _rowsThatWouldNotMove.size(); i > 0; i--)
        {
            Utils::Optional<SensorEntity> row =
                _sensorRepository->get(_rowsThatWouldNotMove[i - 1].c_str());
            if (!row || row.value().pin == pin)
            {
                _rowsThatWouldNotMove.erase(_rowsThatWouldNotMove.begin() + (i - 1));
            }
        }
    }

    bool wasFoundSilent(const std::string &uid) const
    {
        for (size_t i = 0; i < _silentToThePing.size(); i++)
        {
            if (_silentToThePing[i] == uid)
            {
                return true;
            }
        }
        return false;
    }

    void rememberSilent(const std::string &uid)
    {
        if (!wasFoundSilent(uid))
        {
            _silentToThePing.push_back(uid);
        }
    }

    void forgetSilent(const std::string &uid)
    {
        for (size_t i = 0; i < _silentToThePing.size(); i++)
        {
            if (_silentToThePing[i] == uid)
            {
                _silentToThePing.erase(_silentToThePing.begin() + i);
                return;
            }
        }
    }

    void addSensorToStructure(Node &node, const SensorEntity &sensorEntity)
    {
        if (_sensorStructure == nullptr)
        {
            log("STARTUP_SCAN: sensor structure use case is not wired - sensor %s is not added to the structure",
                sensorEntity.uid.c_str());
            return;
        }

        _sensorStructure->addSensor(node, sensorEntity);
    }

    void log(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        writeLogLine(format, args);
        va_end(args);
    }

    /**
     * @brief A line about a pass getting on with its work - the ports it walks, the models it
     *        skips, the power it raises and puts back.
     *
     * Said by the passes that end and that somebody is waiting for: the one a restart arms and the
     * one a ScanSensors command asks for. Both are finite, and a person reading the log of one is
     * reading it because they are watching that boot or that command.
     *
     * The pass over the free ports is neither. It comes round every FREE_PORT_PROBE_INTERVAL_MS
     * for as long as the module runs, and on a module where nothing changes it walks the same
     * ports past the same models to the same answer - so these lines from it are the same handful
     * repeated twice a minute for months, which is what buries the lines that mean something. What
     * it does say is what it found out: a sensor adopted, a port that went quiet, a row moved off
     * an address, a write refused. A pass that learned nothing says nothing.
     *
     * The counters are untouched by this - cheapProbeCount(), presencePollCount(),
     * freePortPassCount() and the rest still say how much of everything happened. Three of them are
     * read from outside: adoptedCount(), factoryAddressMoveRefusedCount() and identifyStepCount()
     * are columns of the DIAG-COUNTERS line, so a quiet pass that took a sensor in, was refused an
     * address or spent an identification step still moves a number the field can see. The rest are
     * reachable from a test and a debugger and nowhere else: the rest of what a quiet pass did is
     * counted and not reported anywhere, and the log is the whole of what the field sees.
     */
    void logPassProgress(const char *format, ...)
    {
        if (_pass == Pass::FREE_PORTS)
        {
            return;
        }

        va_list args;
        va_start(args, format);
        writeLogLine(format, args);
        va_end(args);
    }

    void writeLogLine(const char *format, va_list args)
    {
        if (_logger == nullptr || _debugSystem == nullptr ||
            !_debugSystem->isFeatureEnabled(DebugFeatures::SETUP_DEBUG))
        {
            return;
        }

        char buffer[320];
        vsnprintf(buffer, sizeof(buffer), format, args);

        IDebugFeature *feature = _debugSystem->getFeature(DebugFeatures::SETUP_DEBUG);
        if (feature)
        {
            _logger->println(DebugFeatures::SETUP_DEBUG, buffer, feature->getLogChannel());
        }
        else
        {
            _logger->println(DebugFeatures::SETUP_DEBUG, buffer);
        }
    }

    // --- The presence poll, as the address discovery sends it ----------------------
    // Slave 0xFF is the wildcard those frames use; the two (function code, register) pairs are the
    // ws12s and cwyw queries of ModbusSensorReader, which is where a device that does not hold the
    // address it is asked for still answers.
    static const uint8_t WILDCARD_SLAVE_ADDRESS = 0xFF;
    static const uint8_t WILDCARD_QUERY_FC = 0x03;
    static const uint16_t WILDCARD_QUERY_REGISTER = 0x0100;
    static const uint8_t VENDOR_QUERY_FC = 0x6E;
    static const uint16_t VENDOR_QUERY_REGISTER = 0x0012;
    /** Address + function + one data byte + CRC: nothing shorter can be a frame. */
    static const size_t MIN_MODBUS_FRAME_BYTES = 5;

    ILogger *_logger;
    IDebugSystem *_debugSystem;
    IModuleConfig *_moduleConfig;
    ISensorRepository *_sensorRepository;
    ISensorTypeRepository *_sensorTypeRepository;
    ISensorReader *_sensorReader;
    ModuleStructure *_moduleStructure;
    ISensorStructureUseCase *_sensorStructure;
    IModbusAddressAllocator *_modbusAddressAllocator;
    /** Live health of the stored sensors, as the sweep leaves it. Read only, never written here. */
    ISensorValueModel *_sensorValues;
    /** Which channels of a sensor are computed rather than read - the empty ports owe them too. */
    IVirtualSensorChannelsUseCase *_virtualChannels;
    /** The clock the missed sweeps are counted on. Read only, and only off the line. */
    IArduinoAdapter *_arduino;
    IRepublishModuleStructureUseCase *_republish = nullptr;
    /** The boot sequence's gate in front of the ladder; optional, see setBootGates(). */
    IBootGates *_bootGates = nullptr;
    /**
     * The boot found no catalog to arm the ladder with: what it would have armed is owed to the
     * first synchronization that brings one. See onCatalogSettled().
     */
    bool _armOwed = false;
    bool _armOwedPreparationOnly = false;
    /**
     * The boot reported placeholders for the ports - it had no catalog to describe the stored
     * sensors by - and no structure built from the registry has been sent since. The pass the
     * catalog arms ends with one, whatever it found out; see finish().
     */
    bool _placeholdersReported = false;
    /**
     * A structure of a pass is owed: it was asked for while another was in flight. One at most,
     * and its reason the last one asked for - see sendTheStructureOfTheRestartsPass().
     */
    bool _structureOwed = false;
    const char *_owedStructureReason = "";
    /**
     * The last structure was described from a registry that did not read back - as a module with no
     * sensor - and the server takes it for one. Another is owed once the registry reads back; see
     * reviewSensorLiveness().
     */
    bool _describedARegistryThatDidNotReadBack = false;
    /** The ladder is held until the registry reads back; one line on the tape per hold. */
    bool _heldForTheRegistry = false;
    /** The one way onto the line that can ask a port rather than an address. Optional. */
    IRawBusAccess *_rawBus = nullptr;

    Pass _pass = Pass::WHOLE_MODULE;
    Rung _rung = Rung::CHEAP;
    Phase _phase = Phase::IDLE;
    bool _finished = false;
    bool _stepInFlight = false;
    bool _stepPending = false;
    StartupScanStep _inFlight;

    // The plan, held as uids and port numbers rather than entities: bounded by what the registry
    // holds and by the number of ports of the module, and a fraction of the size either would be
    // as a list of steps.
    std::vector<std::string> _verifyUids;
    size_t _verifyCursor = 0;

    SensorTypeCatalog _catalog;
    std::string _moduleUid;
    std::vector<int> _scanPins;
    size_t _scanPinCursor = 0;
    /**
     * The restart's pass only: the ports the cheap rung left unnamed, walked again for the rest of
     * the ladder once every free port has been asked it. At most one entry per port the pass
     * walks, and let go with the rest of the plan.
     */
    std::vector<int> _pinsLeftToTheLadder;
    /** The restart's pass has asked every free port the cheap rung; the walk is over the rest. */
    bool _cheapRungAskedEverywhere = false;
    /**
     * The restart's pass only: the ports the rest of the ladder walks that are asked the cheap rung
     * again first, because a sensor was found on their wire. At most one entry per port the pass
     * walks, and let go with the rest of the plan.
     */
    std::vector<int> _pinsOwedTheCheapRungAgain;
    size_t _scanTypeCursor = 0;
    /** The cheap rung's range: which address of it, and which model at it, is next. Every pass but a person's asks it. */
    size_t _cheapRangeCursor = 0;
    /** The cheap rung of the port at the cursor has left out an address a stored sensor of its wire holds. */
    bool _cheapRungLeftAnAddressOut = false;
    /** The port of the moment already has its answer: adopted, or claimed by somebody else. */
    bool _pinSettled = false;

    std::vector<std::string> _addressWrites;
    size_t _addressCursor = 0;

    /**
     * Sensors this pass owes an address write to because a free port of their wire kept hearing
     * them at their factory address. Bounded by the ports the pass walks, one entry each, and let
     * go with the rest of the plan when the pass ends.
     */
    std::vector<std::string> _neighboursToMove;

    /**
     * Rows this pass owes an address write to because they still hold the address their model left
     * the factory with. Bounded by the registry, one entry each, and let go with the rest of the
     * plan when the pass ends.
     */
    std::vector<std::string> _rowsOnAFactoryAddress;

    /**
     * Rows this pass identified on the line itself. The model scan that adopts a sensor is the
     * freshest answer there is about that port, and it is the one thing the sweep and the ping
     * cannot have yet: the row did not exist when the pass began. Bounded by the ports the pass
     * walks, one entry each, and let go with the rest of the plan when the pass ends.
     */
    std::vector<std::string> _rowsAdoptedThisPass;

    /**
     * Stored rows that answered the ping of the restart's pass. What keeps the cheap rung off an
     * address of the paired port's sensor on a live wire - only where that sensor answered for
     * itself. One entry per stored row at most, reserved with the ping's plan, and let go with the
     * rest of it when the pass ends.
     */
    std::vector<std::string> _rowsThatAnsweredThePing;

    /** A row the restart's pass has just adopted and owes a move before its next step; empty for none. */
    std::string _rowToPartFromItsWire;

    /**
     * A stored row the restart's pass owes a move off the address a device it has just adopted holds as
     * its own port's - made once no window holds the row's port dark; empty for none.
     */
    std::string _storedRowToMoveOffItsWire;

    /** A port the restart's pass owes the step that takes it down, before its next step; -1 for none. */
    int _pinToTakeDown = -1;

    /**
     * The restart's pass only: the ports taken down after their cheap rung, until the walk comes back
     * to them - see buildTheStepThatTakesDown(). At most one entry per port the pass walks, reserved
     * with the plan and let go with the rest of it.
     */
    std::vector<int> _pinsHeldDarkUntilTheirWalk;

    /**
     * Rows whose device did not take an address the module wrote it. One attempt per row and no
     * more: a model the write does not reach does not start reaching it because the module asks
     * twice a minute, and every attempt costs the rest of its wire its power. Kept for
     * the life of the module, one entry per row at most, and the way back is a ScanSensors command
     * - the same way back a port the module gave up identifying has.
     */
    std::vector<std::string> _rowsThatWouldNotMove;

    /** Stored sensors the discovery pinged and got nothing from, until the sweep reads them. */
    std::vector<std::string> _silentToThePing;
    bool _didDiscover = false;

    /**
     * Ports that answered the line and matched no model, held for as long as they keep answering.
     * At most one entry per port of the module - the entries are written by the pass, from the
     * list of free ports, and dropped when the port goes quiet, when it is identified, or when a
     * ScanSensors command asks for the ladder to be walked again.
     */
    std::vector<int> _unidentifiedPins;

    /**
     * Addresses a free port is answered at by the sensor of the port sharing its wire, one entry
     * per port at most. Written when a confirming scan has proved it - the address answered with
     * the wire whole and nobody answered it with the wire isolated - and dropped as soon as the
     * registry stops carrying a sensor of that wire at that address, which is what the move this
     * class asks for brings about.
     */
    std::vector<WireAnswer> _wireAnswers;

    /**
     * Which UART each port is wired to, as globals.h wires them. One entry per port, written once,
     * empty on a module nobody told - and then every port is treated as a line of its own.
     */
    std::vector<int> _lineOfPort;
    /**
     * Ports whose device answers at an address a stored sensor of their wire holds and was not kept by
     * the restart's pass - see dropTheFindThatCannotBeParted() - and the address: the cheap pass leaves
     * them dark (see thePortIsLeftDark()). One entry per port at most, dropped once no stored sensor of
     * the wire holds that address any more.
     */
    std::vector<WireAnswer> _portsLeftDark;
    /** Bus half only: the ports a presence poll took down, waiting to be given back. */
    std::vector<int> _isolatedPorts;
    /** Bus half only: the ports the pass took down, and when - see keepsThePortDark(). */
    std::vector<int> _portsTakenDown;
    uint32_t _portsTakenDownAtMs = 0;
    /** Bus half only: the port whose window is open, or -1. See holdsAWindowOpen(). */
    int _windowPin = -1;
    /** Bus half only: the ports the last poll gave back, and when - see giveTheLineBack(). */
    std::vector<int> _portsComingBack;
    uint32_t _portsCameBackAtMs = 0;

    /** Control loop only: the window of the port at the cursor, and what it has heard so far. */
    bool _windowOpen = false;
    bool _windowHeard = false;
    bool _windowScanned = false;

    /**
     * When a rung last isolated - took the power off ports it was not asking - and whether one
     * ever has. What it holds off is the address phase: a device is deaf for its warm-up after
     * that, and a write it cannot answer is a write the module gives up on.
     */
    uint32_t _portsLostTheirPowerAtMs = 0;
    bool _portsLostTheirPower = false;

    /**
     * A step of the restart's pass has taken the power of other ports down since the last step that
     * raised it: the next port the pass walks gets its power back, and its warm-up, first.
     */
    bool _portsOweTheirPowerBack = false;
    /** When that step was, and whether there has been one in this pass. */
    bool _portsAheadWentDark = false;
    uint32_t _portsAheadWentDarkAtMs = 0;

    /** The power window of the pass in progress: opened by a step, closed by a step. */
    bool _freePortsPowered = false;
    bool _freePortsReleased = false;
    /** Whether any port had to be switched on by this pass, and when. */
    bool _freePortsOweAWakeUp = false;
    uint32_t _freePortsPoweredAtMs = 0;
    bool _scanWaitingForWakeUp = false;

    /** A pass a person asked for, and the one port it is about; -1 while none is owed. */
    bool _rescanRequested = false;
    int _rescanPin = -1;
    /** The port the pass in progress is about, when that pass is a NAMED_PORT one. */
    int _namedPin = -1;
    /** When the last pass over the free ports ended, and whether there has been one. */
    uint32_t _freePortPassEndedAtMs = 0;
    bool _freePortPassRan = false;
    /** When the boot armed the ladder, and whether the window that starts there is over. */
    uint32_t _armedAtMs = 0;
    bool _settled = false;
    /** Sensors adopted by the pass in progress - what decides whether it owes an address and a structure. */
    uint32_t _adoptedInThisPass = 0;
    /** Steps the pass in progress has applied - what the line it ends with is about. */
    uint32_t _stepsInThisPass = 0;

    /**
     * What the module holds about the liveness of each row: what the last structure said, and how
     * far the row has moved towards the opposite answer. One entry per row of the registry - the
     * describer is the only writer and it writes what the registry gives it - kept for the life of
     * the module, because a change is only visible against what was said before it.
     */
    std::vector<LivenessNote> _describedLiveness;
    /**
     * When the restart's pass ended: what a row's silence is counted from if the row last answered
     * before it. Held for SILENT_AFTER_MISSED_SWEEPS sweeps after that and no longer.
     */
    uint32_t _restartsPassEndedAtMs = 0;
    bool _restartsPassEnded = false;
    uint32_t _livenessReviewedAtMs = 0;
    uint32_t _livenessRepublishedAtMs = 0;

    uint32_t _armCount = 0;
    uint32_t _preparationArmCount = 0;
    uint32_t _lateArmCount = 0;
    uint32_t _takenStepCount = 0;
    uint32_t _appliedStepCount = 0;
    uint32_t _adoptedCount = 0;
    uint32_t _cheapProbeCount = 0;
    uint32_t _presencePollCount = 0;
    uint32_t _identifyStepCount = 0;
    uint32_t _freePortPassCount = 0;
    uint32_t _unidentifiedOccupantCount = 0;
    uint32_t _factoryAddressMoveCount = 0;
    uint32_t _factoryAddressMoveRefusedCount = 0;
    uint32_t _rescanRequestCount = 0;
    uint32_t _livenessRepublishCount = 0;
    uint32_t _livenessRepublishHeldCount = 0;
    uint32_t _livenessUndeliveredCount = 0;
};

#endif // STARTUP_SENSOR_SCAN_USE_CASE_H
