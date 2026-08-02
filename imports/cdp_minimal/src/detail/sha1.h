// detail/sha1.h
//
// Minimal, self-contained SHA-1 (FIPS 180-1). Used only to compute the
// WebSocket Sec-WebSocket-Accept value during the opening handshake.
//
// Not for security-sensitive hashing; SHA-1 is used here because RFC 6455
// mandates it for the handshake.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace cdp::detail {

    // Returns the 20-byte SHA-1 digest of `input` as a raw byte string.
    inline std::string sha1(const std::string& input) {
        std::uint32_t h0 = 0x67452301;
        std::uint32_t h1 = 0xEFCDAB89;
        std::uint32_t h2 = 0x98BADCFE;
        std::uint32_t h3 = 0x10325476;
        std::uint32_t h4 = 0xC3D2E1F0;

        // Pre-processing: append 0x80, pad with zeros, append 64-bit length.
        std::string msg = input;
        const std::uint64_t bit_len = static_cast<std::uint64_t>(input.size()) * 8;
        msg.push_back(static_cast<char>(0x80));
        while (msg.size() % 64 != 56) msg.push_back('\0');
        for (int i = 7; i >= 0; --i)
            msg.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));

        auto rotl = [](std::uint32_t x, int c) -> std::uint32_t {
            return (x << c) | (x >> (32 - c));
        };

        for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
            std::uint32_t w[80];
            for (int i = 0; i < 16; ++i) {
                const unsigned char* p =
                    reinterpret_cast<const unsigned char*>(msg.data()) + chunk + i * 4;
                w[i] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
                       (std::uint32_t(p[2]) << 8)  |  std::uint32_t(p[3]);
            }
            for (int i = 16; i < 80; ++i)
                w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

            std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
            for (int i = 0; i < 80; ++i) {
                std::uint32_t f, k;
                if (i < 20)      { f = (b & c) | ((~b) & d);         k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
                else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }

                std::uint32_t tmp = rotl(a, 5) + f + e + k + w[i];
                e = d; d = c; c = rotl(b, 30); b = a; a = tmp;
            }
            h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
        }

        std::uint32_t hh[5] = { h0, h1, h2, h3, h4 };
        std::string digest(20, '\0');
        for (int i = 0; i < 5; ++i) {
            digest[i * 4 + 0] = static_cast<char>((hh[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<char>((hh[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<char>((hh[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<char>(hh[i] & 0xFF);
        }
        return digest;
    }

} // namespace cdp::detail
