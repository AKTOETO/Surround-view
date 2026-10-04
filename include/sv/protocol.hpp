#pragma once
#include <boost/json.hpp>
#include <cstdint>
#include <string_view>
#include <vector>
namespace sv {
constexpr size_t max_header = 65536, max_payload = 64 * 1024 * 1024;
uint64_t parse_decimal_u64(std::string_view);
struct Message {
    uint16_t type = 0;
    boost::json::object header;
    std::vector<unsigned char> payload;
};
std::vector<unsigned char> encode(const Message &);
class Decoder {
    std::vector<unsigned char> pending_;

  public:
    std::vector<Message> feed(const unsigned char *, size_t);
    size_t buffered() const {
        return pending_.size();
    }
};
} // namespace sv
