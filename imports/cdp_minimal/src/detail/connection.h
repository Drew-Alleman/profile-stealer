// detail/connection.h
//
// Cross-platform, dependency-free WebSocket client connection.
//
// Public interface is unchanged from the previous Boost.Beast-based version so
// that Channel (and any other caller) needs no changes. Internally this now
// uses raw BSD/Winsock sockets and a hand-rolled RFC 6455 implementation.
#pragma once

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

namespace cdp::detail {

    // Thrown when a bounded read does not complete before its deadline.
    class TimeoutError : public std::runtime_error {
    public:
        explicit TimeoutError(const std::string& what) : std::runtime_error(what) {}
    };

    class Connection {
    public:
        Connection();
        ~Connection();

        Connection(Connection&&) noexcept;
        Connection& operator=(Connection&&) noexcept;
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        // Open a WebSocket connection. `target` is the request path (e.g.
        // "/devtools/page/ABCD..."). Throws std::runtime_error on failure.
        void connect(const std::string& host,
                     const std::string& port,
                     const std::string& target);

        // Send one text message. Throws if not connected.
        void send(const std::string& message);

        // Blocking read of the next text message. Returns "" when the peer
        // closes the connection cleanly.
        std::string read();

        // Bounded read. Returns "" on clean close; throws TimeoutError if no
        // complete message arrives within `timeout`.
        std::string receive(std::chrono::milliseconds timeout);

        // Unblocks a concurrent read() and tears the socket down. Safe to call
        // from another thread.
        void close();

        bool is_connected() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

} // namespace cdp::detail
