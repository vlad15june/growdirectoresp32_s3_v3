#pragma once

/**
 * @brief Whether the boot's first association has been made, one way or the other.
 *
 * Setup waits for no network: the station is associated for the first time on the network task once
 * the loop runs. Until that association is over, a station with no link is a station still being
 * given one - not a boot without a link.
 */
class IBootAssociation
{
public:
    virtual ~IBootAssociation() = default;

    /** @brief True once the boot's association has been asked for and has come back, or was never needed. */
    virtual bool isTheBootAssociationOver() const = 0;
};
