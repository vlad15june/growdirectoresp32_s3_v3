#pragma once

/**
 * @brief State of an app slot as the bootloader's rollback machinery sees it.
 *
 * Collapsed from the six esp_ota_img_states_t values to the four distinctions the firmware
 * acts on: confirmed, on probation, rejected, or carrying no verdict at all.
 */
enum class OtaImageState {
    /** Confirmed: the bootloader will keep booting this image. */
    VALID,
    /** On probation: boots once; without confirmation the next reset rolls back. */
    PENDING_VERIFY,
    /** Rejected: the image was rolled back or marked invalid and will not be booted. */
    ABORTED,
    /** No verdict recorded - a factory image or a slot written before rollback was enabled. */
    UNDEFINED,
    /** The state could not be read. */
    UNKNOWN
};

inline const char* getOtaImageStateName(OtaImageState state) {
    switch (state) {
        case OtaImageState::VALID:          return "valid";
        case OtaImageState::PENDING_VERIFY: return "pending-verify";
        case OtaImageState::ABORTED:        return "aborted";
        case OtaImageState::UNDEFINED:      return "undefined";
        default:                            return "unknown";
    }
}

/**
 * @brief The firmware's window into the bootloader's A/B slot bookkeeping.
 *
 * Wraps the esp_ota_* state calls so that the confirmation logic - when a freshly booted image
 * has earned its keep, and what a boot after a rollback looks like - lives in a use case the
 * host tests execute, and the SDK calls stay in a device-only adapter.
 */
class IOtaPartitionControl {
public:
    virtual ~IOtaPartitionControl() = default;

    /** @brief State of the slot the module booted from. */
    virtual OtaImageState runningImageState() = 0;

    /** @brief State of the other app slot - after a rollback it holds the rejected image. */
    virtual OtaImageState otherImageState() = 0;

    /**
     * @brief Confirm the running image: cancel the pending rollback
     *
     * @return true if the bootloader accepted the confirmation
     */
    virtual bool markRunningImageValid() = 0;

    /** @brief Partition-table label of the running slot, for log lines. */
    virtual const char* runningPartitionLabel() = 0;

    /** @brief Partition-table label of the other app slot, for log lines. */
    virtual const char* otherPartitionLabel() = 0;
};
