#pragma once

/**
 * @brief The three file system operations a safe file replacement needs (GDV1-3827)
 *
 * Kept apart from ISpiffsAdapter so that whoever only moves files around — the streaming writer and
 * SpiffsFileReplacer — depends on exactly that and can run outside the ESP32 (the adapter itself
 * implements this interface, so callers that already hold one pass it straight through).
 */
class ISpiffsFileOps {
public:
    virtual ~ISpiffsFileOps() = default;

    /**
     * @brief Whether the path is present in storage
     */
    virtual bool fileExists(const char* filePath) = 0;

    /**
     * @brief Remove the file
     * @return true if the path is gone afterwards (including when it was already absent)
     */
    virtual bool deleteFile(const char* filePath) = 0;

    /**
     * @brief Move a file to another name
     *
     * SPIFFS refuses an existing destination, so callers make sure the destination is free first.
     *
     * @return true if the file now lives under the new name
     */
    virtual bool renameFile(const char* fromPath, const char* toPath) = 0;
};
