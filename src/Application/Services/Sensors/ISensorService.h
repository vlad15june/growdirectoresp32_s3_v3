#pragma once

#include <vector>
#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
#include "Application/DTOs/UpdateDto.h"

class ISensorService {
public:
    virtual ~ISensorService() = default;

    /**
     * Told, on the control loop, how a request submitted to the outbox ended.
     *
     * Default: nothing. SensorService has no consequence to apply - a reading that did not arrive
     * leaves nothing behind to undo - but the control loop offers every service the same hook
     * rather than knowing which ones care.
     */
    virtual void onOutboxCompletion(const OutboxCompletion& completion) { (void)completion; }

    /**
     * Where the first readings of the boot stand: the ModBus sensors the module started reading
     * with, and whether each of them has answered or failed once.
     */
    struct FirstReadings {
        /** Every sensor awaited has an outcome on the control loop, or there was none to await. */
        bool settled = true;
        /** How many active ModBus sensors the first read started with. */
        uint32_t awaited = 0;
        /** Their register channels, all together: what their first outcomes can cost the line. */
        uint32_t awaitedChannels = 0;
        /** How many of them have no outcome yet. */
        uint32_t outstanding = 0;
        /** When the last of those outcomes was applied, ms of uptime; 0 while none was. */
        uint32_t lastAtMs = 0;
    };

    /**
     * Default: settled with nothing awaited, for implementations that read nothing.
     */
    virtual FirstReadings firstReadings() const { return FirstReadings(); }

    /**
     * Stop waiting for the first outcomes that have not come: the boot sequence gave up on them.
     *
     * Default: nothing, for implementations that wait for none.
     */
    virtual void giveUpOnTheFirstReadings() {}

    virtual bool init() = 0;
    virtual void loop() = 0;
    virtual bool readAllSensors() = 0;
    virtual bool readSensor(SensorEntity sensor) = 0;
};
