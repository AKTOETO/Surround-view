#include "research_draft.hpp"
#include "scenario.hpp"
#include "sv/json.hpp"
#include <QJsonDocument>
#include <algorithm>
#include <set>
#include <stdexcept>

boost::json::object researchDraft(const QVariantMap &values, const QString &reference)
{
    const auto bytes = QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact);
    const auto document = sv::parse_json(std::string_view(bytes.constData(), size_t(bytes.size())));
    const auto &form = document.as_object();
    const std::set<std::string> allowed{"modes",          "frames",      "warmup",
                                        "repeats",        "seed",        "boundary",
                                        "pyramid_levels", "seam_solver", "quality_criteria"};
    for (const auto &entry : form)
    {
        if (!allowed.count(std::string(entry.key())))
        {
            throw std::invalid_argument("unknown research form field");
        }
    }
    const auto &modes = form.at("modes").as_array();
    const std::string boundary(form.at("boundary").as_string());
    const std::string solver(form.at("seam_solver").as_string());
    if (modes.empty() || modes.size() > 7 ||
        (boundary != "zero" && boundary != "normalized" && boundary != "both") ||
        (solver != "binary_pairs" && solver != "alpha_expansion"))
    {
        throw std::invalid_argument("select methods and valid pyramid boundary/seam solver");
    }
    const auto &levels = form.at("pyramid_levels");
    if ((!levels.is_int64() && !levels.is_uint64()) || levels.to_number<int>() < 1 ||
        levels.to_number<int>() > 8)
    {
        throw std::invalid_argument("pyramid levels outside 1..8");
    }
    boost::json::array variants;
    std::set<std::string> unique;
    for (const auto &mode_value : modes)
    {
        const std::string mode(mode_value.as_string());
        if (!unique.insert(mode).second)
        {
            throw std::invalid_argument("duplicate research method");
        }
        const bool pyramid = mode == "multi_band" || mode == "graph_cut_multi_band";
        const bool graph = mode == "graph_cut_seam" || mode == "graph_cut_multi_band";
        for (const auto *edge : {"zero", "normalized"})
        {
            if ((!pyramid && std::string_view(edge) != "zero") ||
                (pyramid && boundary != "both" && boundary != edge))
            {
                continue;
            }
            boost::json::object variant{{"mode", mode}};
            if (pyramid)
            {
                variant["pyramid_boundary"] = edge;
                variant["pyramid_levels"] = levels;
            }
            if (graph)
            {
                variant["seam_solver"] = solver;
            }
            variants.push_back(std::move(variant));
        }
    }
    boost::json::object scenario{{"schema_version", 1},
                                 {"reset_replay", true},
                                 {"capture_frames", true},
                                 {"variants", std::move(variants)}};
    for (const auto *field : {"frames", "warmup", "repeats", "seed"})
    {
        scenario[field] = form.at(field);
    }
    if (!reference.isEmpty())
    {
        scenario["quality_reference"] = reference.toStdString();
    }
    if (const auto *criteria = form.if_contains("quality_criteria"))
    {
        scenario["quality_criteria"] = *criteria;
    }
    (void)sv::research::parse_scenario(scenario);
    return scenario;
}
