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
    throw std::invalid_argument("unknown output product");
}

boost::json::object render_support_schema()
{
    return {{"version", 1},
            {"channels", boost::json::array{"eligible_camera_count", "positive_local_weight_count",
                                            "render_support_path", "raster_geometry"}},
            {"scope", "local_base_weights_not_scene_visibility"}};
}

} // namespace sv
