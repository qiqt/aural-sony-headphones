#include "../windows/SocketChannel.h"
#include <ws2tcpip.h>
#include "TestHarness.h"
#include <future>
#include <memory>

namespace
{
struct Runtime
{
    Runtime() { WSADATA data{}; if (::WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed"); }
    ~Runtime() { ::WSACleanup(); }
};

struct Pair
{
    Runtime runtime;
    std::shared_ptr<SocketChannel> client;
    std::shared_ptr<SocketChannel> server;
    Pair()
    {
        auto listener = std::make_shared<SocketChannel>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        CHECK(listener->handle() != INVALID_SOCKET);
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK_EQ(::bind(listener->handle(), reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)), 0);
        CHECK_EQ(::listen(listener->handle(), 1), 0);
        int length = sizeof(endpoint);
        CHECK_EQ(::getsockname(listener->handle(), reinterpret_cast<sockaddr*>(&endpoint), &length), 0);
        client = std::make_shared<SocketChannel>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        CHECK(client->handle() != INVALID_SOCKET);
        client->connectTo(reinterpret_cast<sockaddr*>(&endpoint), length);
        server = std::make_shared<SocketChannel>(::accept(listener->handle(), nullptr, nullptr));
        CHECK(server->handle() != INVALID_SOCKET);
        client->connectedSuccessfully();
        server->connectedSuccessfully();
    }
    ~Pair()
    {
        if (client) client->stop();
        if (server) server->stop();
    }
};
}

TEST(windows_socket_transfers_in_both_directions)
{
    Pair pair;
    char bytes[4]{};
    CHECK_EQ(pair.client->write("ping", 4), 4);
    int received = 0;
    while (received != 4) { const int n = pair.server->read(bytes + received, 4 - received); CHECK(n > 0); received += n; }
    CHECK(std::string(bytes, 4) == "ping");
    CHECK_EQ(pair.server->write("pong", 4), 4);
    received = 0;
    while (received != 4) { const int n = pair.client->read(bytes + received, 4 - received); CHECK(n > 0); received += n; }
    CHECK(std::string(bytes, 4) == "pong");
    CHECK_EQ(pair.client->read(bytes, 0), 0);
    CHECK(pair.client->connected());
}

TEST(windows_socket_shutdown_releases_a_pending_reader)
{
    Pair pair;
    std::promise<void> entered;
    auto started = entered.get_future();
    auto reader = std::async(std::launch::async, [channel = pair.server, &entered] {
        entered.set_value();
        char byte{};
        try { return channel->read(&byte, 1) == 0; }
        catch (const ConnectionClosedException&) { return true; }
    });
    started.get();
    pair.server->stop();
    CHECK(reader.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(reader.get());
    CHECK(!pair.server->connected());
    CHECK_THROWS(pair.server->connectedSuccessfully(), ConnectionClosedException);
    CHECK_THROWS(pair.server->write("x", 1), ConnectionClosedException);
}

TEST(windows_socket_peer_closure_clears_connection_state)
{
    Pair pair;
    pair.client->stop();
    char byte{};
    bool ended = false;
    try { ended = pair.server->read(&byte, 1) == 0; }
    catch (const ConnectionClosedException&) { ended = true; }
    CHECK(ended);
    CHECK(!pair.server->connected());
    pair.server->stop();
    pair.server->stop();
}

TEST(windows_socket_cancelled_connection_never_opens)
{
    Runtime runtime;
    auto channel = std::make_shared<SocketChannel>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    CHECK(channel->handle() != INVALID_SOCKET);
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    channel->stop();
    CHECK_THROWS(channel->connectTo(reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)), ConnectionClosedException);
    CHECK(!channel->connected());
}

TEST(windows_socket_expired_connection_deadline_fails_without_connecting)
{
    Runtime runtime;
    auto channel = std::make_shared<SocketChannel>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    CHECK(channel->handle() != INVALID_SOCKET);
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK_THROWS(channel->connectTo(reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint), std::chrono::milliseconds(0)), ConnectionClosedException);
    CHECK(!channel->connected());
    CHECK_THROWS(channel->connectedSuccessfully(), ConnectionClosedException);
}

TEST(windows_socket_refused_connection_clears_connection_state)
{
    Runtime runtime;
    auto reserved = std::make_shared<SocketChannel>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    CHECK(reserved->handle() != INVALID_SOCKET);
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK_EQ(::bind(reserved->handle(), reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)), 0);
    int length = sizeof(endpoint);
    CHECK_EQ(::getsockname(reserved->handle(), reinterpret_cast<sockaddr*>(&endpoint), &length), 0);
    auto channel = std::make_shared<SocketChannel>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    CHECK(channel->handle() != INVALID_SOCKET);
    CHECK_THROWS(channel->connectTo(reinterpret_cast<sockaddr*>(&endpoint), length, std::chrono::seconds(2)), ConnectionClosedException);
    CHECK(!channel->connected());
}

TEST(windows_socket_shutdown_releases_a_blocked_writer)
{
    Pair pair;
    int capacity = 1024;
    CHECK_EQ(::setsockopt(pair.client->handle(), SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&capacity), sizeof(capacity)), 0);
    std::string bytes(65536, 'x');
    // Stop reading at the peer and fill the socket before testing cancellation.
    bool full = false;
    for (int i = 0; i != 1024; ++i)
    {
        int sent = ::send(pair.client->handle(), bytes.data(), static_cast<int>(bytes.size()), 0);
        if (sent == SOCKET_ERROR)
        {
            CHECK_EQ(::WSAGetLastError(), WSAEWOULDBLOCK);
            full = true;
            break;
        }
    }
    CHECK(full);
    auto writer = std::async(std::launch::async, [channel = pair.client, bytes] {
        try { for (;;) channel->write(bytes.data(), bytes.size()); }
        catch (const ConnectionClosedException&) { return true; }
    });
    CHECK(writer.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    pair.client->stop();
    CHECK(writer.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(writer.get());
}
