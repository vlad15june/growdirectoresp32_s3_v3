#ifndef I_RAW_BUS_ACCESS_H
#define I_RAW_BUS_ACCESS_H

#include <vector>
#include <cstdint>
#include <cstddef>

class IRawBusAccess
{
public:
    virtual ~IRawBusAccess() = default;

    virtual bool modbusRequest(int channel, const uint8_t *pduNoCrc, size_t len, bool expectResponse, unsigned long timeoutMs, std::vector<uint8_t> &out) = 0;

    virtual bool serialWrite(int channel, const uint8_t *bytes, size_t len, bool expectResponse, unsigned long timeoutMs, std::vector<uint8_t> &out) = 0;
};

#endif
