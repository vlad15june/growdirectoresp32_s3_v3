#pragma once

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Application/Interfaces/ISensorAcquisition.h"
#include "Application/Services/BackgroundActs.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Sensors/ISensorReader.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"
#include "Core/Domain/UseCases/Sensors/PortWarmUpWatch.h"
#include "Core/Domain/UseCases/Sensors/SensorRecoveryUseCase.h"

/**
 * @brief One task that talks to the RS-485 sensors, so that the control loop does not.
 *
 * A channel costs about 145 ms when the sensor answers and 1400 ms when it does not (three
 * attempts of a 300 ms budget plus the 100 ms between them and the power settle) - line time no
 * control loop can afford: a module with one silent sensor would park its controller for well
 * over a second at a time, no rule evaluation, no relay switching, no button.
 *
 * So the polling runs in a task of its own and the loop only exchanges two vectors with it:
 * which sensors exist, and what has been read. Neither exchange touches the line.
 *
 * ### One task, not four
 *
 * Not because the ports are independent buses - they are not. `globals.h` (`modbusRS485_pin1` to `modbusRS485_pin4`) puts ports 0 and
 * 1 on the same `HardwareSerial` (RS485_1) and ports 2 and 3 on the other (RS485_2); only the
 * sensor power pin differs. Four tasks could not have polled four lines in parallel because there
 * are two lines, and two tasks would still have needed the same lock. One task walking the ports
 * in turn gives a deterministic order and one stack.
 *
 * ### One owner of the line, not one scheduler and one intruder
 *
 * The raw bus commands the backend sends - ModbusRead, ModbusWrite, SerialWrite,
 * RebootRemoteModule - are executed here too, between two steps of the sweep. Executed anywhere
 * else they would reach the line through the bus lock, a command and the sweep would compete for
 * the line, and the caller would pay for the collision. Queueing them to the task that already
 * owns the line replaces that contention with an order: one owner, one queue, and the control
 * loop never takes the bus lock at all.
 *
 * The bus parts of the complex module commands - the scans of PingPin, SetSensorModel and
 * ScanSensors - ride the same queue, for the same reason: a scan of a silent pin is seconds to
 * tens of seconds of line time, and the only place that time can live without being either
 * control-loop time or lock contention is the task that owns the line.
 *
 * The sensor discovery a restart performs is here too, one step at a time, and it is the one piece
 * of work that yields: it is served in the gap between two sweeps, and once after every sweep that
 * closes. A command has a person waiting for its answer and the sweep has a cadence to keep;
 * the discovery has minutes of work and no deadline, and the line is idle for most of every
 * fifteen-second interval.
 *
 * ### What it does not own
 *
 * The sensor repository. `Repository<T>` keeps an `unordered_map` cache with no lock and writes
 * through to SPIFFS; reading it here while the control loop writes it would be a data race, so
 * this task is handed a snapshot and hands back results, and every repository write stays on the
 * loop. That is also why the result carries `acquiredAtMs`: the value is applied later, but it has
 * to age from the moment the bus answered.
 */
class SensorAcquisitionService : public ISensorAcquisition
{
public:
    /** Name of the acquisition task; also what the host lock records waits against. */
    static constexpr const char* TASK_NAME = "sensor-acq";

    /** One full sweep of all sensors per this interval - the module's polling cadence. */
    static constexpr unsigned long SWEEP_INTERVAL_MS = 15000;

    /** How long the task sleeps when there is nothing configured to poll. */
    static constexpr uint32_t IDLE_STEP_MS = 100;

    /**
     * Stack for the acquisition task. The deepest polling chain is
     * `SensorReader::readMultiChannelSensor()` -> `ModbusSensorReader::readRegister()` ->
     * `awaitFrame()`, whose largest frames are two 64-byte buffers, plus the 256-byte log buffers
     * of SensorReader and the snapshot vector it iterates - about 4 KB. The complex bus commands
     * run through `SensorReader::scanSensor()`, whose chain is the same depth again plus a
     * `SensorEntity` frame; the served command itself is heap-held (see serveOneBusCommand()) so
     * the queue payload does not land here. 8 KB rather than the ~6 KB the polling alone would
     * need, because a stack overflow here shows up as a reboot on the device, not as a bad
     * reading - and on the host as run-to-run corruption.
     */
    static constexpr uint32_t DEFAULT_STACK_BYTES = 8192;

    SensorAcquisitionService(ISensorReader* sensorReader,
                             IArduinoAdapter* arduinoAdapter,
                             ITaskScheduler* taskScheduler,
                             ILogger* logger = nullptr,
                             IDebugSystem* debugSystem = nullptr)
        : _sensorReader(sensorReader),
          _arduino(arduinoAdapter),
          _scheduler(taskScheduler),
          _logger(logger),
          _debugSystem(debugSystem)
    {
        // The ceiling of a realistic drain, allocated once; the swap in drainBusCommandResults()
        // circulates this capacity with the consumer's reserved buffer.
        _busResults.reserve(RESULT_BUFFER_RESERVE);
    }

    // --- ISensorAcquisition ------------------------------------------------------------------

    void setSensors(const std::vector<SensorEntity>& modbusSensors) override
    {
        std::vector<SensorEntity> ordered = interleaveByPort(modbusSensors);

        std::lock_guard<std::mutex> guard(_stateMutex);

        // The entities are refreshed every time, even when the set is unchanged: the recovery
        // logic reads `isError`, which the control loop keeps up to date in the repository, and a
        // snapshot frozen at the first call would feed it a stale flag forever. Only a change of
        // the *set* restarts the sweep - refreshing flags mid-sweep must not send the cursor back
        // to the beginning.
        const bool sameSet = sameSensorSet(ordered, _sensors);
        _sensors = ordered;

        if (sameSet)
        {
            if (_cursor > _sensors.size())
            {
                _cursor = 0;
            }
            return;
        }

        _cursor = 0;
        _sweepStartedAtMs = 0;
        _sensorSetVersion++;
    }

    uint32_t submitBusCommand(const std::string& command, const std::string& options) override
    {
        std::lock_guard<std::mutex> guard(_busCommandMutex);

        if (_busCommands.size() >= MAX_PENDING_BUS_COMMANDS)
        {
            // Refused rather than dropped, and the caller is told so by the return value: a raw
            // command that vanished would leave the app waiting for an answer that never comes.
            _refusedBusCommandCount++;
            return kBusCommandRejected;
        }

        BusCommand queued;
        queued.ticket = nextBusTicket();
        queued.command = command;
        queued.options = options;
        _busCommands.push_back(queued);
        return queued.ticket;
    }

    uint32_t submitComplexBusCommand(const ComplexCommandWork& work) override
    {
        std::lock_guard<std::mutex> guard(_busCommandMutex);

        if (_busCommands.size() >= MAX_PENDING_BUS_COMMANDS)
        {
            _refusedBusCommandCount++;
            return kBusCommandRejected;
        }

        BusCommand queued;
        queued.ticket = nextBusTicket();
        queued.command = work.command.command;
        queued.complex = true;
        queued.work = work;
        _busCommands.push_back(queued);
        return queued.ticket;
    }

    uint32_t submitStartupScanStep(const StartupScanStep& step) override
    {
        std::lock_guard<std::mutex> guard(_busCommandMutex);

        if (_startupStep)
        {
            // One step at a time, by construction rather than by convention: the producer offers
            // the next only once this one's answer has been applied, so a second here would be a
            // caller getting ahead of its own answers. Refused and counted, never queued behind.
            _refusedStartupStepCount++;
            return kBusCommandRejected;
        }

        _startupStep.reset(new StartupScanStep(step));
        _startupTicket = nextBusTicket();
        return _startupTicket;
    }

    bool drainBusCommandResults(std::vector<BusCommandCompletion>& out) override
    {
        std::lock_guard<std::mutex> guard(_busResultMutex);
        if (_busResults.empty())
        {
            return false;
        }
        out.swap(_busResults);
        _busResults.clear();
        // A no-op while the reserved pair circulates; restores the reservation when a caller
        // swapped in a smaller buffer and carried the capacity away.
        _busResults.reserve(RESULT_BUFFER_RESERVE);
        return true;
    }

    /** @brief The use case that knows how to turn a raw command into a bus transaction. */
    void setRawCommandExecutor(IModuleCommandExecutionUseCase* executor) { _rawExecutor = executor; }

    /**
     * @brief The use case that knows what one step of the startup discovery does on the line.
     *
     * Without one, a submitted step is executed as a no-op and answered: the answer is what frees
     * the slot, and a step that vanished would stop the discovery for good.
     */
    void setStartupSensorScan(IStartupSensorScan* scan) { _startupScan = scan; }

    /**
     * @brief Where this task says what it is doing: a sensor of the sweep, a command, a step of
     *        the discovery.
     *
     * Optional, and observation only - one store at each boundary of an act, read by whoever has
     * to name the work a heap low fell on. Nothing here depends on it.
     */
    void setActBoard(BackgroundActBoard* acts) { _acts = acts; }

    bool drain(std::vector<AcquiredReading>& out) override
    {
        std::lock_guard<std::mutex> guard(_resultMutex);
        if (_results.empty())
        {
            return false;
        }
        out.swap(_results);
        _results.clear();
        return true;
    }

    // --- Running -----------------------------------------------------------------------------

    /**
     * @brief Create the acquisition task.
     * @return true if the task was created; false leaves the caller polling nothing
     */
    bool ensureRunning() override
    {
        if (_taskStarted)
        {
            return true;
        }
        return start();
    }

    bool start(uint32_t stackBytes = DEFAULT_STACK_BYTES, uint8_t priority = 1)
    {
        if (_scheduler == nullptr || _taskStarted)
        {
            return false;
        }

        _taskStarted = _scheduler->createTask(TASK_NAME, stackBytes, priority, [this]() { run(); });
        if (!_taskStarted)
        {
            _taskStartFailureCount++;
            log("SensorAcquisitionService: task_start_failed");
        }
        return _taskStarted;
    }

    /**
     * @brief The task body, exposed so a test can drive it without owning a scheduler.
     *
     * Runs until the scheduler stops resuming it, like every other task body in this firmware.
     */
    void run()
    {
        if (_scheduler != nullptr)
        {
            _scheduler->watchdogSubscribeCurrentTask();
        }

        while (true)
        {
            if (_scheduler != nullptr)
            {
                _scheduler->watchdogFeed();
            }

            const uint32_t sleepMs = step();
            if (_scheduler == nullptr)
            {
                return;
            }
            if (sleepMs > 0)
            {
                sleepInterruptibly(sleepMs);
            }
            else
            {
                _scheduler->yieldTask();
            }
        }
    }

    /**
     * @brief One step: serve one bus command, poll one sensor, or advance the startup discovery.
     * @return How long the caller should sleep before the next step; 0 means "go straight on"
     *
     * A queued bus command goes first, and it is served *between* two sensors rather than inside
     * one: the sweep is not interrupted half-way through a transaction, and the command waits for
     * at most the sensor currently in flight. That is the same shape the bus lock gave the control
     * loop before - one transaction of waiting, never a whole sweep - except that now nobody waits
     * at all, because the caller has a ticket and left.
     *
     * Except while a window of the discovery is open: then the command waits for it to close, and
     * the sweep leaves the ports it holds dark alone - see aDiscoveryWindowIsOpen(). The sweep also
     * leaves alone a port that is dark or came up moments ago, whoever switched it - see
     * thePortIsNotUpYet() - and reads its sensors the moment it has come up, not a sweep later -
     * see aHeldSensorIsDue().
     *
     * The startup discovery comes last, in the time between two sweeps - the line is idle for most
     * of every interval and that is where minutes of discovery fit without costing the module its
     * telemetry. One step is still owed to it after every sweep that closes, so a module whose
     * sensors take the whole interval to poll finishes discovering anyway, one step per sweep,
     * instead of never.
     *
     * Public so that the whole polling behaviour can be exercised in a test without a task at all,
     * which is what the acquisition tests do - a task would only add scheduling to the thing under
     * test.
     */
    uint32_t step()
    {
        if (!aDiscoveryWindowIsOpen() && serveOneBusCommand())
        {
            return 0;
        }

        if (_startupStepOwed && serveOneStartupScanStep())
        {
            _startupStepOwed = false;
            return 0;
        }

        SensorEntity sensor;
        if (aHeldSensorIsDue(sensor))
        {
            // Its port has come up since the sweep left it unread: it is read now, not a sweep
            // later - outside the sweep's order, whose cursor and cadence stay as they are.
            if (thePortIsNotUpYet(sensor))
            {
                _heldDarkSkipCount++;
                return 0;
            }
            noteActStarted(BackgroundActs::ACQ_SWEEP);
            pollSensor(sensor);
            noteActFinished(BackgroundActs::ACQ_SWEEP);
            _heldSensorReadCount++;
            return 0;
        }

        if (nextSensor(sensor))
        {
            if (thePortIsNotUpYet(sensor))
            {
                // Not read, and not a miss: see thePortIsNotUpYet(). The sensor keeps its last
                // reading.
                _heldDarkSkipCount++;
                return finishStep();
            }
            noteActStarted(BackgroundActs::ACQ_SWEEP);
            pollSensor(sensor);
            noteActFinished(BackgroundActs::ACQ_SWEEP);
            return finishStep();
        }

        if (serveOneStartupScanStep())
        {
            return 0;
        }

        return IDLE_STEP_MS;
    }

    // --- Observation --------------------------------------------------------------------------

    bool taskStarted() const { return _taskStarted; }
    uint32_t taskStartFailureCount() const { return _taskStartFailureCount; }
    uint64_t pollCount() const { return _pollCount; }
    uint64_t busCommandCount() const { return _busCommandCount; }
    uint32_t refusedBusCommandCount() const { return _refusedBusCommandCount; }
    uint64_t startupScanStepCount() const { return _startupScanStepCount; }

    /** @brief Startup discovery steps turned away because one was already in flight. */
    uint32_t refusedStartupStepCount() const { return _refusedStartupStepCount; }

    /**
     * @brief Sensors of the sweep left unread because their port was dark or still coming up -
     *        see thePortIsNotUpYet().
     */
    uint64_t heldDarkSkipCount() const { return _heldDarkSkipCount; }

    /** @brief Ports the sweep found dark, and gave their power back. */
    uint64_t darkPortPoweredCount() const { return _darkPortPoweredCount; }

    /** @brief Sensors read once their port had come up, after the sweep had left them unread. */
    uint64_t heldSensorReadCount() const { return _heldSensorReadCount; }
    uint64_t sweepCount() const { return _sweepCount; }
    uint64_t recoveryActionCount() const { return _recoveryActionCount; }

    /** @brief Readings taken but never collected because the loop stopped draining. */
    uint64_t droppedReadingCount() const { return _droppedReadingCount; }

    /**
     * @brief Cap on undrained readings.
     *
     * The control loop drains every iteration and one sensor takes at least 145 ms to poll, so in
     * a healthy module this holds one or two entries. It is bounded anyway: if the loop ever stops
     * draining, the memory this task holds must not grow with how long it has been stopped, and
     * the loss must be counted rather than silent.
     */
    static constexpr size_t MAX_PENDING_READINGS = 64;

    /**
     * @brief Cap on raw bus commands waiting to be executed.
     *
     * Small on purpose: these come from a person pressing a button in the app, one at a time. The
     * cap exists so that a backend loop cannot make this task's memory grow with how long the bus
     * has been busy, and a refusal is reported to the caller rather than swallowed.
     */
    static constexpr size_t MAX_PENDING_BUS_COMMANDS = 8;
    static_assert(RESULT_BUFFER_RESERVE <= MAX_PENDING_BUS_COMMANDS,
                  "a result buffer reservation larger than the command queue could never fill");

private:
    /** @brief What this task follows of one port's power, from the record the port keeps. */
    struct PortWatch
    {
        PortWarmUpWatch warmUp;
        /** The sweep has said it left the port's sensors unread, for the stretch in progress. */
        bool heldSaid = false;
        /**
         * The sweep left the port's sensors unread only because the port came up moments ago: they
         * are read once it has come up - `readAfterMs` after `heldAtMs` - from the sensor at
         * `nextIndex` of the list on.
         */
        bool readWhenUp = false;
        unsigned long heldAtMs = 0;
        unsigned long readAfterMs = 0;
        size_t nextIndex = 0;
    };

    struct BusCommand
    {
        uint32_t ticket = 0;
        std::string command;
        std::string options;
        bool complex = false;
        ComplexCommandWork work;
    };

    uint32_t nextBusTicket()
    {
        _lastBusTicket++;
        if (_lastBusTicket == kBusCommandRejected)
        {
            _lastBusTicket = 1;
        }
        return _lastBusTicket;
    }

    /**
     * @brief Execute at most one queued bus command, raw or complex.
     * @return true if one was executed
     *
     * The transaction itself is `IModuleCommandExecutionUseCase::executeRaw()` for a raw command
     * and `executeBusWork()` for a complex one - the single implementations of that work, not
     * copies of them. Splitting work across tasks must not fork the code that does the work, or
     * the tests prove one fork and the board runs the other.
     */
    bool serveOneBusCommand()
    {
        // Heap-held, not a local: a BusCommand carries a ComplexCommandWork - a SensorEntity, an
        // address occupancy and a command inline - and this task's stack is sized for the
        // transaction chain below it, not for the queue payload on top of it.
        std::unique_ptr<BusCommand> command;

        {
            std::lock_guard<std::mutex> guard(_busCommandMutex);
            if (_busCommands.empty())
            {
                return false;
            }
            command.reset(new BusCommand(std::move(_busCommands.front())));
            _busCommands.pop_front();
        }

        const uint8_t act = command->complex ? BackgroundActs::ACQ_SCAN_COMMAND
                                             : BackgroundActs::ACQ_RAW_COMMAND;
        noteActStarted(act);

        std::unique_ptr<BusCommandCompletion> completion(new BusCommandCompletion());
        completion->ticket = command->ticket;
        completion->command = command->command;
        completion->complex = command->complex;

        if (command->complex)
        {
            if (_rawExecutor != nullptr)
            {
                completion->complexOutcome = _rawExecutor->executeBusWork(command->work);
            }
            else
            {
                completion->complexOutcome.status = ModuleCommandStatus::ERROR;
                completion->complexOutcome.errorMessage = "Raw command executor not available";
            }
            completion->complexWork = std::move(command->work);
        }
        else if (_rawExecutor != nullptr)
        {
            completion->result = _rawExecutor->executeRaw(command->command, command->options);
        }
        else
        {
            // Answered rather than left pending: the caller reports an outcome to the server for
            // every command it accepted, and silence here would be the one case where it cannot.
            completion->result = RawBusCommandResult(
                ModuleCommandStatus::ERROR, "Raw command executor not available", "", false, 0);
        }

        _busCommandCount++;
        lookAtThePorts();
        if (command->complex)
        {
            forgetTheHeldReadsOf(completion->complexWork.pin);
        }

        {
            std::lock_guard<std::mutex> guard(_busResultMutex);
            _busResults.push_back(std::move(*completion));
        }

        noteActFinished(act);
        return true;
    }

    /**
     * @brief Execute the queued step of the startup discovery, if there is one.
     * @return true if one was executed
     *
     * The transactions are IStartupSensorScan::runStepOnBus() - the single implementation of that
     * work, reached from here and from nowhere else. What the step means for the registry and the
     * module structure is applied by the control loop when it drains the result.
     */
    bool serveOneStartupScanStep()
    {
        // Taken out of the slot, not read in place: the step is executed without the lock, and
        // the slot has to be free for the loop to fill again the moment the answer is applied.
        std::unique_ptr<StartupScanStep> step;
        uint32_t ticket = 0;

        {
            std::lock_guard<std::mutex> guard(_busCommandMutex);
            if (!_startupStep)
            {
                return false;
            }
            step.swap(_startupStep);
            ticket = _startupTicket;
        }

        const uint8_t act = BackgroundActs::ofLadderStep(step->kind);
        noteActStarted(act);

        BusCommandCompletion completion;
        completion.ticket = ticket;
        completion.startupScan = true;

        if (_startupScan != nullptr)
        {
            completion.startupOutcome = _startupScan->runStepOnBus(*step);
        }

        _startupScanStepCount++;
        lookAtThePorts();
        forgetTheHeldReadsOf(step->pin);

        {
            std::lock_guard<std::mutex> guard(_busResultMutex);
            _busResults.push_back(completion);
        }

        noteActFinished(act);
        return true;
    }

    /**
     * @brief Is a window of the discovery open - a wire it holds dark across several steps?
     *
     * Then no bus command is served until it closes: every one of them powers the port it talks
     * through (ModbusRS485::sendData()), and a scan powers its own port and darkens the rest of its
     * wire, so a command in the middle of a window would give the dark port its power back just
     * before the window's next write to 0x00 re-addresses whatever is plugged into it. The command
     * waits for the rest of that window and no longer - the window closes with a step of its own,
     * and a queued command is served before the next step of the discovery. It has already been
     * answered InProgress by the control loop that queued it.
     */
    bool aDiscoveryWindowIsOpen() const
    {
        return _startupScan != nullptr && _startupScan->holdsAWindowOpen();
    }

    /** @brief True when bus work is waiting that may be served; cuts the between-sweeps sleep short. */
    bool busCommandPending()
    {
        const bool commandsMayRun = !aDiscoveryWindowIsOpen();
        std::lock_guard<std::mutex> guard(_busCommandMutex);
        return (commandsMayRun && !_busCommands.empty()) || (bool)_startupStep;
    }

    /**
     * @brief Sleep, but wake as soon as a raw bus command arrives.
     *
     * The gap between two sweeps is up to SWEEP_INTERVAL_MS. Sleeping through it in one piece
     * would make a ModbusRead issued from the app wait up to 15 s for no reason - the line is idle
     * for all of it. Slicing the sleep bounds that wait to IDLE_STEP_MS instead.
     */
    void sleepInterruptibly(uint32_t totalMs)
    {
        uint32_t remaining = totalMs;
        while (remaining > 0)
        {
            if (busCommandPending())
            {
                return;
            }
            const uint32_t slice = (remaining > IDLE_STEP_MS) ? IDLE_STEP_MS : remaining;
            _scheduler->delayMs(slice);
            remaining -= slice;
            _scheduler->watchdogFeed();
        }
    }

    // --- The sweep ----------------------------------------------------------------------------

    /**
     * @brief Order the sensors so the sweep goes round the ports rather than down one.
     *
     * port0[0], port1[0], port2[0], port3[0], port0[1], ... A port whose only sensor is silent
     * costs 1400 ms; walking one port to its end before starting the next would make every sensor
     * of port 3 wait for all of port 0's silence, and the first reading of the sweep would be
     * minutes old by the time the last one is taken.
     */
    static std::vector<SensorEntity> interleaveByPort(const std::vector<SensorEntity>& sensors)
    {
        std::vector<std::vector<SensorEntity>> byPort(SensorRecoveryUseCase::PORT_COUNT);
        std::vector<SensorEntity> offPort;

        for (size_t i = 0; i < sensors.size(); i++)
        {
            if (!sensors[i].isActive)
            {
                continue;
            }
            const int port = sensors[i].pin;
            if (port >= 0 && port < SensorRecoveryUseCase::PORT_COUNT)
            {
                byPort[static_cast<size_t>(port)].push_back(sensors[i]);
            }
            else
            {
                offPort.push_back(sensors[i]);
            }
        }

        std::vector<SensorEntity> ordered;
        size_t deepest = offPort.size();
        for (size_t p = 0; p < byPort.size(); p++)
        {
            if (byPort[p].size() > deepest)
            {
                deepest = byPort[p].size();
            }
        }

        for (size_t index = 0; index < deepest; index++)
        {
            for (size_t p = 0; p < byPort.size(); p++)
            {
                if (index < byPort[p].size())
                {
                    ordered.push_back(byPort[p][index]);
                }
            }
            if (index < offPort.size())
            {
                ordered.push_back(offPort[index]);
            }
        }

        return ordered;
    }

    static bool sameSensorSet(const std::vector<SensorEntity>& a, const std::vector<SensorEntity>& b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++)
        {
            // Model and registers are part of the identity, not decoration: a sensor whose
            // register configs changed under the same uid/pin/address would otherwise keep being
            // polled with the old configuration until one of those three happened to change. A new
            // identity starts the sweep again, so the sensor is next polled by what it has now and
            // not a sweep later - the control loop drops what the old registers read.
            if (a[i].uid != b[i].uid || a[i].pin != b[i].pin ||
                a[i].modbusDeviceAddress != b[i].modbusDeviceAddress ||
                a[i].model != b[i].model ||
                a[i].registerFingerprint() != b[i].registerFingerprint())
            {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Take the next sensor of the sweep, or report that there is nothing to do yet.
     */
    bool nextSensor(SensorEntity& out)
    {
        std::lock_guard<std::mutex> guard(_stateMutex);

        if (_sensors.empty())
        {
            return false;
        }

        const unsigned long now = _arduino ? _arduino->millis() : 0;

        if (_cursor == 0)
        {
            if (_sweepStartedAtMs != 0 && (now - _sweepStartedAtMs) < SWEEP_INTERVAL_MS)
            {
                return false;
            }
            _sweepStartedAtMs = (now == 0) ? 1 : now;
        }

        out = _sensors[_cursor];
        return true;
    }

    /**
     * @brief Advance the cursor after a sensor was dealt with, and close the sweep at the end.
     * @return How long the task should sleep before the next step
     */
    uint32_t finishStep()
    {
        std::lock_guard<std::mutex> guard(_stateMutex);

        if (_sensors.empty())
        {
            return IDLE_STEP_MS;
        }

        _cursor++;
        if (_cursor < _sensors.size())
        {
            return 0;
        }

        _cursor = 0;
        _sweepCount++;
        // The sweep is done with the line for now, and the discovery is owed a turn on it before
        // the next one starts - see step().
        _startupStepOwed = true;

        const unsigned long now = _arduino ? _arduino->millis() : 0;
        const unsigned long elapsed = now - _sweepStartedAtMs;
        if (elapsed >= SWEEP_INTERVAL_MS)
        {
            // The sweep already outlasted its own interval - a bus full of silent sensors does
            // that. Start the next one now rather than pretending to keep a cadence that the
            // hardware does not allow.
            return 0;
        }
        return sleepNoLongerThanTheHeldSensors(static_cast<uint32_t>(SWEEP_INTERVAL_MS - elapsed));
    }

    // --- Ports that cannot answer yet ---------------------------------------------------------

    /**
     * @brief Must the sweep leave this sensor unread, because its port cannot answer yet?
     *
     * A sensor answers nothing until its port has had its power for PortPower::WAKE_UP_MS, and a
     * port loses it to more than the sweep sees: a model scan and an address write power their own
     * port and switch the rest of its wire off (SensorReader::scanModbusSensor(),
     * ModbusSensorReader::isolatePortForAddressWrite()), a window holds its wire dark, and the power
     * comes back from whoever raises it next - the preparation, a scan of that port, a raw command,
     * or this sweep. So what is asked is the port's own record (ISensorReader::modbusPortPower()),
     * not what the sweep happens to find:
     *
     *  - the discovery holds the port dark, or has just given it back
     *    (IStartupSensorScan::keepsThePortDark()): not read;
     *  - the port is dark: the sweep gives it its power - at the moment a read would have raised it
     *    anyway (ModbusRS485::sendData()), but without the frame - and does not read it;
     *  - the port came up less than PortPower::WAKE_UP_MS ago, whoever switched it on: not read
     *    now, and read the moment it has come up (aHeldSensorIsDue()).
     *
     * A read in any of them is answered by nobody: a miss the port recovery would count towards
     * power-cycling a sensor that is fine. What the stretch leaves out of the recovery is worked out
     * from the same record - see lookAtThePort().
     *
     * The darkness the recovery makes itself is left to it: a port it switched off is switched back
     * on by it, at this same visit.
     */
    bool thePortIsNotUpYet(const SensorEntity& sensor)
    {
        const int pin = sensor.pin;
        const bool heldByTheDiscovery =
            _startupScan != nullptr && _startupScan->keepsThePortDark(pin);
        if (pin < 0 || pin >= SensorRecoveryUseCase::PORT_COUNT || _arduino == nullptr ||
            _sensorReader == nullptr)
        {
            return heldByTheDiscovery;
        }

        lookAtThePort(pin);
        if (heldByTheDiscovery)
        {
            sayTheSensorIsHeld(sensor, "the discovery holds it");
            return true;
        }

        PortPower power = _sensorReader->modbusPortPower(pin);
        if (!power.on)
        {
            if (_recovery.getPhase(pin) == RecoveryPhase::POWER_OFF_PHASE)
            {
                return false;
            }
            _sensorReader->setModbusPortPower(pin, true);
            lookAtThePort(pin);
            power = _sensorReader->modbusPortPower(pin);
            if (!power.on)
            {
                // Nothing this task can switch on: read it, and let the read say what it finds -
                // holding it would leave the sensor unread and unrecovered for good.
                return false;
            }
            _darkPortPoweredCount++;
        }

        const uint32_t owedMs = power.wakeUpOwedMs(static_cast<uint32_t>(_arduino->millis()));
        if (owedMs == 0)
        {
            return false;
        }
        PortWatch& port = _ports[pin];
        port.readWhenUp = true;
        port.heldAtMs = _arduino->millis();
        port.readAfterMs = owedMs;
        port.nextIndex = 0;
        sayTheSensorIsHeld(sensor, "its power came up less than 10 s ago");
        return true;
    }

    /**
     * @brief The next sensor the sweep left unread for its port's wake-up, if that port has come up
     *        since: one per call, every sensor of the port once.
     */
    bool aHeldSensorIsDue(SensorEntity& out)
    {
        if (_arduino == nullptr)
        {
            return false;
        }
        const unsigned long now = _arduino->millis();
        for (int pin = 0; pin < SensorRecoveryUseCase::PORT_COUNT; pin++)
        {
            PortWatch& port = _ports[pin];
            if (!port.readWhenUp || (now - port.heldAtMs) < port.readAfterMs)
            {
                continue;
            }
            std::lock_guard<std::mutex> guard(_stateMutex);
            for (size_t i = port.nextIndex; i < _sensors.size(); i++)
            {
                if (_sensors[i].pin == pin)
                {
                    out = _sensors[i];
                    port.nextIndex = i + 1;
                    return true;
                }
            }
            port.readWhenUp = false;
        }
        return false;
    }

    /**
     * @brief Work about a port - a scan, an address write - may have changed what is on it and at
     *        which address, and the list of sensors the sweep reads is refreshed only at its own
     *        interval: the port's held sensors are left to the next sweep rather than read early
     *        with what may be the address the device had before.
     */
    void forgetTheHeldReadsOf(int pin)
    {
        if (pin >= 0 && pin < SensorRecoveryUseCase::PORT_COUNT)
        {
            _ports[pin].readWhenUp = false;
        }
    }

    /** @brief How long the task may sleep before a held sensor is due, at most `sleepMs`. */
    uint32_t sleepNoLongerThanTheHeldSensors(uint32_t sleepMs) const
    {
        const unsigned long now = _arduino ? _arduino->millis() : 0;
        for (int pin = 0; pin < SensorRecoveryUseCase::PORT_COUNT; pin++)
        {
            const PortWatch& port = _ports[pin];
            if (!port.readWhenUp)
            {
                continue;
            }
            const unsigned long heldForMs = now - port.heldAtMs;
            const unsigned long leftMs =
                heldForMs < port.readAfterMs ? port.readAfterMs - heldForMs : 0;
            sleepMs = leftMs < sleepMs ? static_cast<uint32_t>(leftMs) : sleepMs;
        }
        return sleepMs;
    }

    /** @brief Once per stretch and port: the sweep leaves the sensors of this port unread. */
    void sayTheSensorIsHeld(const SensorEntity& sensor, const char* why)
    {
        PortWatch& port = _ports[sensor.pin];
        if (port.heldSaid)
        {
            return;
        }
        port.heldSaid = true;
        log("SensorAcquisitionService: port=%d held dark (%s) - uid=%s not read, not a miss",
            sensor.pin, why, sensor.uid.c_str());
    }

    /**
     * @brief Take in every port's record, after a piece of work on the line: one piece moves a
     *        port's pin at most once each way, and a look after each is what keeps a transition
     *        from going by unseen.
     */
    void lookAtThePorts()
    {
        for (int pin = 0; _arduino != nullptr && _sensorReader != nullptr &&
                          pin < SensorRecoveryUseCase::PORT_COUNT;
             pin++)
        {
            lookAtThePort(pin);
        }
    }

    /**
     * @brief Take in one port's record, and when a stretch it could not answer in has just ended,
     *        leave it out of the port recovery - of the silence of every sensor on the port and of
     *        the port's polling window.
     *
     * The stretch runs from the moment the pin went down to the moment the port had had its power
     * back for PortPower::WAKE_UP_MS, whoever moved it at either end; silence outside it counts as
     * it always has.
     */
    void lookAtThePort(int pin)
    {
        const unsigned long now = _arduino->millis();
        const PortWarmUpWatch::Stretch stretch =
            _ports[pin].warmUp.look(_sensorReader->modbusPortPower(pin), static_cast<uint32_t>(now));
        if (!stretch.ended)
        {
            return;
        }

        bool carriesASensor = false;
        {
            std::lock_guard<std::mutex> guard(_stateMutex);
            for (size_t i = 0; i < _sensors.size(); i++)
            {
                if (_sensors[i].pin == pin)
                {
                    carriesASensor = true;
                    _recovery.leaveOutOfTheSilence(pin, _sensors[i].uid, stretch.fromMs,
                                                   stretch.untilMs, now);
                }
            }
        }
        _recovery.leaveOutOfThePollingWindow(pin, stretch.fromMs, stretch.untilMs, now);
        _ports[pin].heldSaid = false;
        if (carriesASensor)
        {
            log("SensorAcquisitionService: port=%d up again - %lu ms dark or coming up, left out "
                "of the port recovery",
                pin, (unsigned long)(stretch.untilMs - stretch.fromMs));
        }
    }

    /** @brief The recovery switched this port on or off itself: that is its own to count. */
    void acknowledgeTheRecovery(int pin)
    {
        if (pin >= 0 && pin < SensorRecoveryUseCase::PORT_COUNT && _sensorReader != nullptr)
        {
            _ports[pin].warmUp.acknowledge(_sensorReader->modbusPortPower(pin));
        }
    }

    // --- One sensor ---------------------------------------------------------------------------

    void pollSensor(const SensorEntity& sensor)
    {
        if (_sensorReader == nullptr)
        {
            return;
        }

        const unsigned long now = _arduino ? _arduino->millis() : 0;

        const RecoveryPhase phaseBefore = _recovery.getPhase(sensor.pin);
        const RecoveryAction action = _recovery.decide(sensor.pin, now, sensor.uid);
        const RecoveryPhase phaseAfter = _recovery.getPhase(sensor.pin);

        if (action == RecoveryAction::POWER_OFF)
        {
            _sensorReader->setModbusPortPower(sensor.pin, false);
            acknowledgeTheRecovery(sensor.pin);
            _recoveryActionCount++;
            log(phaseBefore == RecoveryPhase::POLLING
                    ? "SensorAcquisitionService: RECOVERY retry port=%d uid=%s"
                    : "SensorAcquisitionService: RECOVERY power_off port=%d uid=%s",
                sensor.pin, sensor.uid.c_str());
            // The sensor's own error flag, not a hard-coded true. The difference is observable:
            // observe(..., false, ...) *erases* the disconnected-since stamp and a hard-coded true
            // never does, so a port whose sensor had recovered would stay in recovery for good.
            // This is what the control loop passed, and it stays what is passed.
            _recovery.observe(sensor.uid, sensor.isError, now);
            return;
        }

        if (action == RecoveryAction::POWER_ON)
        {
            _sensorReader->setModbusPortPower(sensor.pin, true);
            acknowledgeTheRecovery(sensor.pin);
            _recoveryActionCount++;
            log("SensorAcquisitionService: RECOVERY power_on port=%d uid=%s",
                sensor.pin, sensor.uid.c_str());
            _recovery.observe(sensor.uid, sensor.isError, now);
            return;
        }

        if (phaseBefore == RecoveryPhase::POWER_ON_PHASE)
        {
            log("SensorAcquisitionService: RECOVERY polling_start port=%d uid=%s",
                sensor.pin, sensor.uid.c_str());
        }

        AcquiredReading reading;
        reading.sensorUid = sensor.uid;
        reading.model = sensor.model;
        reading.registerFingerprint = sensor.registerFingerprint();
        reading.port = sensor.pin;
        reading.multiChannel = sensor.isMultiChannel();

        if (reading.multiChannel)
        {
            std::map<std::string, float> channelValues;
            reading.ok = _sensorReader->readMultiChannelSensor(sensor, channelValues);
            for (std::map<std::string, float>::const_iterator it = channelValues.begin();
                 it != channelValues.end(); ++it)
            {
                AcquiredChannel channel;
                channel.channelUid = it->first;
                channel.value = it->second;
                reading.channels.push_back(channel);
            }
        }
        else
        {
            float value = 0.0f;
            reading.ok = _sensorReader->readSensor(sensor, value);
            if (reading.ok)
            {
                AcquiredChannel channel;
                channel.value = value;
                reading.channels.push_back(channel);
            }
        }

        // Stamped after the transaction, not before it: this is the age the freshness window in
        // EvaluateRulesUseCase measures, and a stamp taken before a 1400 ms silent read would make
        // the failure look 1.4 s fresher than it was.
        reading.acquiredAtMs = static_cast<uint32_t>(_arduino ? _arduino->millis() : 0);

        _pollCount++;
        // `now` - taken before the transaction - and not reading.acquiredAtMs. The recovery
        // threshold is 60 s of continuous failure, and the control loop measured it from the
        // moment the poll began; stamping from the end would shift every observation by the cost
        // of the transaction (1100 ms on a silent sensor). The freshness stamp is a different
        // question and does come from the end - see reading.acquiredAtMs above.
        _recovery.observe(sensor.uid, !reading.ok, now);

        if (phaseBefore == RecoveryPhase::POLLING && phaseAfter == RecoveryPhase::NORMAL)
        {
            log("SensorAcquisitionService: RECOVERY recovered port=%d uid=%s",
                sensor.pin, sensor.uid.c_str());
        }

        publish(reading);
    }

    void publish(const AcquiredReading& reading)
    {
        std::lock_guard<std::mutex> guard(_resultMutex);
        if (_results.size() >= MAX_PENDING_READINGS)
        {
            _droppedReadingCount++;
            return;
        }
        _results.push_back(reading);
    }

    void noteActStarted(uint8_t act)
    {
        if (_acts != nullptr)
        {
            _acts->actStarted(BackgroundActBoard::ACQUISITION, act);
        }
    }

    void noteActFinished(uint8_t act)
    {
        if (_acts != nullptr)
        {
            _acts->actFinished(BackgroundActBoard::ACQUISITION, act);
        }
    }

    void log(const char* format, ...)
    {
        if (_logger == nullptr || _debugSystem == nullptr ||
            !_debugSystem->isFeatureEnabled(DebugFeatures::SENSOR_DATA_DEBUG))
        {
            return;
        }

        char buffer[160];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::SENSOR_DATA_DEBUG);
        if (feature != nullptr)
        {
            _logger->println(DebugFeatures::SENSOR_DATA_DEBUG, buffer, feature->getLogChannel());
        }
        else
        {
            _logger->println(DebugFeatures::SENSOR_DATA_DEBUG, buffer);
        }
    }

    ISensorReader* _sensorReader;
    IArduinoAdapter* _arduino;
    ITaskScheduler* _scheduler;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    BackgroundActBoard* _acts = nullptr;

    // Port recovery lives here rather than on the control loop because it is about hardware, not
    // about control: it cuts a port's power and brings it back, and it only ever fires as a
    // consequence of a read this task made.
    SensorRecoveryUseCase _recovery;

    // Read and written on the acquisition task only.
    PortWatch _ports[SensorRecoveryUseCase::PORT_COUNT];

    // Held only across a vector operation, never across a bus transaction. That is the difference
    // between this and the lock on the line itself.
    std::mutex _stateMutex;
    std::vector<SensorEntity> _sensors;
    size_t _cursor = 0;
    unsigned long _sweepStartedAtMs = 0;
    uint32_t _sensorSetVersion = 0;

    std::mutex _resultMutex;
    std::vector<AcquiredReading> _results;

    // The other direction: raw commands in, their results out. Two locks rather than one because
    // the control loop submits and drains on its own task while this one executes, and neither
    // lock is ever held across a bus transaction.
    IModuleCommandExecutionUseCase* _rawExecutor = nullptr;
    IStartupSensorScan* _startupScan = nullptr;
    std::mutex _busCommandMutex;
    std::deque<BusCommand> _busCommands;
    uint32_t _lastBusTicket = 0;
    uint32_t _refusedBusCommandCount = 0;
    uint32_t _refusedStartupStepCount = 0;
    uint64_t _startupScanStepCount = 0;
    /**
     * The one step of the startup discovery this task has been given, held by pointer.
     *
     * A slot of capacity one rather than an entry in the command queue: a step carries a whole
     * SensorEntity and, for the preparation, the registry, and the queue in front of the line is a
     * deque whose block would then be sized for that on a module that never scans. Held only while
     * a step is in flight, refused - and counted - when one already is.
     */
    std::unique_ptr<StartupScanStep> _startupStep;
    uint32_t _startupTicket = 0;
    // Read and written on the acquisition task only, between two steps - never across a
    // transaction and never from the control loop.
    bool _startupStepOwed = false;
    std::mutex _busResultMutex;
    std::vector<BusCommandCompletion> _busResults;
    uint64_t _busCommandCount = 0;

    bool _taskStarted = false;
    uint32_t _taskStartFailureCount = 0;
    uint64_t _pollCount = 0;
    uint64_t _sweepCount = 0;
    uint64_t _recoveryActionCount = 0;
    uint64_t _droppedReadingCount = 0;
    uint64_t _heldDarkSkipCount = 0;
    uint64_t _darkPortPoweredCount = 0;
    uint64_t _heldSensorReadCount = 0;
};
