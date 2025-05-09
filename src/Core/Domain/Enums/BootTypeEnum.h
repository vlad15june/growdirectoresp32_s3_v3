#ifndef BOOT_TYPE_ENUM_H
#define BOOT_TYPE_ENUM_H

enum BootTypeEnum {
    BOOT_TYPE_POWER_ON = 19,
    BOOT_TYPE_USER = 86,
    BOOT_TYPE_TECHNICAL = 101,
    BOOT_TYPE_FIRMWARE_UPDATE = 111,
    BOOT_TYPE_SENSOR_CHNG = 122
};

// Utility function to get string representation of boot type
inline const char* getBootTypeName(BootTypeEnum bootType) {
    if (bootType == BOOT_TYPE_USER)
        return "reboot-user";
    if (bootType == BOOT_TYPE_TECHNICAL)
        return "reboot-technical";
    if (bootType == BOOT_TYPE_SENSOR_CHNG)
        return "reboot-sensor-type-chng";
    if (bootType == BOOT_TYPE_FIRMWARE_UPDATE)
        return "reboot-firmware-update";

    return "power-on"; // Default (BOOT_TYPE_POWER_ON)
}

#endif // BOOT_TYPE_ENUM_H
