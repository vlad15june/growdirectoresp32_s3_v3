#ifndef STORAGE_WRITE_ERROR_ENUM_H
#define STORAGE_WRITE_ERROR_ENUM_H

/**
 * @brief Why the last persistent write attempt did not store the data.
 *
 * Reported next to the boolean/count result so that a refusal to write is distinguishable from a
 * legitimately empty payload. GDV1-3827: a swallowed std::bad_alloc became an empty "[]" catalog,
 * the DAO refused the write, and the only trace left was "saveAll wrote 0 of N" with no reason.
 */
enum StorageWriteErrorEnum {
    STORAGE_WRITE_ERROR_NONE = 0,
    STORAGE_WRITE_ERROR_SERIALIZATION = 1,
    STORAGE_WRITE_ERROR_OPEN = 2,
    STORAGE_WRITE_ERROR_WRITE = 3,
    STORAGE_WRITE_ERROR_COMMIT = 4
};

// Utility function to get string representation of a storage write error
inline const char* getStorageWriteErrorName(StorageWriteErrorEnum error) {
    if (error == STORAGE_WRITE_ERROR_SERIALIZATION)
        return "serialization";
    if (error == STORAGE_WRITE_ERROR_OPEN)
        return "open";
    if (error == STORAGE_WRITE_ERROR_WRITE)
        return "write";
    if (error == STORAGE_WRITE_ERROR_COMMIT)
        return "commit";

    return "none"; // Default (STORAGE_WRITE_ERROR_NONE)
}

#endif // STORAGE_WRITE_ERROR_ENUM_H
