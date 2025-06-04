#pragma once

#include <string>

class CommandResultDto
{
public:
    std::string status;
    std::string command;
    std::string message;

    CommandResultDto() = default;

    CommandResultDto(const std::string &status, const std::string &command, const std::string &message = "")
        : status(status), command(command), message(message) {}
};
