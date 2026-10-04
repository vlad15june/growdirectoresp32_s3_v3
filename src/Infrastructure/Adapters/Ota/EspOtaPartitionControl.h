#pragma once

#include "Core/Domain/Interfaces/Adapters/IOtaPartitionControl.h"

#include <esp_ota_ops.h>

/**
 * @brief IOtaPartitionControl over esp_ota_ops.
 *
 * A thin adapter: each method is a lookup or one SDK call, so the decisions - when to confirm,
 * what a rollback looks like on the next boot - stay in ConfirmRunningFirmwareUseCase where the
 * host tests reach them.
 */
class EspOtaPartitionControl : public IOtaPartitionControl {
private:
    static OtaImageState toImageState(const esp_partition_t* partition) {
        if (partition == nullptr) {
            return OtaImageState::UNKNOWN;
        }
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
            return OtaImageState::UNKNOWN;
        }
        switch (state) {
            case ESP_OTA_IMG_VALID:
                return OtaImageState::VALID;
            case ESP_OTA_IMG_NEW:
            case ESP_OTA_IMG_PENDING_VERIFY:
                return OtaImageState::PENDING_VERIFY;
            case ESP_OTA_IMG_INVALID:
            case ESP_OTA_IMG_ABORTED:
                return OtaImageState::ABORTED;
            case ESP_OTA_IMG_UNDEFINED:
            default:
                return OtaImageState::UNDEFINED;
        }
    }

public:
    OtaImageState runningImageState() override {
        return toImageState(esp_ota_get_running_partition());
    }

    OtaImageState otherImageState() override {
        // "Next update" from the running slot is exactly the other app slot of the A/B pair -
        // after a rollback it is the one holding the rejected image.
        return toImageState(esp_ota_get_next_update_partition(nullptr));
    }

    bool markRunningImageValid() override {
        return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
    }

    const char* runningPartitionLabel() override {
        const esp_partition_t* partition = esp_ota_get_running_partition();
        return (partition != nullptr) ? partition->label : "?";
    }

    const char* otherPartitionLabel() override {
        const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
        return (partition != nullptr) ? partition->label : "?";
    }
};
