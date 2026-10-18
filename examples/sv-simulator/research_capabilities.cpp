#include "research_capabilities.hpp"
#include "research_controller.hpp"
#include "research_draft.hpp"
#include "scenario.hpp"
#include "sv/json.hpp"
#include <QJsonDocument>
#include <QStringList>
#include <algorithm>
#include <stdexcept>

namespace
{
QStringList knownModes()
{
    return {"edge_feather",   "hard_best_angle", "angular_feather",     "seam_distance_feather",
            "graph_cut_seam", "multi_band",      "graph_cut_multi_band"};
}

QStringList strings(const boost::json::object &catalog, const char *key)
{
    const auto *value = catalog.if_contains(key);
    if (!value)
    {
        return {};
    }
    const auto &array = value->as_array();
    if (array.size() > 32)
    {
        throw std::invalid_argument("catalog enum exceeds limit");
    }
    QStringList result;
    for (const auto &item : array)
    {
        const auto &s = item.as_string();
        if (s.empty() || s.size() > 128)
        {
            throw std::invalid_argument("invalid catalog enum");
        }
        const auto text = QString::fromUtf8(s.data(), int(s.size()));
        if (result.contains(text))
        {
            throw std::invalid_argument("duplicate catalog enum");
        }
        result.push_back(text);
    }
    return result;
}

int bound(const boost::json::value &value)
{
    if ((!value.is_int64() && !value.is_uint64()) || value.to_number<double>() < 1 ||
        value.to_number<double>() > 1024)
    {
        throw std::invalid_argument("invalid catalog integer bound");
    }
    return value.to_number<int>();
}

QStringList intersection(const QStringList &known, const QStringList &offered)
{
    QStringList result;
    for (const auto &value : known)
    {
        if (offered.contains(value))
        {
            result.push_back(value);
        }
    }
    return result;
}
} // namespace

QVariantMap researchCapabilities(const QVariantMap &fusionCatalog)
{
    const auto known = knownModes();
    const auto bytes = QJsonDocument::fromVariant(fusionCatalog).toJson(QJsonDocument::Compact);
    const auto parsed = sv::parse_json(std::string_view(bytes.constData(), size_t(bytes.size())));
    const auto &catalog = parsed.as_object();
    if (!catalog.contains("version") || bound(catalog.at("version")) > 5 ||
        !strings(catalog, "diagnostics").contains("color"))
    {
        throw std::invalid_argument("compatible fusion catalog with color diagnostic required");
    }
    const auto offered = strings(catalog, "modes");
    auto modes = intersection(known, offered);
    auto boundaries = intersection({"zero", "normalized"}, strings(catalog, "pyramid_boundary"));
    const auto solvers =
        intersection({"binary_pairs", "alpha_expansion"}, strings(catalog, "seam_solver"));
    int low = 1, high = 0;
    if (const auto *levels = catalog.if_contains("pyramid_levels"))
    {
        const auto &range = levels->as_object();
        const int declaredLow = bound(range.at("min")), declaredHigh = bound(range.at("max"));
        if (declaredLow > declaredHigh)
        {
            throw std::invalid_argument("reversed pyramid range");
        }
        low = std::max(1, declaredLow);
        high = std::min(8, declaredHigh);
    }
    if (boundaries.empty() || low > high)
    {
        modes.removeAll("multi_band");
        modes.removeAll("graph_cut_multi_band");
    }
    if (solvers.empty())
    {
        modes.removeAll("graph_cut_seam");
        modes.removeAll("graph_cut_multi_band");
    }
    if (modes.empty())
    {
        throw std::invalid_argument("no common research fusion methods");
    }
    if (boundaries.size() == 2)
    {
        boundaries.push_back("both");
    }
    return {{"ready", true},
            {"known_modes", known},
            {"modes", modes},
            {"boundaries", boundaries},
            {"solvers", solvers},
            {"levels_min", low},
            {"levels_max", high},
            {"version", bound(catalog.at("version"))},
            {"status", "Выбор ограничен fusion catalog текущей сессии"}};
}

void validateResearchCapabilities(const boost::json::object &document,
                                  const QVariantMap &fusionCatalog)
{
    const auto scenario = sv::research::parse_scenario(document);
    const auto caps = researchCapabilities(fusionCatalog);
    for (const auto &variant : scenario.variants)
    {
        const auto &fusion = variant.fusion;
        if (!caps.value("modes").toStringList().contains(QString::fromStdString(fusion.mode)))
        {
            throw std::invalid_argument("selected method unavailable in server catalog");
        }
        if (fusion.mode == "multi_band" || fusion.mode == "graph_cut_multi_band")
        {
            if (!caps.value("boundaries")
                     .toStringList()
                     .contains(QString::fromStdString(fusion.pyramid_boundary)) ||
                fusion.pyramid_levels < unsigned(caps.value("levels_min").toInt()) ||
                fusion.pyramid_levels > unsigned(caps.value("levels_max").toInt()))
            {
                throw std::invalid_argument("pyramid settings unavailable in server catalog");
            }
        }
        if ((fusion.mode == "graph_cut_seam" || fusion.mode == "graph_cut_multi_band") &&
            !caps.value("solvers").toStringList().contains(
                QString::fromStdString(fusion.seam_solver)))
        {
            throw std::invalid_argument("seam solver unavailable in server catalog");
        }
    }
}

QVariantMap ResearchController::draftCapabilities(const QVariantMap &catalog) const
{
    try
    {
        return researchCapabilities(catalog);
    }
    catch (const std::exception &error)
    {
        return {{"ready", false},
                {"known_modes", knownModes()},
                {"status", QString::fromUtf8(error.what())}};
    }
}

QString ResearchController::draftCapabilityError(const QVariantMap &values,
                                                 const QVariantMap &catalog) const
{
    try
    {
        validateResearchCapabilities(researchDraft(values), catalog);
        return {};
    }
    catch (const std::exception &error)
    {
        return QString::fromUtf8(error.what());
    }
}

bool ResearchController::startCatalogDraft(const QVariantMap &values, const QString &directory,
                                           const QUrl &reference, const QVariantMap &catalog)
{
    if (busy())
    {
        return false;
    }
    try
    {
        validateResearchCapabilities(researchDraft(values), catalog);
        return startDraftWithReference(values, directory, reference);
    }
    catch (const std::exception &error)
    {
        return reject(QString::fromUtf8(error.what()));
    }
}
