#pragma once

#include "ISpiffsFileOps.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include <stdarg.h>
#include <cstdio>
#include <string>

/**
 * @brief Replaces a stored file with a freshly written one without ever leaving it absent (GDV1-3827)
 *
 * SPIFFS.rename() refuses an existing destination, and the first version of the fix worked around
 * that by removing the target and renaming again — between those two calls the catalog existed
 * nowhere, so a failing second rename (or a power cut) lost it for good, which is the opposite of
 * what the fix promised.
 *
 * Here the old file is moved aside first (`<file>.bak`), the new one takes its name, and only then
 * the reserve is dropped. Every step is a rename, so at any instant the data is complete under at
 * least one name:
 *
 *   - cut after `<file>` -> `<file>.bak`:      the previous catalog is in `.bak`, the new one in `.tmp`
 *   - cut after `<file>.tmp` -> `<file>`:      the new catalog is in place, `.bak` is leftover garbage
 *
 * Both states are picked up on the next load (SpiffsSensorTypeDao::getAll()), and leftovers are
 * cleaned up by the next successful replacement. The naming of both companion files lives here so
 * that the writer and the load-time recovery cannot drift apart.
 */
class SpiffsFileReplacer {
public:
    SpiffsFileReplacer(ISpiffsFileOps* fileOps, ILogger* logger = nullptr, IDebugSystem* debugSystem = nullptr)
        : fileOps(fileOps), logger(logger), debugSystem(debugSystem) {}

    /**
     * @brief Where a new version of `targetPath` is streamed before it takes over
     */
    std::string tempPathFor(const std::string& targetPath) const {
        return targetPath + ".tmp";
    }

    /**
     * @brief Where the previous version waits while the new one takes its name
     */
    std::string backupPathFor(const std::string& targetPath) const {
        return targetPath + ".bak";
    }

    /**
     * @brief Publish `tempPath` as `targetPath`, keeping the previous content until that succeeded
     *
     * On failure the previous file is put back under its own name and the caller gets false; the new
     * content is left in `tempPath` for the caller to drop.
     */
    bool replace(const std::string& tempPath, const std::string& targetPath) {
        if (fileOps == nullptr) {
            return false;
        }
        if (!fileOps->fileExists(tempPath.c_str())) {
            log("SpiffsFileReplacer: Nothing to publish, %s is missing", tempPath.c_str());
            return false;
        }

        const std::string backupPath = backupPathFor(targetPath);
        const bool hadTarget = fileOps->fileExists(targetPath.c_str());

        if (hadTarget) {
            // A reserve from an earlier interrupted replacement is stale by definition: the file it
            // was made from has been read and rewritten since.
            if (fileOps->fileExists(backupPath.c_str())) {
                fileOps->deleteFile(backupPath.c_str());
            }
            if (!fileOps->renameFile(targetPath.c_str(), backupPath.c_str())) {
                log("SpiffsFileReplacer: Failed to move %s aside to %s, keeping it as it is",
                    targetPath.c_str(), backupPath.c_str());
                return false;
            }
        }

        if (fileOps->renameFile(tempPath.c_str(), targetPath.c_str())) {
            if (hadTarget) {
                fileOps->deleteFile(backupPath.c_str());
            }
            return true;
        }

        log("SpiffsFileReplacer: Failed to move %s onto %s", tempPath.c_str(), targetPath.c_str());
        if (hadTarget && !fileOps->renameFile(backupPath.c_str(), targetPath.c_str())) {
            // Nothing else can be done here: the previous content is intact under .bak and the next
            // load takes it from there.
            log("SpiffsFileReplacer: %s stays in %s, load-time recovery will pick it up",
                targetPath.c_str(), backupPath.c_str());
        }
        return false;
    }

    /**
     * @brief Accept a leftover companion file as the target file (load-time recovery)
     *
     * Used when the target is missing and the caller has read `candidatePath` and found it complete.
     * A failure is not fatal: the content stays readable under its own name and recovery is retried
     * on the next load.
     */
    bool promote(const std::string& candidatePath, const std::string& targetPath) {
        if (fileOps == nullptr) {
            return false;
        }
        if (fileOps->fileExists(targetPath.c_str())) {
            return false;
        }
        if (!fileOps->renameFile(candidatePath.c_str(), targetPath.c_str())) {
            log("SpiffsFileReplacer: Failed to restore %s from %s", targetPath.c_str(), candidatePath.c_str());
            return false;
        }
        log("SpiffsFileReplacer: Restored %s from %s", targetPath.c_str(), candidatePath.c_str());
        return true;
    }

    void setLogger(ILogger* loggerInstance) {
        logger = loggerInstance;
    }

    void setDebugSystem(IDebugSystem* debugSystemInstance) {
        debugSystem = debugSystemInstance;
    }

    /**
     * @brief Drop both companion files of a target (used when the target is deliberately cleared)
     */
    void removeCompanions(const std::string& targetPath) {
        if (fileOps == nullptr) {
            return;
        }
        const std::string tempPath = tempPathFor(targetPath);
        const std::string backupPath = backupPathFor(targetPath);
        if (fileOps->fileExists(tempPath.c_str())) {
            fileOps->deleteFile(tempPath.c_str());
        }
        if (fileOps->fileExists(backupPath.c_str())) {
            fileOps->deleteFile(backupPath.c_str());
        }
    }

private:
    void log(const char* format, ...) const {
        if (logger != nullptr &&
            (debugSystem == nullptr || debugSystem->isFeatureEnabled(DebugFeatures::SPIFFS_DEBUG))) {
            char buffer[256];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            logger->println(DebugFeatures::SPIFFS_DEBUG, buffer);
        }
    }

    ISpiffsFileOps* fileOps;
    ILogger* logger;
    IDebugSystem* debugSystem;
};
