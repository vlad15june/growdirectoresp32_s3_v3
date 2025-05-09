#ifndef I_ON_CHANGE_LOCAL_IP_USE_CASE_H
#define I_ON_CHANGE_LOCAL_IP_USE_CASE_H

class IOnChangeLocalIpUseCase {
public:
    virtual ~IOnChangeLocalIpUseCase() = default;
    
    // Perform the local IP change operation
    virtual void perform() = 0;
};

#endif // I_ON_CHANGE_LOCAL_IP_USE_CASE_H
