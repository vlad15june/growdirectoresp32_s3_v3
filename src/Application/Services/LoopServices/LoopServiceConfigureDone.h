#ifndef LOOP_SERVICE_CONFIGURE_DONE_H
#define LOOP_SERVICE_CONFIGURE_DONE_H

#include "LoopServiceBase.h"
#include "Application/Services/Boot/BootSequencer.h"
#include "Application/Services/Net/LinkSupervisor.h"
#include "Core/Domain/UseCases/Diagnostics/PreventiveRestartUseCase.h"
#include "Application/Interfaces/ICommandService.h"
#include "Application/Interfaces/IOutbox.h"
#include "Application/Interfaces/ISensorTypeCatalogWriter.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeLocalIpUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IOnConnectivityRestoredUseCase.h"
#include "Core/Domain/Interfaces/UseCases/IRepublishModuleStructureUseCase.h"
#include "Core/Domain/Interfaces/UseCases/Network/IWifiRoamUseCase.h"
#include "Core/Domain/Interfaces/UseCases/SensorTypes/ISyncSensorTypesUseCase.h"
#include "Core/Domain/Interfaces/Mqtt/IMqttManager.h"
#include "Application/Interfaces/IDiagnosticService.h"
#include "Application/Services/Devices/IDeviceService.h"
#include "Application/Services/Sensors/ISensorService.h"
#include "Application/Services/Rules/IRuleService.h"
#include "Core/Domain/Interfaces/IOtaUpdateLog.h"
#include "Core/Domain/Interfaces/IBootEventLog.h"
#include "Core/Domain/Interfaces/UseCases/IConfirmRunningFirmwareUseCase.h"

class LoopServiceConfigureDone : public LoopServiceBase {
private:
    IOnChangeLocalIpUseCase* onChangeLocalIpUseCase;
    IOnConnectivityRestoredUseCase* onConnectivityRestoredUseCase;
    IMqttManager* mqttManager;
    IDiagnosticService* diagnosticService;
    IDeviceService* deviceService;
    ISensorService* sensorService;
    IRuleService* ruleService;
    IOtaUpdateLog* otaUpdateLog;
    IBootEventLog* bootEventLog;
    IBootEventLog* restartEventLog;
    IOutbox* outbox = nullptr;
    ICommandService* commandService = nullptr;
    // Routed from here and not from a service, because nothing else owns it: the catalog sync has
    // no service wrapping it the way SyncRulesUseCase has RuleService, and its own loop() is not
    // called from anywhere.
    ISyncSensorTypesUseCase* syncSensorTypesUseCase = nullptr;
    IConfirmRunningFirmwareUseCase* confirmRunningFirmwareUseCase = nullptr;
    // Routed from here for the same reason, and to the same use case: the catalog is written on the
    // storage task, and what a stored catalog allows happens back here.
    ISensorTypeCatalogWriter* sensorTypeCatalogWriter = nullptr;
    std::vector<CatalogWriteCompletion> catalogWrites;
    // And the last thing a changed catalog sets off: the structure republish submits to the outbox
    // from the catalog sync, so its outcome comes back through the same drain as everyone else's.
    IRepublishModuleStructureUseCase* republishModuleStructureUseCase = nullptr;
    // Routed from here for the same reason again: the decision to move the station to a better
    // access point is made on this loop, and the move itself is a job of the outbox - so both its
    // calls belong where the other producers' do.
    IWifiRoamUseCase* wifiRoamUseCase = nullptr;
    // The order the module comes up in after setup. Ticked here because the producers it stands in
    // front of are driven from here, and between the drains and them is the one point of an
    // iteration where what came back is known and nothing new has been asked for.
    BootSequencer* bootSequencer = nullptr;
    // What the module decides about its link, and its nightly restart: on their own tick, thirty
    // seconds from millis 0, after the producers that read the offline decision (the rule and
    // catalog syncs see an edge on the next iteration) and before the diagnostic that reports it.
    LinkSupervisor* linkSupervisor = nullptr;
    PreventiveRestartUseCase* preventiveRestartUseCase = nullptr;
    unsigned long lastSupervisionTime = 0;

    /**
     * Apply the consequences of everything the network task finished since the last iteration.
     *
     * This runs on the control loop and nowhere else, which is the point: a producer's reaction to
     * a delivery - DeviceService erasing a device's reason for switching on, SyncRulesUseCase
     * applying a fetched rule snapshot - touches state that only this task owns, so none of it
     * needs a lock and all of it can be driven by a host test.
     *
     * Bounded by construction: the completion queue holds a fixed number of entries and the
     * network task adds at most one per finished request, so this cannot grow with how long the
     * network was down.
     */
    void drainOutboxCompletions() {
        if (!outbox) {
            return;
        }

        OutboxCompletion completion;
        while (outbox->pollCompletion(completion)) {
            if (deviceService) {
                deviceService->onOutboxCompletion(completion);
            }
            if (sensorService) {
                sensorService->onOutboxCompletion(completion);
            }
            if (ruleService) {
                ruleService->onOutboxCompletion(completion);
            }
            if (commandService) {
                commandService->onOutboxCompletion(completion);
            }
            if (onChangeLocalIpUseCase) {
                onChangeLocalIpUseCase->onOutboxCompletion(completion);
            }
            if (republishModuleStructureUseCase) {
                republishModuleStructureUseCase->onOutboxCompletion(completion);
            }
            if (syncSensorTypesUseCase) {
                syncSensorTypesUseCase->onOutboxCompletion(completion);
            }
            if (linkSupervisor) {
                linkSupervisor->onOutboxCompletion(completion);
            }
            if (wifiRoamUseCase) {
                wifiRoamUseCase->onOutboxCompletion(completion);
            }
        }
    }

    /**
     * Apply the outcome of every catalog write the storage task finished since the last iteration.
     *
     * Bounded the same way: the writer accepts a fixed number of writes and produces one outcome
     * each, and the buffer that collects them is reserved for that number once.
     */
    void drainCatalogWrites() {
        if (!sensorTypeCatalogWriter || !syncSensorTypesUseCase) {
            return;
        }

        if (!sensorTypeCatalogWriter->drainResults(catalogWrites)) {
            return;
        }

        for (size_t i = 0; i < catalogWrites.size(); i++) {
            syncSensorTypesUseCase->onCatalogWriteCompletion(catalogWrites[i]);
        }
        catalogWrites.clear();
    }

    // Diagnostic timer variables
    unsigned long lastDiagnosticTime = 0;
    const unsigned long DIAGNOSTIC_INTERVAL = 30 * 1000; // 30 sec in milliseconds
    // A tick of the boot that took the snapshot and could not send it: sent once, at the first STEADY.
    bool diagnosticOwed = false;
    unsigned long diagnosticOwedSinceMs = 0;

    // OTA log flush timer
    unsigned long lastOtaFlushTime = 0;
    const unsigned long OTA_FLUSH_INTERVAL = 5 * 1000; // 5 sec in milliseconds

public:
    LoopServiceConfigureDone()
        : LoopServiceBase(),
          onChangeLocalIpUseCase(nullptr),
          onConnectivityRestoredUseCase(nullptr),
          mqttManager(nullptr),
          diagnosticService(nullptr),
          deviceService(nullptr),
          sensorService(nullptr),
          ruleService(nullptr),
          otaUpdateLog(nullptr),
          bootEventLog(nullptr),
          restartEventLog(nullptr) {
        catalogWrites.reserve(ISensorTypeCatalogWriter::MAX_PENDING_WRITES);
    }
    
    ~LoopServiceConfigureDone() override = default;
    
    void executeLoopImplementation() override {
        log("Running loop for CONFIGURED_DONE mode");

        // First, before anything asks for more network work: what came back since last time.
        drainOutboxCompletions();

        // And what came back from flash: a catalog that reached storage lets the sensors bound to
        // its changed types be updated, which the fetch above may have just asked for.
        drainCatalogWrites();

        // Then the boot sequence, on what the two drains above delivered: it decides which of the
        // producers below may act in this iteration.
        if (bootSequencer) {
            bootSequencer->onIteration();
        }

        // The boot's association, asked for on the first iteration: setup waits for no network, so
        // the link is the first thing this loop asks of the network task. Once; a no-op after.
        if (linkSupervisor) {
            linkSupervisor->associateForTheBoot();
        }

        // The other asynchronous half of command handling: the raw bus transactions the
        // acquisition task finished, and any report the outbox had no room for last time. Both are
        // vector swaps and queue pushes - nothing here waits.
        if (commandService) {
            commandService->loop();
        }

        if (deviceService) {
            deviceService->loop();
        }

        if (sensorService) {
            sensorService->loop();
        }

        if (ruleService) {
            ruleService->loop();
        }
        
        if (buttonUseCases) {
            if (buttonUseCases->isButtonHeldForDuration()) {
                log("Long button press detected in CONFIGURED_DONE mode - potential reconfiguration");
                
                moduleConfig->setRequestedModuleMode(ModuleModeEnum::USER_CAN_RE_CONFIGURE);
            }
        }
        
        if (onChangeLocalIpUseCase) {
            log("Checking for IP address changes");
            onChangeLocalIpUseCase->perform();
        }

        if (onConnectivityRestoredUseCase) {
            onConnectivityRestoredUseCase->perform();
        }

        // Next to the connectivity check and for the same reason: both watch the module's offline
        // decision for the moment it clears, and answer it by asking the server for what the
        // outage cost them. This one returns on its first line unless a catalog synchronization
        // finished without succeeding.
        if (syncSensorTypesUseCase) {
            syncSensorTypesUseCase->loop();
        }

        if (mqttManager) {
            log("Processing MQTT messages");
            mqttManager->loop();
        }

        // Cheap on every iteration and expensive almost never: it returns on its first line while
        // the signal is good, and what it does otherwise is one narrow scan every few minutes.
        if (wifiRoamUseCase) {
            wifiRoamUseCase->onControlLoopIteration();
        }
        
        // The decisions first, the report after them: the report reads what they decided.
        if ((linkSupervisor || preventiveRestartUseCase) && arduinoAdapter) {
            if (arduinoAdapter->millis() - lastSupervisionTime >= LinkSupervisor::TICK_INTERVAL_MS) {
                if (linkSupervisor) {
                    linkSupervisor->superviseOnce();
                }
                if (preventiveRestartUseCase) {
                    preventiveRestartUseCase->perform();
                }
                lastSupervisionTime = arduinoAdapter->millis();
            }
        }

        if (diagnosticService && arduinoAdapter) {
            unsigned long currentTime = arduinoAdapter->millis();
            const bool sendAllowed =
                (bootSequencer == nullptr) || bootSequencer->allows(BootGate::DIAGNOSTIC_SEND);
            if (currentTime - lastDiagnosticTime >= DIAGNOSTIC_INTERVAL) {
                log("Running diagnostic service");
                if (sendAllowed) {
                    diagnosticService->performAndSend();
                    if (diagnosticOwed) {
                        // The boot's first STEADY fell on this tick: its send is the one owed.
                        diagnosticOwed = false;
                        bootSequencer->noteTheOwedDiagnosticWasSent(static_cast<uint32_t>(diagnosticOwedSinceMs));
                    }
                } else {
                    // Before the boot's first STEADY the tick takes its snapshot and writes its
                    // counters line as always; only the send waits.
                    diagnosticService->perform();
                    if (!diagnosticOwed) {
                        diagnosticOwed = true;
                        diagnosticOwedSinceMs = currentTime;
                    }
                }
                // Restart the interval from the moment the call came back, not from the moment it
                // was made. Taking `currentTime` here meant that a send lasting longer than the
                // interval satisfied the condition again on the very next iteration, and the
                // diagnostics turned into a back-to-back stream with the loop parked inside it.
                lastDiagnosticTime = arduinoAdapter->millis();
            } else if (diagnosticOwed && sendAllowed) {
                // The boot's first STEADY: the one send its ticks owed, and the interval from here.
                diagnosticOwed = false;
                diagnosticService->performAndSend();
                lastDiagnosticTime = arduinoAdapter->millis();
                bootSequencer->noteTheOwedDiagnosticWasSent(static_cast<uint32_t>(diagnosticOwedSinceMs));
            }
        }

        if (otaUpdateLog && arduinoAdapter) {
            unsigned long currentTime = arduinoAdapter->millis();
            if (currentTime - lastOtaFlushTime >= OTA_FLUSH_INTERVAL) {
                lastOtaFlushTime = currentTime;
                if (otaUpdateLog->hasPending()) {
                    otaUpdateLog->flushToMqttIfAny();
                }
                if (bootEventLog && bootEventLog->hasPending()) {
                    bootEventLog->flushToMqttIfAny();
                }
                if (restartEventLog && restartEventLog->hasPending()) {
                    restartEventLog->flushToMqttIfAny();
                }
            }
        }

        // Last on purpose: an iteration vouches for a freshly booted image only once the whole
        // pass - every service above - actually completed.
        if (confirmRunningFirmwareUseCase) {
            confirmRunningFirmwareUseCase->onControlLoopIteration();
        }
    }

    virtual void setOtaUpdateLog(IOtaUpdateLog* otaLog) {
        this->otaUpdateLog = otaLog;
    }

    virtual void setBootEventLog(IBootEventLog* bootLog) {
        this->bootEventLog = bootLog;
    }

    virtual void setRestartEventLog(IBootEventLog* restartLog) {
        this->restartEventLog = restartLog;
    }

    virtual void setOnChangeLocalIpUseCase(IOnChangeLocalIpUseCase* useCase) {
        this->onChangeLocalIpUseCase = useCase;
    }

    virtual void setOnConnectivityRestoredUseCase(IOnConnectivityRestoredUseCase* useCase) {
        this->onConnectivityRestoredUseCase = useCase;
    }

    virtual void setMqttManager(IMqttManager* manager) {
        this->mqttManager = manager;
    }
    
    virtual void setDiagnosticService(IDiagnosticService* service) {
        this->diagnosticService = service;
    }

    virtual void setOutbox(IOutbox* outboxToUse) {
        this->outbox = outboxToUse;
    }

    virtual void setCommandService(ICommandService* service) {
        this->commandService = service;
    }

    virtual void setSyncSensorTypesUseCase(ISyncSensorTypesUseCase* useCase) {
        this->syncSensorTypesUseCase = useCase;
    }

    virtual void setSensorTypeCatalogWriter(ISensorTypeCatalogWriter* writer) {
        this->sensorTypeCatalogWriter = writer;
    }

    virtual void setRepublishModuleStructureUseCase(IRepublishModuleStructureUseCase* useCase) {
        this->republishModuleStructureUseCase = useCase;
    }

    virtual void setConfirmRunningFirmwareUseCase(IConfirmRunningFirmwareUseCase* useCase) {
        this->confirmRunningFirmwareUseCase = useCase;
    }

    virtual void setWifiRoamUseCase(IWifiRoamUseCase* useCase) {
        this->wifiRoamUseCase = useCase;
    }

    virtual void setBootSequencer(BootSequencer* sequencer) {
        this->bootSequencer = sequencer;
    }

    virtual void setLinkSupervisor(LinkSupervisor* supervisor) {
        this->linkSupervisor = supervisor;
    }

    virtual void setPreventiveRestartUseCase(PreventiveRestartUseCase* useCase) {
        this->preventiveRestartUseCase = useCase;
    }
    
    virtual void setDeviceService(IDeviceService* service) {
        this->deviceService = service;
    }
    
    virtual void setSensorService(ISensorService* service) {
        this->sensorService = service;
    }
    
    virtual void setRuleService(IRuleService* service) {
        this->ruleService = service;
    }
};

#endif // LOOP_SERVICE_CONFIGURE_DONE_H
