#pragma once

/**
 * @brief What a producer asks before it does something whose moment the boot sequence decides.
 *
 * After setup the module comes up in an order of events - the link, the structure that reports the
 * module, the catalog, the first readings - and not in an order of timers. The sequence that keeps
 * that order does not send, queue or read anything itself: each producer asks its own gate at the
 * moment it would act, and acts on its own path when the gate lets it.
 */
enum class BootGate : unsigned char
{
    /** The sensor update to the server: after the boot's structure has settled and every stored
     *  ModBus sensor has answered or failed once. */
    TELEMETRY,
    /** The rule list fetch: after the boot's structure has settled. */
    RULE_LIST,
    /** The device update to the server: after the boot's structure has settled. */
    DEVICE_UPDATE,
    /**
     * The startup discovery on the line, before its boot window is over: once every stored ModBus
     * sensor has answered or failed once and the boot's network phases are over - or, with no link,
     * at once with a catalog the boot armed the discovery with. Never later than the window.
     */
    DISCOVERY,
    /**
     * A structure the discovery sends: only once the boot's own structure - the module's report of
     * itself and of its address - has settled. Until then the discovery owes its structure.
     */
    STRUCTURE_REPUBLISH,
    /**
     * The diagnostic's send: from the boot's first STEADY on. Until then the diagnostic's own tick
     * takes its snapshot and sends nothing; the send owed goes once, at the first STEADY.
     */
    DIAGNOSTIC_SEND
};

class IBootGates
{
public:
    virtual ~IBootGates() = default;

    /**
     * @brief Has the sequence started deciding?
     *
     * False until its first tick on the control loop. A producer that is asked to act before then -
     * or one that was given no sequence at all - acts on its own timers alone.
     */
    virtual bool isSequencing() const = 0;

    /** @brief May the producer this gate stands in front of act now? */
    virtual bool allows(BootGate gate) const = 0;
};
