#pragma once
#include <optional>
#include <string>

namespace sv
{
struct CameraRecoverySettings
{
    unsigned open_delay_ms = 250, read_delay_ms = 100, invalid_frame_delay_ms = 100;
    unsigned exception_delay_ms = 250, max_delay_ms = 5000, backoff_multiplier = 1;
    unsigned max_consecutive_failures = 0; // Zero keeps retrying indefinitely.
};

void validate_camera_recovery(const CameraRecoverySettings &);

struct CameraCaptureSettings
{
    std::optional<double> fps;
    std::string fourcc;
    std::optional<unsigned> buffer_count;
    CameraRecoverySettings recovery = {};
};
} // namespace sv
