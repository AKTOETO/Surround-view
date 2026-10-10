#pragma once
#include "sv/config.hpp"

namespace sv
{
inline boost::json::object fusion_settings(const Fusion &fusion)
{
    return {{"mode", fusion.mode},
            {"diagnostic", fusion.diagnostic},
            {"edge_width_px", fusion.edge_width_px},
            {"angle_power", fusion.angle_power}};
}

inline boost::json::object fusion_catalog()
{
    return {{"version", 1},
            {"modes", boost::json::array{"edge_feather", "hard_best_angle", "angular_feather"}},
            {"diagnostics", boost::json::array{"color", "coverage", "weights"}},
            {"edge_width_px", boost::json::object{{"exclusive_min", 0}, {"max", 4096}}},
            {"angle_power", boost::json::object{{"exclusive_min", 0}, {"max", 32}}},
            {"apply", "between_render_calls"},
            {"persistence", "temporary"}};
}
} // namespace sv
