#ifndef I_LOOP_SERVICE_H
#define I_LOOP_SERVICE_H

class ILoopService {
public:
    virtual ~ILoopService() = default;
    virtual void loop() = 0;
};

#endif // I_LOOP_SERVICE_H
