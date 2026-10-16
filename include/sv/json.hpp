#pragma once
#include <boost/json.hpp>
#include <boost/version.hpp>
#include <string_view>

namespace sv
{
enum class JsonNumberPolicy
{
    Legacy,
    Precise
};

inline constexpr bool json_precise_numbers_available = BOOST_VERSION >= 108300;

inline boost::json::parse_options json_parse_options(
    JsonNumberPolicy policy = JsonNumberPolicy::Precise)
{
    boost::json::parse_options options;
#if BOOST_VERSION >= 108300
    options.numbers = policy == JsonNumberPolicy::Precise
                          ? boost::json::number_precision::precise
                          : boost::json::number_precision::imprecise;
#else
    // Keep the supported Boost 1.75 API buildable; exact fidelity is not promised here.
    (void)policy;
#endif
    return options;
}

inline boost::json::value parse_json(std::string_view text,
                                     JsonNumberPolicy policy = JsonNumberPolicy::Precise)
{
    return boost::json::parse(boost::json::string_view(text.data(), text.size()), {},
                              json_parse_options(policy));
}
} // namespace sv
