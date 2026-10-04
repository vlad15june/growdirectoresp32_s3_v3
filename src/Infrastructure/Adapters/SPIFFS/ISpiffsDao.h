#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief What a read of everything stored brought back.
 */
enum class StoredRead : uint8_t {
    /** The stored entities, at least one of them. */
    Read,
    /** Nothing is stored: no file, or a complete list with nothing in it that can be read. */
    Nothing,
    /** Something is stored and did not come back whole. Not the same as Nothing: a write built
     *  on it would replace every stored entity with the few it was given. */
    Unreadable,
};

/**
 * @brief Interface for data access operations using SPIFFS
 * @tparam T The entity type this DAO works with
 */
template <typename T>
class ISpiffsDao {
public:
    virtual ~ISpiffsDao() = default;

    /**
     * @brief Read everything stored, and say whether what came back is all of it
     *
     * A store that cannot tell a read that failed from an empty one answers from getAll(): Read
     * when it returned true, Nothing otherwise.
     */
    virtual StoredRead readAll(std::vector<T>& entities) {
        return getAll(entities) ? StoredRead::Read : StoredRead::Nothing;
    }

    /**
     * @brief Save an entity to SPIFFS
     * @param entity The entity to save
     * @param uid Unique identifier for the entity
     * @return true if the operation was successful, false otherwise
     */
    virtual bool save(const T& entity, const std::string& uid) = 0;

    /**
     * @brief Get an entity by its ID
     * @param uid The ID of the entity to retrieve
     * @param entity Reference to an entity object to populate
     * @return true if the entity was found, false otherwise
     */
    virtual bool getById(const std::string& uid, T& entity) = 0;

    /**
     * @brief Remove an entity from SPIFFS storage
     * @param uid The ID of the entity to remove
     * @return true if the entity was removed, false otherwise
     */
    virtual bool remove(const std::string& uid) = 0;

    /**
     * @brief Save multiple entities to SPIFFS
     * @param entities Vector of entities to save
     * @return true if the operation was successful, false otherwise
     */
    virtual bool saveAll(const std::vector<T>& entities) = 0;

    /**
     * @brief Get all entities from SPIFFS
     * @param entities Reference to a vector to populate with entities
     * @return true if the operation was successful, false otherwise
     */
    virtual bool getAll(std::vector<T>& entities) = 0;

    /**
     * @brief Clear all data from storage (remove file)
     * @return true if the operation was successful, false otherwise
     */
    virtual bool clear() = 0;
};