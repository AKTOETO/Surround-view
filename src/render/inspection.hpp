#pragma once
#include "sv/render_inspection.hpp"
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
            positions.ptr<float>(y)[x * positions.channels() + axis] = value;
        }
    }
}
} // namespace sv

namespace sv
{
template <class Pass>
void inspect_sources(RenderInspection &inspection, int width, int height, Pass pass)
{
    for (int camera = 0; camera < 4; ++camera)
    {
        auto &source = inspection.source_projections[camera];
        source.pixels = cv::Mat(height, width, CV_32FC2,
                                cv::Scalar::all(std::numeric_limits<float>::quiet_NaN()));
        cv::extractChannel(pass(7, camera), source.status, 0);
        for (int axis = 0; axis < 2; ++axis)
        {
            unpack_coordinate(pass(5 + axis, camera), inspection.carrier_regions, source.pixels,
                              axis);
        }
    }
}
} // namespace sv
