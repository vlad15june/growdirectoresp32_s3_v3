#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Core/Domain/Interfaces/UseCases/IModuleCommandExecutionUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Sensors/IStartupSensorScan.h"

/**
 * @brief One channel of one finished acquisition.
 *
 * `channelUid` is the UID the value is stored under - `<sensor uid>.<NN>` for a multi-channel
 * sensor, empty for a single-channel one, where the sensor UID is the storage key.
 */
struct AcquiredChannel
{
    std::string channelUid;
    float value = 0.0f;
};

/**
 * @brief What the acquisition task finished with one sensor, and when.
 *
 * The timestamp is the point the bus answered, not the point the control loop got round to
 * looking. That difference is the whole reason it is carried here rather than taken by the
 * consumer: the freshness window a rule is judged against (EvaluateRulesUseCase, 300 s) has to
 * measure the age of the *measurement*. A value stamped when it was applied would read as fresh
 * forever, and a rule would act on a reading of any age without anything saying so.
 */
struct AcquiredReading
{
    std::string sensorUid;
    /**
     * The model the sensor was polled as. The task polls the list it was last handed, and the
     * registry can have given the uid another model since - a reading of the old model is not a
     * reading of the sensor the uid names now.
     */
    std::string model;
    /**
     * The registers the sensor was polled by (SensorEntity::registerFingerprint()). The catalog can
     * give the model other registers after the task was handed its list - values read by the old
     * ones are not the values of the channels the sensor has now.
     */
    uint32_t registerFingerprint = 0;
    int port = 0;
    bool multiChannel = false;
    /** False when the sensor did not answer: no values, and the sensor goes to the error state. */
    bool ok = false;
    uint32_t acquiredAtMs = 0;
    std::vector<AcquiredChannel> channels;
};

/**
 * @brief What one raw bus command, executed by the acquisition task, finished with.
 *
 * The command name travels back with the result because the caller reports it to the server by
 * name (`RawIoResultDto::command`), and by the time the answer arrives the control loop has moved
 * on from the message that asked for it.
 */
struct BusCommandCompletion
{
    uint32_t ticket = 0;
    std::string command;
    RawBusCommandResult result;
    /**
     * True when this is a complex bus command (PingPin, SetSensorModel, ScanSensors): the outcome
     * is in `complexOutcome`, and `complexWork` travels back with it because the control loop
     * needs the snapshot it prepared to apply the outcome to its own state.
     */
    bool complex = false;
    ComplexCommandWork complexWork;
    ComplexCommandOutcome complexOutcome;
    /**
     * True when this is a step of the startup discovery: the answer is in `startupOutcome`, and
     * the step itself does not travel back - the use case that produced it is holding it, and a
     * step carries an entity list the result buffer has no reason to reserve room for.
     */
    bool startupScan = false;
    StartupScanOutcome startupOutcome;
};

/**
 * @brief The hardware side of sensor reading, seen from the control loop.
 *
 * The loop hands over which sensors exist and takes back what has been read. It never waits: both
 * calls are a lock around a vector swap, and neither of them touches the RS-485 line.
 *
 * It also carries the other direction: a raw bus command from the backend (ModbusRead,
 * ModbusWrite, SerialWrite, RebootRemoteModule) is submitted here and executed by the same task,
 * between two steps of the sweep. That is not a convenience - it is what makes "the control loop
 * never takes the bus lock" true. A raw transaction on a silent device costs up to 1 s, and
 * running it on the loop is the one case that would break the iteration bound.
 */
class ISensorAcquisition
{
public:
    /** @brief Ticket value returned when a bus command could not be queued. Real tickets start at 1. */
    static const uint32_t kBusCommandRejected = 0;

    /**
     * @brief Up-front capacity of the bus-command result buffers: the acquisition task's vector
     *        and the drain buffer the consumer swaps with it.
     *
     * A burst is bounded by the commands in flight (the bus command queue holds eight), and
     * results are collected by the control loop every iteration while completions arrive at the
     * pace of bus transactions - so eight covers everything a drain can find, and the pair
     * circulates without reallocating.
     */
    static const size_t RESULT_BUFFER_RESERVE = 8;

    virtual ~ISensorAcquisition() = default;

    /**
     * @brief Tell the acquisition task which Modbus sensors to poll.
     *
     * A snapshot, not a reference to the repository: the repository is a cache plus SPIFFS with
     * no lock of its own, and reading it from a second task would be a data race against the
     * control loop that writes it.
     */
    virtual void setSensors(const std::vector<SensorEntity>& modbusSensors) = 0;

    /**
     * @brief Make sure the acquisition task exists, creating it if it does not.
     *
     * Called from the control loop on the polling tick rather than once at startup, because the
     * one failure the scheduler can report is "the heap cannot spare the stack", and a module that
     * lost that race at boot would otherwise never read a Modbus sensor again.
     *
     * @return true if a task is running
     */
    virtual bool ensureRunning() = 0;

    /**
     * @brief Take everything finished since the last call.
     * @param out Filled with the finished readings, in the order they were acquired
     * @return true if there was anything to take
     */
    virtual bool drain(std::vector<AcquiredReading>& out) = 0;

    /**
     * @brief Queue a raw bus command for the acquisition task and return without waiting.
     *
     * @param command The command name, as the backend sent it
     * @param options The command's JSON options, passed through untouched
     * @return a ticket, or kBusCommandRejected when the queue is full
     */
    virtual uint32_t submitBusCommand(const std::string& command, const std::string& options) = 0;

    /**
     * @brief Queue the bus part of a complex module command and return without waiting.
     *
     * Same queue and same turn-taking as the raw commands: the work is executed between two steps
     * of the sweep, by the task that owns the line. A scan against a silent pin costs seconds to
     * tens of seconds - the reason it must not run on the caller's task.
     *
     * @param work Prepared on the control loop by IModuleCommandExecutionUseCase::prepareComplexCommand()
     * @return a ticket, or kBusCommandRejected when the queue is full
     */
    virtual uint32_t submitComplexBusCommand(const ComplexCommandWork& work) = 0;

    /**
     * @brief Hand one step of the startup discovery to the acquisition task and return.
     *
     * Same task and same lock as the commands above, and for the same reason: a model tried on a
     * port is seconds of line time, and the only place that time can live without being
     * control-loop time or lock contention is the task that owns the line. A slot of one rather
     * than a place in the command queue - the producer offers the next step only once this one has
     * been answered, so a second could only be a caller getting ahead of its own answers, and a
     * step carries a whole entity that the command queue has no reason to be sized for.
     *
     * @param step Prepared on the control loop by IStartupSensorScan::takeNextStep()
     * @return a ticket, or kBusCommandRejected when a step is already in flight
     */
    virtual uint32_t submitStartupScanStep(const StartupScanStep& step) = 0;

    /**
     * @brief Take every bus command that has finished since the last call.
     * @return true if there was anything to take
     */
    virtual bool drainBusCommandResults(std::vector<BusCommandCompletion>& out) = 0;
};
