#include "camera_failure_timing.hpp"
#include "sv/protocol.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sv::client
{
std::optional<CameraFailureTiming> parse_camera_failure_timing(const boost::json::value &value,
                                                               uint64_t epoch, uint64_t sample_ns)
{
    if (value.is_null())
    {
        return {};
    }
    const auto &object = value.as_object();
    if (object.size() != 8 || object.at("policy") != "camera_failure_wall_v1")
    {
        throw std::invalid_argument("unknown camera failure timing schema");
    }
    auto integer = [&](const char *name)
    { return parse_decimal_u64(std::string(object.at(name).as_string())); };
    CameraFailureTiming result{integer("epoch"),
                               integer("event_id"),
                               integer("started_ns"),
                               integer("completed_ns"),
                               std::string(object.at("operation").as_string()),
                               std::string(object.at("outcome").as_string())};
    const bool backend = result.operation == "is_open" || result.operation == "open" ||
                         result.operation == "read" || result.operation == "close";
    const bool outcome = result.outcome == "opencv_exception" ||
                         (result.outcome == "false_return" &&
                          (result.operation == "open" || result.operation == "read")) ||
                         (result.outcome == "empty_frame" && result.operation == "read");
    if (result.epoch != epoch || !result.event_id || !backend || !outcome ||
        result.completed_ns < result.started_ns || result.completed_ns > sample_ns)
    {
        throw std::invalid_argument("inconsistent camera failure timing identity/interval");
    }
    const auto &wall = object.at("wall_ms");
    if (!wall.is_number() || !std::isfinite(wall.to_number<double>()) ||
        wall.to_number<double>() < 0 ||
        std::abs(wall.to_number<double>() - result.wall_ms()) >
            std::max(1e-9, result.wall_ms() * 1e-12))
    {
        throw std::invalid_argument("inconsistent camera failure duration");
    }
    return result;
}
} // namespace sv::client
