#ifndef I_MODULE_MODE_SERVICE_H
#define I_MODULE_MODE_SERVICE_H

class IModuleModeService {
public:
    virtual ~IModuleModeService() = default;
    
    /**
     * Main loop for handling module mode transitions
     * Should be called regularly from main application loop
     */
    virtual void loop() = 0;
};

#endif // I_MODULE_MODE_SERVICE_H
