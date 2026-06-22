#ifndef MODULE_DELETION_STATUS_ENUM_H
#define MODULE_DELETION_STATUS_ENUM_H

enum class ModuleDeletionStatusEnum {
    DELETED,
    NOT_DELETED,
    UNKNOWN
};

inline const char* getModuleDeletionStatusName(ModuleDeletionStatusEnum status) {
    switch (status) {
        case ModuleDeletionStatusEnum::DELETED:
            return "DELETED";
        case ModuleDeletionStatusEnum::NOT_DELETED:
            return "NOT_DELETED";
        case ModuleDeletionStatusEnum::UNKNOWN:
            return "UNKNOWN";
        default:
            return "UNKNOWN";
    }
}

#endif // MODULE_DELETION_STATUS_ENUM_H
