#ifndef I_BUS_LOCK_H
#define I_BUS_LOCK_H

/**
 * @brief Exclusive access to the RS-485 line.
 *
 * There is one physical line per UART and several pieces of code that put frames on it: the
 * sensor acquisition task, `ModuleCommandExecutionUseCase` (scanSensor, ModbusRead, ModbusWrite,
 * SerialWrite) and `ModuleSetupService` (scanning, writing a device address). Making the
 * acquisition task the sole owner of the line would mean rewriting the command paths into a state
 * machine over completions; that is a task of its own. Until then the line is shared and has to be
 * guarded, or a scan issued from the backend interleaves its request with a poll's response and
 * both come back as garbage.
 *
 * Behind this interface the device gets a recursive mutex and the host gets a cooperative lock
 * driven by the same virtual-time scheduler as the tasks. The reason is the one ITaskScheduler
 * gives: a FreeRTOS or std::mutex call compiled into a host test would block the single thread the
 * cooperative fibers share, so nothing built on it could be proven by a test.
 *
 * **Reentrant on purpose.** `ModbusSensorReader::writeSensorAddress()` runs a sequence of
 * transactions - isolate the port, write, ping to verify, fall back to a wildcard write, ping
 * again - and calls its own public `pingAddress()` and `powerOffPort()` on the way. The sequence
 * has to be atomic on the line (a wildcard write with another device's poll in the middle changes
 * the wrong device), so the nested calls must not deadlock against the outer hold.
 */
class IBusLock
{
public:
    virtual ~IBusLock() = default;

    /**
     * @brief Take the line, waiting for as long as another owner holds it.
     *
     * Reentrant: an owner that already holds the lock takes it again without waiting, and has to
     * release it as many times as it took it.
     */
    virtual void lock() = 0;

    /** @brief Give the line back. */
    virtual void unlock() = 0;
};

/**
 * @brief Scope guard for IBusLock; null-safe so a caller with no lock wired behaves as before.
 */
class BusLockGuard
{
public:
    explicit BusLockGuard(IBusLock* busLock) : _busLock(busLock)
    {
        if (_busLock != nullptr)
        {
            _busLock->lock();
        }
    }

    ~BusLockGuard()
    {
        if (_busLock != nullptr)
        {
            _busLock->unlock();
        }
    }

    BusLockGuard(const BusLockGuard&) = delete;
    BusLockGuard& operator=(const BusLockGuard&) = delete;

private:
    IBusLock* _busLock;
};

#endif // I_BUS_LOCK_H
