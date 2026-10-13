#pragma once
#include "sv/protocol.hpp"

namespace sv
{
enum class OutputProduct
{
    FinalFrame,
    RenderSupport,
    SourceVisibility,
    SourceDisagreement
};

const char *output_product_name(OutputProduct);
OutputProduct parse_output_product(std::string_view);
boost::json::object render_support_schema();
boost::json::object source_visibility_schema();
boost::json::object source_disagreement_schema();
} // namespace sv
