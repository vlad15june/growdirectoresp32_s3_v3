#pragma once

#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Data/Entities/Sensor/SensorTypeCatalog.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Interfaces/UseCases/IStoredSensorDescriber.h"
#include "Core/Domain/Interfaces/UseCases/Sensors/IScanEntryStatus.h"

/**
 * @brief One indivisible piece of line time the startup discovery asks the bus task for.
 *
 * The discovery a module performs after a restart - ping what the registry holds, try the catalog
 * on the free ports, move the devices that collide off each other's addresses, apply the per-model
 * register writes - is minutes of RS-485 traffic. It is expressed as a sequence of steps of this
 * shape so that the sweep and the commands of the backend keep their turn on the line: one step is
 * one model tried on one port, or one sensor pinged, or one address written - seconds, not minutes.
 *
 * Every step is prepared on the control loop, from the repositories the loop owns, and carries
 * everything the bus task needs. The bus task reads no repository.
 */
struct StartupScanStep
{
    enum Kind
    {
        NONE = 0,
        /** Ping one entity the registry already holds, as it is stored. */
        VERIFY_KNOWN,
        /**
         * Ask one catalog model at one address on a free port - its own factory address, or on the
         * restart's pass and the periodic one an address of the module's range: one request, no
         * retries.
         */
        PROBE_CANDIDATE,
        /**
         * Ask a free port whether anything at all is on it, without naming an address. On the
         * restart's own pass it also opens the port's window: the ports sharing the wire stay
         * dark after it, for the window's later steps - see `catalog`.
         */
        PROBE_PRESENCE,
        /** Try one catalog model on one port that no entity occupies, as the identification procedure does. */
        SCAN_CANDIDATE,
        /** Move one entity to an address no other entity of the module holds. */
        WRITE_ADDRESS,
        /** Port power and the per-model register writes, over the whole registry. */
        PREPARE_KNOWN,
        /** Raise the power of the free ports this pass is about to ask, and no frames. */
        POWER_FREE_PORTS,
        /**
         * Lower it again on the ports the pass found nothing on - or, with `pin` set, on the one free
         * port a pass takes down after its cheap rung (see
         * StartupSensorScanUseCase::buildTheStepThatTakesDown()).
         */
        RELEASE_FREE_PORTS,
        /** Inside a port's window: one catalog model read at every address of the module's range. */
        PROBE_RANGE,
        /** Inside a port's window: one catalog model's address writes, sent to 0x00. */
        WRITE_PORT_ADDRESS,
        /** Close a port's window: give the ports of its wire their power back. No frames. */
        RELEASE_LINE
    };

    Kind kind = NONE;
    int pin = -1;
    /**
     * VERIFY_KNOWN, PROBE_CANDIDATE, SCAN_CANDIDATE, WRITE_ADDRESS: the entity the step talks to -
     * for PROBE_CANDIDATE at the address it asks.
     */
    SensorEntity entity;
    /** WRITE_ADDRESS: the address the entity is moved to. */
    int targetAddress = 0;
    /** PREPARE_KNOWN: the set the preparation is applied to. */
    std::vector<SensorEntity> entities;
    /** POWER_FREE_PORTS, RELEASE_FREE_PORTS: the ports whose power the step moves. */
    std::vector<int> pins;
    /**
     * PROBE_PRESENCE of the restart's pass, PROBE_RANGE, WRITE_PORT_ADDRESS: the catalog of the
     * pass - a handle to the list it already holds, not a copy. On PROBE_PRESENCE its presence is
     * what opens a window; every other pass polls and gives the power straight back.
     */
    SensorTypeCatalog catalog;
    /** PROBE_RANGE, WRITE_PORT_ADDRESS: the index of the model in `catalog`. */
    int model = -1;
};

/**
 * @brief What one startup step found on the line.
 *
 * Two words, not an entity. It travels back through the same result buffer the bus commands use,
 * which is reserved for eight entries and lives for as long as the module does; a `SensorEntity`
 * in here would be that struct eight times over in each of the two buffers that circulate, for a
 * value the loop can reconstruct. The step is not carried back either - the use case that produced
 * it is still holding it - so a detected model is that step's entity at the address below.
 */
struct StartupScanOutcome
{
    /**
     * A device answered: the ping succeeded, the model was detected, the address was written.
     *
     * POWER_FREE_PORTS reads it as "a port had to be given its power", which is what tells the
     * pass whether anything on those ports is still waking up.
     */
    bool answered = false;
    /** SCAN_CANDIDATE, PROBE_RANGE, WRITE_PORT_ADDRESS: the ModBus address the device answered on. */
    int answeredAddress = 0;
    /**
     * PROBE_RANGE, WRITE_PORT_ADDRESS: the index, in the step's catalog, of the model whose scan
     * passed - at answeredAddress. -1 when it did not.
     */
    int answeredModel = -1;
    /**
     * PROBE_CANDIDATE: the address was answered while the whole wire was live, and answered by
     * nobody once the confirming scan had taken the power of the rest of the wire down.
     *
     * So the frame came from another port of the same wire, and the port being asked has nothing
     * on it. Which port it came from is a question for the registry, and the registry belongs to
     * the control loop - this is the half of the answer the line can give.
     */
    bool heardBeforeTheWireWasIsolated = false;
};

/**
 * @brief The startup discovery, split between the control loop and the task that owns the line.
 *
 * `takeNextStep()` and `applyStepOutcome()` run on the control loop and are the only two that
 * touch the repositories and the module structure; `runStepOnBus()` runs on the acquisition task
 * and makes only bus transactions. One step is in flight at a time, so the queue in front of the
 * line never holds more of this than a single entry.
 */
/**
 * @brief The startup discovery.
 *
 * It is also the module's IStoredSensorDescriber, and not by convenience: describing a stored row
 * takes knowing whether the line is answering for it, and asking the line is what this use case
 * does until the sweep has an answer of its own.
 *
 * And it is the module's IScanEntryStatus, for the same kind of reason: whether a pass with an
 * end to it is under way is a fact only this machinery holds.
 */
class IStartupSensorScan : public IStoredSensorDescriber, public IScanEntryStatus
{
public:
    virtual ~IStartupSensorScan() = default;

    /** @brief Let the discovery produce steps. Until this is called it produces none. */
    virtual void arm() = 0;

    /**
     * @brief Ask for one more pass over a single named port, whatever the last one concluded.
     * @param pin The port the pass is about - the one the person named
     *
     * The discovery gives a port one full identification pass and then leaves it alone: a device
     * that answers the line but matches no model would otherwise be chased for ever. This is the
     * way back - the person's, through the ScanSensors command, which names the port they have
     * just plugged something into. It forgets what the last pass concluded about that port and
     * asks it again.
     *
     * The caller is the ScanSensors command and the pass belongs to it: the command runs the
     * full identification procedure on this same port itself, and the queue in front of the line
     * serves that before any step of this. So what this adds is the cheap questions once more, at
     * the end of the procedure's long power window - which is what a device too slow to answer any
     * pass of the module's own can still be found by.
     *
     * Safe to call at any moment: the pass in flight is never cut in half, the request is
     * honoured as soon as the discovery is between two steps.
     */
    virtual void rescan(int pin) = 0;

    /**
     * @brief Produce the preparation step and nothing else.
     *
     * The per-model register writes and the port power arrangement are not discovery: they are
     * what makes the sensors the module already holds readable, and they are owed to every boot -
     * including the ones that skip the discovery. A boot that skips it arms this instead.
     */
    virtual void armPreparationOnly() = 0;

    /**
     * @brief The boot had no catalog to arm with: arm - or arm the preparation only - once a
     *        catalog synchronization brings one.
     * @param preparationOnly What `noScanSensors` said on this boot, read before the boot cleared it
     *
     * Default: nothing is owed, for implementations that arm nothing late.
     */
    virtual void armWhenTheCatalogArrives(bool preparationOnly) { (void)preparationOnly; }

    /**
     * @brief A catalog synchronization has finished. Control loop only.
     *
     * Default: nothing to do.
     */
    virtual void onCatalogSettled() {}

    /** @brief True while there is discovery left to do. */
    virtual bool isRunning() const = 0;

    /**
     * @brief Take the next step to execute on the line. Control loop only.
     * @param step Filled with the work when there is any
     * @return true when a step was taken; false when one is already in flight or none is left
     */
    virtual bool takeNextStep(StartupScanStep& step) = 0;

    /** @brief Give a taken step back when it could not be queued. Control loop only. */
    virtual void returnStep() = 0;

    /** @brief The bus transactions of the step in flight. Acquisition task only. */
    virtual StartupScanOutcome runStepOnBus(const StartupScanStep& step) = 0;

    /**
     * @brief Is a port's window open - the ports sharing its wire held dark across several steps?
     *        Acquisition task only.
     *
     * While it is, nothing else may put power on those ports or a frame on that wire: a request
     * raises the power of the port it is sent through (ModbusRS485::sendData()), and the next
     * address write of the window, sent to 0x00, would then re-address whatever is plugged into
     * it. So the task that owns the line serves no bus command until the window is closed.
     */
    virtual bool holdsAWindowOpen() const = 0;

    /**
     * @brief Must the sweep leave this port alone right now? Acquisition task only.
     *
     * True for the ports a window holds dark, and for as long after the window - or after a
     * presence poll that took them down and gave them back - as what is plugged into them needs to
     * come up again. A read in that time would raise the port's power in the middle of the window,
     * or be answered by nobody and counted as a sensor that went quiet.
     */
    virtual bool keepsThePortDark(int pin) const = 0;

    /**
     * @brief Apply what the line answered to the registry and the structure. Control loop only.
     * @param outcome What runStepOnBus() came back with
     */
    virtual void applyStepOutcome(const StartupScanOutcome& outcome) = 0;

    /**
     * @brief Check the rows against the structure that was last sent, and send it again if they
     *        have parted. Control loop only.
     *
     * A structure is a snapshot and the liveness of a row is not: a sensor that was quiet when the
     * structure was built and answers ten minutes later stays absent from the app, and one that
     * has really gone stays in it, until something makes the module describe itself again. Nothing
     * on a settled module ever does - this is what does.
     *
     * Called on every iteration of the control loop; what it costs and how often it acts are the
     * implementation's business.
     */
    virtual void reviewSensorLiveness() = 0;
};
