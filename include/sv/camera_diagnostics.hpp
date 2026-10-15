#pragma once
#include "sv/camera_capture_mode.hpp"
#include <array>
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace sv
{
struct CameraDiagnostics
{
    uint64_t open_attempts = 0, read_failures = 0, opencv_exceptions = 0;
    uint64_t reconnects = 0, release_failures = 0;
    bool worker_stopped = false;
    std::optional<CameraCaptureMode> negotiated_mode = {};
};

using CameraDiagnosticsSet = std::array<CameraDiagnostics, 4>;

inline boost::json::value camera_diagnostics_json(const std::optional<CameraDiagnosticsSet> &set)
{
    if (!set)
    {
        return nullptr;
    }
    boost::json::array result;
    for (size_t id = 0; id < set->size(); ++id)
    {
        const auto &camera = (*set)[id];
        result.emplace_back(boost::json::object{
            {"camera_id", id},
            {"negotiated_mode", camera_capture_mode_json(camera.negotiated_mode)},
            {"open_attempts", std::to_string(camera.open_attempts)},
            {"read_failures", std::to_string(camera.read_failures)},
            {"opencv_exceptions", std::to_string(camera.opencv_exceptions)},
            {"reconnects", std::to_string(camera.reconnects)},
            {"release_failures", std::to_string(camera.release_failures)},
            {"worker_stopped", camera.worker_stopped}});
    }
    return result;
}
} // namespace sv
