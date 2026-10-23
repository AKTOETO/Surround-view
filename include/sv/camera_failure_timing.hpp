#pragma once
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace sv
{
// Latest completed failed backend call in the current timing session, not a worker event log.
struct CameraFailureTiming
{
    uint64_t epoch = 0, event_id = 0, started_ns = 0, completed_ns = 0;
    std::string operation, outcome;

    double wall_ms() const
    {
        return double(completed_ns - started_ns) / 1e6;
    }
};

inline boost::json::value camera_failure_timing_json(
    const std::optional<CameraFailureTiming> &value)
{
    if (!value)
    {
        return nullptr;
    }
    return boost::json::object{{"policy", "camera_failure_wall_v1"},
                               {"epoch", std::to_string(value->epoch)},
                               {"event_id", std::to_string(value->event_id)},
                               {"operation", value->operation},
                               {"outcome", value->outcome},
                               {"started_ns", std::to_string(value->started_ns)},
                               {"completed_ns", std::to_string(value->completed_ns)},
                               {"wall_ms", value->wall_ms()}};
}
} // namespace sv
