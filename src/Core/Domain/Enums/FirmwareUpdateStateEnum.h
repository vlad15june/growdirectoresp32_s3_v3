#ifndef FIRMWARE_UPDATE_STATE_ENUM_H
#define FIRMWARE_UPDATE_STATE_ENUM_H

/**
 * @brief What the module last did about a firmware update, kept in the settings across a restart
 *
 * The numbers 3, 4 and 5 are what the module has always written and what the OTA and SETTINGS
 * logs are read against, so they stay as they are. IN_PROGRESS is the state S2 has and S3 did
 * not need until an update started surviving a restart (GDV1-3834).
 */
enum FirmwareUpdateStateEnum {
    FIRMWARE_UPDATE_STATE_IN_PROGRESS = 1,
    FIRMWARE_UPDATE_STATE_DONE = 3,
    FIRMWARE_UPDATE_STATE_ERROR = 4,
    FIRMWARE_UPDATE_STATE_HTTP_ERROR = 5
};

#endif // FIRMWARE_UPDATE_STATE_ENUM_H
