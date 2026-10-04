#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "Application/Interfaces/IOutbox.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#include "Core/Domain/Interfaces/UseCases/Sensors/IStartupSensorScan.h"

/**
 * @brief The acts the background tasks write to the BackgroundActBoard, and what a log line calls
 *        them.
 *
 * The board carries codes and knows nothing about what they mean; this is the one place that
 * does. A task takes its code from here when it crosses an act's boundary, and the control loop
 * turns a snapshot of the board back into words here - once per reported heap low, after the
 * memory has been given back.
 *
 * What counts as an act is chosen by what holds memory: one attempt of an outbox request rather
 * than the request with its backoffs, one sensor of a sweep, one step of the discovery, one
 * command, one catalog write.
 */
namespace BackgroundActs
{
    // --- outbox-net: one attempt of a request of the kind, or an act of the link ------------------

    inline uint8_t ofOutboxKind(OutboxKind kind) { return (uint8_t)(1u + (uint8_t)kind); }

    /** The MQTT connect act: from the teardown of the stale socket to the CONNACK or its timeout. */
    const uint8_t NET_MQTT_CONNECT = 0x40;

    // --- sensor-acq ---------------------------------------------------------------------------

    /** One sensor of the sweep being read. */
    const uint8_t ACQ_SWEEP = 1;
    /** A raw bus command of the backend: ModbusRead, ModbusWrite, SerialWrite, a remote reboot. */
    const uint8_t ACQ_RAW_COMMAND = 2;
    /** The bus part of PingPin, SetSensorModel or ScanSensors. */
    const uint8_t ACQ_SCAN_COMMAND = 3;

    const uint8_t ACQ_LADDER_BASE = 0x10;
    /** Room for the discovery's step kinds above the base; which of them exist is the switch's. */
    const uint8_t ACQ_LADDER_KINDS = 0x10;

    /** One step of the discovery, by what the step does on the line. */
    inline uint8_t ofLadderStep(StartupScanStep::Kind kind)
    {
        return (uint8_t)(ACQ_LADDER_BASE + (uint8_t)kind);
    }

    // --- cmd-net ------------------------------------------------------------------------------

    const uint8_t CMD_CATALOG_FETCH = 1;
    const uint8_t CMD_FIRMWARE_UPDATE = 2;
    const uint8_t CMD_DELETION_CHECK = 3;
    const uint8_t CMD_OTHER = 4;

    // --- catalog-io ---------------------------------------------------------------------------

    const uint8_t CATALOG_WRITE = 1;

    // --- names ----------------------------------------------------------------------------------

    /** The longest name below, "LADDER:RELEASE_FREE_PORTS"; the line buffer is sized from it. */
    const size_t LONGEST_NAME_CHARS = 25;

// A kind, a step or a slot added later has to be given a name in the switches below, or this
// header does not compile. They have no default, and the missing-case diagnostic is made an error
// for exactly their extent: on its own it is a warning that the device build does not even enable,
// and an unnamed act would then reach the log as "?" - visible, but only once somebody is reading
// a line about a low and needs the name.
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"

    inline const char* outboxKindName(OutboxKind kind)
    {
        switch (kind)
        {
        case OutboxKind::SENSOR_UPDATE:         return "SENSOR_UPDATE";
        case OutboxKind::DEVICE_UPDATE:         return "DEVICE_UPDATE";
        case OutboxKind::DIAGNOSTIC:            return "DIAGNOSTIC";
        case OutboxKind::RULE_LIST:             return "RULE_LIST";
        case OutboxKind::RULE_STATE_BULK:       return "RULE_STATE_BULK";
        case OutboxKind::RULE_STATE_BY_UID:     return "RULE_STATE_BY_UID";
        case OutboxKind::RULE_SENSOR_OFFLINE:   return "RULE_SENSOR_OFFLINE";
        case OutboxKind::DEVICE_COMMAND_FETCH:  return "DEVICE_COMMAND_FETCH";
        case OutboxKind::MODULE_COMMAND_FETCH:  return "MODULE_COMMAND_FETCH";
        case OutboxKind::COMMAND_RESULT:        return "COMMAND_RESULT";
        case OutboxKind::DEVICE_COMMAND_RESULT: return "DEVICE_COMMAND_RESULT";
        case OutboxKind::MODULE_COMMAND_RESULT: return "MODULE_COMMAND_RESULT";
        case OutboxKind::RAW_IO_RESULT:         return "RAW_IO_RESULT";
        case OutboxKind::MODULE_STRUCTURE:      return "MODULE_STRUCTURE";
        case OutboxKind::SENSOR_TYPES_FETCH:    return "SENSOR_TYPES_FETCH";
        case OutboxKind::WIFI_RECONNECT:        return "WIFI_RECONNECT";
        case OutboxKind::WIFI_ROAM:             return "WIFI_ROAM";
        case OutboxKind::LINK_PROBE:            return "LINK_PROBE";
        }
        return "?";
    }

    inline const char* ladderStepName(StartupScanStep::Kind kind)
    {
        switch (kind)
        {
        case StartupScanStep::NONE:               return "LADDER:NONE";
        case StartupScanStep::VERIFY_KNOWN:       return "LADDER:VERIFY_KNOWN";
        case StartupScanStep::PROBE_CANDIDATE:    return "LADDER:PROBE_CANDIDATE";
        case StartupScanStep::PROBE_PRESENCE:     return "LADDER:PROBE_PRESENCE";
        case StartupScanStep::SCAN_CANDIDATE:     return "LADDER:SCAN_CANDIDATE";
        case StartupScanStep::WRITE_ADDRESS:      return "LADDER:WRITE_ADDRESS";
        case StartupScanStep::PREPARE_KNOWN:      return "LADDER:PREPARE_KNOWN";
        case StartupScanStep::POWER_FREE_PORTS:   return "LADDER:POWER_FREE_PORTS";
        case StartupScanStep::RELEASE_FREE_PORTS: return "LADDER:RELEASE_FREE_PORTS";
        case StartupScanStep::PROBE_RANGE:        return "LADDER:PROBE_RANGE";
        case StartupScanStep::WRITE_PORT_ADDRESS: return "LADDER:WRITE_PORT_ADDRESS";
        case StartupScanStep::RELEASE_LINE:       return "LADDER:RELEASE_LINE";
        }
        return "?";
    }

    /** @brief What the log calls an act of a slot. Never null; "?" for a code nobody assigned. */
    inline const char* name(BackgroundActBoard::Slot slot, uint8_t act)
    {
        switch (slot)
        {
        case BackgroundActBoard::NETWORK:
            if (act == NET_MQTT_CONNECT)
            {
                return "MQTT_CONNECT";
            }
            // Every code under the link act's is an outbox kind; which of them have a name is
            // the switch's business alone, so that there is one place to extend and not two.
            if (act >= 1 && act < NET_MQTT_CONNECT)
            {
                return outboxKindName((OutboxKind)(act - 1u));
            }
            return "?";

        case BackgroundActBoard::ACQUISITION:
            if (act == ACQ_SWEEP)        return "SWEEP";
            if (act == ACQ_RAW_COMMAND)  return "RAW_COMMAND";
            if (act == ACQ_SCAN_COMMAND) return "SCAN_COMMAND";
            if (act >= ACQ_LADDER_BASE && act < ACQ_LADDER_BASE + ACQ_LADDER_KINDS)
            {
                return ladderStepName((StartupScanStep::Kind)(act - ACQ_LADDER_BASE));
            }
            return "?";

        case BackgroundActBoard::COMMAND_NETWORK:
            if (act == CMD_CATALOG_FETCH)   return "CATALOG_FETCH";
            if (act == CMD_FIRMWARE_UPDATE) return "FIRMWARE_UPDATE";
            if (act == CMD_DELETION_CHECK)  return "DELETION_CHECK";
            if (act == CMD_OTHER)           return "COMMAND";
            return "?";

        case BackgroundActBoard::CATALOG_STORAGE:
            return (act == CATALOG_WRITE) ? "WRITE" : "?";

        case BackgroundActBoard::SLOT_COUNT:
            break;
        }
        return "?";
    }

    inline const char* slotLabel(BackgroundActBoard::Slot slot)
    {
        switch (slot)
        {
        case BackgroundActBoard::NETWORK:         return "net";
        case BackgroundActBoard::ACQUISITION:     return "acq";
        case BackgroundActBoard::COMMAND_NETWORK: return "cmd";
        case BackgroundActBoard::CATALOG_STORAGE: return "cat";
        case BackgroundActBoard::SLOT_COUNT:      break;
        }
        return "?";
    }

#pragma GCC diagnostic pop

    /**
     * The widest a snapshot can be written: per slot " acq=" and the longest name followed by
     * "(done 16777215ms ago)" - 5 + 25 + 21 - and the terminator.
     */
    const size_t CONTEXT_BYTES =
        BackgroundActBoard::SLOT_COUNT * (5 + LONGEST_NAME_CHARS + 21) + 1;

    /**
     * @brief Write a snapshot as the tail of a log line: " net=... acq=... cmd=... cat=...".
     *
     * Every slot is written, idle ones too: a task that is absent from the line and a task that
     * was doing nothing must not read the same. A running act carries how long it had been
     * running, a finished one how long before the snapshot it ended.
     *
     * @return the number of characters the tail needs, terminator excluded - as snprintf does
     */
    inline size_t formatContext(const BackgroundActBoard::Snapshot& snapshot, char* out,
                                size_t outBytes)
    {
        size_t needed = 0;
        if (out != nullptr && outBytes > 0)
        {
            out[0] = '\0';
        }

        for (int index = 0; index < BackgroundActBoard::SLOT_COUNT; index++)
        {
            const BackgroundActBoard::Slot slot = (BackgroundActBoard::Slot)index;
            const BackgroundActBoard::Reading reading = BackgroundActBoard::read(snapshot, slot);

            char* at = (out != nullptr && needed < outBytes) ? out + needed : nullptr;
            const size_t room = (at != nullptr) ? outBytes - needed : 0;

            int written;
            if (reading.act == BackgroundActBoard::NO_ACT)
            {
                written = snprintf(at, room, " %s=idle", slotLabel(slot));
            }
            else if (reading.finished)
            {
                written = snprintf(at, room, " %s=%s(done %lums ago)", slotLabel(slot),
                                   name(slot, reading.act), (unsigned long)reading.ms);
            }
            else
            {
                written = snprintf(at, room, " %s=%s(%lums)", slotLabel(slot),
                                   name(slot, reading.act), (unsigned long)reading.ms);
            }

            if (written > 0)
            {
                needed += (size_t)written;
            }
        }

        return needed;
    }
}
