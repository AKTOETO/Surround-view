#include "research_matrix_draft.hpp"
#include "sv/surface_runtime.hpp"
#include "trial_plan.hpp"
#include <set>
#include <stdexcept>
#include <string>

namespace
{
unsigned level(const boost::json::value &value)
{
    if ((!value.is_int64() && !value.is_uint64()) || value.to_number<double>() < 1 ||
        value.to_number<double>() > 8)
    {
        throw std::invalid_argument("pyramid levels/range outside integer 1..8");
    }
    return value.to_number<unsigned>();
}

boost::json::array levels(const boost::json::object &form)
{
    const auto fixed = level(form.at("pyramid_levels"));
    const auto *range = form.if_contains("pyramid_levels_range");
    if (!range)
    {
        return {fixed};
    }
    const auto &fields = range->as_object();
    for (const auto &field : fields)
    {
        if (field.key() != "from" && field.key() != "to" && field.key() != "step")
        {
            throw std::invalid_argument("unknown pyramid range field");
        }
    }
    const auto first = level(fields.at("from")), last = level(fields.at("to")),
               step = level(fields.at("step"));
    if (first > last)
    {
        throw std::invalid_argument("pyramid range must be ascending");
    }
    boost::json::array result;
    for (unsigned value = first; value <= last; value += step)
    {
        result.push_back(value);
    }
    return result;
}

boost::json::array solvers(const boost::json::object &form)
{
    const auto &fixed = form.at("seam_solver").as_string();
    if (fixed != "binary_pairs" && fixed != "alpha_expansion")
    {
        throw std::invalid_argument("unsupported seam solver");
    }
    const auto *axis = form.if_contains("seam_solvers");
    if (!axis)
    {
        return {fixed};
    }
    const auto &values = axis->as_array();
    if (values.empty() || values.size() > 2)
    {
        throw std::invalid_argument("select one or two distinct seam solvers");
    }
    std::set<std::string> unique;
    for (const auto &value : values)
    {
        const std::string name(value.as_string());
        if ((name != "binary_pairs" && name != "alpha_expansion") || !unique.insert(name).second)
        {
            throw std::invalid_argument("unsupported or duplicate seam solver");
        }
    }
    return values;
}
} // namespace

boost::json::array researchFusionVariants(const boost::json::object &form)
{
    const auto &modes = form.at("modes").as_array();
    const auto &boundary = form.at("boundary").as_string();
    if (modes.empty() || modes.size() > 7 ||
        (boundary != "zero" && boundary != "normalized" && boundary != "both"))
    {
        throw std::invalid_argument("select methods and a valid pyramid boundary");
    }
    const auto pyramidLevels = levels(form), seamSolvers = solvers(form);
    boost::json::array variants;
    std::set<std::string> unique;
    for (const auto &value : modes)
    {
        const std::string mode(value.as_string());
        if (!unique.insert(mode).second)
        {
            throw std::invalid_argument("duplicate research method");
        }
        const bool pyramid = mode == "multi_band" || mode == "graph_cut_multi_band";
        const bool graph = mode == "graph_cut_seam" || mode == "graph_cut_multi_band";
        boost::json::object axes{{"mode", boost::json::array{mode}}};
        if (pyramid)
        {
            axes["pyramid_levels"] = pyramidLevels;
            axes["pyramid_boundary"] = boundary == "both" ? boost::json::array{"zero", "normalized"}
                                                          : boost::json::array{boundary};
        }
        if (graph)
        {
            axes["seam_solver"] = seamSolvers;
        }
        const auto expanded = sv::research::expand_variant_matrix({{"axes", axes}});
        if (variants.size() + expanded.size() > 32)
        {
            throw std::invalid_argument("research form matrix exceeds 32 variants");
        }
        for (const auto &variant : expanded)
        {
            variants.push_back(variant);
        }
    }
    return variants;
}

boost::json::array researchCarrierVariants(const boost::json::array &fusion,
                                           const boost::json::value &profiles)
{
    const auto &surfaces = profiles.as_array();
    if (surfaces.empty() || surfaces.size() > 32 || fusion.size() > 32 / surfaces.size())
    {
        throw std::invalid_argument(
            "carrier matrix requires 1..32 profiles and at most 32 variants");
    }
    const auto descriptors = sv::surface_geometry_fields();
    boost::json::array unique, result;
    for (const auto &value : surfaces)
    {
        const auto &surface = value.as_object();
        const auto &type = surface.at("type").as_string();
        const auto *descriptor = descriptors.if_contains(type);
        if (!descriptor)
        {
            throw std::invalid_argument("unsupported carrier profile type");
        }
        for (const auto &required : descriptor->as_object().at("required").as_array())
        {
            if (!surface.contains(required.as_string()))
            {
                throw std::invalid_argument("incomplete carrier profile");
            }
        }
        for (const auto &prior : unique)
        {
            if (prior == value)
            {
                throw std::invalid_argument("duplicate carrier profile");
            }
        }
        unique.push_back(value);
        for (const auto &entry : fusion)
        {
            auto variant = entry.as_object();
            variant["surface"] = surface;
            result.push_back(std::move(variant));
        }
    }
    return result;
}
