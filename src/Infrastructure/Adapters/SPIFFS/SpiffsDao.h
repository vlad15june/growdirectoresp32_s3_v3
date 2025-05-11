#pragma once

#include "ISpiffsDao.h"
#include "ISpiffsAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include <string>
#include <vector>
#include <stdexcept>

template <typename T>
class SpiffsDao : public ISpiffsDao<T> {
protected:
    ISpiffsAdapter* spiffsAdapter;
    IJsonConverter* jsonConverter;
    std::string filePath;

    // Methods to get path and file name
    virtual std::string getBasePath() const { return "/data"; }
    virtual std::string getFileName() const = 0;

    // Protected methods for JSON transformations
    virtual std::string entityToJson(const T& entity) = 0;
    virtual bool jsonToEntity(const std::string& jsonStr, T& entity) = 0;
    virtual std::string entitiesToJson(const std::vector<T>& entities) = 0;
    virtual bool jsonToEntities(const std::string& jsonStr, std::vector<T>& entities) = 0;

public:
    SpiffsDao(ISpiffsAdapter* spiffsAdapter, IJsonConverter* jsonConverter)
        : spiffsAdapter(spiffsAdapter), jsonConverter(jsonConverter) {
        if (!spiffsAdapter || !jsonConverter) {
            throw std::invalid_argument("Adapters cannot be null");
        }

        std::string basePath = getBasePath();
        if (!spiffsAdapter->directoryExists(basePath.c_str())) {
            spiffsAdapter->createDirectory(basePath.c_str());
        }

        filePath = basePath + "/" + getFileName();
    }

    virtual ~SpiffsDao() = default;

    bool save(const T& entity, const std::string& id) override {
        std::vector<T> entities;
        getAll(entities);
        auto it = std::find_if(entities.begin(), entities.end(), [&id](const T& e) { return e.id == id; });
        if (it != entities.end()) {
            *it = entity;
        } else {
            entities.push_back(entity);
        }

        return saveAll(entities);
    }

    bool getById(const std::string& id, T& entity) override {
        std::vector<T> entities;
        if (!getAll(entities)) return false;
        auto it = std::find_if(entities.begin(), entities.end(), [&id](const T& e) { return e.id == id; });
        if (it == entities.end()) return false;
        entity = *it;
        return true;
    }

    bool remove(const std::string& id) override {
        std::vector<T> entities;
        if (!getAll(entities)) return false;
        auto it = std::find_if(entities.begin(), entities.end(), [&id](const T& e) { return e.id == id; });
        if (it == entities.end()) return false;
        entities.erase(it);
        return saveAll(entities);
    }

    bool saveAll(const std::vector<T>& entities) override {
        std::string jsonStr = entitiesToJson(entities);
        return spiffsAdapter->RewriteToFile(filePath.c_str(), jsonStr.c_str());
    }

    bool getAll(std::vector<T>& entities) override {
        String content = spiffsAdapter->readSPIFFSstr(filePath.c_str());
        if (content.length() == 0) return false;
        return jsonToEntities(content.c_str(), entities);
    }
};