#pragma once

#include <string>

namespace cdp {

    enum class Errc {
        ok = 0,
        not_connected,
        transport,
        protocol,
        bad_response,
        timeout,
        io
    };

    struct Error {
        Errc        code = Errc::ok;
        std::string message;
    };

    inline const char* to_string(Errc e) noexcept {
        switch (e) {
        case Errc::ok:            return "ok";
        case Errc::not_connected: return "not_connected";
        case Errc::transport:     return "transport";
        case Errc::protocol:      return "protocol";
        case Errc::bad_response:  return "bad_response";
        case Errc::timeout:       return "timeout";
        case Errc::io:            return "io";
        }
        return "unknown";
    }

} // namespace cdp
