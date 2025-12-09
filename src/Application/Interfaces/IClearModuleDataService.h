#ifndef I_CLEAR_MODULE_DATA_SERVICE_H
#define I_CLEAR_MODULE_DATA_SERVICE_H

/**
 * @brief Interface for clearing all module data from SPIFFS storage
 */
class IClearModuleDataService {
public:
    virtual ~IClearModuleDataService() = default;
    
    /**
     * @brief Clear all module data files from SPIFFS
     * @return true if all files were successfully cleared, false otherwise
     */
    virtual bool clearAllData() = 0;
};

#endif // I_CLEAR_MODULE_DATA_SERVICE_H
