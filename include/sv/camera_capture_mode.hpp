#pragma once
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace sv
{
// Successfully checked backend readback. This is not an independently measured sensor mode.
struct CameraCaptureMode
{
    unsigned width = 0, height = 0;
    uint64_t open_attempt = 0;
    std::optional<double> fps;
    std::optional<std::string> fourcc;
    std::optional<unsigned> buffer_count;
};

inline boost::json::value camera_capture_mode_json(const std::optional<CameraCaptureMode> &mode)
{
    if (!mode)
    {
        return nullptr;
    }
    return boost::json::object{
        {"width", mode->width},
        {"height", mode->height},
        {"open_attempt", std::to_string(mode->open_attempt)},
        {"fps", mode->fps ? boost::json::value(*mode->fps) : boost::json::value(nullptr)},
        {"fourcc", mode->fourcc ? boost::json::value(*mode->fourcc) : boost::json::value(nullptr)},
        {"buffer_count", mode->buffer_count ? boost::json::value(*mode->buffer_count)
                                            : boost::json::value(nullptr)}};
}
} // namespace sv
