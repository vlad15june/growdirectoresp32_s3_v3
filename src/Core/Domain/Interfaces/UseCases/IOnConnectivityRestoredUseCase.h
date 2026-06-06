#ifndef I_ON_CONNECTIVITY_RESTORED_USE_CASE_H
#define I_ON_CONNECTIVITY_RESTORED_USE_CASE_H

class IOnConnectivityRestoredUseCase {
public:
    virtual ~IOnConnectivityRestoredUseCase() = default;

    virtual bool init() = 0;
    virtual void perform() = 0;
};

#endif // I_ON_CONNECTIVITY_RESTORED_USE_CASE_H
