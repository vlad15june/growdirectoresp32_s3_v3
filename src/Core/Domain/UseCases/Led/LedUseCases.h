#ifndef LED_USE_CASES_H
#define LED_USE_CASES_H

#include "Core/Domain/Interfaces/ILedAdapter.h"
#include "Core/Domain/Interfaces/IModuleConfig.h"
#include <memory>

// Use case class for LED behaviors and patterns
class LedUseCases {
private:
    std::shared_ptr<ILedAdapter> ledAdapter;
    
    // Internal state variables
    unsigned long timing;
    int flag_led;
    int key[4];
    bool isBlinking;
    uint8_t moduleType;
    uint8_t moduleSubType;
    uint8_t deviceCount;
    
    // Connection state variables
    int count_online;
    int count_offline;
    int max_count_online;
    int max_count_offline;
    bool *pinValue;  // Array of device states (ON/OFF)
    
    // Constants
    static const uint8_t ON = 1;
    static const uint8_t OFF = 0;

public:
    // Constructor
    LedUseCases(std::shared_ptr<ILedAdapter> adapter, std::shared_ptr<IModuleConfig> moduleConfig, uint8_t deviceCount = 1);
    
    // Destructor
    ~LedUseCases();
    
    // Initialize connection counters
    void setConnectionParameters(int maxOnlineCount, int maxOfflineCount);
    
    // Set device pin values
    void setPinValues(bool* values, uint8_t count);
    
    // Set blinking state
    void setBlinkingState(bool blinking);
    
    // Set online/offline counters
    void setOnlineCounter(int count);
    void setOfflineCounter(int count);
    
    // LED behaviors
    void ledOnLoad();
    void ledOffLoad();
    void ledBlinkGreen2Times();
    void ledBlinkYellow2Times();
    void ledBlinks(bool mqttConnected);
    void ledON(int index);
    void ledOFF(int index);
    
    // Update method to be called in the main loop
    void update(bool mqttConnected);


    void delay(unsigned int ms);

    unsigned long millis();
};

#endif // LED_USE_CASES_H