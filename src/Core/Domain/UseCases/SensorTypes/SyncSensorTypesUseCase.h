#ifndef SYNC_SENSOR_TYPES_USE_CASE_H
#define SYNC_SENSOR_TYPES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/UseCases/IRepublishModuleStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IVirtualSensorChannelsUseCase.h"
#include "Core/Domain/Enums/StorageWriteErrorEnum.h"
#include <cstdarg>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * Use case for synchronizing sensor types with the server
 */
class SyncSensorTypesUseCase : public ISyncSensorTypesUseCase {
private:
    IHybridAPIService* _apiService;
    ISensorTypeRepository* _sensorTypeRepository;
    ISensorRepository* _sensorRepository;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IArduinoAdapter* _arduinoAdapter;
    INTPTimeAdapter* _ntpTimeAdapter;
    IJsonConverter* _jsonConverter;
    IModuleConfig* _moduleConfig;

    // Sync timing - longer interval for sensor types as they change less frequently
    const int SYNC_INTERVAL_MS = 300000; // 5 minutes
    unsigned long _lastSyncTime;
    bool _lastSyncStatus = false;
    time_t _lastSuccessfulSyncTimestamp = 0;
    bool _forceSyncRequested = false;

    // Injected (PR #17 review): which computed channels a sensor exposes is not knowledge of the
    // sync, and neither is how the structure is republished when the catalog moved one of them
    IVirtualSensorChannelsUseCase* _virtualChannels = nullptr;
    IRepublishModuleStructureUseCase* _republishModuleStructure = nullptr;

    void log(const char* format, ...) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::SENSOR_DATA_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            IDebugFeature* feature = _debugSystem->getFeature(DebugFeatures::SENSOR_DATA_DEBUG);
            if (feature)
            {
                _logger->println(DebugFeatures::SENSOR_DATA_DEBUG, buffer, feature->getLogChannel());
            }
            else
            {
                _logger->println(DebugFeatures::SENSOR_DATA_DEBUG, buffer);
            }
        }
    }

    bool moduleTypeHasLocalSensors() const {
        return !_moduleConfig || _moduleConfig->getSensorPinCount() > 0;
    }

public:
    SyncSensorTypesUseCase(
        IHybridAPIService* apiService,
        ISensorTypeRepository* sensorTypeRepository,
        IArduinoAdapter* arduinoAdapter,
        INTPTimeAdapter* ntpTimeAdapter,
        IJsonConverter* jsonConverter = nullptr,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        ISensorRepository* sensorRepository = nullptr,
        IModuleConfig* moduleConfig = nullptr
    ) : _apiService(apiService),
        _sensorTypeRepository(sensorTypeRepository),
        _sensorRepository(sensorRepository),
        _logger(logger),
        _debugSystem(debugSystem),
        _arduinoAdapter(arduinoAdapter),
        _ntpTimeAdapter(ntpTimeAdapter),
        _jsonConverter(jsonConverter),
        _moduleConfig(moduleConfig),
        _lastSyncTime(0) {
        
        if (_arduinoAdapter) {
            _lastSyncTime = _arduinoAdapter->millis() - SYNC_INTERVAL_MS;
        }
    }
    
    ~SyncSensorTypesUseCase() override = default;

    void setVirtualSensorChannelsUseCase(IVirtualSensorChannelsUseCase* virtualChannels) {
        _virtualChannels = virtualChannels;
    }

    void setRepublishModuleStructureUseCase(IRepublishModuleStructureUseCase* republishModuleStructure) {
        _republishModuleStructure = republishModuleStructure;
    }

    bool init() override {
        log("SYNC_SENSOR_TYPES: Initializing sensor types sync use case");
        
        if (!_apiService) {
            log("SYNC_SENSOR_TYPES: API service not available");
            return false;
        }
        
        if (!_sensorTypeRepository) {
            log("SYNC_SENSOR_TYPES: Sensor type repository not available");
            return false;
        }
        
        if (!_arduinoAdapter) {
            log("SYNC_SENSOR_TYPES: Arduino adapter not available");
            return false;
        }
        
        if (!_ntpTimeAdapter) {
            log("SYNC_SENSOR_TYPES: NTP time adapter not available");
            return false;
        }
        
        log("SYNC_SENSOR_TYPES: Sensor types sync use case initialized successfully");
        return true;
    }
    
    void loop() override {
        if (!_arduinoAdapter) {
            return;
        }

        if (!moduleTypeHasLocalSensors()) {
            _forceSyncRequested = false;
            log("SYNC_SENSOR_TYPES: Module type %d has no local sensor pins, skipping catalog sync",
                _moduleConfig->getModuleType());
            return;
        }

        unsigned long now = _arduinoAdapter->millis();
        unsigned long elapsed = now - _lastSyncTime;
        
        bool shouldSync = _forceSyncRequested || (elapsed >= SYNC_INTERVAL_MS);
        
        if (!shouldSync) {
            return;
        }
        
        _forceSyncRequested = false;
        _lastSyncTime = now;
        
        log("SYNC_SENSOR_TYPES: Starting sensor types synchronization");
        
        bool syncSuccess = syncSensorTypesFromServer();
        _lastSyncStatus = syncSuccess;
        
        if (syncSuccess) {
            if (_ntpTimeAdapter) {
                _lastSuccessfulSyncTimestamp = _ntpTimeAdapter->getEpochTime();
            }
            log("SYNC_SENSOR_TYPES: Sensor types synchronization completed successfully");
        } else {
            log("SYNC_SENSOR_TYPES: Sensor types synchronization failed");
        }
    }
    
    bool forceSync() override {
        if (!moduleTypeHasLocalSensors()) {
            log("SYNC_SENSOR_TYPES: Module type %d has no local sensor pins, skipping catalog sync",
                _moduleConfig->getModuleType());
            return true;
        }

        log("SYNC_SENSOR_TYPES: Force sync requested");
        _forceSyncRequested = true;

        //NOTE:  debug - immediate sync on force request
        syncSensorTypesFromServer();

        return true;
    }

    bool getLastSyncStatus() const override {
        return _lastSyncStatus;
    }
    
    time_t getLastSyncTime() const override {
        return _lastSuccessfulSyncTimestamp;
    }

private:
    bool syncSensorTypesFromServer() {
        if (!_apiService || !_sensorTypeRepository) {
            log("SYNC_SENSOR_TYPES: API service or repository not available");
            return false;
        }
        
        log("SYNC_SENSOR_TYPES: Fetching sensor types from server");
        std::vector<SensorType> serverSensorTypes = _apiService->getSensorTypesList();
        
        if (serverSensorTypes.empty()) {
            log("SYNC_SENSOR_TYPES: No sensor types received from server, checking local storage");
            return handleEmptyServerResponse();
        }
        
        log("SYNC_SENSOR_TYPES: Received %d sensor types from server", static_cast<int>(serverSensorTypes.size()));
        
        // Log sensor type names for debugging
        for (size_t i = 0; i < serverSensorTypes.size() && i < serverSensorTypes.size(); i++) {
            log("SYNC_SENSOR_TYPES: - Sensor type %d: %s (UID: %s)", 
                i + 1, serverSensorTypes[i].name.c_str(), serverSensorTypes[i].uid.c_str());
        }
        
        return processServerSensorTypes(serverSensorTypes);
    }
    
    bool processServerSensorTypes(const std::vector<SensorType>& serverSensorTypes) {
        // Validate incoming sensor types
        std::vector<SensorType> validSensorTypes;
        
        for (const auto& sensorType : serverSensorTypes) {
            if (validateSensorType(sensorType)) {
                validSensorTypes.push_back(sensorType);
            } else {
                log("SYNC_SENSOR_TYPES: Sensor type %s validation failed", sensorType.uid.c_str());
            }
        }
        
        // Store valid sensor types in repository
        bool syncSuccess = false;
        if (!validSensorTypes.empty()) {
            int syncedCount = synchronizeSensorTypes(validSensorTypes, serverSensorTypes);
            syncSuccess = (syncedCount > 0);
            log("SYNC_SENSOR_TYPES: Synchronized %d/%d sensor types successfully", syncedCount, static_cast<int>(validSensorTypes.size()));
        }
        
        trackSyncAttempt(syncSuccess);
        return syncSuccess;
    }
    
    bool validateSensorType(const SensorType& sensorType) {
        // Basic validation
        if (sensorType.uid.empty()) {
            log("SYNC_SENSOR_TYPES: Sensor type missing UID");
            return false;
        }
        
        if (sensorType.name.empty()) {
            log("SYNC_SENSOR_TYPES: Sensor type %s missing name", sensorType.uid.c_str());
            return false;
        }
        
        // Additional validation could be added here
        return true;
    }
    
    /**
     * Merge the server catalog into the local one, prune what the server no longer publishes and
     * write the result in a single bulk write.
     *
     * @param publishedSensorTypes The raw server response, not the validated subset: a type that
     *        failed validateSensorType() is not a retired type and must not be pruned.
     */
    int synchronizeSensorTypes(const std::vector<SensorType>& serverSensorTypes,
                               const std::vector<SensorType>& publishedSensorTypes) {
        if (serverSensorTypes.empty() || publishedSensorTypes.empty()) {
            log("SYNC_SENSOR_TYPES: Empty server sensor type list, catalog left untouched");
            return 0;
        }

        // Load existing sensor types once into an in-memory map
        std::vector<SensorType> toSave = _sensorTypeRepository->getAll();
        std::unordered_map<std::string, size_t> toSaveIndex;
        for (size_t i = 0; i < toSave.size(); i++) {
            toSaveIndex[toSave[i].uid] = i;
        }

        int syncedCount = 0;
        bool changed = false;
        std::vector<SensorType> changedSensorTypes;

        for (const auto& serverSensorType : serverSensorTypes) {
            try {
                auto it = toSaveIndex.find(serverSensorType.uid);
                if (it != toSaveIndex.end()) {
                    if (shouldUpdateSensorType(toSave[it->second], serverSensorType)) {
                        toSave[it->second] = serverSensorType;
                        changed = true;
                        syncedCount++;
                        changedSensorTypes.push_back(serverSensorType);
                        log("SYNC_SENSOR_TYPES: Marked sensor type %s for update", serverSensorType.uid.c_str());
                    } else {
                        log("SYNC_SENSOR_TYPES: Sensor type %s already up to date", serverSensorType.uid.c_str());
                    }
                } else {
                    toSaveIndex[serverSensorType.uid] = toSave.size();
                    toSave.push_back(serverSensorType);
                    changed = true;
                    syncedCount++;
                    changedSensorTypes.push_back(serverSensorType);
                    log("SYNC_SENSOR_TYPES: Marked sensor type %s for addition", serverSensorType.uid.c_str());
                }
            } catch (const std::exception& e) {
                log("SYNC_SENSOR_TYPES: Exception synchronizing sensor type %s: %s", serverSensorType.uid.c_str(), e.what());
            }
        }

        int removedCount = countAndCompactRetiredSensorTypes(toSave, toSaveIndex, publishedSensorTypes);
        if (removedCount > 0) {
            changed = true;
        }

        if (!changed) {
            log("SYNC_SENSOR_TYPES: No changes detected, skipping save");
            return 0;
        }

        // Single bulk write — 1 SPIFFS read (getAll above) + 1 SPIFFS write
        int saved = _sensorTypeRepository->saveAll(toSave);
        if (saved != static_cast<int>(toSave.size())) {
            log("SYNC_SENSOR_TYPES: saveAll wrote %d of %d sensor types, removed=%d, reason=%s, bytes=%d, freeHeap=%d, maxAllocHeap=%d",
                saved, static_cast<int>(toSave.size()), removedCount,
                getStorageWriteErrorName(_sensorTypeRepository->getLastSaveError()),
                static_cast<int>(_sensorTypeRepository->getLastSaveBytes()),
                _arduinoAdapter ? static_cast<int>(_arduinoAdapter->getFreeHeap()) : -1,
                _arduinoAdapter ? static_cast<int>(_arduinoAdapter->getMaxAllocHeap()) : -1);
            return 0;
        }

        log("SYNC_SENSOR_TYPES: Bulk saved %d sensor types to storage, removed=%d, bytes=%d, freeHeap=%d, maxAllocHeap=%d",
            saved, removedCount, static_cast<int>(_sensorTypeRepository->getLastSaveBytes()),
            _arduinoAdapter ? static_cast<int>(_arduinoAdapter->getFreeHeap()) : -1,
            _arduinoAdapter ? static_cast<int>(_arduinoAdapter->getMaxAllocHeap()) : -1);

        // Update existing sensors with data from changed/added sensor types
        if (_sensorRepository && !changedSensorTypes.empty()) {
            updateSensorsForChangedSensorTypes(changedSensorTypes);
        }

        return syncedCount + removedCount;
    }

    /**
     * Drop from `toSave` every type whose uid is absent from `publishedSensorTypes`, in place.
     * `toSaveIndex` is left cleared when anything was dropped: the compaction invalidates it.
     *
     * @return how many types were dropped
     */
    int countAndCompactRetiredSensorTypes(std::vector<SensorType>& toSave,
                                          std::unordered_map<std::string, size_t>& toSaveIndex,
                                          const std::vector<SensorType>& publishedSensorTypes) {
        std::vector<bool> stillPublished(toSave.size(), false);
        for (const auto& publishedSensorType : publishedSensorTypes) {
            if (publishedSensorType.uid.empty()) {
                continue;
            }
            auto it = toSaveIndex.find(publishedSensorType.uid);
            if (it != toSaveIndex.end()) {
                stillPublished[it->second] = true;
            }
        }

        int removedCount = 0;
        size_t kept = 0;
        for (size_t i = 0; i < toSave.size(); i++) {
            if (stillPublished[i]) {
                if (kept != i) {
                    toSave[kept] = std::move(toSave[i]);
                }
                kept++;
            } else {
                removedCount++;
                log("SYNC_SENSOR_TYPES: Marked sensor type %s for removal, no longer published by server",
                    toSave[i].uid.c_str());
            }
        }

        if (removedCount > 0) {
            toSave.resize(kept);
            toSaveIndex.clear();
        }

        return removedCount;
    }

    void updateSensorsForChangedSensorTypes(const std::vector<SensorType>& changedSensorTypes) {
        // Build a lookup map from sensor type UID to sensor type
        std::unordered_map<std::string, const SensorType*> changedMap;
        for (const auto& st : changedSensorTypes) {
            changedMap[st.uid] = &st;
        }

        std::vector<SensorEntity> allSensors = _sensorRepository->getAll();
        int updatedCount = 0;
        bool structureDirty = false;

        for (auto& sensor : allSensors) {
            if (sensor.sensorTypeUid.empty()) continue;

            auto it = changedMap.find(sensor.sensorTypeUid);
            if (it == changedMap.end()) continue;

            const SensorType* sensorType = it->second;
            bool wasUpdated = false;

            std::string virtualChannelsBefore = describeVirtualChannels(sensor);

            // Update Modbus register configs and baud rate
            if (sensor.isModbusSensor() && !sensorType->modbusRegisterConfig.empty()) {
                sensor.modbusRegisterConfigs = sensorType->modbusRegisterConfig;
                sensor.baudRate = sensorType->baudRate;
                wasUpdated = true;
            }

            std::string virtualChannelsAfter = describeVirtualChannels(sensor);

            if (virtualChannelsBefore != virtualChannelsAfter) {
                structureDirty = true;
                log("SYNC_SENSOR_TYPES: VIRTUAL_CHANNEL_SHIFT uid=%s old=%s new=%s",
                    sensor.uid.c_str(),
                    virtualChannelsBefore.c_str(),
                    virtualChannelsAfter.c_str());
            }

            // Update I2C register configs
            if (sensor.isI2CSensor() && !sensorType->i2cRegisterConfig.empty()) {
                sensor.i2cRegisterConfigs = sensorType->i2cRegisterConfig;
                wasUpdated = true;
            }

            if (wasUpdated) {
                if (_sensorRepository->save(sensor)) {
                    updatedCount++;
                    log("SYNC_SENSOR_TYPES: Updated sensor %s registers from sensor type %s",
                        sensor.uid.c_str(), sensorType->uid.c_str());
                } else {
                    log("SYNC_SENSOR_TYPES: Failed to update sensor %s", sensor.uid.c_str());
                }
            }
        }

        log("SYNC_SENSOR_TYPES: Updated %d sensors from changed sensor types", updatedCount);

        // One call per synchronization, not per sensor
        if (structureDirty && _republishModuleStructure) {
            _republishModuleStructure->execute("virtual_channel_shift");
        }
    }

    // Computed channels of a sensor as one comparable string ("none" when it exposes none): the
    // sync only has to notice that the layout moved, not what the channels mean
    std::string describeVirtualChannels(const SensorEntity& sensor) {
        if (!_virtualChannels) {
            return "none";
        }

        std::string description;
        for (const auto& channel : _virtualChannels->channels(sensor)) {
            if (!description.empty()) {
                description += ",";
            }
            description += channel.type + ":" + channel.uid;
        }

        return description.empty() ? "none" : description;
    }
    
    bool shouldUpdateSensorType(const SensorType& existingSensorType, const SensorType& newSensorType) {
        // If updatedAt is empty in either existing or new sensor type, sync is required
        if (existingSensorType.updatedAt.empty() || newSensorType.updatedAt.empty()) {
            log("SYNC_SENSOR_TYPES: Sensor type %s has empty updatedAt (existing: %s, new: %s) - forcing update", 
                newSensorType.uid.c_str(),
                existingSensorType.updatedAt.empty() ? "empty" : existingSensorType.updatedAt.c_str(),
                newSensorType.updatedAt.empty() ? "empty" : newSensorType.updatedAt.c_str());
            return true;
        }
        
        // Check if main sensor type updatedAt has changed
        if (existingSensorType.updatedAt != newSensorType.updatedAt) {
            log("SYNC_SENSOR_TYPES: Sensor type %s updatedAt changed: %s -> %s", 
                newSensorType.uid.c_str(),
                existingSensorType.updatedAt.c_str(),
                newSensorType.updatedAt.c_str());
            return true;
        }
        
        // Compare sub sensor types - check if any timestamp has changed
        if (existingSensorType.sensorTypes.size() != newSensorType.sensorTypes.size()) {
            return true;
        }
        
        for (size_t i = 0; i < existingSensorType.sensorTypes.size(); ++i) {
            const auto& existingSubSensor = existingSensorType.sensorTypes[i];
            const auto& newSubSensor = newSensorType.sensorTypes[i];
            
            if (existingSubSensor.createdAt != newSubSensor.createdAt ||
                existingSubSensor.updatedAt != newSubSensor.updatedAt) {
                return true;
            }
        }
        
        return false;
    }
    
    bool handleEmptyServerResponse() {
        std::vector<SensorType> localSensorTypes = _sensorTypeRepository->getAll();
        
        if (!localSensorTypes.empty()) {
            log("SYNC_SENSOR_TYPES: Using %d sensor types from local storage", static_cast<int>(localSensorTypes.size()));
            return true;
        }
        
        log("SYNC_SENSOR_TYPES: No sensor types available locally or from server");
        return true;
    }
    
    void trackSyncAttempt(bool success) {
        if (success) {
            if (_ntpTimeAdapter) {
                _lastSuccessfulSyncTimestamp = _ntpTimeAdapter->getEpochTime();
            }
        }
        _lastSyncStatus = success;
    }
};

#endif // SYNC_SENSOR_TYPES_USE_CASE_H