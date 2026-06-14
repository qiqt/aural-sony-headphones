#pragma once

#include <winsock2.h>
#include "Exceptions.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>

// Operations hold shared ownership of the channel. Shutdown interrupts I/O;
// the handle is released only after every operation has returned.
class SocketChannel
{
public:
    explicit SocketChannel(SOCKET socket) : _socket(socket) {}
    SocketChannel(const SocketChannel&) = delete;
    SocketChannel& operator=(const SocketChannel&) = delete;
    ~SocketChannel() { ::closesocket(_socket); }

    SOCKET handle() const noexcept { return _socket; }
    bool connected() const noexcept { return _connected.load(); }

    void connectedSuccessfully()
    {
        std::lock_guard guard(_stateMutex);
        if (_stopped) throw ConnectionClosedException("Connection was cancelled");
        u_long nonblocking = 1;
        if (::ioctlsocket(_socket, FIONBIO, &nonblocking) == SOCKET_ERROR)
            throw ConnectionClosedException("Cannot configure nonblocking socket I/O");
        _connected = true;
    }

    void stop() noexcept
    {
        std::lock_guard guard(_stateMutex);
        _connected = false;
        if (!_stopped)
        {
            _stopped = true;
            ::shutdown(_socket, SD_BOTH);
        }
    }

    int write(const char* bytes, std::size_t size)
    {
        if (!connected()) throw ConnectionClosedException("Headphones are disconnected");
        if (size == 0) return 0;
        return transfer([&] { return ::send(_socket, bytes, count(size), 0); }, true);
    }

    int read(char* bytes, std::size_t size)
    {
        if (!connected()) throw ConnectionClosedException("Headphones are disconnected");
        if (size == 0) return 0;
        return transfer([&] { return ::recv(_socket, bytes, count(size), 0); }, false);
    }

private:
    static int count(std::size_t size)
    {
        return static_cast<int>((std::min)(size, static_cast<std::size_t>((std::numeric_limits<int>::max)())));
    }

    template<class Operation>
    int transfer(Operation operation, bool writing)
    {
        while (connected())
        {
            const int transferred = operation();
            if (transferred >= 0)
            {
                if (transferred == 0) stop();
                return transferred;
            }
            auto error = ::WSAGetLastError();
            if (error == WSAEWOULDBLOCK)
            {
                fd_set ready;
                FD_ZERO(&ready);
                FD_SET(_socket, &ready);
                timeval timeout{0, 50000};
                const int result = ::select(0, writing ? nullptr : &ready,
                    writing ? &ready : nullptr, nullptr, &timeout);
                if (result != SOCKET_ERROR) continue;
                error = ::WSAGetLastError();
            }
            stop();
            throw ConnectionClosedException(std::string(writing ? "Send" : "Receive") +
                " failed (Windows error " + std::to_string(error) + ")");
        }
        throw ConnectionClosedException("Headphones are disconnected");
    }

    const SOCKET _socket;
    std::atomic<bool> _connected{false};
    std::mutex _stateMutex;
    bool _stopped = false;
};
