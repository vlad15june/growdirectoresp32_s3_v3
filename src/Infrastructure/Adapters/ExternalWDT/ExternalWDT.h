#ifndef EXTERNAL_WDT_H
#define EXTERNAL_WDT_H

#include "LoopLivenessGate.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>

#define EXT_WDT_WDI_PIN         GPIO_NUM_34
#define WDT_KICK_INTERVAL_MS    100

class ExternalWDT
{
public:
    /**
     * @brief Get singleton instance
     * @return Reference to ExternalWDT instance
     */
    static ExternalWDT& getInstance();

    /**
     * @brief Initialize WDT GPIOs (CLK and INH as outputs)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();

    /**
     * @brief Enable WDT monitoring (set INH=LOW)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t enable();

    /**
     * @brief Disable WDT monitoring (set INH=HIGH)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t disable();

    /**
     * @brief Toggle WDI pin level (single kick)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t kick();

    /**
     * @brief Start FreeRTOS task that kicks WDT periodically
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t startTask();

    /**
     * @brief Stop the periodic kick task
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t stopTask();

    /**
     * @brief Get total number of kicks performed
     * @return Kick counter value
     */
    uint64_t getKickCount() const;

    /**
     * @brief Check if WDT is enabled
     * @return true if enabled, false otherwise
     */
    bool isEnabled() const;

    /**
     * @brief Check if kick task is running
     * @return true if running, false otherwise
     */
    bool isTaskRunning() const;

    /**
     * @brief Make the kicks conditional on the control loop's liveness.
     *
     * The kick task keeps its own cadence (the chip's period is a hardware property), but once
     * the gate is configured it kicks only while the loop has beaten within stallLimitMs -
     * see LoopLivenessGate. Never configured means unconditional kicks.
     */
    void configureLoopGate(uint32_t stallLimitMs);

    /** @brief The control loop reports one completed iteration. Called from loop() and nowhere else. */
    void noteLoopAlive();

    /** @brief Kicks withheld because the loop had been silent past the stall limit. */
    uint64_t getStarvedKickCount() const;

    ExternalWDT(const ExternalWDT&) = delete;
    ExternalWDT& operator=(const ExternalWDT&) = delete;

private:
    ExternalWDT();
    ~ExternalWDT();
    static void wdtTask(void* param);

    bool initialized_;
    bool enabled_;
    bool taskRunning_;
    bool level_;
    LoopLivenessGate loopGate_;
    std::atomic<uint64_t> kickCount_;
    std::atomic<uint64_t> starvedKickCount_;
    TaskHandle_t taskHandle_;
    esp_timer_handle_t timer_;
    SemaphoreHandle_t mutex_;
};

#endif // EXTERNAL_WDT_H
