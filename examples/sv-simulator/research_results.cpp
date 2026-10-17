#include "research_results.hpp"
#include <QString>
#include <QVariantMap>

namespace
{
QString label(const boost::json::object &object, const char *key, const char *fallback = "—")
{
    const auto *value = object.if_contains(key);
    return value && value->is_string()
               ? QString::fromUtf8(value->as_string().data(), int(value->as_string().size()))
               : QString::fromUtf8(fallback);
}

QString metric(const boost::json::object &object, const char *key)
{
    const auto *value = object.if_contains(key);
    return value && value->is_number() ? QString::number(value->to_number<double>(), 'f', 3)
                                       : QString::fromUtf8("—");
}
} // namespace

QVariantList researchResultRows(const boost::json::object &report)
{
    QVariantList rows;
    const auto &variants = report.at("scenario").as_object().at("variants").as_array();
    const auto &summaries = report.at("summaries").as_array();
    const auto *quality = report.if_contains("quality_summaries");
    for (size_t index = 0; index < variants.size(); ++index)
    {
        const auto &variant = variants[index].as_object();
        QVariantMap row{{"mode", label(variant, "mode")},
                        {"boundary", label(variant, "pyramid_boundary", "zero")},
                        {"p50", QString::fromUtf8("—")},
                        {"p95", QString::fromUtf8("—")},
                        {"mae", QString::fromUtf8("—")},
                        {"measured", 0}};
        if (index < summaries.size())
        {
            const auto &timings = summaries[index].as_object().at("timings").as_object();
            const auto *wall = timings.if_contains("render_wall");
            if (wall && wall->is_object())
            {
                row["p50"] = metric(wall->as_object(), "p50_ms");
                row["p95"] = metric(wall->as_object(), "p95_ms");
                row["measured"] = wall->as_object().at("count").to_number<int>();
            }
        }
        if (quality && index < quality->as_array().size())
        {
            row["mae"] = metric(quality->as_array()[index].as_object(), "pixel_weighted_mae_rgb8");
        }
        rows.push_back(std::move(row));
    }
    return rows;
}
