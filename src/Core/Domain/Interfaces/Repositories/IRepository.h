#ifndef I_REPOSITORY_H
#define I_REPOSITORY_H

#include <vector>
#include <string>
#include <optional>

template<typename T>
class IRepository {
public:
    /**
     * @brief Get entity by unique identifier
     * @param uid Unique identifier of the entity
     * @return Entity if found, nullopt otherwise
     */
    virtual std::optional<T> get(const char* uid) = 0;

    /**
     * @brief Get all entities
     * @return Vector of all entities
     */
    virtual std::vector<T> getAll() = 0;

    /**
     * @brief Save entity (create or update)
     * @param entity Entity to save
     * @return true if operation was successful, false otherwise
     */
    virtual bool save(const T& entity) = 0;

    /**
     * @brief Save multiple entities (create or update)
     * @param entities Vector of entities to save
     * @return Number of successfully saved entities
     */
    virtual int saveAll(const std::vector<T>& entities) = 0;

    /**
     * @brief Delete entity by unique identifier
     * @param uid Unique identifier of the entity to delete
     * @return true if operation was successful, false otherwise
     */
    virtual bool remove(const char* uid) = 0;

    /**
     * @brief Delete all entities
     * @return Number of deleted entities
     */
    virtual int removeAll() = 0;

    /**
     * @brief Count total number of entities
     * @return Number of entities
     */
    virtual int count() = 0;

    /**
     * @brief Check if entity with given UID exists
     * @param uid Unique identifier to check
     * @return true if entity exists, false otherwise
     */
    virtual bool exists(const char* uid) = 0;

    virtual ~IRepository() = default;
};

#endif // I_REPOSITORY_H
