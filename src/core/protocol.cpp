#include "sv/protocol.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>
namespace sv {
namespace {
uint64_t get(const unsigned char *p, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v = (v << 8) | p[i];
    return v;
}
void put(std::vector<unsigned char> &b, uint64_t v, int n) {
    for (int i = n - 1; i >= 0; i--)
        b.push_back((v >> (8 * i)) & 255);
}
} // namespace
uint64_t parse_decimal_u64(std::string_view s) {
    uint64_t value = 0;
    if (s.empty() || s.size() > 20)
        throw std::invalid_argument("invalid decimal uint64");
    auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size())
        throw std::invalid_argument("invalid decimal uint64");
    return value;
}
std::vector<unsigned char> encode(const Message &m) {
    auto json = boost::json::serialize(m.header);
    if (json.size() + 24 > max_header || m.payload.size() > max_payload)
        throw std::invalid_argument("message size limit");
    std::vector<unsigned char> b{'S', 'V', '0', '1'};
    put(b, 1, 2);
    put(b, m.type, 2);
    put(b, json.size() + 24, 4);
    put(b, m.payload.size(), 8);
    put(b, 0, 4);
    b.insert(b.end(), json.begin(), json.end());
    b.insert(b.end(), m.payload.begin(), m.payload.end());
    return b;
}
std::vector<Message> Decoder::feed(const unsigned char *data, size_t n) {
    if (n > max_payload + max_header || pending_.size() + n > max_payload + max_header)
        throw std::runtime_error("message buffer limit");
    pending_.insert(pending_.end(), data, data + n);
    std::vector<Message> result;
    size_t pos = 0;
    while (pending_.size() - pos >= 24) {
        const auto *p = pending_.data() + pos;
        if (!std::equal(p, p + 4, "SV01") || get(p + 4, 2) != 1 || get(p + 20, 4) != 0)
            throw std::runtime_error("message magic/version/flags");
        uint64_t hs = get(p + 8, 4), ps = get(p + 12, 8);
        if (hs < 24 || hs > max_header || ps > max_payload)
            throw std::runtime_error("message length");
        size_t total = static_cast<size_t>(hs + ps);
        if (pending_.size() - pos < total)
            break;
        auto value = boost::json::parse(
            boost::json::string_view(reinterpret_cast<const char *>(p + 24), hs - 24));
        if (!value.is_object())
            throw std::runtime_error("metadata object required");
        Message m;
        m.type = get(p + 6, 2);
        m.header = std::move(value.as_object());
        m.payload.assign(p + hs, p + total);
        result.push_back(std::move(m));
        pos += total;
    }
    pending_.erase(pending_.begin(), pending_.begin() + pos);
    return result;
}
} // namespace sv
