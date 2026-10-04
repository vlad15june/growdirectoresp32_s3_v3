#pragma once

#include "ISpiffsDao.h"
#include "ISpiffsAdapter.h"
#include "ISpiffsFileWriter.h"
#include "SpiffsFileReplacer.h"
#include "Infrastructure/Adapters/Json/JsonArrayStreamSplitter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include <memory>
#include <string>
#include <vector>
#include <stdarg.h>
#include <algorithm>

template <typename T>
class SpiffsDao : public ISpiffsDao<T> {
protected:
    ISpiffsAdapter* spiffsAdapter;
    IJsonConverter* jsonConverter;
    IDebugSystem* debugSystem;
    ILogger* logger;
    SpiffsFileReplacer fileReplacer;
    std::string filePath;

    /**
     * @brief Log method for DAO operations
     */
    void log(const char *format, ...) const {
        if (logger != nullptr && 
            (debugSystem == nullptr || debugSystem->isFeatureEnabled(DebugFeatures::SPIFFS_DEBUG))) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            logger->println(DebugFeatures::SPIFFS_DEBUG, buffer);
        }
    }

    // Methods to get path and file name
    virtual std::string getBasePath() const { return "/data"; }
    virtual std::string getFileName() const = 0;

    // Protected methods for JSON transformations
    virtual std::string entityToJson(const T& entity) = 0;
    virtual bool jsonToEntity(const std::string& jsonStr, T& entity) = 0;
    virtual std::string entitiesToJson(const std::vector<T>& entities) = 0;
    virtual bool jsonToEntities(const std::string& jsonStr, std::vector<T>& entities) = 0;

    /**
     * @brief Work out where this DAO's file lives; every entry point calls it on first use.
     *
     * Not called from the constructor: these DAOs are members of the dependency graph, which is
     * built before the file system is mounted, so anything they ask of storage there fails.
     *
     * No directory is made: SPIFFS is flat, mkdir() on it always answers ENOTSUP, and the base
     * path is simply the first part of the file's name.
     */
    void initialize() {
        log("SpiffsDao: Initializing for file: %s", getFileName().c_str());
        std::string basePath = getBasePath();
        filePath = basePath + "/" + getFileName();
        log("SpiffsDao: File path set to: %s", filePath.c_str());
    }

    /**
     * @brief Whether a text is a whole list, closed - with nothing in it, or anything
     *
     * Cut by the same splitter the readers cut the list with, so "whole" means what it means to
     * them: an array that never closes, or an object cut in half, is not one. What the records in
     * it are worth is not asked here.
     */
    class WholeListCheck : public JsonArrayStreamSplitter {
    public:
        explicit WholeListCheck(size_t maxObjectBytes) : JsonArrayStreamSplitter(maxObjectBytes) {}

    protected:
        bool onObjectStart() override { return true; }
        bool onObjectByte(char) override { return true; }
        bool onObjectEnd() override { return true; }
    };

    static bool isWholeList(const std::string& content) {
        // No object can be longer than the text it is in, so the cap never decides.
        WholeListCheck check(content.size() + 1);
        check.feed(content.data(), content.size());
        return check.finish() != JsonListParse::Failed;
    }

    /**
     * @brief Read one file whole, as statFile() sizes it
     *
     * @return Read when the file came back whole (`content` holds it), Nothing when the file
     *         system says it is not there, Unreadable when it is there and did not come back whole -
     *         the lookup failed, the open failed, or the read brought back fewer bytes than the file
     *         holds, wherever they went missing.
     */
    StoredRead readWholeFile(const std::string& path, std::string& content) {
        content.clear();
        size_t size = 0;
        bool missing = false;
        if (!spiffsAdapter->statFile(path.c_str(), size, missing)) {
            if (missing) {
                return StoredRead::Nothing;
            }
            log("SpiffsDao: %s could not be looked up", path.c_str());
            return StoredRead::Unreadable;
        }
        content = spiffsAdapter->readSPIFFSstr(path.c_str());
        if (content.size() != size) {
            log("SpiffsDao: %s came back with %d of %d bytes", path.c_str(),
                static_cast<int>(content.size()), static_cast<int>(size));
            content.clear();
            return StoredRead::Unreadable;
        }
        return StoredRead::Read;
    }

    /**
     * @brief Take the store from a leftover companion file when the file itself is not there
     *
     * saveAll() publishes a new file by renames (SpiffsFileReplacer), so a power cut between them
     * leaves the whole file under `<file>.tmp` (the new one) or `<file>.bak` (the one it was
     * replacing), with `<file>` missing. The new one is taken when it is a whole list - an empty
     * list included, which is what removing the last entity publishes - and the previous one only
     * when the new one is not whole. A companion that is there and does not come back whole leaves
     * the store Unreadable: what it holds cannot be judged, and the other one may be older.
     *
     * The companion is given the store's name; when it cannot be, it is the store all the same,
     * and its name is tried again at the next read.
     */
    StoredRead recoverFromCompanionFile(std::vector<T>& entities) {
        const std::string candidates[] = {
            fileReplacer.tempPathFor(filePath),   // the file that was being published
            fileReplacer.backupPathFor(filePath)  // the file it was replacing
        };

        for (const std::string& candidate : candidates) {
            std::string content;
            const StoredRead read = readWholeFile(candidate, content);
            if (read == StoredRead::Nothing) {
                continue;
            }
            if (read == StoredRead::Unreadable) {
                entities.clear();
                return StoredRead::Unreadable;
            }

            entities.clear();
            StoredRead found = StoredRead::Read;
            if (!jsonToEntities(content, entities)) {
                entities.clear();
                if (!isWholeList(content)) {
                    log("SpiffsDao: %s is not a whole list, ignoring it", candidate.c_str());
                    continue;
                }
                found = StoredRead::Nothing;
            }

            if (!fileReplacer.promote(candidate, filePath)) {
                log("SpiffsDao: Read %d entities from %s, its name is tried again at the next read",
                    static_cast<int>(entities.size()), candidate.c_str());
                return found;
            }

            log("SpiffsDao: Recovered %d entities from %s", static_cast<int>(entities.size()),
                candidate.c_str());
            // Whatever is left of the interrupted replacement describes an older state.
            fileReplacer.removeCompanions(filePath);
            return found;
        }

        return StoredRead::Nothing;
    }

public:
    SpiffsDao(ISpiffsAdapter* spiffsAdapter, IJsonConverter* jsonConverter, IDebugSystem* debugSystem = nullptr, ILogger* logger = nullptr)
        : spiffsAdapter(spiffsAdapter), jsonConverter(jsonConverter), debugSystem(debugSystem), logger(logger),
          fileReplacer(spiffsAdapter, logger, debugSystem) {
        if (!spiffsAdapter || !jsonConverter) {
            throw std::invalid_argument("Adapters cannot be null");
        }
    }

    virtual ~SpiffsDao() = default;

    /**
     * @brief Set debug system for logging
     */
    void setDebugSystem(IDebugSystem *debugSystemInstance) {
        debugSystem = debugSystemInstance;
        fileReplacer.setDebugSystem(debugSystemInstance);
    }

    /**
     * @brief Set logger for output
     */
    void setLogger(ILogger *loggerInstance) {
        logger = loggerInstance;
        fileReplacer.setLogger(loggerInstance);
    }

    bool save(const T& entity, const std::string& uid) override {
        log("SpiffsDao: Saving entity with UID: %s", uid.c_str());
        if (filePath.empty()) initialize();
        
        std::vector<T> entities;
        if (readAll(entities) == StoredRead::Unreadable) {
            log("SpiffsDao: Stored entities did not come back whole, not writing over them");
            return false;
        }
        
        auto it = std::find_if(entities.begin(), entities.end(), [&uid](const T& e) { return e.uid == uid; });
        if (it != entities.end()) {
            log("SpiffsDao: Updating existing entity");
            *it = entity;
        } else {
            log("SpiffsDao: Adding new entity");
            entities.push_back(entity);
        }
        
        bool result = saveAll(entities);
        log("SpiffsDao: Save operation %s", result ? "successful" : "failed");
        return result;
    }

    bool getById(const std::string& uid, T& entity) override {
        log("SpiffsDao: Getting entity by UID: %s", uid.c_str());
        if (filePath.empty()) initialize();
        
        std::vector<T> entities;
        if (!getAll(entities)) {
            log("SpiffsDao: Failed to get all entities");
            return false;
        }
        
        auto it = std::find_if(entities.begin(), entities.end(), [&uid](const T& e) { return e.uid == uid; });
        if (it == entities.end()) {
            log("SpiffsDao: Entity not found");
            return false;
        }
        
        entity = *it;
        log("SpiffsDao: Entity found and retrieved");
        return true;
    }

    bool remove(const std::string& uid) override {
        log("SpiffsDao: Removing entity with UID: %s", uid.c_str());
        if (filePath.empty()) initialize();
        
        std::vector<T> entities;
        if (!getAll(entities)) {
            log("SpiffsDao: Failed to get all entities for removal");
            return false;
        }
        
        auto it = std::find_if(entities.begin(), entities.end(), [&uid](const T& e) { return e.uid == uid; });
        if (it == entities.end()) {
            log("SpiffsDao: Entity not found for removal");
            return false;
        }
        
        entities.erase(it);
        bool result = saveAll(entities);
        log("SpiffsDao: Remove operation %s", result ? "successful" : "failed");
        return result;
    }

    /**
     * @brief Write the whole file
     *
     * The array goes to a companion file that takes the stored file's place only once all of it
     * is there (ISpiffsAdapter::openForRewrite()), so a write that fails part of the way leaves
     * the previous file whole. Written over in place, it would leave the file cut short: an array
     * that never closes, which the readers refuse whole - the module would come up with nothing.
     */
    bool saveAll(const std::vector<T>& entities) override {
        log("SpiffsDao: Saving %d entities", entities.size());
        if (filePath.empty()) initialize();
        
        std::string jsonStr = entitiesToJson(entities);
        log("SpiffsDao: JSON serialization complete, length: %d", jsonStr.length());
        
        bool result = false;
        std::unique_ptr<ISpiffsFileWriter> writer = spiffsAdapter->openForRewrite(filePath.c_str());
        if (!writer) {
            log("SpiffsDao: Could not open %s for writing", filePath.c_str());
        } else if (!writer->write(jsonStr.c_str())) {
            writer->abort();
        } else {
            result = writer->commit();
        }
        log("SpiffsDao: File write %s", result ? "complete" : "failed");
        return result;
    }

    /**
     * @brief Read the whole file, and say whether what came back is all of it
     *
     * Every read is checked against the file's size as the file system's index holds it
     * (statFile()), not against what the read itself brought back. Unreadable is a file that is
     * there and did not come back whole: the open failed, or the read lost bytes - at its end, or
     * anywhere inside, as a string that could not grow for one character and could for the next
     * does. A write built on such a read would store what the read made of the file.
     *
     * A file that came back whole is taken for what it holds. With no bytes in it, it holds
     * nothing. If it is not a whole list it is broken rather than unread: reading it again brings
     * back the same bytes, and nothing in them can be read. Both are reported as Nothing, so that
     * the next write replaces them rather than every write being refused for as long as they are
     * there.
     */
    StoredRead readAll(std::vector<T>& entities) override {
        log("SpiffsDao: Reading all entities");
        if (filePath.empty()) initialize();
        entities.clear();

        std::string content;
        const StoredRead read = readWholeFile(filePath, content);
        if (read == StoredRead::Nothing) {
            log("SpiffsDao: %s is not there", filePath.c_str());
            return recoverFromCompanionFile(entities);
        }
        if (read == StoredRead::Unreadable) {
            return StoredRead::Unreadable;
        }

        log("SpiffsDao: File content read, length: %d", content.length());
        if (content.empty()) {
            return StoredRead::Nothing;
        }
        if (jsonToEntities(content, entities)) {
            log("SpiffsDao: %d entities read", static_cast<int>(entities.size()));
            return StoredRead::Read;
        }
        entities.clear();
        if (!isWholeList(content)) {
            log("SpiffsDao: %s is not a whole list; the next write replaces it", filePath.c_str());
        }
        return StoredRead::Nothing;
    }

    bool getAll(std::vector<T>& entities) override {
        log("SpiffsDao: Getting all entities");
        return readAll(entities) == StoredRead::Read;
    }

    bool clear() override {
        log("SpiffsDao: Clearing all data (removing file)");
        if (filePath.empty()) initialize();
        
        // Just remove the file, no need to check if it exists first
        spiffsAdapter->removeFile(filePath.c_str());
        // An empty store has to stay empty: a leftover companion file would otherwise be taken
        // for the file by the recovery in readAll().
        fileReplacer.removeCompanions(filePath);
        log("SpiffsDao: Clear operation completed");
        return true;
    }
};

// Force explicit template instantiation for Rule
#include "Core/Domain/Data/Entities/Rule.h"
template class SpiffsDao<Rule>;

// Force explicit template instantiation for SensorEntity
#include "Core/Domain/Data/Entities/Sensor/Sensor.h"
template class SpiffsDao<SensorEntity>;
