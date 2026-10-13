#include "sv/output_product.hpp"
#include <stdexcept>

namespace sv
{
const char *output_product_name(OutputProduct product)
{
    switch (product)
    {
    case OutputProduct::FinalFrame:
        return "final_frame";
    case OutputProduct::RenderSupport:
        return "render_support_v1";
    case OutputProduct::SourceDisagreement:
        return "source_disagreement_v1";
    case OutputProduct::SourceVisibility:
        return "source_visibility_v1";
    }
    throw std::invalid_argument("unknown output product");
}

OutputProduct parse_output_product(std::string_view product)
{
    if (product == "final_frame")
    {
        return OutputProduct::FinalFrame;
    }
    if (product == "render_support_v1")
    {
        return OutputProduct::RenderSupport;
    }
    if (product == "source_visibility_v1")
    {
        return OutputProduct::SourceVisibility;
    }
    if (product == "source_disagreement_v1")
    {
        return OutputProduct::SourceDisagreement;
    }
    throw std::invalid_argument("unknown output product");
}

boost::json::object render_support_schema()
{
    return {{"version", 1},
            {"channels", boost::json::array{"eligible_camera_count", "positive_local_weight_count",
                                            "render_support_path", "raster_geometry"}},
            {"scope", "local_base_weights_not_scene_visibility"}};
}

boost::json::object source_visibility_schema()
{
    return {
        {"version", 1},
        {"channels", boost::json::array{"camera0", "camera1", "camera2", "camera3"}},
        {"statuses",
         boost::json::array{"no_carrier", "eligible_unclassified", "unavailable", "behind_camera",
                            "outside_angle", "outside_image", "vehicle_footprint",
                            "central_occluder", "uncertain_occluder", "backfacing_support",
                            "accepted_ambiguous", "accepted_clear", "footprint_occluder"}},
        {"scope", "supplied_geometry_policy_not_scene_visibility"}};
}

boost::json::object source_disagreement_schema()
{
    return {{"version", 1},
            {"channels", boost::json::array{"encoded_rgb_max_range_u8", "eligible_camera_count",
                                            "defined_pair_comparison", "carrier_domain"}},
            {"scope", "eligible_samples_not_fusion_weights_or_scene_confidence"},
            {"sampling", "bilinear_encoded_rgb_before_color_compensation"},
            {"defined", "count_at_least_two"}};
}

} // namespace sv
