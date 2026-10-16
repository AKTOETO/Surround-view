#pragma once
#include "sv/camera_capture_config.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

namespace sv
{
enum class CameraFailure
{
    Open,
    Read,
    InvalidFrame,
    Exception
};

class CameraRecoveryPolicy
{
  public:

    explicit CameraRecoveryPolicy(CameraRecoverySettings settings) : settings_(settings)
    {
        validate_camera_recovery(settings_);
    }

    std::optional<std::chrono::milliseconds> failure(CameraFailure kind)
    {
        if (failures_ != std::numeric_limits<uint64_t>::max())
        {
            ++failures_;
        }
        if (settings_.max_consecutive_failures && failures_ >= settings_.max_consecutive_failures)
        {
            return {};
        }
        uint64_t delay = kind == CameraFailure::Open           ? settings_.open_delay_ms
                         : kind == CameraFailure::Read         ? settings_.read_delay_ms
                         : kind == CameraFailure::InvalidFrame ? settings_.invalid_frame_delay_ms
                                                               : settings_.exception_delay_ms;
        if (settings_.backoff_multiplier > 1)
        {
            for (uint64_t step = 1; step < failures_ && delay < settings_.max_delay_ms; ++step)
            {
                delay = std::min<uint64_t>(settings_.max_delay_ms,
                                           delay * settings_.backoff_multiplier);
            }
        }
        return std::chrono::milliseconds(delay);
    }

    void success() noexcept
    {
        failures_ = 0;
    }

    uint64_t consecutive_failures() const noexcept
    {
        return failures_;
    }

  private:

    CameraRecoverySettings settings_;
    uint64_t failures_ = 0;
};
} // namespace sv
