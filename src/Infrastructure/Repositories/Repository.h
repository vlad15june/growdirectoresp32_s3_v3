#pragma once

#include "Core/Domain/Interfaces/Repositories/IRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsDao.h"
#include <string>
#include <vector>
#include <optional>
#include <memory>

/**
 * @brief Generic repository implementation using SpiffsDao for data access
 * @tparam T Entity type
 */
template <typename T>
class Repository : public IRepository<T> {
private:
    ISpiffsDao<T>* dao;

public:
    /**
     * @brief Construct repository with SpiffsDao implementation
     * @param dao Data access object for entity type T
     */
    Repository(ISpiffsDao<T>* dao) : dao(dao) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
    }

    /**
     * @brief Get entity by unique identifier
     * @param uid Unique identifier of the entity
     * @return Entity if found, nullopt otherwise
     */
    std::optional<T> get(const char* uid) override {
        if (!uid) return std::nullopt;
        T entity;
        if (dao->getById(uid, entity)) {
            return entity;
        }
        return std::nullopt;
    }

    /**
     * @brief Get all entities
     * @return Vector of all entities
     */
    std::vector<T> getAll() override {
        std::vector<T> entities;
        dao->getAll(entities);
        return entities;
    }

    /**
     * @brief Save entity (create or update)
     * @param entity Entity to save
     * @return true if operation was successful, false otherwise
     */
    bool save(const T& entity) override {
        // Assuming entity has a 'uid' field
        return dao->save(entity, entity.uid);
    }

    /**
     * @brief Save multiple entities (create or update)
     * @param entities Vector of entities to save
     * @return Number of successfully saved entities
     */
    int saveAll(const std::vector<T>& entities) override {
        if (dao->saveAll(entities)) {
            return entities.size();
        }
        return 0;
    }

    /**
     * @brief Delete entity by unique identifier
     * @param uid Unique identifier of the entity to delete
     * @return true if operation was successful, false otherwise
     */
    bool remove(const char* uid) override {
        return dao->remove(uid);
    }

    /**
     * @brief Delete all entities
     * @return Number of deleted entities
     */
    int removeAll() override {
        int count = this->count();
        dao->saveAll(std::vector<T>());  // Save empty vector to clear all entities
        return count;
    }

    /**
     * @brief Count total number of entities
     * @return Number of entities
     */
    int count() override {
        std::vector<T> entities;
        if (dao->getAll(entities)) {
            return entities.size();
        }
        return 0;
    }

    /**
     * @brief Check if entity with given UID exists
     * @param uid Unique identifier to check
     * @return true if entity exists, false otherwise
     */
    bool exists(const char* uid) override {
        T entity;
        return dao->getById(uid, entity);
    }

    /**
     * @brief Get the underlying DAO object
     * @return Pointer to the DAO object
     */
    ISpiffsDao<T>* getDao() {
        return dao;
    }
};
