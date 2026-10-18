#include "research_draft.hpp"
#include "research_matrix_draft.hpp"
#include "scenario.hpp"
#include "sv/json.hpp"
#include <QJsonDocument>
#include <set>
#include <stdexcept>

boost::json::object researchDraft(const QVariantMap &values, const QString &reference)
{
    const auto bytes = QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact);
    const auto document = sv::parse_json(std::string_view(bytes.constData(), size_t(bytes.size())));
    const auto &form = document.as_object();
    const std::set<std::string> allowed{"modes",
                                        "frames",
                                        "warmup",
                                        "repeats",
                                        "seed",
                                        "boundary",
                                        "pyramid_levels",
                                        "seam_solver",
                                        "quality_criteria",
                                        "pyramid_levels_range",
                                        "seam_solvers",
                                        "surface_profiles"};
    for (const auto &entry : form)
    {
        if (!allowed.count(std::string(entry.key())))
        {
            throw std::invalid_argument("unknown research form field");
        }
    }
    auto variants = researchFusionVariants(form);
    if (const auto *profiles = form.if_contains("surface_profiles"))
    {
        variants = researchCarrierVariants(variants, *profiles);
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
