#pragma once

#include <string>
#include <vector>

/**
 * @brief Interface for data access operations using SPIFFS
 * @tparam T The entity type this DAO works with
 */
template <typename T>
class ISpiffsDao {
public:
    virtual ~ISpiffsDao() = default;

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