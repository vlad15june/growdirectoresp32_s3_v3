#pragma once

#include "Application/Interfaces/IDiagnosticService.h"
#include "Application/Interfaces/IOutbox.h"
#include "Application/Services/CounterReport.h"
#include "Application/Services/Net/LinkSupervisor.h"
#include "Core/Domain/Interfaces/API/IRestAPIService.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/Adapters/Json/IJsonConverter.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include <cstdarg>
#include <cstdio>

/**
 * @brief The module's diagnostic report: a snapshot of its heap, its link and its counters, built on
 *        the control loop and sent through the outbox.
 *
 * It reports and decides nothing. What the module decides about its link - the radio restarts, the
 * offline decision, the restart of a module offline too long - is LinkSupervisor's, on its own tick;
 * the report reads what was decided there.
 */
class DiagnosticService : public IDiagnosticService
{
private:
    IRestAPIService *_restApiService;
    IHybridAPIService *_hybridApiService; // Added hybrid API service
    IModuleConfig *_moduleConfig;
    ILogger &_logger;
    IDebugSystem &_debugSystem;
    IJsonConverter *_jsonConverter;
    IWifiAdapter *_wifiAdapter;
    INTPTimeAdapter *_timeAdapter;
    IArduinoAdapter *_arduinoAdapter;
    IMqttManager *_mqttManager;
    IOutbox *_outbox = nullptr;
    CounterReport *_counterReport = nullptr;
    // What the link's supervision decided, for the report to read; without it the report says the
    // network never failed and the server is reachable.
    LinkSupervisor *_linkSupervisor = nullptr;

    void log(const char *format, ...)
    {
        if (!_debugSystem.isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG))
        {
            return;
        }

        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        _logger.println(DebugFeatures::DIAGNOSTIC_DEBUG, buffer);
    }

public:
    DiagnosticService(
        IRestAPIService *restApiService,
        IModuleConfig *moduleConfig,
        ILogger &logger,
        IDebugSystem &debugSystem,
        IJsonConverter *jsonConverter = nullptr,
        IWifiAdapter *wifiAdapter = nullptr,
        INTPTimeAdapter *timeAdapter = nullptr,
        IArduinoAdapter *arduinoAdapter = nullptr,
        IMqttManager *mqttManager = nullptr,
        IHybridAPIService *hybridApiService = nullptr) // Added hybrid API service parameter
        : _restApiService(restApiService),
          _hybridApiService(hybridApiService),
          _moduleConfig(moduleConfig),
          _logger(logger),
          _debugSystem(debugSystem),
          _jsonConverter(jsonConverter),
          _wifiAdapter(wifiAdapter),
          _timeAdapter(timeAdapter),
          _arduinoAdapter(arduinoAdapter),
          _mqttManager(mqttManager)
    {
    }

    /** @brief Where the report reads the network failures and the server's state from. */
    void setLinkSupervisor(LinkSupervisor *linkSupervisor) { _linkSupervisor = linkSupervisor; }

    DiagnosticDto perform() override
    {
        DiagnosticDto diagnosticData;

        if (_wifiAdapter != nullptr)
        {
            diagnosticData.rssi = _wifiAdapter->getRSSI();
            diagnosticData.uid = _moduleConfig->getModuleUid();
        }
        else
        {
            diagnosticData.rssi = 0;
            diagnosticData.uid = "unknown";
        }

        if (_moduleConfig != nullptr)
        {
            ISettings *settings = _moduleConfig->getSettings();
            if (settings != nullptr)
            {
                char buffer[BUFSIZE];
                settings->getUser(buffer, BUFSIZE);
                diagnosticData.email = buffer;
                settings->getAccountUid(buffer, BUFSIZE);
                diagnosticData.accountUid = buffer;
            }
        }

        if (_timeAdapter != nullptr)
        {
            diagnosticData.timestamp = _timeAdapter->getFormattedTime();
        }
        else
        {
            diagnosticData.timestamp = "Unknown time";
        }

        if (_arduinoAdapter)
        {
            diagnosticData.uptimeMs = _arduinoAdapter->millis();
            diagnosticData.freeHeap = _arduinoAdapter->getFreeHeap();
            diagnosticData.minFreeHeap = _arduinoAdapter->getMinFreeHeap();
            diagnosticData.heapSize = _arduinoAdapter->getHeapSize();
            diagnosticData.maxAllocHeap = _arduinoAdapter->getMaxAllocHeap();
            diagnosticData.psramSize = _arduinoAdapter->getPsramSize();
            diagnosticData.psramFree = _arduinoAdapter->getFreePsram();
        }
        else
        {
            diagnosticData.uptimeMs = 0;
            diagnosticData.freeHeap = 0;
            diagnosticData.minFreeHeap = 0;
            diagnosticData.heapSize = 0;
            diagnosticData.maxAllocHeap = 0;
            diagnosticData.psramSize = 0;
            diagnosticData.psramFree = 0;
        }

        diagnosticData.networkFails = _linkSupervisor ? _linkSupervisor->networkFails() : 0;
        diagnosticData.apiSendFails = _restApiService ? _restApiService->getApiSendFails() : 0;

        // The server's state as the link's supervision reads it off the REST client
        const bool httpDown = _linkSupervisor != nullptr && _linkSupervisor->isHttpConnectionDown();
        diagnosticData.apiStatus = httpDown ? "disconnected" : "connected";

        // Use the actual MQTT connection status directly from the manager
        diagnosticData.mqttStatus = (_mqttManager && _mqttManager->isConnected()) ? "connected" : "disconnected";

        // Get MQTT failures from the manager instead of using local variable
        diagnosticData.mqttFails = _mqttManager ? _mqttManager->getMqttFails() : 0;

        // Inbound messages the module refused because they were over the size limit:
        // reported here so the drop is visible without a live debug session
        diagnosticData.mqttOversizeDrops = _mqttManager ? _mqttManager->getInboundOversizeDrops() : 0;

        diagnosticData.apiSendFailRatio = _restApiService ? _restApiService->getApiSendFailRatio() : 0.0f;

        if (_moduleConfig != nullptr)
        {
            diagnosticData.firmware = _moduleConfig->getFirmwareVersion();
        }
        else
        {
            diagnosticData.firmware = "unknown";
        }

        diagnosticData.failsInHour = _linkSupervisor ? _linkSupervisor->failsInHour() : 0;

        // The last outage, once the module has come back from one: it cannot be sent while it lasts.
        LinkTroubleshooting::Outage outage;
        if (_linkSupervisor != nullptr && _linkSupervisor->lastOutage(outage))
        {
            diagnosticData.lastOutageCause = LinkTroubleshooting::causeName(outage.cause);
            diagnosticData.lastOutageStartUptimeMs = outage.startedAtMs;
            diagnosticData.lastOutageDurationMs = outage.durationMs;
        }

        log("DIAG: Performed diagnostics - Free heap: %d, API status: %s",
            diagnosticData.freeHeap, diagnosticData.apiStatus.c_str());

        // The counters line rides the same period and the same channel as the message above.
        // Log only: the DTO built here is a wire contract and gets nothing from the counters.
        // The gate is checked before the line is built, so a disabled channel costs nothing.
        if (_counterReport != nullptr
            && _debugSystem.isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG))
        {
            // Sized to the worst line the report can produce, so a full table with saturated
            // values still carries every column - the watermarks at the end above all. Emitted
            // straight to the logger: log() formats through its own 512-byte buffer, which
            // would truncate exactly the line this buffer was sized to keep whole.
            char line[CounterReport::LINE_BUFFER_CHARS];
            static_assert(sizeof(line) > CounterReport::WORST_LINE_CHARS,
                          "the line buffer must hold the worst counter line whole");
            _counterReport->format(line, sizeof(line));
            _logger.println(DebugFeatures::DIAGNOSTIC_DEBUG, line);
        }

        return diagnosticData;
    }

    bool send(const DiagnosticDto& diagnosticData) override
    {
        // First try to send data using hybrid API if available
        if (_hybridApiService != nullptr)
        {
            log("DIAG: Sending diagnostic data via Hybrid API");
            return _hybridApiService->sendDiagnosticData(diagnosticData);
        }
        // Fall back to REST API if hybrid is not available
        else if (_restApiService != nullptr)
        {
            log("DIAG: Sending diagnostic data via REST API");
            return _restApiService->sendDiagnosticData(diagnosticData);
        }

        log("DIAG: No API service available");
        return false;
    }

    /**
     * @brief Hand diagnostic sends to the outbox instead of making them on the caller's task.
     *
     * The hand-off lives here rather than in a wrapping decorator: every periodic network path
     * goes through the one outbox queue, and a decorator would be a second mechanism doing what
     * the first one does.
     */
    void setOutbox(IOutbox *outbox) { _outbox = outbox; }

    /** The periodic counters line; emitted by perform() when diagnostic logging is enabled. */
    void setCounterReport(CounterReport *counterReport) { _counterReport = counterReport; }

    /**
     * @brief Collect the snapshot here, on the control loop, and queue the send.
     *
     * perform() stays on the control loop: it reads the counters and the states the loop owns.
     * Only the send moves.
     *
     * @return with an outbox: true if the snapshot was accepted for delivery, not that it was
     *         delivered. Without one: delivered or not. The only caller in the
     *         firmware (LoopServiceConfigureDone) ignores it.
     */
    bool performAndSend() override
    {
        if (_moduleConfig == nullptr)
        {
            log("DIAG: Module configuration not available");
            return false;
        }

        DiagnosticDto diagnosticData = perform();

        if (_outbox != nullptr)
        {
            return _outbox->submitDiagnostic(diagnosticData) != IOutbox::kRejected;
        }

        return send(diagnosticData);
    }
};
