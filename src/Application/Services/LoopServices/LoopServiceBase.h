#ifndef LOOP_SERVICE_BASE_H
#define LOOP_SERVICE_BASE_H

#include "../../Interfaces/LoopServices/ILoopService.h"
#include "Core/Domain/Interfaces/UseCases/ILedUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IButtonUseCases.h"
#include "Core/Domain/Interfaces/UseCases/IOnChangeModuleModeUseCase.h"
#include "Application/Services/BackgroundActs.h"
#include "Core/Domain/Diagnostics/BackgroundActBoard.h"
#ifdef GD_HEAP_FORENSICS
#include "Core/Domain/Diagnostics/RxFrameClassCounter.h"
#endif
#include "Core/Domain/Diagnostics/HeapWatermarkTracer.h"
#include "Core/Domain/Diagnostics/LoopStepMonitor.h"
#include "Core/Domain/Interfaces/IClearModuleDataService.h"
#include "Core/Domain/Interfaces/ILogger.h"
#include "Core/Domain/Interfaces/IDebugSystem.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include "Core/Domain/Interfaces/Adapters/IArduinoAdapter.h"
#include "Core/Domain/Interfaces/BoardTesting/IBoardTest.h"
#include "Application/Interfaces/WebServices/IWebServerLooper.h"
#include <stdarg.h>
#include <cstdio>

class LoopServiceBase : public ILoopService {
protected:
    ILedUseCases* ledUseCases;
    IButtonUseCases* buttonUseCases;
    IOnChangeModuleModeUseCase* onChangeModuleModeUseCase;
    ILogger* logger;
    IDebugSystem* debugSystem;
    IWebServerLooper* webServer;
    IModuleConfig* moduleConfig;
    IArduinoAdapter* arduinoAdapter;
    IBoardTest* boardTest;
    IClearModuleDataService* clearModuleDataService;

    // One reading of the heap watermark per step, so that a dip too short for the thirty-second
    // diagnostic snapshot still gets a moment and a neighbourhood in the log. Held here rather
    // than process-wide: the task that samples it is the task that prints it, and a loop service
    // replaced on a mode change re-baselines on a watermark that only ever falls, so nothing is
    // said twice.
    HeapWatermarkTracer heapWatermarkTracer;

    // What the background tasks say they are doing; the reading that finds a new low copies it, so
    // the line can name the work the low fell on. Optional: without one the line is as it was.
    BackgroundActBoard* backgroundActBoard = nullptr;

#ifdef GD_HEAP_FORENSICS
    // What the radio has handed to the network stack, by class, counted by the radio adapter; the
    // reading that walks the heap copies the pair, so the two lines of an episode say what
    // arrived between them. Optional: without one the lines are as they were.
    RxFrameClassCounter* rxFrameClasses = nullptr;
#endif

    LoopServiceBase(
        ILedUseCases* ledUseCases = nullptr,
        IButtonUseCases* buttonUseCases = nullptr,
        IOnChangeModuleModeUseCase* moduleModeUseCase = nullptr,
        ILogger* logger = nullptr,
        IDebugSystem* debugSystem = nullptr,
        IWebServerLooper* webServer = nullptr,
        IModuleConfig* moduleConfig = nullptr,
        IArduinoAdapter* arduinoAdapter = nullptr,
        IBoardTest* boardTest = nullptr,
        IClearModuleDataService* clearModuleDataService = nullptr
    ) : ledUseCases(ledUseCases),
        buttonUseCases(buttonUseCases),
        onChangeModuleModeUseCase(moduleModeUseCase),
        logger(logger),
        debugSystem(debugSystem),
        webServer(webServer),
        moduleConfig(moduleConfig),
        arduinoAdapter(arduinoAdapter),
        boardTest(boardTest),
        clearModuleDataService(clearModuleDataService) {}
    
    void log(const char* format, ...) {
        if (logger != nullptr && debugSystem != nullptr && 
            debugSystem->isFeatureEnabled(DebugFeatures::LOOP_SERVICE_DEBUG)) {
            char buffer[128];
            va_list args;
            va_start(args, format);
            vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            
            logger->println(DebugFeatures::LOOP_SERVICE_DEBUG, buffer);
        }
    }

    /**
     * Write the line an earlier step's reading of the heap watermark asked for, if it asked.
     *
     * Called at the top of a step, which is the nearest safe point: the step that took the memory
     * has returned, so the line is written against the heap the module settles at rather than
     * against its floor, and it still lands beside that step's own lines. Serial only and
     * formatted into a stack buffer - the MQTT channel would build the topic and the payload out
     * of the very heap the line is about.
     *
     * The report is taken before the channel is checked: a line the log cannot carry is lost like
     * any other line, and holding it back would date the dip to whenever the channel came back.
     */
    void reportHeapWatermarkDrop() {
        HeapWatermarkTracer::Report report;
        if (!heapWatermarkTracer.takeReport(report)) {
            return;
        }

        if (logger == nullptr || debugSystem == nullptr ||
            !debugSystem->isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG)) {
            return;
        }

        // 52 characters at the widest - the fixed text plus two ten-digit numbers - so the line
        // is never truncated into a number that reads as a smaller one; and room behind them for
        // the widest account of the background tasks and for " free=<ten digits> step=<ten
        // digits>ms", 34 characters.
        char line[64 + BackgroundActs::CONTEXT_BYTES + 40];
        int written = snprintf(line, sizeof(line), "HEAP_LOW: new watermark %lu (was %lu)",
                               (unsigned long)report.watermarkBytes,
                               (unsigned long)report.previousBytes);

#ifdef GD_HEAP_FORENSICS
        // An episode low opens differently and continues the same way: the free heap the reading
        // found, and the since-boot watermark as it found it. 55 characters at the widest.
        if (report.episode) {
            written = snprintf(line, sizeof(line), "HEAP_LOW: episode low %lu (watermark %lu)",
                               (unsigned long)report.watermarkBytes,
                               (unsigned long)report.previousBytes);
        }
#endif

        // Appended, so that whatever reads the front of the line keeps reading it. The account is
        // of the moment the low was found - the report carries it - not of this moment, a step
        // later, when the work that took the memory may be over and replaced.
        if (report.hasActs && written > 0 && (size_t)written < sizeof(line))
        {
            written += (int)BackgroundActs::formatContext(report.acts, line + written,
                                                          sizeof(line) - (size_t)written);
        }

        // Last, the two figures that tell a dip inside the step from memory held past its end:
        // free heap back at its level with a long step is the step's own work; free heap still at
        // the floor is somebody holding on, and the line about its release follows.
        if (report.hasMoment && written > 0 && (size_t)written < sizeof(line))
        {
            snprintf(line + written, sizeof(line) - (size_t)written, " free=%lu step=%lums",
                     (unsigned long)report.freeBytes, (unsigned long)report.stepMs);
        }

        logger->println(DebugFeatures::DIAGNOSTIC_DEBUG, line, MqttLoggerMode::SerialOnly);

#ifdef GD_HEAP_FORENSICS
        if (report.hasHolders) {
            reportHeapHolders("HEAP_LOW: held by", report.holders);
        }
#endif
    }

#ifdef GD_HEAP_FORENSICS
    bool heapTraceIsHeard() const {
        return logger != nullptr && debugSystem != nullptr &&
               debugSystem->isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG);
    }

    /**
     * Ask the platform who holds the heap, and time the asking: the walk locks the heap and the
     * list of tasks locks the kernel for as long as each lasts, and how long that is on a given
     * module is itself a figure the line carries - each on its own, because what to do about a
     * long one depends on which. The ledger is static - a kilobyte the control task's stack is
     * not asked for at the moment the heap is at its floor - and scratch: what outlives the call
     * is the cut-down copy.
     */
    bool readTheHeapHolders(HeapHolders& out) {
        static HeapTaskLedger ledger;

        const unsigned long startedAtUs = arduinoAdapter->micros();
        if (!arduinoAdapter->readHeapOwners(ledger)) {
            return false;
        }
        const unsigned long walkedAtUs = arduinoAdapter->micros();
        arduinoAdapter->readLiveTasks(ledger);
        const unsigned long listedAtUs = arduinoAdapter->micros();

        // Unsigned subtraction, so a micros() wraparound inside a call still yields the span.
        HeapHolders::reduce(ledger, (uint32_t)(walkedAtUs - startedAtUs),
                            (uint32_t)(listedAtUs - walkedAtUs), out);

        // Taken with the walk, so the pair dates from the reading and not from the print.
        if (rxFrameClasses != nullptr) {
            out.rxFramesKnown = true;
            out.rxUnicastFrames = rxFrameClasses->unicast();
            out.rxGroupFrames = rxFrameClasses->group();
        }
        return true;
    }

    /** One line of holders, under the same rules as every line of this trace. */
    void reportHeapHolders(const char* opening, const HeapHolders& holders) {
        if (!heapTraceIsHeard()) {
            return;
        }

        char line[HeapHolders::LINE_BYTES];
        const int written = snprintf(line, sizeof(line), "%s", opening);
        if (written > 0 && (size_t)written < sizeof(line)) {
            HeapHolders::format(holders, line + written, sizeof(line) - (size_t)written);
        }

        logger->println(DebugFeatures::DIAGNOSTIC_DEBUG, line, MqttLoggerMode::SerialOnly);
    }

    /**
     * The heap-forensics build's part of the reading at the end of a step: one look at the free heap for
     * the episode trace, and - only when that look or the reading before it found a low that is
     * due an account - the walk over the heap, taken here, while the memory is still held. The
     * line it fills goes out at the top of the next step with the line about the low.
     */
    void sampleTheHeapEpisode(uint32_t stepMs) {
        heapWatermarkTracer.sampleFreeHeap(arduinoAdapter->getFreeHeap(), stepMs,
                                           backgroundActBoard);

        if (heapWatermarkTracer.holdersAreDue()) {
            // A trace nobody hears is not worth locking the heap for.
            HeapHolders holders;
            const bool read = heapTraceIsHeard() && readTheHeapHolders(holders);
            heapWatermarkTracer.noteTheHolders(read ? &holders : nullptr);
        }
    }
#endif

    /**
     * Write how a watch for the memory's return ended: released, and after how long - or still
     * held when the limit ran out. One line per episode, under the same rules as the line about
     * the low: Serial only, a stack buffer. The second case is written with the heap still down
     * by definition, which is exactly what those rules are for.
     */
    void reportHeapRelease(const HeapWatermarkTracer::Release& release) {
        if (logger == nullptr || debugSystem == nullptr ||
            !debugSystem->isFeatureEnabled(DebugFeatures::DIAGNOSTIC_DEBUG)) {
            return;
        }

        // 58 characters at the widest: the longer of the two texts plus two ten-digit numbers.
        char line[64];
        snprintf(line, sizeof(line),
                 release.released ? "HEAP_LOW: released after %lu ms (free %lu)"
                                  : "HEAP_LOW: still held after %lu ms (free %lu)",
                 (unsigned long)release.afterMs, (unsigned long)release.freeBytes);

        logger->println(DebugFeatures::DIAGNOSTIC_DEBUG, line, MqttLoggerMode::SerialOnly);
    }

    /**
     * Everything the heap trace asks of a step beyond its one reading, in one place the ordinary
     * step never enters: the line a reading asked for and, while a watch is open, one look at the
     * free heap.
     */
    void attendToTheHeapTrace(uint32_t nowMs) {
        reportHeapWatermarkDrop();

        if (heapWatermarkTracer.isWatchingRelease() && arduinoAdapter != nullptr) {
            HeapWatermarkTracer::Release release;
            if (heapWatermarkTracer.pollRelease(arduinoAdapter->getFreeHeap(), nowMs, release)) {
                reportHeapRelease(release);

#ifdef GD_HEAP_FORENSICS
                // After a release, the other half of the account taken at the low - for an
                // episode that had one: what the tasks hold at rest, and the difference between
                // the two lines is who took the memory. After a watch that ran out, who still
                // holds the level a minute on - for every episode, accounted for at its low or
                // not: this is the account the watch exists for, and a watch runs out once a
                // minute at most, so no ration is needed to keep it rare.
                HeapHolders holders;
                if ((release.walked || !release.released) && heapTraceIsHeard() &&
                    readTheHeapHolders(holders)) {
                    reportHeapHolders(release.released ? "HEAP_LOW: after the release held by"
                                                       : "HEAP_LOW: held by",
                                      holders);
                }
#endif
            }
        }
    }

public:
    virtual ~LoopServiceBase() = default;
    
    /**
     * One control-loop step, and the measurement of how long it took.
     *
     * main.cpp calls this back to back, so the span between these two timestamps is the step: the
     * time in which the module answers no button, evaluates no rule and does not blink. The number
     * goes to LoopStepMonitor, which the diagnostic line reports as max_loop_step_ms - measured
     * here rather than in main.cpp because main.cpp is not in the host build, and a bound that no
     * test can observe is a bound nobody checks.
     */
    void loop() override {
        const unsigned long stepStartedAtMs = arduinoAdapter ? arduinoAdapter->millis() : 0;

        // Inside the measurement and before the step's work: whatever the previous step's reading
        // of the heap watermark found, said now that the heap is back where it settles - and,
        // while a low is still being held, one look at whether it has been let go. One flag
        // covers both, so a settled module tests a byte here and goes on.
        if (heapWatermarkTracer.needsAttention()) {
            attendToTheHeapTrace((uint32_t)stepStartedAtMs);
        }

        if (buttonUseCases) {
            buttonUseCases->loop();
        }

        if (onChangeModuleModeUseCase) {
            onChangeModuleModeUseCase->loop();
        }

        executeLoopImplementation();

        if (ledUseCases) {
            ledUseCases->loop();
        }

        if (arduinoAdapter) {
            // Read at the end of the step, so that a dip anywhere inside it is caught: the
            // platform's watermark is the minimum free heap since boot, not an instantaneous
            // level, and it keeps the low whether the dip lasted a millisecond or the whole step.
            // The reading compares and returns; the line it may have asked for is written at the
            // top of the next step.
            //
            // The reading is dated, because the span between two readings is the span a new low
            // can have happened in, and only work that was alive inside it is named with the low.
            // Dated before it is taken, not after: a low this reading just misses happened after
            // this date, so whatever made it ended after it too - inside the next reading's
            // window. The other order leaves a gap between the two calls in which work on the
            // other core can end unseen by either reading.
            const unsigned long stepEndedAtMs = arduinoAdapter->millis();
            const uint32_t minFreeHeapBytes = arduinoAdapter->getMinFreeHeap();
            if (heapWatermarkTracer.sample(minFreeHeapBytes, (uint32_t)stepEndedAtMs,
                                           backgroundActBoard)) {
                // Only a reading that found a low asks for the free heap and the step's length.
                heapWatermarkTracer.noteTheMoment(arduinoAdapter->getFreeHeap(),
                                                  (uint32_t)(stepEndedAtMs - stepStartedAtMs));
            }

#ifdef GD_HEAP_FORENSICS
            sampleTheHeapEpisode((uint32_t)(stepEndedAtMs - stepStartedAtMs));
#endif

            // Unsigned subtraction, so a millis() wraparound inside a step still yields the span.
            LoopStepMonitor::record((uint32_t)(stepEndedAtMs - stepStartedAtMs));
        }
    }
    
    virtual void executeLoopImplementation() = 0;
    
    virtual void setLedUseCases(ILedUseCases* ledUC) {
        this->ledUseCases = ledUC;
    }
    
    virtual void setButtonUseCases(IButtonUseCases* buttonUC) {
        this->buttonUseCases = buttonUC;
    }
    
    virtual void setOnChangeModuleModeUseCase(IOnChangeModuleModeUseCase* modeUC) {
        this->onChangeModuleModeUseCase = modeUC;
    }
    
    // Method to set logging components
    virtual void setLogging(ILogger* logger, IDebugSystem* debugSystem) {
        this->logger = logger;
        this->debugSystem = debugSystem;
    }
    
    // Method to set web server
    virtual void setWebServer(IWebServerLooper* server) {
        this->webServer = server;
    }
    
    // Method to set module config
    virtual void setModuleConfig(IModuleConfig* config) {
        this->moduleConfig = config;
    }
    
    // Method to set Arduino adapter
    virtual void setArduinoAdapter(IArduinoAdapter* adapter) {
        this->arduinoAdapter = adapter;
    }
    
    // Method to set the board the background tasks keep their current acts on
    virtual void setBackgroundActBoard(BackgroundActBoard* board) {
        this->backgroundActBoard = board;
    }

#ifdef GD_HEAP_FORENSICS
    virtual void setRxFrameClasses(RxFrameClassCounter* counter) {
        this->rxFrameClasses = counter;
    }
#endif

    // Method to set board test
    virtual void setBoardTest(IBoardTest* test) {
        this->boardTest = test;
    }
    
    // Method to set clear module data service
    virtual void setClearModuleDataService(IClearModuleDataService* service) {
        this->clearModuleDataService = service;
    }
};

#endif // LOOP_SERVICE_BASE_H
