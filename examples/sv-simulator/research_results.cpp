#include "research_results.hpp"
#include <QString>
#include <QVariantMap>
#include <cmath>

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
    if (!value || !value->is_number())
    {
        return QString::fromUtf8("—");
    }
    const auto number = value->to_number<double>();
    return QString::number(number, number != 0 && std::abs(number) < .001 ? 'g' : 'f',
                           number != 0 && std::abs(number) < .001 ? 6 : 3);
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
                        {"rmse", QString::fromUtf8("—")},
                        {"psnr", QString::fromUtf8("—")},
                        {"qualityFrames", 0},
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
            const auto &entry = quality->as_array()[index].as_object();
            row["mae"] = metric(entry, "pixel_weighted_mae_rgb8");
            row["rmse"] = metric(entry, "pixel_weighted_rmse_rgb8");
            row["psnr"] = metric(entry, "pixel_weighted_psnr_db");
            if (const auto *exact = entry.if_contains("exact_match"); exact && exact->as_bool())
            {
                row["psnr"] = QString::fromUtf8("∞ (точное совпадение)");
            }
            if (const auto *frames = entry.if_contains("frames"))
            {
                row["qualityFrames"] = frames->to_number<int>();
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

QVariantList researchBatchRows(const boost::json::object &summary)
{
    QVariantList rows;
    for (const auto &value : summary.at("runs").as_array())
    {
        const auto &member = value.as_object();
        const auto *restored = member.if_contains("restored");
        const auto *success = member.if_contains("success");
        rows.push_back(QVariantMap{
            {"id", label(member, "id")},
            {"status", label(member, "status")},
            {"quality", label(member, "quality_status", "not_available")},
            {"workflow",
             success ? (success->as_bool() ? "завершён" : "НЕ завершён") : "не подтверждён"},
            {"restore", restored ? (restored->as_bool() ? "подтверждено" : "НЕ подтверждено")
                                 : "не подтверждено"},
            {"report", label(member, "report")},
            {"error", label(member, "error", "")}});
    }
    return rows;
}
