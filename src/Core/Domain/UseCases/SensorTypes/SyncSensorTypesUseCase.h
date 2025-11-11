#ifndef SYNC_SENSOR_TYPES_USE_CASE_H
#define SYNC_SENSOR_TYPES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"

/**
 * Use case for synchronizing sensor types with the server
 */
class SyncSensorTypesUseCase : public ISyncSensorTypesUseCase {
private:
    IHybridAPIService* _apiService;
    ISensorTypeRepository* _sensorTypeRepository;
    ILogger* _logger;
    IDebugSystem* _debugSystem;
    IArduinoAdapter* _arduinoAdapter;
    INTPTimeAdapter* _ntpTimeAdapter;
    IJsonConverter* _jsonConverter;
    
    // Sync timing - longer interval for sensor types as they change less frequently
    const int SYNC_INTERVAL_MS = 300000; // 5 minutes
    unsigned long _lastSyncTime;
    bool _lastSyncStatus = false;
    time_t _lastSuccessfulSyncTimestamp = 0;
    bool _forceSyncRequested = false;
    
    void log(const char* format, ...) {
        if (_logger && _debugSystem && _debugSystem->isFeatureEnabled(DebugFeatures::SENSOR_DATA_DEBUG)) {
            char buffer[512];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            _logger->println(DebugFeatures::SENSOR_DATA_DEBUG, buffer);
        }
    }

public:
    SyncSensorTypesUseCase(
        IHybridAPIService* apiService,
        ISensorTypeRepository* sensorTypeRepository,
        IArduinoAdapter* arduinoAdapter,
        INTPTimeAdapter* ntpTimeAdapter,
        IJsonConverter* jsonConverter = nullptr,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr
    ) : _apiService(apiService),
        _sensorTypeRepository(sensorTypeRepository),
        _logger(logger),
        _debugSystem(debugSystem),
        _arduinoAdapter(arduinoAdapter),
        _ntpTimeAdapter(ntpTimeAdapter),
        _jsonConverter(jsonConverter),
        _lastSyncTime(0) {
        
        if (_arduinoAdapter) {
            _lastSyncTime = _arduinoAdapter->millis() - SYNC_INTERVAL_MS;
        }
    }
    
    ~SyncSensorTypesUseCase() override = default;
    
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
            return false;
        }
        
        log("SYNC_SENSOR_TYPES: Fetching sensor types from server");
        std::vector<SensorType> serverSensorTypes = _apiService->getSensorTypesList();
        
        if (serverSensorTypes.empty()) {
            log("SYNC_SENSOR_TYPES: No sensor types received from server, checking local storage");
            return handleEmptyServerResponse();
        }
        
        log("SYNC_SENSOR_TYPES: Received %d sensor types from server", static_cast<int>(serverSensorTypes.size()));
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
            int syncedCount = synchronizeSensorTypes(validSensorTypes);
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
    
    int synchronizeSensorTypes(const std::vector<SensorType>& serverSensorTypes) {
        int syncedCount = 0;
        
        for (const auto& serverSensorType : serverSensorTypes) {
            try {
                // Check if sensor type already exists
                auto existingSensorType = _sensorTypeRepository->get(serverSensorType.uid.c_str());
                
                if (existingSensorType.has_value()) {
                    // Update existing sensor type if different
                    if (shouldUpdateSensorType(existingSensorType.value(), serverSensorType)) {
                        if (_sensorTypeRepository->save(serverSensorType)) {
                            syncedCount++;
                            log("SYNC_SENSOR_TYPES: Updated sensor type %s", serverSensorType.uid.c_str());
                        } else {
                            log("SYNC_SENSOR_TYPES: Failed to update sensor type %s", serverSensorType.uid.c_str());
                        }
                    } else {
                        syncedCount++; // Already up to date
                        log("SYNC_SENSOR_TYPES: Sensor type %s already up to date", serverSensorType.uid.c_str());
                    }
                } else {
                    // Add new sensor type
                    if (_sensorTypeRepository->save(serverSensorType)) {
                        syncedCount++;
                        log("SYNC_SENSOR_TYPES: Added new sensor type %s", serverSensorType.uid.c_str());
                    } else {
                        log("SYNC_SENSOR_TYPES: Failed to add sensor type %s", serverSensorType.uid.c_str());
                    }
                }
            } catch (const std::exception& e) {
                log("SYNC_SENSOR_TYPES: Exception synchronizing sensor type %s: %s", serverSensorType.uid.c_str(), e.what());
            }
        }
        
        return syncedCount;
    }
    
    bool shouldUpdateSensorType(const SensorType& existingSensorType, const SensorType& newSensorType) {
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