#pragma once

// MD5 over a stream of bytes, for the host side of the OTA scenarios.
//
// It exists so that the flash stand-in can do what the device's update mechanism does with an
// expected checksum: hash the bytes it was actually given and compare. Without it a test could
// only assert that a string was passed around, and "the image is rejected" would be whatever the
// test told the mock to answer.
//
// This is a stand-in for hardware and SDK (the Arduino Update singleton hashing what it writes
// to the flash slot), not a copy of anything under test - the firmware contains no MD5 of its
// own. The digests it produces are pinned in the tests against values computed outside this
// codebase, so a wrong implementation here shows up as a failing test rather than as agreement
// with itself.
//
// RFC 1321, straightforward: 64-byte blocks, little-endian length, lowercase hex output.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace hostsim {

class HostMd5 {
public:
    HostMd5() { reset(); }

    void reset()
    {
        _a = 0x67452301u;
        _b = 0xefcdab89u;
        _c = 0x98badcfeu;
        _d = 0x10325476u;
        _messageBits = 0;
        _bufferLen = 0;
    }

    void add(const uint8_t* data, size_t len)
    {
        _messageBits += (uint64_t)len * 8ULL;
        while (len > 0)
        {
            const size_t room = 64 - _bufferLen;
            const size_t take = (len < room) ? len : room;
            memcpy(_buffer + _bufferLen, data, take);
            _bufferLen += take;
            data += take;
            len -= take;
            if (_bufferLen == 64)
            {
                transform(_buffer);
                _bufferLen = 0;
            }
        }
    }

    /** @brief Finish the digest and return it as 32 lowercase hex characters. */
    std::string hexDigest() const
    {
        HostMd5 copy = *this;

        const uint64_t bits = copy._messageBits;
        const uint8_t one = 0x80;
        copy._messageBits = 0; // padding is not part of the message length
        copy.add(&one, 1);
        const uint8_t zero = 0x00;
        while (copy._bufferLen != 56)
        {
            copy.add(&zero, 1);
        }
        uint8_t lengthBytes[8];
        for (int i = 0; i < 8; i++)
        {
            lengthBytes[i] = (uint8_t)((bits >> (8 * i)) & 0xFF);
        }
        copy.add(lengthBytes, 8);

        const uint32_t words[4] = {copy._a, copy._b, copy._c, copy._d};
        char out[33];
        for (int w = 0; w < 4; w++)
        {
            for (int i = 0; i < 4; i++)
            {
                snprintf(out + (w * 8) + (i * 2), 3, "%02x", (unsigned)((words[w] >> (8 * i)) & 0xFF));
            }
        }
        out[32] = '\0';
        return std::string(out);
    }

private:
    static uint32_t rotateLeft(uint32_t value, uint32_t count)
    {
        return (value << count) | (value >> (32 - count));
    }

    void transform(const uint8_t block[64])
    {
        static const uint32_t K[64] = {
            0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au,
            0xa8304613u, 0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
            0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u,
            0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
            0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u,
            0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
            0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
            0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
            0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u,
            0xffeff47du, 0x85845dd1u, 0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
            0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};
        static const uint32_t S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                       5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                       4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                       6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

        uint32_t m[16];
        for (int i = 0; i < 16; i++)
        {
            m[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
                   ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);
        }

        uint32_t a = _a;
        uint32_t b = _b;
        uint32_t c = _c;
        uint32_t d = _d;

        for (uint32_t i = 0; i < 64; i++)
        {
            uint32_t f = 0;
            uint32_t g = 0;
            if (i < 16)
            {
                f = (b & c) | (~b & d);
                g = i;
            }
            else if (i < 32)
            {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            }
            else if (i < 48)
            {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            }
            else
            {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }

            const uint32_t temp = d;
            d = c;
            c = b;
            b = b + rotateLeft(a + f + K[i] + m[g], S[i]);
            a = temp;
        }

        _a += a;
        _b += b;
        _c += c;
        _d += d;
    }

    uint32_t _a;
    uint32_t _b;
    uint32_t _c;
    uint32_t _d;
    uint64_t _messageBits;
    uint8_t _buffer[64];
    size_t _bufferLen;
};

} // namespace hostsim
