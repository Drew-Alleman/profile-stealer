// detail/connection.cpp
//
// Dependency-free WebSocket client (RFC 6455) over raw sockets.
// * POSIX (Linux/macOS): BSD sockets.
// * Windows: Winsock2.
//
// Only the client role is implemented, which is all a CDP client needs: it
// connects out to Chrome's DevTools endpoint, performs the opening handshake,
// then exchanges text frames.

#include "connection.h"
#include "base64.h"
#include "sha1.h"
#include "stealth/encryption.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#define CDP_CLOSESOCK ::closesocket
#define CDP_SHUTDOWN_BOTH SD_BOTH
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#define CDP_CLOSESOCK ::close
#define CDP_SHUTDOWN_BOTH SHUT_RDWR
#endif

#include <chrono>

namespace cdp::detail {

    namespace {
        void ensure_platform_init() {
#if defined(_WIN32)
            static std::once_flag once;
            std::call_once(once, [] {
                WSADATA wsa;
                WSAStartup(MAKEWORD(2, 2), &wsa);
                });
#endif
        }

        [[maybe_unused]] int last_socket_error() {
#if defined(_WIN32)
            return WSAGetLastError();
#else
            return errno;
#endif
        }

#if defined(MSG_NOSIGNAL)
        constexpr int kSendFlags = MSG_NOSIGNAL;
#else
        constexpr int kSendFlags = 0;
#endif

        constexpr std::uint64_t kMaxFramePayload = 256ull * 1024 * 1024; // 256 MiB guard
        const std::string kWsMagic = CRYPT("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

        struct ReadBuffer {
            std::string data;
            std::size_t head = 0;
            std::size_t avail() const { return data.size() - head; }
            const unsigned char* ptr() const {
                return reinterpret_cast<const unsigned char*>(data.data()) + head;
            }
            void append(const char* p, std::size_t n) { data.append(p, n); }
            void consume(std::size_t n) {
                head += n;
                if (head > 65536 && head * 2 >= data.size()) {
                    data.erase(0, head);
                    head = 0;
                }
            }
        };
    } // namespace

    struct Connection::Impl {
        socket_t sock = kInvalidSocket;
        std::atomic<bool> connected{ false };
        std::atomic<bool> closing{ false };
        ReadBuffer buf;
        std::mutex write_mtx;
        std::mt19937 rng{ std::random_device{}() };

        ~Impl() { destroy_socket(); }

        void shutdown_socket() {
            closing.store(true);
            connected.store(false);
            socket_t s = sock;
            if (s != kInvalidSocket)
                ::shutdown(s, CDP_SHUTDOWN_BOTH);
        }

        void destroy_socket() {
            socket_t s = sock;
            sock = kInvalidSocket;
            if (s != kInvalidSocket)
                CDP_CLOSESOCK(s);
            connected.store(false);
        }

        void write_all(const unsigned char* p, std::size_t n) {
            std::size_t sent = 0;
            while (sent < n) {
#if defined(_WIN32)
                int chunk = static_cast<int>(std::min<std::size_t>(n - sent, 1 << 20));
                int r = ::send(sock, reinterpret_cast<const char*>(p + sent), chunk, kSendFlags);
#else
                ssize_t r = ::send(sock, p + sent, n - sent, kSendFlags);
#endif
                if (r > 0) { sent += static_cast<std::size_t>(r); continue; }
                connected.store(false);
                throw std::runtime_error(CRYPT("websocket send failed"));
            }
        }

        std::size_t pull(const std::chrono::steady_clock::time_point* deadline) {
            if (deadline) {
                auto now = std::chrono::steady_clock::now();
                if (now >= *deadline)
                    throw TimeoutError(CRYPT("websocket read timed out"));
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    *deadline - now).count();
                fd_set rs;
                FD_ZERO(&rs);
                FD_SET(sock, &rs);
                timeval tv;
                tv.tv_sec = static_cast<long>(ms / 1000);
                tv.tv_usec = static_cast<long>((ms % 1000) * 1000);
#if defined(_WIN32)
                int sel = ::select(0, &rs, nullptr, nullptr, &tv);
#else
                int sel = ::select(sock + 1, &rs, nullptr, nullptr, &tv);
#endif
                if (sel == 0) throw TimeoutError(CRYPT("websocket read timed out"));
                if (sel < 0) {
                    if (closing.load()) return 0;
                    throw std::runtime_error(CRYPT("websocket select failed"));
                }
            }

            char tmp[16384];
#if defined(_WIN32)
            int r = ::recv(sock, tmp, sizeof(tmp), 0);
#else
            ssize_t r = ::recv(sock, tmp, sizeof(tmp), 0);
#endif
            if (r == 0) { connected.store(false); return 0; }
            if (r < 0) {
#if !defined(_WIN32)
                if (last_socket_error() == EINTR) return pull(deadline);
#endif
                connected.store(false);
                if (closing.load()) return 0;
                throw std::runtime_error(CRYPT("websocket recv failed"));
            }
            buf.append(tmp, static_cast<std::size_t>(r));
            return static_cast<std::size_t>(r);
        }

        bool ensure(std::size_t n, const std::chrono::steady_clock::time_point* deadline) {
            while (buf.avail() < n) {
                if (pull(deadline) == 0) return false;
            }
            return true;
        }

        struct Frame {
            bool closed = false;
            bool fin = true;
            int opcode = 0;
            std::string payload;
        };

        Frame read_frame(const std::chrono::steady_clock::time_point* deadline) {
            Frame f;
            if (!ensure(2, deadline)) { f.closed = true; return f; }
            const unsigned char* h = buf.ptr();
            const bool fin = (h[0] & 0x80) != 0;
            const int opcode = h[0] & 0x0F;
            const bool masked = (h[1] & 0x80) != 0;
            std::uint64_t len = h[1] & 0x7F;
            buf.consume(2);

            if (len == 126) {
                if (!ensure(2, deadline)) { f.closed = true; return f; }
                const unsigned char* e = buf.ptr();
                len = (std::uint64_t(e[0]) << 8) | e[1];
                buf.consume(2);
            }
            else if (len == 127) {
                if (!ensure(8, deadline)) { f.closed = true; return f; }
                const unsigned char* e = buf.ptr();
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | e[i];
                buf.consume(8);
            }

            if (len > kMaxFramePayload)
                throw std::runtime_error(CRYPT("websocket frame too large"));

            unsigned char mask[4] = { 0, 0, 0, 0 };
            if (masked) {
                if (!ensure(4, deadline)) { f.closed = true; return f; }
                std::memcpy(mask, buf.ptr(), 4);
                buf.consume(4);
            }

            if (!ensure(static_cast<std::size_t>(len), deadline)) { f.closed = true; return f; }
            f.payload.assign(reinterpret_cast<const char*>(buf.ptr()),
                static_cast<std::size_t>(len));
            buf.consume(static_cast<std::size_t>(len));

            if (masked) {
                for (std::size_t i = 0; i < f.payload.size(); ++i)
                    f.payload[i] ^= static_cast<char>(mask[i % 4]);
            }

            f.fin = fin;
            f.opcode = opcode;
            return f;
        }

        void write_frame(int opcode, const std::string& payload) {
            std::lock_guard<std::mutex> lk(write_mtx);
            std::string frame;
            frame.push_back(static_cast<char>(0x80 | (opcode & 0x0F)));
            const std::uint64_t n = payload.size();
            if (n < 126) {
                frame.push_back(static_cast<char>(0x80 | n));
            }
            else if (n <= 0xFFFF) {
                frame.push_back(static_cast<char>(0x80 | 126));
                frame.push_back(static_cast<char>((n >> 8) & 0xFF));
                frame.push_back(static_cast<char>(n & 0xFF));
            }
            else {
                frame.push_back(static_cast<char>(0x80 | 127));
                for (int i = 7; i >= 0; --i)
                    frame.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
            }

            unsigned char mask[4];
            std::uniform_int_distribution<int> dist(0, 255);
            for (auto& m : mask) m = static_cast<unsigned char>(dist(rng));
            frame.append(reinterpret_cast<const char*>(mask), 4);

            std::size_t base = frame.size();
            frame.append(payload);
            for (std::size_t i = 0; i < payload.size(); ++i)
                frame[base + i] ^= static_cast<char>(mask[i % 4]);

            write_all(reinterpret_cast<const unsigned char*>(frame.data()), frame.size());
        }

        std::string read_message(const std::chrono::steady_clock::time_point* deadline,
            bool& closed) {
            std::string message;
            for (;;) {
                Frame f = read_frame(deadline);
                if (f.closed) { closed = true; return {}; }

                switch (f.opcode) {
                case 0x0: // continuation
                case 0x1: // text
                case 0x2: // binary
                    message += f.payload;
                    if (f.fin) { closed = false; return message; }
                    break;
                case 0x8: // close
                    connected.store(false);
                    try { write_frame(0x8, f.payload); }
                    catch (...) {}
                    closed = true;
                    return {};
                case 0x9: // ping -> pong
                    try { write_frame(0xA, f.payload); }
                    catch (...) {}
                    break;
                case 0xA: // pong
                    break;
                default:
                    break;
                }
            }
        }
    };

    // ---- Connection public interface ----------------------------------

    Connection::Connection() : impl(std::make_unique<Impl>()) {}
    Connection::~Connection() = default;
    Connection::Connection(Connection&&) noexcept = default;
    Connection& Connection::operator=(Connection&&) noexcept = default;

    void Connection::connect(const std::string& host,
        const std::string& port,
        const std::string& target)
    {
        ensure_platform_init();
        impl->destroy_socket();
        impl->closing.store(false);

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* res = nullptr;
        if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
            throw std::runtime_error(CRYPT("websocket: cannot resolve ") + host + ":" + port);

        socket_t s = kInvalidSocket;
        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
            s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (s == kInvalidSocket) continue;
            if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
            CDP_CLOSESOCK(s);
            s = kInvalidSocket;
        }
        ::freeaddrinfo(res);

        if (s == kInvalidSocket)
            throw std::runtime_error(CRYPT("websocket: cannot connect to ") + host + ":" + port);

        int one = 1;
        ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<const char*>(&one), sizeof(one));

        impl->sock = s;
        impl->buf = ReadBuffer{};

        // ---- opening handshake ----
        unsigned char key_bytes[16];
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& b : key_bytes) b = static_cast<unsigned char>(dist(impl->rng));
        const std::string key = base64Encode(key_bytes, sizeof(key_bytes));

        std::string req =
            CRYPT("GET ") + target + CRYPT(" HTTP/1.1\r\n")
            + CRYPT("Host: ") + host + ":" + port + CRYPT("\r\n")
            + CRYPT("Upgrade: websocket\r\n")
            + CRYPT("Connection: Upgrade\r\n")
            + CRYPT("Sec-WebSocket-Key: ") + key + CRYPT("\r\n")
            + CRYPT("Sec-WebSocket-Version: 13\r\n")
            + CRYPT("\r\n");

        {
            std::lock_guard<std::mutex> lk(impl->write_mtx);
            impl->write_all(reinterpret_cast<const unsigned char*>(req.data()), req.size());
        }

        // Read until end of headers
        std::string header;
        std::size_t hdr_end = std::string::npos;
        for (;;) {
            if (impl->pull(nullptr) == 0)
                throw std::runtime_error(CRYPT("websocket handshake: connection closed"));
            header.assign(reinterpret_cast<const char*>(impl->buf.ptr()), impl->buf.avail());
            hdr_end = header.find(CRYPT("\r\n\r\n"));
            if (hdr_end != std::string::npos) break;
            if (header.size() > 65536)
                throw std::runtime_error(CRYPT("websocket handshake: response too large"));
        }

        impl->buf.consume(hdr_end + 4);
        header.resize(hdr_end);

        // Validate status line
        std::size_t line_end = header.find(CRYPT("\r\n"));
        std::string status_line = header.substr(0, line_end);
        if (status_line.find(CRYPT(" 101")) == std::string::npos)
            throw std::runtime_error(CRYPT("websocket handshake failed: ") + status_line);

        // Validate Sec-WebSocket-Accept
        const std::string expected = base64Encode(sha1(key + kWsMagic));
        std::string lower = header;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        std::size_t pos = lower.find(CRYPT("sec-websocket-accept:"));
        if (pos == std::string::npos)
            throw std::runtime_error(CRYPT("websocket handshake: missing accept header"));

        pos += CRYPT("sec-websocket-accept:").size();
        std::size_t end = header.find(CRYPT("\r\n"), pos);
        std::string got = header.substr(pos, end - pos);

        // trim
        std::size_t b = got.find_first_not_of(CRYPT(" \t"));
        std::size_t e = got.find_last_not_of(CRYPT(" \t\r"));
        got = (b == std::string::npos) ? "" : got.substr(b, e - b + 1);

        if (got != expected)
            throw std::runtime_error(CRYPT("websocket handshake: bad accept token"));

        impl->connected.store(true);
    }

    void Connection::send(const std::string& message) {
        if (!impl->connected.load() || impl->sock == kInvalidSocket)
            throw std::runtime_error(CRYPT("websocket send: not connected"));
        impl->write_frame(0x1, message); // text frame
    }

    std::string Connection::read() {
        if (impl->sock == kInvalidSocket) return {};
        bool closed = false;
        std::string msg = impl->read_message(nullptr, closed);
        return closed ? std::string{} : msg;
    }

    std::string Connection::receive(std::chrono::milliseconds timeout) {
        if (impl->sock == kInvalidSocket) return {};
        auto deadline = std::chrono::steady_clock::now() + timeout;
        bool closed = false;
        std::string msg = impl->read_message(&deadline, closed);
        return closed ? std::string{} : msg;
    }

    void Connection::close() {
        impl->shutdown_socket();
    }

    bool Connection::is_connected() const {
        return impl->connected.load();
    }

} // namespace cdp::detail