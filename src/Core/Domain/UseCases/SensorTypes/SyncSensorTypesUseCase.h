#ifndef SYNC_SENSOR_TYPES_USE_CASE_H
#define SYNC_SENSOR_TYPES_USE_CASE_H

#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include "Application/Interfaces/ISensorTypeCatalogWriter.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/Repositories/ISensorTypeRepository.h"
#include "Core/Domain/Interfaces/Repositories/ISensorRepository.h"
#include "Core/Domain/Data/Entities/Sensor/SensorTypeContent.h"
#include "Core/Domain/Data/Models/ISensorValueModel.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/INetworkStatusProvider.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/UseCases/IRepublishModuleStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IVirtualSensorChannelsUseCase.h"
#include "Core/Domain/Enums/StorageWriteErrorEnum.h"
#include <cstdarg>
#include <string>
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

    /**
     * The shortest gap between two re-arms of a synchronization that did not succeed.
     *
     * The same five minutes OnConnectivityRestoredUseCase throttles its own forced rule sync
     * with, and for the same reason: a link that flaps produces one offline->online edge per
     * flap, and without a floor under the rate a long outage would be answered with a request
     * per flap for as long as it lasts.
     */
    static const unsigned long REARM_COOLDOWN_MS = 5UL * 60 * 1000;

    /**
     * How long a synchronization that did not succeed waits before asking again on its own.
     *
     * The connectivity edge below is the fast path and covers the common case - the server was
     * unreachable while the network was still coming up, and the module notices the moment its
     * own offline decision clears. This is the net under it, for an outage the module never
     * registered as one: a REST endpoint that answers errors while MQTT stays connected leaves
     * isNetworkOffline() false throughout, and then nothing would ever ask again.
     *
     * Hours rather than minutes, deliberately. The catalog is the list of sensor models an
     * account owns; it is edited a few times a year, the module keeps working from the copy it
     * has, and nothing a grower can see depends on picking a change up within the hour. Six
     * hours is four requests a day from a module that cannot reach the catalog at all - the
     * cost of the pathological case, which is what an interval like this has to be priced on.
     */
    static const unsigned long REARM_SAFETY_INTERVAL_MS = 6UL * 60 * 60 * 1000;

    /** Attempts syncNow() makes; the transport itself makes one attempt per call. */
    static const uint8_t SYNC_NOW_MAX_ATTEMPTS = 3;
    /** When the module last went to the server for the catalog, however that fetch was asked for. */
    unsigned long _lastSyncTime;
    bool _lastSyncStatus = false;
    time_t _lastSuccessfulSyncTimestamp = 0;
    bool _forceSyncRequested = false;

    // Re-arming a synchronization that did not succeed.
    //
    // Everything else that asks for the catalog asks once, at a moment of its own - setup, and a
    // change of local IP. Neither comes back, so without this a module that failed its fetch
    // because the server was unreachable while the network was still coming up holds the catalog
    // it had stored until the next reboot. `_syncNeedsRearm` is raised by a synchronization that
    // finished without succeeding and cleared by one that succeeded, so a module with a current
    // catalog asks for nothing and costs nothing here.
    INetworkStatusProvider* _networkStatus = nullptr;
    bool _networkWasOffline = false;
    bool _syncNeedsRearm = false;
    unsigned long _lastRearmTime;
    /** The module-type skip is reported once, not on every control-loop iteration. */
    bool _noLocalSensorsReported = false;

    // The queue the catalog fetch travels through, and the ticket of the one fetch in flight.
    IOutbox* _outbox = nullptr;
    uint32_t _requestTicket = IOutbox::kRejected;
    uint32_t _requestCount = 0;

    // The task that stores the catalog, and what this use case still owes the write in flight.
    // Without a writer the catalog is stored by the calling task, as it always was.
    ISensorTypeCatalogWriter* _catalogWriter = nullptr;
    uint32_t _writeTicket = ISensorTypeCatalogWriter::kRejected;
    uint32_t _writeCount = 0;
    /**
     * The uids one merge decided to change, and how many types of the module's own it matched.
     *
     * Members rather than locals because the merge answers with the catalog itself; both are set by
     * mergeLocalCatalogIntoAnswer() and read by the step that stores it, and both are cleared at
     * the start of every merge.
     */
    std::vector<std::string> _changedUids;
    int _localMatchCount = 0;
    /** What synchronizeSensorTypes() will return once that write lands. */
    int _syncedCountAwaitingWrite = 0;
    /** How many retired types that write drops, for the line that reports how it ended. */
    int _removedCountAwaitingWrite = 0;

    /** synchronizeSensorTypes(): the catalog is on its way to storage, nothing has landed yet. */
    static const int WRITE_PENDING = -1;

    /**
     * synchronizeSensorTypes(): the merged catalog never reached storage.
     *
     * Told apart from a plain zero on purpose: zero is "the server sent the catalog the module
     * already has", which is an exchange that completed with nothing to do. Collapsing the two
     * into one value makes a module in its ordinary state - a catalog nobody has edited -
     * indistinguishable from one whose flash refused the write.
     */
    static const int WRITE_REFUSED = -2;

    // Injected (PR #17 review): which computed channels a sensor exposes is not knowledge of the
    // sync, and neither is how the structure is republished when the catalog moved one of them
    IVirtualSensorChannelsUseCase* _virtualChannels = nullptr;
    IRepublishModuleStructureUseCase* _republishModuleStructure = nullptr;

    // The readings the module holds, kept under the uid of the sensor they were read from.
    ISensorValueModel* _sensorValues = nullptr;

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
        _lastSyncTime(0),
        _lastRearmTime(0) {

        if (_arduinoAdapter) {
            _lastSyncTime = _arduinoAdapter->millis();
            // Aged by a whole cooldown, so that the first re-arm is not throttled: the module the
            // re-arm exists for has just failed its only fetch, and making it wait five minutes
            // for the connectivity edge it is about to see would be the cooldown protecting the
            // server from a single request.
            _lastRearmTime = _arduinoAdapter->millis() - REARM_COOLDOWN_MS;
        }
    }
    
    ~SyncSensorTypesUseCase() override = default;

    void setVirtualSensorChannelsUseCase(IVirtualSensorChannelsUseCase* virtualChannels) {
        _virtualChannels = virtualChannels;
    }

    void setRepublishModuleStructureUseCase(IRepublishModuleStructureUseCase* republishModuleStructure) {
        _republishModuleStructure = republishModuleStructure;
    }

    /**
     * The readings the module holds, so that a sensor the catalog gave other registers is not
     * reported with what the old ones read - see dropReadingsOfOtherChannels(). Without it the
     * readings are left as they are.
     */
    void setSensorValueModel(ISensorValueModel* sensorValues) {
        _sensorValues = sensorValues;
    }

    /**
     * Fetch the catalog through the outbox instead of blocking the caller on a GET.
     *
     * The same shape rule sync uses: the request goes into the queue of requests, the answer comes
     * back on the completion queue with the types left in the outbox's response slot, and
     * onOutboxCompletion() applies them on the control loop. Without an outbox this use case keeps
     * its previous synchronous behaviour.
     */
    void setOutbox(IOutbox* outbox) {
        _outbox = outbox;
    }

    /**
     * Store the catalog on the writer's task instead of on the caller's.
     *
     * Only the applying half moves. What to store is still decided here, on the control loop, out
     * of repositories only the loop touches; the writer receives a finished snapshot and answers
     * with onCatalogWriteCompletion(), and the consequences of a stored catalog - the sensors brought
     * in line with their types, the structure republish, the reported sync status - happen there, on
     * the loop, after the file is on flash and not before.
     *
     * Without a writer this use case stores the catalog itself.
     * syncNow() does that in any case: it is called before the control loop exists and its caller
     * reads the repository back on the next line.
     */
    void setCatalogWriter(ISensorTypeCatalogWriter* catalogWriter) {
        _catalogWriter = catalogWriter;
    }

    /**
     * The module's own offline decision, watched for the moment it clears.
     *
     * The seam is the same one OnConnectivityRestoredUseCase uses to re-arm the rule sync -
     * LinkSupervisor's routerDown || (!mqttConnected && httpDown) - and it is read here rather
     * than routed through that use case because the two answer to different gates: rules are for
     * rule-capable module types, the catalog is for module types that have local sensor pins, and
     * a sensor module is on the wrong side of the first gate and the right side of the second.
     *
     * Without a provider the connectivity edge simply never fires and the safety-net interval is
     * the only thing that re-arms.
     */
    void setNetworkStatusProvider(INetworkStatusProvider* networkStatus) {
        _networkStatus = networkStatus;
    }

    /** @brief Catalog fetches this use case has handed to the outbox. */
    uint32_t requestCount() const { return _requestCount; }

    /** @brief True while a synchronization that did not succeed is waiting to be asked again. */
    bool isRearmPending() const { return _syncNeedsRearm; }

    /** @brief Catalog writes this use case has handed to the writer task. */
    uint32_t writeCount() const { return _writeCount; }

    /** @brief True while a catalog is on its way to storage. */
    bool isWritePending() const override { return _writeTicket != ISensorTypeCatalogWriter::kRejected; }

    bool isSyncInFlight() const override
    {
        return _forceSyncRequested || _requestTicket != IOutbox::kRejected || isWritePending();
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
    
    /**
     * Called from the control loop. Asks for the catalog again when the last attempt to get it
     * did not succeed, and does nothing at all otherwise.
     *
     * Not a periodic poll: a module that holds the catalog the server sent asks for nothing here,
     * whatever the interval, because a fleet-wide poll would put a cost on every module and every
     * server to fix what only the modules that failed have. What is re-armed is the failure, on
     * two triggers - the moment the module's own offline decision clears, and a slow interval
     * under it for the outages that decision never saw.
     */
    void loop() override {
        if (!_arduinoAdapter) {
            return;
        }

        if (!moduleTypeHasLocalSensors()) {
            _forceSyncRequested = false;
            if (!_noLocalSensorsReported) {
                _noLocalSensorsReported = true;
                log("SYNC_SENSOR_TYPES: Module type %d has no local sensor pins, skipping catalog sync",
                    _moduleConfig->getModuleType());
            }
            return;
        }

        const unsigned long now = _arduinoAdapter->millis();
        // Read on every iteration and not only while something is armed: an edge missed while
        // the module had nothing to re-arm is an edge that never comes back.
        const bool connectivityRestored = observeConnectivity();

        const bool rearm = !_forceSyncRequested && shouldRearm(now, connectivityRestored);

        if (!_forceSyncRequested && !rearm) {
            return;
        }

        if (rearm) {
            // Consumed here rather than when the request is accepted: at most one re-arm per
            // cooldown, whatever happens to it afterwards.
            _lastRearmTime = now;
            log("SYNC_SENSOR_TYPES: Re-arming the catalog fetch (%s)",
                connectivityRestored ? "connectivity restored" : "safety-net interval");
        }

        if (_outbox != nullptr) {
            requestThroughOutbox(now);
            return;
        }

        _forceSyncRequested = false;
        _lastSyncTime = now;

        log("SYNC_SENSOR_TYPES: Starting sensor types synchronization");

        const bool syncSuccess = syncSensorTypesFromServer();
        trackSyncAttempt(syncSuccess);

        if (syncSuccess) {
            log("SYNC_SENSOR_TYPES: Sensor types synchronization completed successfully");
        } else {
            log("SYNC_SENSOR_TYPES: Sensor types synchronization failed");
        }
    }

    void onOutboxCompletion(const OutboxCompletion& completion) override {
        if (completion.kind != OutboxKind::SENSOR_TYPES_FETCH || _outbox == nullptr) {
            return;
        }

        if (_requestTicket == IOutbox::kRejected || completion.ticket != _requestTicket) {
            return;
        }

        _requestTicket = IOutbox::kRejected;

        if (!completion.delivered) {
            // Nothing is applied and nothing is written: a fetch that failed is not a server
            // saying "no sensor models", and the two must never be conflated. Reported as a failed
            // sync so that getLastSyncStatus() does not claim a synchronization that never
            // happened, and the local catalog is left exactly as it was.
            trackSyncAttempt(false);
            // count() and not getAll().size(): the arguments of log() are built whether or not the
            // debug feature is on, and getAll() answers with a copy of the whole catalog - which
            // would be a catalog-sized allocation made to print a number, on the path taken when
            // the module could not reach the server.
            log("SYNC_SENSOR_TYPES: Sensor type fetch failed after %u attempts - keeping the %d "
                "types the module already has",
                (unsigned)completion.attempts,
                _sensorTypeRepository ? _sensorTypeRepository->count() : -1);
            return;
        }

        std::vector<SensorType> serverSensorTypes;
        if (!_outbox->takeSensorTypesResponse(serverSensorTypes)) {
            trackSyncAttempt(false);
            log("SYNC_SENSOR_TYPES: Sensor types reported delivered but the response slot was empty");
            return;
        }

        // Deferred: this runs on the control loop, so the file operations do not.
        const ApplyResult applied = applyServerSensorTypes(std::move(serverSensorTypes), true);

        if (applied == ApplyResult::WritePending) {
            // Not a result yet, and deliberately not reported as one: getLastSyncStatus() keeps
            // describing the last synchronization that finished until this one does.
            log("SYNC_SENSOR_TYPES: Catalog handed to the storage task, ticket %u",
                (unsigned)_writeTicket);
            return;
        }

        const bool syncSuccess = (applied == ApplyResult::Applied);
        trackSyncAttempt(syncSuccess);

        if (syncSuccess) {
            log("SYNC_SENSOR_TYPES: Sensor types synchronization completed successfully");
        } else {
            log("SYNC_SENSOR_TYPES: Sensor types synchronization failed");
        }
    }

    /**
     * Told, on the control loop, how the write handed to the storage task ended.
     *
     * Everything the stored catalog is a precondition for happens here and not at submission time:
     * the repository's cached copy is dropped, the stored sensors are brought in line with their
     * types, the structure is republished if the channels of one of them changed, and only then is
     * the synchronization reported as successful. A refused write reports the same reason, in the
     * same words, as one refused on the calling task - the reason travels back in the completion
     * instead of being read off a DAO that another task has been writing.
     */
    void onCatalogWriteCompletion(const CatalogWriteCompletion& completion) override {
        if (_writeTicket == ISensorTypeCatalogWriter::kRejected ||
            completion.ticket != _writeTicket) {
            return;
        }

        _writeTicket = ISensorTypeCatalogWriter::kRejected;
        // The storage task is done with the file: it reports a write only after the DAO returned.
        if (_sensorTypeRepository) {
            _sensorTypeRepository->setStorageWriteInFlight(false);
        }
        const int syncedCount = _syncedCountAwaitingWrite;
        const int removedCount = _removedCountAwaitingWrite;
        _syncedCountAwaitingWrite = 0;
        _removedCountAwaitingWrite = 0;

        if (!completion.written) {
            logRefusedWrite(0, completion.typeCount, removedCount,
                            getStorageWriteErrorName(completion.error), completion.bytes);
            trackSyncAttempt(false);
            log("SYNC_SENSOR_TYPES: Sensor types synchronization failed");
            return;
        }

        // The types that were written, taken back from the storage task: they are what the stored
        // sensors follow, and what the repository holds from here on. A writer that does not hand
        // them back leaves the cache to be loaded from the file again, which is then also where
        // the sensors' types come from.
        std::vector<SensorType> storedCatalog;
        const bool takenBack = _catalogWriter != nullptr &&
                               _catalogWriter->takeStoredCatalog(completion.ticket, storedCatalog);
        // A writer that hands nothing back leaves the file as the source, and on that path the
        // types are only read from - so what the repository loaded is read where it lies, and the
        // module does not hold a second catalog while it looks up the changed ones.
        SensorTypeCatalog loadedCatalog;
        if (!takenBack && _sensorTypeRepository) {
            _sensorTypeRepository->invalidateCache();
            loadedCatalog = _sensorTypeRepository->catalogSnapshot();
        }

        logStoredCatalog(completion.typeCount, removedCount, completion.bytes);

        if (_sensorRepository) {
            updateSensorsToFollowTheCatalog(loadedCatalog ? *loadedCatalog : storedCatalog);
        }

        if (takenBack && _sensorTypeRepository) {
            _sensorTypeRepository->adoptStoredCatalog(std::move(storedCatalog));
        }

        trackSyncAttempt(syncedCount > 0);
        if (syncedCount > 0) {
            log("SYNC_SENSOR_TYPES: Sensor types synchronization completed successfully");
        }
    }

    /**
     * A catalog a ScanSensors command fetched, applied the way onOutboxCompletion() applies one the
     * outbox fetched: the same merge, the same write on the storage task, the same record of how
     * the synchronization ended.
     *
     * Refused while a write is in flight. Merged now, it would be merged against a catalog that is
     * about to be replaced, and submitCatalogWrite() would refuse to queue it behind that write
     * anyway; the caller waits for the write in flight instead, and finds its outcome in the
     * repository.
     */
    StoreResult storeFetchedCatalog(std::vector<SensorType> sensorTypes) override {
        if (isWritePending()) {
            log("SYNC_SENSOR_TYPES: A catalog write is still in flight (ticket %u), the catalog "
                "fetched for a scan is not stored",
                (unsigned)_writeTicket);
            return StoreResult::NotStored;
        }

        log("SYNC_SENSOR_TYPES: Storing %d sensor types fetched for a sensor scan",
            static_cast<int>(sensorTypes.size()));

        // Deferred, as for the outbox's answer: this runs on the control loop.
        const ApplyResult applied = applyServerSensorTypes(std::move(sensorTypes), true);

        if (applied == ApplyResult::WritePending) {
            log("SYNC_SENSOR_TYPES: Catalog handed to the storage task, ticket %u",
                (unsigned)_writeTicket);
            return StoreResult::Pending;
        }

        // processServerSensorTypes() has recorded how the synchronization ended.
        return applied == ApplyResult::Applied ? StoreResult::Stored : StoreResult::NotStored;
    }

    bool forceSync() override {
        if (!moduleTypeHasLocalSensors()) {
            log("SYNC_SENSOR_TYPES: Module type %d has no local sensor pins, skipping catalog sync",
                _moduleConfig->getModuleType());
            return true;
        }

        log("SYNC_SENSOR_TYPES: Force sync requested");
        _forceSyncRequested = true;

        if (_outbox != nullptr) {
            // Asked for here and not left to the next loop(): the fetch reaches the network task
            // at the moment the caller asked for it, not a control-loop period later. That is
            // what the removed `//NOTE: debug - immediate sync on force request` line was working
            // around; the difference is that this hands the fetch to the network task instead of
            // making it on the caller's. If a fetch is already travelling, the raised flag is
            // what carries the ask - loop() submits it once that one lands, so an ask that
            // arrives mid-flight is delayed rather than dropped.
            requestThroughOutbox(_arduinoAdapter ? _arduinoAdapter->millis() : _lastSyncTime);
            return true;
        }

        syncSensorTypesFromServer();

        return true;
    }

    bool syncNow() override {
        log("SYNC_SENSOR_TYPES: Blocking sync requested");

        const bool syncSuccess = syncSensorTypesFromServer();
        trackSyncAttempt(syncSuccess);

        if (syncSuccess) {
            log("SYNC_SENSOR_TYPES: Blocking sync completed successfully");
        } else {
            log("SYNC_SENSOR_TYPES: Blocking sync failed");
        }

        return syncSuccess;
    }

    bool getLastSyncStatus() const override {
        return _lastSyncStatus;
    }

    time_t getLastSyncTime() const override {
        return _lastSuccessfulSyncTimestamp;
    }

private:
    /**
     * Watch the module's own offline decision and answer whether this is the iteration it cleared.
     *
     * @return true on the offline->online edge, false on every other iteration and always when no
     *         status provider was given
     */
    bool observeConnectivity() {
        if (!_networkStatus) {
            return false;
        }

        const bool isOffline = _networkStatus->isNetworkOffline();
        const bool restored = _networkWasOffline && !isOffline;
        _networkWasOffline = isOffline;
        return restored;
    }

    /**
     * Whether this iteration should ask for the catalog again.
     *
     * Three conditions, in the order they cost anything. Nothing is re-armed unless a
     * synchronization actually finished without succeeding - a module that has a catalog, and a
     * module that has never been asked for one, both answer false here and never reach the
     * network. Then the cooldown, which is what bounds the rate whatever asked. Only then the two
     * triggers: the edge, and the interval measured from the last fetch of any kind, so that a
     * fetch asked for by an IP change also postpones the net under it.
     */
    bool shouldRearm(unsigned long now, bool connectivityRestored) {
        if (!_syncNeedsRearm) {
            return false;
        }

        if ((now - _lastRearmTime) < REARM_COOLDOWN_MS) {
            return false;
        }

        return connectivityRestored || (now - _lastSyncTime) >= REARM_SAFETY_INTERVAL_MS;
    }

    /**
     * Hand one catalog fetch to the outbox, unless one is already travelling.
     *
     * The in-flight guard is the same one rule sync has and exists for the same reason: a second
     * ask would collapse into the same slot in the outbox anyway, but it would also move the
     * interval forward, so the module would keep re-arming a request it is already waiting for.
     */
    void requestThroughOutbox(unsigned long now) {
        if (_requestTicket != IOutbox::kRejected) {
            return;
        }

        const uint32_t ticket = _outbox->submitSensorTypesRequest();
        if (ticket == IOutbox::kRejected) {
            // Nothing was consumed - not the force flag, not the interval - so the next call tries
            // again.
            return;
        }

        _forceSyncRequested = false;
        _lastSyncTime = now;
        _requestTicket = ticket;
        _requestCount++;
        log("SYNC_SENSOR_TYPES: Sensor types requested through the outbox, ticket %u",
            (unsigned)ticket);
    }

    /**
     * Fetch the catalog on the calling task and apply it. Blocks.
     *
     * `httpFailed` is read here for the same reason the outbox reads it: a request that never
     * reached the server says nothing about the catalog, and handing an empty vector to
     * applyServerSensorTypes() would take it for "this account has no sensor models" and report a
     * successful synchronization against types that were never received.
     *
     * SYNC_NOW_MAX_ATTEMPTS attempts, at this call site: getSensorTypesList() makes one attempt
     * per call, because its main driver - the outbox - brings a retry budget of its own.
     * Retried on `httpFailed` and on nothing else - an empty catalog from a server that
     * answered is a real answer.
     */
    bool syncSensorTypesFromServer() {
        if (!_apiService || !_sensorTypeRepository) {
            log("SYNC_SENSOR_TYPES: API service or repository not available");
            return false;
        }

        log("SYNC_SENSOR_TYPES: Fetching sensor types from server");

        if (_arduinoAdapter) {
            // The safety-net interval is measured from here as well as from the outbox path: this
            // is the fetch a module makes at setup, and the one whose failure the re-arm is for.
            _lastSyncTime = _arduinoAdapter->millis();
        }

        std::vector<SensorType> serverSensorTypes;
        for (uint8_t attempt = 1; attempt <= SYNC_NOW_MAX_ATTEMPTS; ++attempt) {
            bool httpFailed = false;
            serverSensorTypes = _apiService->getSensorTypesList(&httpFailed);

            if (!httpFailed) {
                // Stored on this task, before returning: the caller reads the repository back on
                // its next line, so a catalog still on its way to flash would be no answer at all.
                return applyServerSensorTypes(std::move(serverSensorTypes), false) ==
                       ApplyResult::Applied;
            }

            log("SYNC_SENSOR_TYPES: Sensor type fetch failed, attempt %d of %d",
                (int)attempt, (int)SYNC_NOW_MAX_ATTEMPTS);
        }

        // Every attempt failed. Nothing is applied and nothing is written; the local catalog is
        // left exactly as it was.
        log("SYNC_SENSOR_TYPES: Sensor type fetch failed after %d attempts - keeping the catalog "
            "the module already has", (int)SYNC_NOW_MAX_ATTEMPTS);
        return false;
    }

    /** How far applyServerSensorTypes() got. */
    enum class ApplyResult {
        Failed,        // nothing was stored, and nothing is on its way
        Applied,       // the catalog the server sent is stored, or there was nothing to store
        WritePending   // handed to the storage task; the outcome arrives in onCatalogWriteCompletion()
    };

    /**
     * What to do with a catalog the server actually answered with - whichever task fetched it.
     *
     * One copy on purpose: the synchronous path and the outbox path must not be two versions of
     * this decision, or the tests would prove one of them and the device would run the other. The
     * one thing they differ in is who performs the write, and that is `deferWrite`.
     *
     * Taken by value and moved through from here on. The types the server sent are ~1.4 KB each
     * and there are as many of them as the account has models; every copy of that list made on the
     * way to storage is a second catalog the module has to have room for, at the one moment it
     * already holds two.
     */
    ApplyResult applyServerSensorTypes(std::vector<SensorType> serverSensorTypes,
                                       bool deferWrite) {
        if (!_sensorTypeRepository) {
            log("SYNC_SENSOR_TYPES: Sensor type repository not available");
            return ApplyResult::Failed;
        }

        if (serverSensorTypes.empty()) {
            log("SYNC_SENSOR_TYPES: No sensor types received from server, checking local storage");
            return handleEmptyServerResponse() ? ApplyResult::Applied : ApplyResult::Failed;
        }

        log("SYNC_SENSOR_TYPES: Received %d sensor types from server", static_cast<int>(serverSensorTypes.size()));

        // Log sensor type names for debugging
        for (size_t i = 0; i < serverSensorTypes.size() && i < serverSensorTypes.size(); i++) {
            log("SYNC_SENSOR_TYPES: - Sensor type %d: %s (UID: %s)",
                i + 1, serverSensorTypes[i].name.c_str(), serverSensorTypes[i].uid.c_str());
        }

        return processServerSensorTypes(std::move(serverSensorTypes), deferWrite);
    }

    ApplyResult processServerSensorTypes(std::vector<SensorType> serverSensorTypes,
                                         bool deferWrite) {
        int removedCount = 0;
        const size_t usableCount = mergeLocalCatalogIntoAnswer(serverSensorTypes, removedCount);

        if (usableCount == 0 || serverSensorTypes.empty()) {
            // An answer that yields no usable sensor type is not a statement about the account's
            // catalog: nothing is stored, nothing is retired, and what the module holds is left
            // exactly as it is.
            log("SYNC_SENSOR_TYPES: No usable sensor types in the answer, catalog left untouched");
            trackSyncAttempt(false);
            return ApplyResult::Failed;
        }

        int syncedCount = synchronizeSensorTypes(std::move(serverSensorTypes), removedCount,
                                                 deferWrite);
        if (syncedCount == WRITE_PENDING) {
            // Neither the count nor the sync status is known yet; both are settled by the
            // completion, and nothing here may pretend otherwise.
            return ApplyResult::WritePending;
        }

        // A catalog the module already had is an exchange with nothing to do, and that is a
        // completed synchronization: what makes one fail is the module ending up with a
        // catalog that is not the server's, which only a refused write does here.
        const bool syncSuccess = (syncedCount != WRITE_REFUSED);
        if (syncSuccess) {
            log("SYNC_SENSOR_TYPES: Synchronized %d/%d sensor types successfully", syncedCount, static_cast<int>(usableCount));
        }

        trackSyncAttempt(syncSuccess);
        return syncSuccess ? ApplyResult::Applied : ApplyResult::Failed;
    }

    /**
     * Turn the server's answer into the catalog to store, in the vector the answer arrived in.
     *
     * The catalog to store is the published set: the server's version of every type it has edited,
     * the module's own version of every type it has not, and nothing else - so the answer, with the
     * unchanged entries replaced one at a time by what the repository holds, is already it. The
     * types the server no longer publishes are simply the ones nothing matched.
     *
     * Nothing is taken out of the repository to do this. Its cache is what the control loop reads
     * from while the storage task writes the file - the invariant SensorTypeCatalogWriter documents
     * - and it is also the module's catalog if the write is refused, so it stays exactly as it is
     * until the write is known to have landed. What this costs above the answer and the cache is
     * one sensor type at a time.
     *
     * @param serverSensorTypes in/out: the answer, left holding the merged catalog
     * @param removedCount out: how many types the module has that the server no longer publishes
     * @return how many of the published types this module can use; zero means the answer says
     *         nothing about the catalog and nothing at all is to be done with it
     */
    size_t mergeLocalCatalogIntoAnswer(std::vector<SensorType>& serverSensorTypes,
                                       int& removedCount) {
        _changedUids.clear();
        _localMatchCount = 0;

        // How many types the module holds, asked once, before anything else.
        //
        // Two things depend on it, and the second is the reason it is asked here rather than where
        // it is used. A repository whose storage cannot be read keeps no cache - it has nothing to
        // keep - so it asks storage again on every call, and a module whose catalog file is
        // unreadable or missing would otherwise load that file once per published type: the whole
        // file into one string, and one parser document per type inside it, tens of times over, on
        // the control loop, in the second an answer is already in RAM. With the count in hand, a
        // module that holds nothing asks for nothing: every published type is an addition, and
        // there is no local version to look for.
        const int held = _sensorTypeRepository->count();
        const bool holdsCatalog = held > 0;

        size_t usableCount = 0;
        size_t kept = 0;
        for (size_t i = 0; i < serverSensorTypes.size(); i++) {
            SensorType& published = serverSensorTypes[i];
            const std::string uid = published.uid;

            // A uid the answer already carried: the module stores one type per uid. The first
            // entry of a repeated uid is the one kept - including when it is the one that failed
            // validation and a later copy of the same uid would have been usable, which is a
            // malformed answer either way.
            if (uid.empty() || holdsUid(serverSensorTypes, kept, uid)) {
                if (uid.empty()) {
                    log("SYNC_SENSOR_TYPES: Sensor type missing UID");
                } else {
                    log("SYNC_SENSOR_TYPES: Sensor type %s published twice, keeping the first",
                        uid.c_str());
                }
                continue;
            }

            Utils::Optional<SensorType> local;
            if (holdsCatalog) {
                local = _sensorTypeRepository->get(uid.c_str());
            }
            if (local.has_value()) {
                _localMatchCount++;
            }

            if (!validateSensorType(published)) {
                // Published, and unusable. Not a retired type: what the module has of it stays, and
                // if it has nothing then this uid simply does not enter the catalog.
                log("SYNC_SENSOR_TYPES: Sensor type %s validation failed", uid.c_str());
                if (!local.has_value()) {
                    continue;
                }
                published = std::move(local.value());
            } else {
                usableCount++;
                if (local.has_value() && !shouldUpdateSensorType(local.value(), published)) {
                    log("SYNC_SENSOR_TYPES: Sensor type %s already up to date", uid.c_str());
                    published = std::move(local.value());
                } else {
                    log("SYNC_SENSOR_TYPES: Marked sensor type %s for %s", uid.c_str(),
                        local.has_value() ? "update" : "addition");
                    _changedUids.push_back(uid);
                }
            }

            if (kept != i) {
                serverSensorTypes[kept] = std::move(serverSensorTypes[i]);
            }
            kept++;
        }

        serverSensorTypes.resize(kept);

        // Everything the module holds that no published type matched. Counted rather than searched
        // for: every local type a published one matched was counted above.
        removedCount = (held > _localMatchCount) ? (held - _localMatchCount) : 0;
        if (removedCount > 0) {
            log("SYNC_SENSOR_TYPES: Marked %d sensor types for removal, no longer published by server",
                removedCount);
        }

        return usableCount;
    }

    /** Whether the first `count` entries of a catalog already hold this uid. */
    static bool holdsUid(const std::vector<SensorType>& catalog, size_t count,
                         const std::string& uid) {
        for (size_t i = 0; i < count && i < catalog.size(); i++) {
            if (catalog[i].uid == uid) {
                return true;
            }
        }
        return false;
    }

    /** The type with this uid in a catalog, or nullptr. */
    static const SensorType* findByUid(const std::vector<SensorType>& catalog,
                                       const std::string& uid) {
        for (size_t i = 0; i < catalog.size(); i++) {
            if (catalog[i].uid == uid) {
                return &catalog[i];
            }
        }
        return nullptr;
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
     * Store the merged catalog - here or on the storage task, see `deferWrite`.
     *
     * The catalog is moved on to whoever writes it; nothing here holds a second copy of it. What
     * the repository holds is not touched until the write is known to have landed: it is what the
     * control loop reads from meanwhile, and what the module is left with if the write is refused.
     *
     * @param mergedCatalog The catalog to store: the published types, as the module will hold them
     * @param removedCount How many types the server no longer publishes
     */
    int synchronizeSensorTypes(std::vector<SensorType> mergedCatalog,
                               int removedCount,
                               bool deferWrite) {
        if (mergedCatalog.empty()) {
            log("SYNC_SENSOR_TYPES: Empty server sensor type list, catalog left untouched");
            return 0;
        }

        const int syncedCount = static_cast<int>(_changedUids.size());
        const bool changed = (syncedCount > 0) || (removedCount > 0);

        if (!changed) {
            log("SYNC_SENSOR_TYPES: No changes detected, skipping save");
            // The repository already holds these very types, so there is nothing to store and
            // nothing to replace: this is the ordinary state of a catalog nobody has edited.
            //
            // The stored sensors are looked at all the same. One the flash refused to store again
            // when its type changed is still behind that type, and the type itself is now stored,
            // so a comparison of the types will never point at it again. They are looked up in
            // what the repository holds and not in the answer, which is let go of first: the two
            // are the same types, and keeping both would be a second catalog held for the pass. A
            // write still in flight is left to bring them in line when it lands.
            if (_sensorRepository && !isWritePending()) {
                std::vector<SensorType>().swap(mergedCatalog);
                const SensorTypeCatalog held = _sensorTypeRepository->catalogSnapshot();
                updateSensorsToFollowTheCatalog(*held);
            }
            return 0;
        }

        if (deferWrite && _catalogWriter != nullptr) {
            return submitCatalogWrite(std::move(mergedCatalog), syncedCount, removedCount);
        }

        // Single bulk write - 1 SPIFFS write; the catalog was already in memory
        const size_t typeCount = mergedCatalog.size();
        int saved = _sensorTypeRepository->saveAll(mergedCatalog);
        if (saved != static_cast<int>(typeCount)) {
            logRefusedWrite(saved, typeCount, removedCount,
                            getStorageWriteErrorName(_sensorTypeRepository->getLastSaveError()),
                            _sensorTypeRepository->getLastSaveBytes());
            return WRITE_REFUSED;
        }

        logStoredCatalog(static_cast<size_t>(saved), removedCount,
                         _sensorTypeRepository->getLastSaveBytes());

        if (_sensorRepository) {
            updateSensorsToFollowTheCatalog(mergedCatalog);
        }

        // The write landed, so what was written is what the module holds: saveAll() drops the
        // cached copy, and the next reader would otherwise load the file this catalog was just
        // written to.
        _sensorTypeRepository->adoptStoredCatalog(std::move(mergedCatalog));
        _changedUids.clear();

        return syncedCount + removedCount;
    }

    /**
     * Hand the merged catalog to the storage task, unless one is already on its way there.
     *
     * A refusal is a failed synchronization and nothing else: the catalog the module holds is
     * exactly as it was - it was never taken out of the repository - the merge is cheap to redo,
     * and the next fetch redoes it. Nothing is queued behind the write in flight, so a module
     * cannot accumulate catalogs waiting for flash.
     */
    int submitCatalogWrite(std::vector<SensorType> toSave,
                           int syncedCount,
                           int removedCount) {
        if (isWritePending()) {
            log("SYNC_SENSOR_TYPES: A catalog write is still in flight (ticket %u), keeping the "
                "catalog the module already has",
                (unsigned)_writeTicket);
            return WRITE_REFUSED;
        }

        const size_t typeCount = toSave.size();
        const uint32_t ticket = _catalogWriter->submit(std::move(toSave));
        if (ticket == ISensorTypeCatalogWriter::kRejected) {
            // Not a storage error - storage was never reached - but reported through the same
            // sentence, because what a reader of the log needs to know first is that the catalog
            // the module has is not the catalog the server sent.
            logRefusedWrite(0, typeCount, removedCount, "storage_task_refused", 0);
            return WRITE_REFUSED;
        }

        _writeTicket = ticket;
        _writeCount++;
        // From here until the outcome is collected the storage task owns the file: whatever reads
        // the catalog on the loop meanwhile is answered from what the repository holds.
        _sensorTypeRepository->setStorageWriteInFlight(true);
        _syncedCountAwaitingWrite = syncedCount + removedCount;
        _removedCountAwaitingWrite = removedCount;
        _changedUids.clear();
        return WRITE_PENDING;
    }

    /** Why nothing was stored - one wording for both the deferred write and the inline one. */
    void logRefusedWrite(int saved, size_t requested, int removed, const char* reason, size_t bytes) {
        log("SYNC_SENSOR_TYPES: saveAll wrote %d of %d sensor types, removed=%d, reason=%s, bytes=%d, freeHeap=%d, maxAllocHeap=%d",
            saved, static_cast<int>(requested), removed,
            reason,
            static_cast<int>(bytes),
            _arduinoAdapter ? static_cast<int>(_arduinoAdapter->getFreeHeap()) : -1,
            _arduinoAdapter ? static_cast<int>(_arduinoAdapter->getMaxAllocHeap()) : -1);
    }

    /** And what was stored, in the same wording, whoever wrote it. */
    void logStoredCatalog(size_t saved, int removed, size_t bytes) {
        log("SYNC_SENSOR_TYPES: Bulk saved %d sensor types to storage, removed=%d, bytes=%d, freeHeap=%d, maxAllocHeap=%d",
            static_cast<int>(saved), removed, static_cast<int>(bytes),
            _arduinoAdapter ? static_cast<int>(_arduinoAdapter->getFreeHeap()) : -1,
            _arduinoAdapter ? static_cast<int>(_arduinoAdapter->getMaxAllocHeap()) : -1);
    }

    /**
     * Bring every stored sensor in line with its type in the catalog the module holds now.
     *
     * Every sensor, and not only those of the types this synchronization changed: what decides a
     * write is whether a sensor differs from its type, not that its type was marked changed. A type
     * is stored again for any of its fields, and most of them - its name, the ranges a scan matches
     * by - are not copied into a sensor, so a sensor that already has what its type gives is not
     * written. And a sensor the flash refused to store again is behind its type while the type
     * itself is stored: no later comparison of types points at it, only a comparison of the sensor.
     *
     * @param catalog The catalog the module holds now; the types are looked up in it by uid
     */
    void updateSensorsToFollowTheCatalog(const std::vector<SensorType>& catalog) {
        std::vector<SensorEntity> allSensors = _sensorRepository->getAll();
        int updatedCount = 0;
        bool channelsChanged = false;
        bool computedChannelsMoved = false;

        for (auto& sensor : allSensors) {
            if (sensor.sensorTypeUid.empty()) continue;

            const SensorType* sensorType = findByUid(catalog, sensor.sensorTypeUid);
            if (sensorType == nullptr) continue;

            // What the readings the module holds for this sensor were taken by and reported under.
            const uint32_t registersBefore = sensor.registerFingerprint();
            const std::string channelsBefore = describeChannels(sensor);
            const std::string virtualChannelsBefore = describeVirtualChannels(sensor);

            if (!takeRegistersOf(*sensorType, sensor)) continue;

            if (!_sensorRepository->save(sensor)) {
                // The registry keeps the sensor as it was, and its readings and its records in the
                // structure stay with it. The next synchronization finds it behind its type again.
                log("SYNC_SENSOR_TYPES: Failed to update sensor %s", sensor.uid.c_str());
                continue;
            }

            updatedCount++;
            log("SYNC_SENSOR_TYPES: Updated sensor %s registers from sensor type %s",
                sensor.uid.c_str(), sensorType->uid.c_str());

            const bool channelsMoved = describeChannels(sensor) != channelsBefore;
            dropReadingsOfOtherChannels(sensor, registersBefore, channelsMoved);

            if (channelsMoved) {
                channelsChanged = true;
                const std::string virtualChannelsAfter = describeVirtualChannels(sensor);
                if (virtualChannelsAfter != virtualChannelsBefore) {
                    computedChannelsMoved = true;
                    log("SYNC_SENSOR_TYPES: VIRTUAL_CHANNEL_SHIFT uid=%s old=%s new=%s",
                        sensor.uid.c_str(),
                        virtualChannelsBefore.c_str(),
                        virtualChannelsAfter.c_str());
                }
            }
        }

        log("SYNC_SENSOR_TYPES: Updated %d sensors to follow their sensor types", updatedCount);

        // One call per synchronization, not per sensor, and only for a sensor stored with other
        // channels: the records are rebuilt from the registry, so they describe what was stored.
        if (channelsChanged && _republishModuleStructure) {
            _republishModuleStructure->execute(computedChannelsMoved ? "virtual_channel_shift"
                                                                     : "sensor_channels_changed");
        }
    }

    /**
     * Give a stored sensor what its type has for it now: the Modbus channels and the baud rate to a
     * Modbus sensor, the I2C channels to an I2C sensor - each only when the type has any.
     *
     * @return whether that changed the sensor; one that already has it is left as it is
     */
    static bool takeRegistersOf(const SensorType& sensorType, SensorEntity& sensor) {
        bool changed = false;

        if (sensor.isModbusSensor() && !sensorType.modbusRegisterConfig.empty() &&
            (!SensorTypeContent::sameConfigs(sensor.modbusRegisterConfigs, sensorType.modbusRegisterConfig) ||
             sensor.baudRate != sensorType.baudRate)) {
            sensor.modbusRegisterConfigs = sensorType.modbusRegisterConfig;
            sensor.baudRate = sensorType.baudRate;
            changed = true;
        }

        if (sensor.isI2CSensor() && !sensorType.i2cRegisterConfig.empty() &&
            !SensorTypeContent::sameConfigs(sensor.i2cRegisterConfigs, sensorType.i2cRegisterConfig)) {
            sensor.i2cRegisterConfigs = sensorType.i2cRegisterConfig;
            changed = true;
        }

        return changed;
    }

    /**
     * Forget what a sensor was read as when this catalog gave it other channels.
     *
     * The value model keeps a sensor's readings under its uid, channel by channel, and the update
     * packet reports every channel kept there: a channel the catalog took out would go on being
     * reported until a restart, and one it rescaled would be reported at the old scale until the
     * sensor is read again. Nothing can be converted - only the scaled value is kept, not the
     * register - so the values go, all of them: which register channels a computed one is made of is
     * not this use case's to know. The sensor enters the packet again with its first reading by the
     * channels it has now. When it was last read stays: the device that answered is the same.
     *
     * Two things change the channels: the registers (SensorEntity::registerFingerprint()), and the
     * channels' numbers and types (describeChannels()). The types decide the computed channels while
     * the registers stay as they were - a type edited away from humidity takes the VPD away, one that
     * moves humidity to another register makes the VPD of another pair - and the value kept from
     * before would stay, until a reading overwrote it or for good.
     *
     * Nothing goes when neither moved: a sensor stored again for anything else reads what it read.
     */
    void dropReadingsOfOtherChannels(const SensorEntity& sensor, uint32_t registersBefore,
                                     bool channelsMoved) {
        if (_sensorValues == nullptr ||
            (sensor.registerFingerprint() == registersBefore && !channelsMoved)) {
            return;
        }
        _sensorValues->clearChannelValues(sensor.uid);
        log("SYNC_SENSOR_TYPES: Dropped the readings of sensor %s, taken by the channels it had before",
            sensor.uid.c_str());
    }

    /**
     * The channels a sensor is reported by, as one comparable string: each register channel by its
     * number and type, then the computed ones.
     *
     * Of what a sensor takes from its type, these are what the structure describes it by - a record
     * per channel, carrying the channel's type - and what its computed channels are worked out from:
     * which channels are the sources of one is a matter of their types. While this and the register
     * fingerprint both stay as they were, the values kept for the sensor still mean what they meant
     * and its records still say what they said.
     */
    std::string describeChannels(const SensorEntity& sensor) {
        std::string description;
        for (const auto& config : sensor.modbusRegisterConfigs) {
            description += std::to_string(config.channel) + ":" + config.sensorType + ",";
        }
        description += "|";
        for (const auto& config : sensor.i2cRegisterConfigs) {
            description += std::to_string(config.channel) + ":" + config.sensorType + ",";
        }
        description += "|";
        return description + describeVirtualChannels(sensor);
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

    /**
     * Whether the published type is not the one the module holds.
     *
     * Decided by what the type holds, its timestamp being one of its fields. The timestamp moves
     * when the type is edited through the platform; an edit made around the platform - an SQL
     * migration - leaves it where it was, and a type nobody has edited since it was created has
     * none at all. The fields are what tell those apart from a type that did not change: an edit
     * that left the timestamp alone still reaches the module, and a type without a timestamp is not
     * stored again on every synchronization.
     */
    bool shouldUpdateSensorType(const SensorType& heldSensorType, const SensorType& publishedSensorType) {
        if (heldSensorType.updatedAt != publishedSensorType.updatedAt) {
            log("SYNC_SENSOR_TYPES: Sensor type %s updatedAt changed: %s -> %s",
                publishedSensorType.uid.c_str(),
                heldSensorType.updatedAt.empty() ? "empty" : heldSensorType.updatedAt.c_str(),
                publishedSensorType.updatedAt.empty() ? "empty" : publishedSensorType.updatedAt.c_str());
            return true;
        }

        if (!SensorTypeContent::sameSensorType(heldSensorType, publishedSensorType)) {
            log("SYNC_SENSOR_TYPES: Sensor type %s changed with its updatedAt as it was (%s)",
                publishedSensorType.uid.c_str(),
                publishedSensorType.updatedAt.empty() ? "empty" : publishedSensorType.updatedAt.c_str());
            return true;
        }

        return false;
    }
    
    bool handleEmptyServerResponse() {
        // count() and not getAll(): the only question asked here is how many types the module
        // already holds, and getAll() answers it with a second copy of the whole catalog.
        const int heldCount = _sensorTypeRepository->count();

        if (heldCount > 0) {
            log("SYNC_SENSOR_TYPES: Using %d sensor types from local storage", heldCount);
            return true;
        }

        log("SYNC_SENSOR_TYPES: No sensor types available locally or from server");
        return true;
    }
    
    /**
     * One place where a finished synchronization is recorded, whichever path finished it.
     *
     * Also where the re-arm is armed and disarmed, and that is the point of routing every path
     * through here: the re-arm must describe the last thing that actually happened, not the last
     * thing one of the four entry points happened to remember to set.
     */
    void trackSyncAttempt(bool success) {
        if (success) {
            if (_ntpTimeAdapter) {
                _lastSuccessfulSyncTimestamp = _ntpTimeAdapter->getEpochTime();
            }
        }
        _lastSyncStatus = success;
        _syncNeedsRearm = !success;
    }
};

#endif // SYNC_SENSOR_TYPES_USE_CASE_H