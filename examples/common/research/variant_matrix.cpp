#include "trial_plan.hpp"
#include <algorithm>
#include <stdexcept>

namespace sv::research
{
boost::json::array expand_variant_matrix(const boost::json::object &matrix)
{
    for (const auto &field : matrix)
    {
        if (field.key() != "base" && field.key() != "axes")
        {
            throw std::invalid_argument("unknown variant matrix field");
        }
    }
    boost::json::object base;
    if (const auto *value = matrix.if_contains("base"))
    {
        base = value->as_object();
    }
    const auto &axes = matrix.at("axes").as_object();
    if (axes.empty() || axes.size() > 16)
    {
        throw std::invalid_argument("variant matrix requires 1..16 axes");
    }
    std::vector<std::string> names;
    size_t count = 1;
    for (const auto &axis : axes)
    {
        const auto &values = axis.value().as_array();
        if (values.empty() || values.size() > 32 || count > 32 / values.size())
        {
            throw std::invalid_argument("variant matrix exceeds 32 variants");
        }
        count *= values.size();
        names.emplace_back(axis.key());
    }
    std::sort(names.begin(), names.end());
    boost::json::array expanded{base};
    for (const auto &name : names)
    {
        boost::json::array next;
        for (const auto &variant : expanded)
        {
            for (const auto &value : axes.at(name).as_array())
            {
                auto object = variant.as_object();
                object[name] = value;
                next.push_back(std::move(object));
            }
        }
        expanded = std::move(next);
    }
    return expanded;
}
} // namespace sv::research
