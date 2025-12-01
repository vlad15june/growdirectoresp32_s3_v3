#include "ExternalWDT.h"
#include <esp_log.h>
#include <esp_rom_sys.h>

static const char* TAG = "EXT_WDT";

// Singleton instance
ExternalWDT& ExternalWDT::getInstance()
{
    static ExternalWDT instance;
    return instance;
}

// Private constructor
ExternalWDT::ExternalWDT()
    : initialized_(false)
    , enabled_(false)
    , taskRunning_(false)
    , kickCount_(0)
    , taskHandle_(nullptr)
    , timer_(nullptr)
    , mutex_(nullptr)
{
}

// Destructor
ExternalWDT::~ExternalWDT()
{
    stopTask();
    
    if (mutex_ != nullptr)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

esp_err_t ExternalWDT::init()
{
    if (initialized_)
    {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing External WDT (BD87A28FVM-TR)");
    ESP_LOGI(TAG, "CLK Pin: GPIO%d, INH Pin: GPIO%d", EXT_WDT_CLK_PIN, EXT_WDT_INH_PIN);

    // Create mutex for thread-safe operations
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Configure CLK pin
    gpio_config_t clk_conf = {};
    clk_conf.pin_bit_mask = (1ULL << EXT_WDT_CLK_PIN);
    clk_conf.mode = GPIO_MODE_OUTPUT;
    clk_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    clk_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    clk_conf.intr_type = GPIO_INTR_DISABLE;
    
    esp_err_t ret = gpio_config(&clk_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure CLK pin: %s", esp_err_to_name(ret));
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return ret;
    }

    // Configure INH pin
    gpio_config_t inh_conf = {};
    inh_conf.pin_bit_mask = (1ULL << EXT_WDT_INH_PIN);
    inh_conf.mode = GPIO_MODE_OUTPUT;
    inh_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    inh_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    inh_conf.intr_type = GPIO_INTR_DISABLE;
    
    ret = gpio_config(&inh_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure INH pin: %s", esp_err_to_name(ret));
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return ret;
    }

    // Set initial states: CLK=LOW, INH=HIGH (disabled)
    gpio_set_level(EXT_WDT_CLK_PIN, 0);
    gpio_set_level(EXT_WDT_INH_PIN, 1);

    initialized_ = true;
    ESP_LOGI(TAG, "External WDT initialized successfully");
    
    return ESP_OK;
}

esp_err_t ExternalWDT::enable()
{
    if (!initialized_)
    {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (enabled_)
    {
        ESP_LOGW(TAG, "Already enabled");
        return ESP_OK;
    }

    // Set INH=LOW to enable WDT monitoring (BD87A series: L-Active)
    gpio_set_level(EXT_WDT_INH_PIN, 0);
    enabled_ = true;

    ESP_LOGI(TAG, "External WDT enabled");
    return ESP_OK;
}

esp_err_t ExternalWDT::disable()
{
    if (!initialized_)
    {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!enabled_)
    {
        ESP_LOGW(TAG, "Already disabled");
        return ESP_OK;
    }

    // Set INH=HIGH to disable WDT monitoring
    gpio_set_level(EXT_WDT_INH_PIN, 1);
    enabled_ = false;

    ESP_LOGI(TAG, "External WDT disabled");
    return ESP_OK;
}

esp_err_t ExternalWDT::kick()
{
    if (!initialized_)
    {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Thread-safe kick operation
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to acquire mutex for kick");
        return ESP_ERR_TIMEOUT;
    }

    // Generate positive pulse: CLK=HIGH → delay → CLK=LOW
    gpio_set_level(EXT_WDT_CLK_PIN, 1);
    esp_rom_delay_us(WDT_PULSE_WIDTH_US);
    gpio_set_level(EXT_WDT_CLK_PIN, 0);

    // Increment kick counter
    kickCount_++;

    xSemaphoreGive(mutex_);

    return ESP_OK;
}

void ExternalWDT::wdtTask(void* param)
{
    ExternalWDT* wdt = static_cast<ExternalWDT*>(param);
    ESP_LOGI(TAG, "WDT kick task started");

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(WDT_KICK_INTERVAL_MS);

    while (wdt->taskRunning_)
    {
        // Kick the WDT
        esp_err_t ret = wdt->kick();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to kick WDT: %s", esp_err_to_name(ret));
        }

        // Wait for next period using absolute timing
        vTaskDelayUntil(&lastWakeTime, period);
    }

    ESP_LOGI(TAG, "WDT kick task stopped");
    vTaskDelete(nullptr);
}

esp_err_t ExternalWDT::startTask()
{
    if (!initialized_)
    {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (taskRunning_)
    {
        ESP_LOGW(TAG, "Task already running");
        return ESP_OK;
    }

    taskRunning_ = true;

    // Create high-priority task for WDT kicking
    // Priority: configMAX_PRIORITIES - 2 (very high, but below critical system tasks)
    BaseType_t ret = xTaskCreate(
        wdtTask,
        "ext_wdt_task",
        2048,                               // Stack size
        this,                               // Parameter
        configMAX_PRIORITIES - 2,          // High priority
        &taskHandle_
    );

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create WDT task");
        taskRunning_ = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "WDT kick task started (interval: %dms, pulse: %dus)", 
             WDT_KICK_INTERVAL_MS, WDT_PULSE_WIDTH_US);
    
    return ESP_OK;
}

esp_err_t ExternalWDT::stopTask()
{
    if (!taskRunning_)
    {
        return ESP_OK;
    }

    taskRunning_ = false;

    // Wait for task to finish
    if (taskHandle_ != nullptr)
    {
        vTaskDelay(pdMS_TO_TICKS(WDT_KICK_INTERVAL_MS * 2));
        taskHandle_ = nullptr;
    }

    ESP_LOGI(TAG, "WDT kick task stopped");
    return ESP_OK;
}

uint64_t ExternalWDT::getKickCount() const
{
    return kickCount_.load();
}

bool ExternalWDT::isEnabled() const
{
    return enabled_;
}

bool ExternalWDT::isTaskRunning() const
{
    return taskRunning_;
}
