#pragma once
#include <boost/json.hpp>
#include <cstddef>
#include <vector>

namespace sv::research
{
// Packed, top-left RGB8 reference/mask and RGBA8 actual; binary RGB mask only.
boost::json::object masked_rgb_metrics(const std::vector<unsigned char> &rgba,
                                       const std::vector<unsigned char> &rgb,
                                       const std::vector<unsigned char> &mask);
boost::json::array quality_summaries(const boost::json::array &samples, size_t variants);
} // namespace sv::research
