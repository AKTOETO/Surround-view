#pragma once
#include <cstring>
#include <limits>
#include <opencv2/core.hpp>

namespace sv
{
// RGBA8 bytes encode IEEE754 bits in a specified order, independent of host endianness.
inline void unpack_coordinate(const cv::Mat &packed, const cv::Mat &regions, cv::Mat &positions,
                              int axis)
{
    static_assert(sizeof(float) == sizeof(uint32_t) && std::numeric_limits<float>::is_iec559);
    for (int y = 0; y < packed.rows; ++y)
    {
        for (int x = 0; x < packed.cols; ++x)
        {
            const auto region = regions.at<uchar>(y, x);
            if (region == 0 || region == 5)
            {
                continue;
            }
            const auto bytes = packed.at<cv::Vec4b>(y, x);
            const uint32_t bits = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
                                  (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
            float value;
            std::memcpy(&value, &bits, sizeof(value));
            positions.at<cv::Vec3f>(y, x)[axis] = value;
        }
    }
}
} // namespace sv
