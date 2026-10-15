#pragma once
#include <boost/json.hpp>

namespace sv
{
struct TelemetryConfig
{
    bool enabled = false;
    unsigned period_ms = 100, history_capacity = 128;

    bool operator==(const TelemetryConfig &other) const noexcept
    {
        return enabled == other.enabled && period_ms == other.period_ms &&
               history_capacity == other.history_capacity;
    }
};

inline boost::json::object telemetry_settings(const TelemetryConfig &config)
{
    return {{"enabled", config.enabled},
            {"period_ms", config.period_ms},
            {"history_capacity", config.history_capacity}};
}

inline boost::json::object telemetry_config_catalog()
{
    return {{"enabled_default", false},
            {"period_ms", boost::json::object{{"min", 50}, {"max", 5000}, {"default", 100}}},
            {"history_capacity", boost::json::object{{"min", 8}, {"max", 256}, {"default", 128}}},
            {"scope", "devel_process_and_existing_frame_spans"},
            {"apply", "prepared_hot_swap"}};
}
} // namespace sv
