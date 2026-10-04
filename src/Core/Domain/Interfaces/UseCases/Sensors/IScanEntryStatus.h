#pragma once

/**
 * @brief Is a sensor-discovery pass that has an end currently under way?
 *
 * The narrow question the front panel asks the discovery, and the only one it asks - which is why
 * it is a separate interface: the LED use case has no business seeing sensor entities, catalogs
 * or scan steps.
 *
 * "Has an end" is the whole point. The discovery runs two shapes of pass: the finite ones - the
 * one a restart arms and the one a ScanSensors command leaves behind it - and the cheap periodic
 * walk over the free ports, which goes on for as long as the module does. Only the finite ones
 * answer true here. A module that indicated the periodic walk would blink for the rest of its
 * life, which says nothing to anyone looking at it.
 */
class IScanEntryStatus
{
public:
    virtual ~IScanEntryStatus() = default;

    /** @brief True from the moment a finite pass is owed until the moment it is over. */
    virtual bool isScanEntryRunning() const = 0;
};
