#pragma once

#include <stdexcept>
#include <string>
#include <utility>

class RecoverableException : public std::runtime_error
{
public:
    explicit RecoverableException(std::string message, bool disconnect = false)
        : std::runtime_error(std::move(message)), shouldDisconnect(disconnect) {}
    bool shouldDisconnect;
};

// A broken transport ends the session; a malformed individual reply need not.
class ConnectionClosedException : public RecoverableException
{
public:
    explicit ConnectionClosedException(std::string message)
        : RecoverableException(std::move(message), true) {}
};
