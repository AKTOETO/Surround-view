#pragma once
#include "sv/output_product.hpp"

namespace sv
{
inline boost::json::object output_catalog()
{
    return {{"version", 1},
            {"products",
             boost::json::array{"final_frame", "render_support_v1", "source_visibility_v1"}},
            {"default", "final_frame"},
            {"selection", "one_product_per_data_channel"},
            {"persistence", "data_generation_only"},
            {"render_support_requires", "fusion.diagnostic=color"},
            {"configure_during_experiment", false},
            {"source_visibility_requires", "fusion.diagnostic=color"},
            {"source_visibility_schema", source_visibility_schema()},
            {"render_support_schema", render_support_schema()}};
}
} // namespace sv
