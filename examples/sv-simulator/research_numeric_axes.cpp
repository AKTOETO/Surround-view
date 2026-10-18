#include "research_numeric_axes.hpp"
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
struct Axis
{
    const char *form, *parameter;
    double maximum;
    bool zeroAllowed;
};

constexpr Axis definitions[] = {{"edge_width_values", "edge_width_px", 4096, false},
                                {"angle_power_values", "angle_power", 32, false},
                                {"smoothness_values", "smoothness_weight", 100, true}};

bool applicable(std::string_view mode, std::string_view parameter)
{
    if (parameter == "edge_width_px")
    {
        return mode == "edge_feather" || mode == "angular_feather" || mode == "multi_band";
    }
    if (parameter == "angle_power")
    {
        return mode == "angular_feather";
    }
    return mode == "graph_cut_seam" || mode == "graph_cut_multi_band";
}

double number(const boost::json::value &value)
{
    if (!value.is_number() || !std::isfinite(value.to_number<double>()))
    {
        throw std::invalid_argument("numeric fusion axis requires finite numbers");
    }
    return value.to_number<double>();
}
} // namespace

void researchNumericAxes(const boost::json::object &form, std::string_view mode,
                         boost::json::object &axes)
{
    for (const auto &definition : definitions)
    {
        const auto *axis = form.if_contains(definition.form);
        if (!axis)
        {
            continue;
        }
        const auto &values = axis->as_array();
        if (values.empty() || values.size() > 8)
        {
            throw std::invalid_argument("numeric fusion axis requires 1..8 distinct values");
        }
        std::set<double> unique;
        for (const auto &value : values)
        {
            const auto n = number(value);
            if (n < 0 || (!definition.zeroAllowed && n == 0) || n > definition.maximum ||
                !unique.insert(n).second)
            {
                throw std::invalid_argument("numeric fusion axis outside bounds or duplicate");
            }
        }
        if (applicable(mode, definition.parameter))
        {
            axes[definition.parameter] = values;
        }
    }
}

void validateResearchNumericCatalog(const boost::json::object &variant,
                                    const boost::json::object &catalog)
{
    for (const auto &definition : definitions)
    {
        const auto *value = variant.if_contains(definition.parameter);
        if (!value)
        {
            continue;
        }
        const auto &limits = catalog.at(definition.parameter).as_object();
        const auto n = number(*value), high = number(limits.at("max"));
        const auto *exclusive = limits.if_contains("exclusive_min");
        const auto low = number(exclusive ? *exclusive : limits.at("min"));
        if (low > high || n > high || (exclusive ? n <= low : n < low))
        {
            throw std::invalid_argument("numeric fusion value unavailable in server catalog");
        }
    }
}
