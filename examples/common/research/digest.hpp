#pragma once
#include <cstddef>
#include <openssl/sha.h>
#include <string>

namespace sv::research
{
inline std::string sha256(const void *data, size_t size)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(static_cast<const unsigned char *>(data), size, digest);
    std::string result;
    constexpr char digits[] = "0123456789abcdef";
    for (auto byte : digest)
    {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
} // namespace sv::research
