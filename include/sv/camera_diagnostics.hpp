#pragma once
#include "sv/camera_capture_mode.hpp"
#include <array>
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace sv
{
// Snapshot of the recovery policy, independent of telemetry collection epochs.
struct CameraRecoveryStatus
{
    uint64_t consecutive_failures = 0;
    std::optional<unsigned> retry_delay_ms = {}; // Present only during the retry wait.
    std::optional<std::string> terminal_reason =
        {}; // stop_requested/retry_exhausted/release_failure.
};

inline boost::json::value camera_recovery_status_json(
    const std::optional<CameraRecoveryStatus> &status)
{
    if (!status)
    {
        return nullptr;
    }
    return boost::json::object{
        {"consecutive_failures", std::to_string(status->consecutive_failures)},
        {"retry_delay_ms", status->retry_delay_ms ? boost::json::value(*status->retry_delay_ms)
                                                  : boost::json::value(nullptr)},
        {"terminal_reason", status->terminal_reason ? boost::json::value(*status->terminal_reason)
                                                    : boost::json::value(nullptr)}};
}

struct CameraDiagnostics
{
    uint64_t open_attempts = 0, read_failures = 0, opencv_exceptions = 0;
    uint64_t reconnects = 0, release_failures = 0;
    bool worker_stopped = false;
    std::optional<CameraCaptureMode> negotiated_mode = {};
    std::optional<CameraRecoveryStatus> recovery = {};
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
            {"recovery", camera_recovery_status_json(camera.recovery)},
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
