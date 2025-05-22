#pragma once

class IProcessDeviceStateUseCase {
public:
    virtual ~IProcessDeviceStateUseCase() = default;
    virtual void loop() = 0;
};
