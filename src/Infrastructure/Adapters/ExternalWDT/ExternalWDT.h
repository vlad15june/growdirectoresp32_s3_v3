#ifndef EXTERNAL_WDT_H
#define EXTERNAL_WDT_H

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>

/**
 * @brief External WDT Driver for BD87A28FVM-TR (ROHM, L-Active type)
 * 
 * Hardware Configuration:
 * - WDT IC: BD87A28FVM-TR
 * - Monitor time (twH): 10ms typical (7-20ms range)
 * - Reset time (twL): 3.3ms typical
 * - CLK pulse width: minimum 500ns, recommended 1-5µs
 * - Kick interval: must be < 10ms, recommended 5-7ms
 */

// Hardware configuration
#define EXT_WDT_CLK_PIN         GPIO_NUM_4      // CLK pin (WDT pin 3) - positive edge trigger
#define EXT_WDT_INH_PIN         GPIO_NUM_5      // INH pin (WDT pin 6) - LOW = WDT enabled
#define WDT_KICK_INTERVAL_MS    5               // Must be less than 10ms
#define WDT_PULSE_WIDTH_US      2               // 1-5µs range

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
     * @brief Send single kick pulse to CLK pin (HIGH→delay→LOW)
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

    ExternalWDT(const ExternalWDT&) = delete;
    ExternalWDT& operator=(const ExternalWDT&) = delete;

private:
    ExternalWDT();
    ~ExternalWDT();
    static void wdtTask(void* param);

    bool initialized_;
    bool enabled_;
    bool taskRunning_;
    std::atomic<uint64_t> kickCount_;
    TaskHandle_t taskHandle_;
    esp_timer_handle_t timer_;
    SemaphoreHandle_t mutex_;
};

#endif // EXTERNAL_WDT_H
