#pragma once
#include "sv/camera_failure_timing.hpp"

namespace sv::client
{
std::optional<CameraFailureTiming> parse_camera_failure_timing(const boost::json::value &,
                                                               uint64_t epoch, uint64_t sample_ns);
}
