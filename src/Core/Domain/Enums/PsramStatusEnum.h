#ifndef PSRAM_STATUS_ENUM_H
#define PSRAM_STATUS_ENUM_H

enum PsramStatusEnum {
    PSRAM_STATUS_NONE = 0,
    PSRAM_STATUS_DONE = 1,
    PSRAM_STATUS_FAIL = 2
};

inline const char* getPsramStatusName(PsramStatusEnum status) {
    if (status == PSRAM_STATUS_DONE)
        return "done";
    if (status == PSRAM_STATUS_FAIL)
        return "fail";

    return "none";
}

#endif // PSRAM_STATUS_ENUM_H
