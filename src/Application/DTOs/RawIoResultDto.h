#pragma once

#include <string>

class RawIoResultDto
{
public:
    std::string command;
    int channel;
    std::string dataHex;
    bool isError;
    std::string errorMessage;

    RawIoResultDto() : channel(0), isError(false) {}

    RawIoResultDto(const std::string &command, int channel, const std::string &dataHex, bool isError = false, const std::string &errorMessage = "")
        : command(command), channel(channel), dataHex(dataHex), isError(isError), errorMessage(errorMessage) {}
};
