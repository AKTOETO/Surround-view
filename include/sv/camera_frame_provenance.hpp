#pragma once
#include "sv/camera_capture_mode.hpp"

namespace sv
{
struct CameraFrameProvenance
{
    uint64_t open_attempt = 0;
    std::optional<CameraCaptureMode> negotiated_mode = {};
};

struct CameraInputProvenance
{
    int camera_id = 0;
    uint64_t source_sequence = 0;
    CameraFrameProvenance capture;
};

inline boost::json::object camera_frame_provenance_json(const CameraFrameProvenance &capture)
{
    return {{"open_attempt", std::to_string(capture.open_attempt)},
            {"negotiated_mode", camera_capture_mode_json(capture.negotiated_mode)}};
}

inline boost::json::object camera_input_provenance_json(const CameraInputProvenance &input)
{
    return {{"camera_id", input.camera_id},
            {"source_sequence_id", std::to_string(input.source_sequence)},
            {"camera_capture", camera_frame_provenance_json(input.capture)}};
}
} // namespace sv
