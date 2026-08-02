// detail/http.h
//
// Minimal, dependency-free HTTP/1.1 GET over a raw socket. Cross-platform
// (Winsock / BSD sockets). Intended for talking to Chrome's DevTools HTTP
// endpoints (e.g. http://127.0.0.1:9222/json/version) to discover the
// WebSocket debugger URL. No TLS (the DevTools HTTP endpoint is plain HTTP).
//
// Drop-in replacement for the previous Boost.Beast `http_get`: same signature,
// returns the response body, and returns "" on any error or timeout.
//
// Header-only, C++17, standard library only.

#pragma once

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
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "stealth/encryption.hpp"

namespace cdp::detail {

    namespace http_detail {

#if defined(_WIN32)
        using socket_t = SOCKET;
        static constexpr socket_t kInvalid = INVALID_SOCKET;
        inline int close_sock(socket_t s) { return ::closesocket(s); }
#else
        using socket_t = int;
        static constexpr socket_t kInvalid = -1;
        inline int close_sock(socket_t s) { return ::close(s); }
#endif

        inline void platform_init() {
#if defined(_WIN32)
            static std::once_flag once;
            std::call_once(once, [] { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); });
#endif
        }

        inline bool set_nonblocking(socket_t s, bool nb) {
#if defined(_WIN32)
            u_long m = nb ? 1u : 0u;
            return ::ioctlsocket(s, FIONBIO, &m) == 0;
#else
            int f = ::fcntl(s, F_GETFL, 0);
            if (f < 0) return false;
            return ::fcntl(s, F_SETFL, nb ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK)) == 0;
#endif
        }

        inline long remaining_ms(std::chrono::steady_clock::time_point deadline) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            return ms < 0 ? 0 : static_cast<long>(ms);
        }

        inline bool wait_for(socket_t s, bool write, long ms) {
            fd_set fs; FD_ZERO(&fs); FD_SET(s, &fs);
            timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
#if defined(_WIN32)
            int r = ::select(0, write ? nullptr : &fs, write ? &fs : nullptr, nullptr, &tv);
#else
            int r = ::select(s + 1, write ? nullptr : &fs, write ? &fs : nullptr, nullptr, &tv);
#endif
            return r > 0;
        }

        inline bool connect_with_timeout(socket_t s, const sockaddr* addr, socklen_t len,
            std::chrono::steady_clock::time_point deadline) {
            if (!set_nonblocking(s, true)) return false;
            int r = ::connect(s, addr, len);
            bool ok = false;
            if (r == 0) {
                ok = true;
            }
            else {
#if defined(_WIN32)
                bool in_progress = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
                bool in_progress = (errno == EINPROGRESS);
#endif
                if (in_progress && wait_for(s, /*write=*/true, remaining_ms(deadline))) {
                    int err = 0; socklen_t el = sizeof(err);
                    if (::getsockopt(s, SOL_SOCKET, SO_ERROR,
                        reinterpret_cast<char*>(&err), &el) == 0 && err == 0)
                        ok = true;
                }
            }
            set_nonblocking(s, false);
            return ok;
        }

        inline bool send_all(socket_t s, const char* p, std::size_t n) {
            std::size_t sent = 0;
            while (sent < n) {
#if defined(_WIN32)
                int r = ::send(s, p + sent, static_cast<int>(n - sent), 0);
#elif defined(MSG_NOSIGNAL)
                ssize_t r = ::send(s, p + sent, n - sent, MSG_NOSIGNAL);
#else
                ssize_t r = ::send(s, p + sent, n - sent, 0);
#endif
                if (r <= 0) return false;
                sent += static_cast<std::size_t>(r);
            }
            return true;
        }

        inline int recv_more(socket_t s, std::string& buf,
            std::chrono::steady_clock::time_point deadline) {
            long ms = remaining_ms(deadline);
            if (ms <= 0) return -1;
            if (!wait_for(s, /*write=*/false, ms)) return -1;
            char tmp[8192];
#if defined(_WIN32)
            int r = ::recv(s, tmp, sizeof(tmp), 0);
#else
            ssize_t r = ::recv(s, tmp, sizeof(tmp), 0);
#endif
            if (r == 0) return 0;
            if (r < 0) return -1;
            buf.append(tmp, static_cast<std::size_t>(r));
            return static_cast<int>(r);
        }

        inline std::string to_lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        inline std::string header_value(const std::string& headers_lower,
            const std::string& headers_raw,
            const std::string& name_lower) {
            std::size_t p = headers_lower.find(CRYPT("\r\n") + name_lower + CRYPT(":"));
            if (p == std::string::npos) return {};
            p += 2 + name_lower.size() + 1;
            std::size_t e = headers_raw.find(CRYPT("\r\n"), p);
            std::string v = headers_raw.substr(p, e - p);
            std::size_t b = v.find_first_not_of(CRYPT(" \t"));
            std::size_t f = v.find_last_not_of(CRYPT(" \t"));
            return (b == std::string::npos) ? "" : v.substr(b, f - b + 1);
        }

    } // namespace http_detail

    inline std::string http_get(const std::string& host,
        const std::string& port,
        const std::string& target,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        using namespace http_detail;
        platform_init();

        const auto deadline = std::chrono::steady_clock::now() + timeout;

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* res = nullptr;
        if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
            return {};

        socket_t s = kInvalid;
        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
            s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (s == kInvalid) continue;
            if (connect_with_timeout(s, ai->ai_addr,
                static_cast<socklen_t>(ai->ai_addrlen), deadline))
                break;
            close_sock(s);
            s = kInvalid;
        }
        ::freeaddrinfo(res);
        if (s == kInvalid) return {};

        // Build and send the request
        std::string req =
            CRYPT("GET ") + target + CRYPT(" HTTP/1.1\r\n")
            + CRYPT("Host: ") + host + ":" + port + CRYPT("\r\n")
            + CRYPT("User-Agent: cdp-minimal/1.0\r\n")
            + CRYPT("Accept: */*\r\n")
            + CRYPT("Connection: close\r\n")
            + CRYPT("\r\n");

        if (!send_all(s, req.data(), req.size())) {
            close_sock(s);
            return {};
        }

        // Read until the end of the headers
        std::string buf;
        std::size_t hdr_end = std::string::npos;
        while ((hdr_end = buf.find(CRYPT("\r\n\r\n"))) == std::string::npos) {
            int r = recv_more(s, buf, deadline);
            if (r <= 0) { close_sock(s); return {}; }
            if (buf.size() > (1u << 20)) { close_sock(s); return {}; }
        }

        const std::string headers_raw = buf.substr(0, hdr_end + 2);
        const std::string headers_lower = to_lower(headers_raw);
        std::string body = buf.substr(hdr_end + 4);

        const std::string te = to_lower(header_value(headers_lower, headers_raw, CRYPT("transfer-encoding")));
        const std::string cl = header_value(headers_lower, headers_raw, CRYPT("content-length"));

        std::string result;

        if (te.find(CRYPT("chunked")) != std::string::npos) {
            std::size_t pos = 0;
            for (;;) {
                std::size_t line_end;
                while ((line_end = body.find(CRYPT("\r\n"), pos)) == std::string::npos) {
                    if (recv_more(s, body, deadline) <= 0) { close_sock(s); return result; }
                }
                std::size_t semi = body.find(';', pos);
                std::string hex = body.substr(pos, (semi < line_end ? semi : line_end) - pos);
                unsigned long chunk = std::strtoul(hex.c_str(), nullptr, 16);
                pos = line_end + 2;
                if (chunk == 0) break;
                while (body.size() < pos + chunk + 2) {
                    if (recv_more(s, body, deadline) <= 0) { close_sock(s); return result; }
                }
                result.append(body, pos, chunk);
                pos += chunk + 2;
            }
        }
        else if (!cl.empty()) {
            std::size_t need = static_cast<std::size_t>(std::strtoull(cl.c_str(), nullptr, 10));
            while (body.size() < need) {
                if (recv_more(s, body, deadline) <= 0) break;
            }
            result = body.substr(0, need < body.size() ? need : body.size());
        }
        else {
            for (;;) {
                int r = recv_more(s, body, deadline);
                if (r <= 0) break;
            }
            result = body;
        }

        close_sock(s);
        return result;
    }

} // namespace cdp::detail