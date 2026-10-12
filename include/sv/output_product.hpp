#pragma once
#include "sv/protocol.hpp"

namespace sv
{
enum class OutputProduct
{
    FinalFrame,
    RenderSupport
};

const char *output_product_name(OutputProduct);
OutputProduct parse_output_product(std::string_view);
boost::json::object render_support_schema();
} // namespace sv
