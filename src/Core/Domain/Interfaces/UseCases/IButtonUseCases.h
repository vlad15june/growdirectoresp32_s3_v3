#ifndef I_BUTTON_USE_CASES_H
#define I_BUTTON_USE_CASES_H

// Interface for Button use cases
class IButtonUseCases {
public:
    // Virtual destructor for proper cleanup in derived classes
    virtual ~IButtonUseCases() = default;
    
    // Button behavior use cases
    virtual bool checkButtonPressedWhenOn() = 0;
    virtual bool isButtonPressedWhenOn() = 0;
    virtual bool isButtonHeldForDuration() = 0;
    virtual bool isTestModeRequested() = 0;
    virtual bool isFactoryResetRequested() = 0;
    virtual bool isAddModeRequested() = 0;
    virtual void clearSavedButtonState() = 0;
};

#endif // I_BUTTON_USE_CASES_H
