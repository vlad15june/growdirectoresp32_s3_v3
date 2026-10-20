#ifndef BUZZER_CONTROLLER_H
#define BUZZER_CONTROLLER_H

#include "Core/Domain/Interfaces/ILogger.h"

class BuzzerController {
private:
    ILogger* logger = nullptr;
    static constexpr unsigned int BEEP_PERIOD_MS = 100;
public:
    BuzzerController(ILogger* logger = nullptr) : logger(logger) {}
    void beep(int times)
    {
        if (logger != nullptr)
        {
            logger->printf("BUZZER: beep x%d", times);
        }
    }
};

#endif // BUZZER_CONTROLLER_H