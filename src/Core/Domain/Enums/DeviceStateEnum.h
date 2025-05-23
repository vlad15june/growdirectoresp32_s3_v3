#ifndef DEVICE_STATE_ENUM_H
#define DEVICE_STATE_ENUM_H

enum DeviceStateEnum {
    DEVICE_STATE_NONE,
    DEVICE_STATE_ON,
    DEVICE_STATE_OFF,
    DEVICE_STATE_OFF_BY_POWER_ON,
    DEVICE_STATE_DELAY_ON,
    DEVICE_STATE_DELAY_OFF,
    DEVICE_STATE_REQUEST_ON,
    DEVICE_STATE_REQUEST_STEP_ON,
    DEVICE_STATE_STEP_ON,
    DEVICE_STATE_STEP_WAITING
};
//DEVICE_STATE_OFF_BY_RULE -> DEVICE_STATE_OFF
//

// Utility function to get string representation of device state
inline const char* getDeviceStateName(DeviceStateEnum deviceState) {
    switch (deviceState) {
        case DEVICE_STATE_NONE:
            return "none";
        case DEVICE_STATE_ON:
            return "on";
        case DEVICE_STATE_OFF:
            return "off";
        case DEVICE_STATE_OFF_BY_POWER_ON:
            return "offByPowerOn";
        case DEVICE_STATE_DELAY_ON:
            return "delayOn";
        case DEVICE_STATE_DELAY_OFF:
            return "delayOff";
        case DEVICE_STATE_REQUEST_ON:
            return "requestOn";
        case DEVICE_STATE_REQUEST_STEP_ON:
            return "requestStepOn";
        case DEVICE_STATE_STEP_ON:
            return "stepOn";
        case DEVICE_STATE_STEP_WAITING:
            return "stepWaiting";
        default:
            return "unknown";
    }
}

#endif // DEVICE_STATE_ENUM_H
