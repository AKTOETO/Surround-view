#pragma once
#include "sv/camera_frame_provenance.hpp"
#include <vector>

namespace sv::client
{
std::optional<CameraCaptureMode> parse_camera_mode(const boost::json::value &);
CameraInputProvenance parse_camera_input(const boost::json::object &);
std::vector<CameraInputProvenance> parse_selected_camera_inputs(const boost::json::value &);
} // namespace sv::client
