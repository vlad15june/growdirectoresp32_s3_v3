#ifndef I_ON_CHANGE_MODULE_MODE_USE_CASE_H
#define I_ON_CHANGE_MODULE_MODE_USE_CASE_H

class IOnChangeModuleModeUseCase {
public:
    virtual ~IOnChangeModuleModeUseCase() = default;
    
    /**
     * Main loop for handling module mode transitions
     * Should be called regularly from main application loop
     */
    virtual void loop() = 0;
};

#endif // I_ON_CHANGE_MODULE_MODE_USE_CASE_H
