#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace Utils {

class HexCodec
{
public:
    static bool hexToBytes(const std::string &hex, std::vector<uint8_t> &out)
    {
        if (hex.size() % 2 != 0)
        {
            return false;
        }

        out.clear();
        out.reserve(hex.size() / 2);

        for (size_t i = 0; i < hex.size(); i += 2)
        {
            int hi = hexDigit(hex[i]);
            int lo = hexDigit(hex[i + 1]);
            if (hi < 0 || lo < 0)
            {
                out.clear();
                return false;
            }
            out.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }

        return true;
    }

    static std::string bytesToHex(const uint8_t *data, size_t len)
    {
        static const char *digits = "0123456789ABCDEF";
        std::string result;
        result.reserve(len * 2);
        for (size_t i = 0; i < len; i++)
        {
            result.push_back(digits[(data[i] >> 4) & 0x0F]);
            result.push_back(digits[data[i] & 0x0F]);
        }
        return result;
    }

private:
    static int hexDigit(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
};

} // namespace Utils
