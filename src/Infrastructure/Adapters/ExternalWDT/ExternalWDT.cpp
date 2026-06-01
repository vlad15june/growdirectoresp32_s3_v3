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
    , level_(false)
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

    ESP_LOGI(TAG, "Initializing External WDT");
    ESP_LOGI(TAG, "WDI Pin: GPIO%d", EXT_WDT_WDI_PIN);

    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t wdi_conf = {};
    wdi_conf.pin_bit_mask = (1ULL << EXT_WDT_WDI_PIN);
    wdi_conf.mode = GPIO_MODE_OUTPUT;
    wdi_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    wdi_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    wdi_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t ret = gpio_config(&wdi_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure WDI pin: %s", esp_err_to_name(ret));
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return ret;
    }

    gpio_set_level(EXT_WDT_WDI_PIN, 1);
    level_ = true;

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

    level_ = !level_;
    gpio_set_level(EXT_WDT_WDI_PIN, level_ ? 1 : 0);

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

    ESP_LOGI(TAG, "WDT kick task started (interval: %dms)", WDT_KICK_INTERVAL_MS);
    
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
