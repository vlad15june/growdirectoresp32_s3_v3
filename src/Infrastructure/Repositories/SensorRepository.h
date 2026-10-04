#pragma once

#include "Repository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Infrastructure/Adapters/SPIFFS/ISpiffsSensorDao.h"
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Implementation of sensor repository using SPIFFS storage
 */
class SensorRepository : public Repository<SensorEntity>, public ISensorRepository {

private:
    ISpiffsSensorDao* sensorDao;

    // Cached filtered view behind getActiveSensors(). The control loop asks for the active
    // sensors on every iteration; rebuilding the view only when the repository's generation
    // moved makes the settled iteration allocation-free. The base class bumps the generation on
    // every change path, so a stale view cannot survive a save, a status update or a removal. The
    // one change that does not move the generation is a runtime error flag, which is written into
    // this view directly instead (see errorOverlay).
    std::vector<SensorEntity> activeSensorsView;
    uint32_t activeSensorsGeneration = 0;
    bool activeSensorsViewValid = false;

    // Runtime error flags, by sensor uid. `isError` is the health of the sensor right now, not
    // part of its configuration: it changes on every sweep a sensor misses and every sweep it
    // comes back, and nothing reads it from storage - the readers take it from here, and after a
    // reboot the first bus sweep establishes it again. Keeping it here is what stops a flapping
    // sensor from rewriting the whole registry file, and dropping the cache with it, twice a
    // minute for as long as it flaps.
    //
    // Only entries that disagree with the stored entity are held, so the map is empty on a
    // healthy module and never holds more than one entry per stored sensor: an entry is created
    // only for a uid the registry has, and dropped when that sensor is saved or removed, or when
    // the whole registry is replaced.
    std::unordered_map<std::string, bool> errorOverlay;

    /** @brief The error flag the module currently holds for a stored entity. */
    bool liveErrorFlag(const SensorEntity& stored) const {
        if (errorOverlay.empty()) {
            return stored.isError;
        }
        auto it = errorOverlay.find(stored.uid);
        return it != errorOverlay.end() ? it->second : stored.isError;
    }

    /** @brief Put the live error flag on an entity leaving the repository. */
    void applyErrorOverlay(SensorEntity& sensor) const {
        if (errorOverlay.empty()) {
            return;
        }
        auto it = errorOverlay.find(sensor.uid);
        if (it != errorOverlay.end()) {
            sensor.isError = it->second;
        }
    }

    void applyErrorOverlay(std::vector<SensorEntity>& sensors) const {
        if (errorOverlay.empty()) {
            return;
        }
        for (auto& sensor : sensors) {
            applyErrorOverlay(sensor);
        }
    }

    /**
     * @brief Record a runtime error flag without touching storage.
     * @param uid Sensor the flag belongs to
     * @param storedValue The flag as the stored entity carries it
     * @param value The flag the module now holds
     */
    void setErrorOverlay(const std::string& uid, bool storedValue, bool value) {
        if (storedValue == value) {
            errorOverlay.erase(uid);
        } else {
            if (errorOverlay.size() >= loadedEntities().size() &&
                errorOverlay.find(uid) == errorOverlay.end()) {
                pruneErrorOverlay();
            }
            errorOverlay[uid] = value;
        }

        // The cached active view is rebuilt off the storage generation, and an error-flag change does not
        // move it - so the view is corrected here instead of being thrown away, which also keeps
        // a flapping sensor from rebuilding it on every transition.
        if (activeSensorsViewValid) {
            for (auto& sensor : activeSensorsView) {
                if (sensor.uid == uid) {
                    sensor.isError = value;
                    break;
                }
            }
        }
    }

    /** @brief Forget the runtime flag of a sensor whose stored entity has just been written. */
    void dropErrorOverlay(const std::string& uid) {
        if (!errorOverlay.empty()) {
            errorOverlay.erase(uid);
        }
    }

    /**
     * @brief Drop the entries that no longer name a stored sensor.
     *
     * Every removal made through this repository drops its entry directly, so this has something
     * to do only when the registry changed behind the repository's back (a write straight into
     * the DAO, then refreshCache()). It is what holds the map to one entry per stored sensor: it
     * drops the entries whose uid the registry no longer has, and says nothing about the entries
     * whose uid is still there - a sensor that left the registry and came back keeps the flag it
     * left with.
     */
    void pruneErrorOverlay() {
        const std::unordered_map<std::string, SensorEntity>& entities = loadedEntities();
        for (auto it = errorOverlay.begin(); it != errorOverlay.end(); ) {
            if (entities.find(it->first) == entities.end()) {
                it = errorOverlay.erase(it);
            } else {
                ++it;
            }
        }
    }

public:
    ~SensorRepository() noexcept override = default;

    /**
     * @brief Constructor for SensorRepository
     * @param dao Data access object for SensorEntity entities
     * @param debugSystem Debug system for logging (optional)
     * @param logger Logger for output (optional)
     * @param clock What a failed read is retried by (optional; see FAILED_READ_RETRY_MS)
     */
    SensorRepository(ISpiffsSensorDao* dao, IDebugSystem *debugSystem = nullptr, ILogger *logger = nullptr,
                     IArduinoAdapter* clock = nullptr)
        : Repository<SensorEntity>(dao, debugSystem, logger, clock), sensorDao(dao) {
        if (!dao) {
            throw std::invalid_argument("DAO cannot be null");
        }
    }

    // IRepository<SensorEntity> implementations 
    Utils::Optional<SensorEntity> get(const char* uid) override {
        Utils::Optional<SensorEntity> sensor = Repository<SensorEntity>::get(uid);
        if (sensor.has_value()) {
            applyErrorOverlay(sensor.value());
        }
        return sensor;
    }

    std::vector<SensorEntity> getAll() override {
        std::vector<SensorEntity> sensors = Repository<SensorEntity>::getAll();
        applyErrorOverlay(sensors);
        return sensors;
    }

    // A whole entity is being stored, error flag included: that flag is what the sensor is now
    // known to be, so the runtime one has nothing left to say.
    bool save(const SensorEntity& entity) override {
        bool result = Repository<SensorEntity>::save(entity);
        if (result) {
            dropErrorOverlay(entity.uid);
        }
        return result;
    }

    int saveAll(const std::vector<SensorEntity>& entities) override {
        int saved = Repository<SensorEntity>::saveAll(entities);
        if (saved > 0) {
            // saveAll replaces the registry: every uid in it was just written, and every uid that
            // is not in it no longer exists.
            errorOverlay.clear();
        }
        return saved;
    }

    bool remove(const char* uid) override {
        bool result = Repository<SensorEntity>::remove(uid);
        if (result && uid) {
            dropErrorOverlay(uid);
        }
        return result;
    }

    int removeAll() override {
        int removed = Repository<SensorEntity>::removeAll();
        errorOverlay.clear();
        return removed;
    }

    int count() override {
        return Repository<SensorEntity>::count();
    }

    bool exists(const char* uid) override {
        return Repository<SensorEntity>::exists(uid);
    }

    bool clear() override {
        bool result = Repository<SensorEntity>::clear();
        if (result) {
            errorOverlay.clear();
        }
        return result;
    }

    // ISensorRepository implementations
    bool storedSensorsUnread() override {
        return storedEntitiesUnread();
    }

    std::vector<SensorEntity> getSensorsByProtocol(const char* protocol) override {
        if (!protocol) return {};

        std::vector<SensorEntity> allSensors = getAll();
        std::vector<SensorEntity> sensors;

        for (const auto& sensor : allSensors) {
            if (sensor.protocol == protocol) {
                sensors.push_back(sensor);
            }
        }

        return sensors;
    }

    std::vector<SensorEntity> getSensorsByType(const char* sensorType) override {
        if (!sensorType) return {};

        std::vector<SensorEntity> allSensors = getAll();
        std::vector<SensorEntity> sensors;

        for (const auto& sensor : allSensors) {
            if (sensor.sensorType == sensorType) {
                sensors.push_back(sensor);
            }
        }

        return sensors;
    }

    std::vector<SensorEntity> getSensorsByModel(const char* model) override {
        if (!model) return {};

        std::vector<SensorEntity> allSensors = getAll();
        std::vector<SensorEntity> sensors;

        for (const auto& sensor : allSensors) {
            if (sensor.model == model) {
                sensors.push_back(sensor);
            }
        }

        return sensors;
    }

    const std::vector<SensorEntity>& getActiveSensors() override {
        if (!activeSensorsViewValid || activeSensorsGeneration != generation()) {
            activeSensorsView.clear();
            for (const auto& pair : loadedEntities()) {
                if (pair.second.isActive) {
                    activeSensorsView.push_back(pair.second);
                    applyErrorOverlay(activeSensorsView.back());
                }
            }
            // Recorded after the load: loadedEntities() settles the cache first, so the
            // generation stamped here is the one the view was built from.
            activeSensorsGeneration = generation();
            activeSensorsViewValid = true;
        }
        return activeSensorsView;
    }

    std::vector<SensorEntity> getSensorsWithErrors() override {
        std::vector<SensorEntity> allSensors = getAll();
        std::vector<SensorEntity> sensors;

        for (const auto& sensor : allSensors) {
            if (sensor.isError) {
                sensors.push_back(sensor);
            }
        }

        return sensors;
    }

    std::vector<SensorEntity> getModbusSensors() override {
        return getSensorsByProtocol("ModBus");
    }

    std::vector<SensorEntity> getI2CSensors() override {
        return getSensorsByProtocol("I2C");
    }

    std::vector<SensorEntity> getAtlasSensors() override {
        std::vector<SensorEntity> allSensors = getAll();
        std::vector<SensorEntity> atlasSensors;

        for (const auto& sensor : allSensors) {
            if (sensor.isAtlasSensor()) {
                atlasSensors.push_back(sensor);
            }
        }

        return atlasSensors;
    }

    std::vector<SensorEntity> getMultiChannelSensors() override {
        std::vector<SensorEntity> allSensors = getAll();
        std::vector<SensorEntity> multiChannelSensors;

        for (const auto& sensor : allSensors) {
            if (sensor.isMultiChannel()) {
                multiChannelSensors.push_back(sensor);
            }
        }

        return multiChannelSensors;
    }

    bool updateSensorStatus(const char* uid, bool isActive, bool isError) override {
        if (!uid) return false;

        const std::unordered_map<std::string, SensorEntity>& entities = loadedEntities();
        auto it = entities.find(uid);
        if (it == entities.end()) {
            return false;
        }

        const SensorEntity& stored = it->second;

        if (stored.isActive == isActive && liveErrorFlag(stored) == isError) {
            return true;
        }

        if (stored.isActive == isActive) {
            // Nothing but the runtime health of the sensor moved. It is recorded in RAM: storing
            // it would serialize and rewrite the whole registry, and drop the cache the control
            // loop reads on every iteration, for a value that is re-established by the next bus
            // sweep anyway.
            setErrorOverlay(uid, stored.isError, isError);
            return true;
        }

        // isActive is configuration and belongs in storage. It is written with the error flag the
        // module currently holds, which is what the file carried before this method could leave
        // one behind in RAM.
        SensorEntity sensor = stored;
        sensor.isActive = isActive;
        sensor.isError = isError;

        return save(sensor);
    }

    bool createOrUpdateSensor(const SensorEntity& sensor) override {
        return save(sensor);
    }

    bool updateSensorStatusEnum(const char* uid, SensorStatusEnum status) override {
        if (!uid) return false;

        auto sensorOpt = get(uid);
        if (!sensorOpt.has_value()) {
            return false;
        }

        SensorEntity sensor = sensorOpt.value();
        
        if (sensor.status == status) {
            return true;
        }
        
        sensor.status = status;
        
        return save(sensor);
    }

    std::vector<SensorEntity> getSensorsByStatus(SensorStatusEnum status) override {
        std::vector<SensorEntity> allSensors = getAll();
        std::vector<SensorEntity> filteredSensors;

        for (const auto& sensor : allSensors) {
            if (sensor.status == status) {
                filteredSensors.push_back(sensor);
            }
        }

        return filteredSensors;
    }
};
