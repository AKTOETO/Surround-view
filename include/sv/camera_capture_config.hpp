#pragma once
#include <optional>
#include <string>

namespace sv
{
struct CameraCaptureSettings
{
    std::optional<double> fps;
    std::string fourcc;
    std::optional<unsigned> buffer_count;
};
} // namespace sv
