#ifndef FILE_SYSTEM_STATUS_ENUM_H
#define FILE_SYSTEM_STATUS_ENUM_H

enum FileSystemStatusEnum {
    FILE_SYSTEM_STATUS_NONE = 0,
    FILE_SYSTEM_STATUS_DONE = 1,
    FILE_SYSTEM_STATUS_FAIL = 2
};

// Utility function to get string representation of file system status
inline const char* getFileSystemStatusName(FileSystemStatusEnum status) {
    if (status == FILE_SYSTEM_STATUS_DONE)
        return "done";
    if (status == FILE_SYSTEM_STATUS_FAIL)
        return "fail";
    
    return "none"; // Default (FILE_SYSTEM_STATUS_NONE)
}

#endif // FILE_SYSTEM_STATUS_ENUM_H
