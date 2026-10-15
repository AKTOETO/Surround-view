#pragma once
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace sv
{
// Timings of one successful input operation, not exposure or capture-to-display latency.
struct SourceWorkTiming
{
    int camera_id = 0;
    std::string kind;
    uint64_t epoch = 0, source_sequence = 0, started_ns = 0, completed_ns = 0;
    std::optional<double> camera_read_wall_ms, image_load_wall_ms, image_prepare_wall_ms;
    bool cache_hit = false;
};

inline boost::json::object source_work_json(const SourceWorkTiming &value)
{
    auto number = [](std::optional<double> v)
    { return v ? boost::json::value(*v) : boost::json::value(nullptr); };
    return {{"camera_id", value.camera_id},
            {"kind", value.kind},
            {"epoch", std::to_string(value.epoch)},
            {"source_sequence", std::to_string(value.source_sequence)},
            {"started_ns", std::to_string(value.started_ns)},
            {"completed_ns", std::to_string(value.completed_ns)},
            {"camera_read_wall_ms", number(value.camera_read_wall_ms)},
            {"image_load_wall_ms", number(value.image_load_wall_ms)},
            {"image_prepare_wall_ms", number(value.image_prepare_wall_ms)},
            {"cache_hit", value.cache_hit}};
}
} // namespace sv
