#pragma once
#include "sv/config.hpp"

namespace sv
{
inline bool research_fusion(const std::string &mode)
{
    return mode == "seam_distance_feather" || mode == "graph_cut_seam" || mode == "multi_band" ||
           mode == "graph_cut_multi_band";
}

inline boost::json::object fusion_settings(const Fusion &fusion)
{
    return {{"mode", fusion.mode},
            {"diagnostic", fusion.diagnostic},
            {"edge_width_px", fusion.edge_width_px},
            {"angle_power", fusion.angle_power},
            {"smoothness_weight", fusion.smoothness_weight},
            {"pyramid_levels", fusion.pyramid_levels}};
}

inline boost::json::object fusion_catalog()
{
    return {{"version", 2},
            {"modes", boost::json::array{"edge_feather", "hard_best_angle", "angular_feather",
                                         "seam_distance_feather", "graph_cut_seam", "multi_band",
                                         "graph_cut_multi_band"}},
            {"diagnostics", boost::json::array{"color", "coverage", "weights"}},
            {"edge_width_px", boost::json::object{{"exclusive_min", 0}, {"max", 4096}}},
            {"angle_power", boost::json::object{{"exclusive_min", 0}, {"max", 32}}},
            {"smoothness_weight", boost::json::object{{"min", 0}, {"max", 100}}},
            {"pyramid_levels", boost::json::object{{"min", 1}, {"max", 8}}},
            {"research_backend", "gles_projection_cpu_fusion_v1"},
            {"research_fusion_implementation", "validity_zero_extension_v2"},
            {"research_pixel_limit", 262144},
            {"graph_cut_scope", "independent_binary_pairs; centrality_ties_for_3plus"},
            {"apply", "between_render_calls"},
            {"persistence", "temporary"}};
}
} // namespace sv
