#pragma once

#include <vector>
#include <string>
#include <cstdarg>
#include "IDeviceService.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Data/Entities/Device.h"
#include "Core/Domain/Data/Models/ModuleStructure.h"
#include "Core/Domain/Data/Models/IDeviceModel.h"
#include "Application/DTOs/UpdateDto.h"
#include "Infrastructure/Adapters/Time/INTPTimeAdapter.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCase.h"
#include "Core/Domain/Interfaces/UseCases/DeviceControl/IProcessDeviceStateUseCaseFactory.h"
#include "Core/Domain/Interfaces/API/IHybridAPIService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/UseCases/Boot/IBootGates.h"

class DeviceService : public IDeviceService {
private:
    IModuleConfig* moduleConfig;
    ModuleStructure* moduleStructure;
    IDeviceModel* deviceModel;
    INTPTimeAdapter* timeAdapter;
    IArduinoAdapter* arduinoAdapter;
    IProcessDeviceStateUseCaseFactory* deviceStateUseCaseFactory;
    IProcessDeviceStateUseCase* processDeviceStateUseCase;
    IHybridAPIService* hybridApiService;
    IOutbox* outbox = nullptr;
    // The boot sequence's gate in front of the device update; without one an update goes when a
    // state changed or the interval is over.
    IBootGates* bootGates = nullptr;
    // An update that fell due while the gate was shut, and when: see loop().
    bool updateHeld = false;
    unsigned long updateHeldSinceMs = 0;
    ILogger* logger;
    IDebugSystem* debugSystem;

    const unsigned long DEVICE_UPDATE_INTERVAL = 30000;
    unsigned long lastDeviceUpdateTime = 0;

    /**
     * Last seen state of each device, for stamping switchedAt on a change.
     *
     * Kept in device-list order so the every-iteration pass compares by index without building
     * a lookup key: the loop runs thousands of times a second, and a std::string key per device
     * per iteration would be a steady stream of small heap blocks. Entries for devices no longer in
     * the list are kept at the tail, so a device that reappears is still compared against the
     * state it disappeared with. Growth is bounded by the number of distinct device UIDs ever
     * seen - the module's channel count in practice, since devices are rebuilt with the same
     * UIDs on reconfiguration.
     */
    struct TrackedDeviceState {
        std::string uid;
        DeviceStateEnum state;
    };
    std::vector<TrackedDeviceState> prevDeviceStates;

    /**
     * The device state the server was last given, field by field.
     *
     * This replaces a signature string concatenated from the same fields on every iteration:
     * comparing a snapshot costs no allocation, building a string cost a dozen. The fields are
     * exactly the ones the signature carried - a change in any of them means the server's
     * picture is stale and an update goes out ahead of the periodic interval.
     *
     * Captured only when an update was actually accepted for sending, so a refused submission
     * leaves the change still visible and the next iteration retries it.
     */
    struct ReportedDeviceState {
        std::string uid;
        DeviceStateEnum state;
        bool isAuto;
        bool isManual;
        bool workByRule;
        bool workManual;
        bool needToSwitchBack;
        long commandId;
        std::string ruleUid;
        std::string switchedAt;
    };
    std::vector<ReportedDeviceState> reportedDeviceStates;
    bool reportedStatesValid = false;

    // The update this service is waiting to hear about, and the devices whose reason for
    // switching on that update carried.
    //
    // UIDs rather than pointers, for two reasons: a device appearing or disappearing between
    // submission and completion cannot turn this into a dangling read, and a device that only
    // started owing a report *after* this update was submitted is not cleared by it.
    //
    // What this does NOT distinguish is a device whose rule context changed to a different one
    // while the update travelled - the comparison is by UID only. In practice the older
    // completion is already void by then, because the reported-state snapshot includes
    // workByRule and ruleUid, so a changed reason reads as a state change, and the next
    // iteration submits a new update whose ticket replaces this one.
    uint32_t pendingUpdateTicket = 0;
    std::vector<std::string> pendingReportedRuleUids;

    UpdateDto createDeviceUpdateDto();
    bool sendDeviceUpdate();
    bool deviceStateChangedSinceReported() const;
    void captureReportedDeviceStates();
    void refreshSwitchedAtForChangedDevices();
    void stampSwitchedAt(Device* device);
    void clearReportedRuleContext();
    void clearReportedRuleContext(const std::vector<std::string>& deviceUids);
    std::vector<std::string> devicesAwaitingRuleReport() const;

    void log(const char* format, ...)
    {
        if (logger != nullptr && debugSystem != nullptr && debugSystem->isFeatureEnabled(DebugFeatures::CONTROL_DEBUG))
        {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
    
            logger->println(DebugFeatures::CONTROL_DEBUG, buffer);
        }
    }
    
public:
    DeviceService(IModuleConfig* moduleConfig, 
                  ModuleStructure* moduleStructure,
                  IDeviceModel* deviceModel,
                  INTPTimeAdapter* timeAdapter,
                  IArduinoAdapter* arduinoAdapter,
                  IProcessDeviceStateUseCaseFactory* deviceStateUseCaseFactory,
                  IHybridAPIService* hybridApiService = nullptr,
                  ILogger* logger = nullptr,
                  IDebugSystem* debugSystem = nullptr);

    ~DeviceService();

    /**
     * @brief Send device updates through the outbox instead of inline.
     *
     * With an outbox set, loop() hands the update over and the reason a device switched on is
     * erased only when a completion says the server actually took it. Without one this service
     * keeps its previous synchronous behaviour, which is what the tests predating the outbox
     * exercise.
     */
    void setOutbox(IOutbox* outboxToUse) { outbox = outboxToUse; }

    /**
     * @brief Put the device update behind the boot sequence.
     *
     * While the gate is shut nothing is handed over and nothing is recorded as reported, so the
     * change that is owed - the first picture of the relays above all - stays owed and goes out on
     * the iteration the gate opens. The relays themselves are driven regardless.
     */
    void setBootGates(IBootGates* gates) { bootGates = gates; }

    bool init() override;
    void loop() override;
    void onOutboxCompletion(const OutboxCompletion& completion) override;
};
