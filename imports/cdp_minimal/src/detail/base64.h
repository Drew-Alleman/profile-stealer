// detail/base64.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace cdp::detail {

    inline std::string base64Decode(const std::string& in) {
        static const std::string chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static const std::vector<int> table = [] {
            std::vector<int> t(256, -1);
            for (int i = 0; i < 64; ++i) t[static_cast<unsigned char>(chars[i])] = i;
            return t;
            }();

        std::string out;
        int val = 0, valb = -8;
        for (unsigned char c : in) {
            if (table[c] == -1) continue;
            val = (val << 6) + table[c];
            valb += 6;
            if (valb >= 0) {
                out.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    // Standard base64 encode (with '=' padding). Used for the WebSocket
    // Sec-WebSocket-Key and for computing the expected Sec-WebSocket-Accept.
    inline std::string base64Encode(const unsigned char* data, std::size_t len) {
        static const char* chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        std::size_t i = 0;
        while (i + 3 <= len) {
            std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            out.push_back(chars[(n >> 18) & 0x3F]);
            out.push_back(chars[(n >> 12) & 0x3F]);
            out.push_back(chars[(n >> 6) & 0x3F]);
            out.push_back(chars[n & 0x3F]);
            i += 3;
        }
        if (len - i == 1) {
            std::uint32_t n = data[i] << 16;
            out.push_back(chars[(n >> 18) & 0x3F]);
            out.push_back(chars[(n >> 12) & 0x3F]);
            out.push_back('=');
            out.push_back('=');
        } else if (len - i == 2) {
            std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
            out.push_back(chars[(n >> 18) & 0x3F]);
            out.push_back(chars[(n >> 12) & 0x3F]);
            out.push_back(chars[(n >> 6) & 0x3F]);
            out.push_back('=');
        }
        return out;
    }

    inline std::string base64Encode(const std::string& in) {
        return base64Encode(reinterpret_cast<const unsigned char*>(in.data()), in.size());
    }

} // namespace cdp::detail
