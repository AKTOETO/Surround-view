#include "report_series.hpp"
#include "digest.hpp"
#include "regular_input.hpp"
#include "report_comparison.hpp"
#include "report_evidence.hpp"
#include "sv/json.hpp"
#include <algorithm>
#include <stdexcept>

namespace sv::research
{
namespace
{
void count(size_t size)
{
    if (size < 2 || size > 8)
    {
        throw std::invalid_argument("report series requires 2..8 reports");
    }
}

const boost::json::object *find_row(const boost::json::object &comparison, unsigned index)
{
    for (const auto &value : comparison.at("rows").as_array())
    {
        const auto &row = value.as_object();
        const auto &left = row.at("left_index");
        if (left.is_number() && left.to_number<unsigned>() == index)
        {
            return &row;
        }
    }
    return nullptr;
}

boost::json::object range(const boost::json::object &baseline, unsigned index,
                          const boost::json::array &pairs, const char *delta, const char *metric)
{
    std::vector<double> values;
    auto collect = [&](const boost::json::object *row, const char *side)
    {
        if (row && row->at("pairing") == "matched" && row->at(delta).is_number())
        {
            values.push_back(row->at(side).as_object().at(metric).to_number<double>());
        }
    };
    collect(find_row(baseline, index), "left");
    for (const auto &value : pairs)
    {
        collect(find_row(value.as_object().at("comparison").as_object(), index), "right");
    }
    const auto required = pairs.size() + 1;
    boost::json::object result{{"eligible_reports", values.size()},
                               {"required_reports", required},
                               {"status", "not_all_reports_comparable"},
                               {"minimum", nullptr},
                               {"median", nullptr},
                               {"maximum", nullptr}};
    if (values.size() == required)
    {
        std::sort(values.begin(), values.end());
        const auto half = values.size() / 2;
        const auto median =
            values.size() % 2 ? values[half] : values[half - 1] / 2 + values[half] / 2;
        result["status"] = "all_reports_comparable";
        result["minimum"] = values.front();
        result["median"] = median;
        result["maximum"] = values.back();
    }
    return result;
}
} // namespace

boost::json::object compare_report_series(const std::vector<boost::json::object> &reports,
                                          const std::atomic_bool *cancel)
{
    comparison::cancelled(cancel);
    count(reports.size());
    for (size_t index = 0; index < reports.size(); ++index)
    {
        for (size_t earlier = 0; earlier < index; ++earlier)
        {
            comparison::cancelled(cancel);
            if (comparison::equal(reports[index], reports[earlier]))
            {
                throw std::invalid_argument("duplicate report documents in series");
            }
        }
    }
    const auto baseline = compare_reports(reports.front(), reports.front(), cancel);
    boost::json::array pairs;
    for (size_t index = 1; index < reports.size(); ++index)
    {
        comparison::cancelled(cancel);
        pairs.push_back(boost::json::object{
            {"report_index", index},
            {"comparison", compare_reports(reports.front(), reports[index], cancel)}});
    }
    boost::json::array rows;
    for (const auto &value : baseline.at("rows").as_array())
    {
        comparison::cancelled(cancel);
        const auto &row = value.as_object();
        if (!row.at("left_index").is_number())
        {
            continue;
        }
        const auto index = row.at("left_index").to_number<unsigned>();
        rows.push_back(boost::json::object{
            {"baseline_variant_index", index},
            {"settings", row.at("settings")},
            {"render_p50_ms", range(baseline, index, pairs, "timing_delta_ms", "render_p50_ms")},
            {"mae_rgb8", range(baseline, index, pairs, "mae_delta", "mae")},
            {"rmse_rgb8", range(baseline, index, pairs, "rmse_delta", "rmse")}});
    }
    return {{"schema_version", 1},
            {"policy", "report_series_comparison_v1"},
            {"pair_policy", baseline.at("policy")},
            {"baseline_index", 0},
            {"report_count", reports.size()},
            {"profile_scope", "baseline_variants_only"},
            {"aggregation", "min_median_max_of_report_summaries_v1"},
            {"comparisons", std::move(pairs)},
            {"rows", std::move(rows)},
            {"limitation",
             "Descriptive report summaries, not pooled frame percentiles, independent "
             "replicates, confidence intervals, acceptance, sustained FPS or a ranking. "
             "Claims are not platform attestation."}};
}

boost::json::object compare_report_series_files(const std::vector<std::filesystem::path> &paths,
                                                const std::atomic_bool *cancel)
{
    comparison::cancelled(cancel);
    count(paths.size());
    std::vector<boost::json::object> reports;
    boost::json::array inputs;
    size_t total = 0;
    constexpr size_t file_limit = 16 * 1024 * 1024, total_limit = 64 * 1024 * 1024;
    for (const auto &path : paths)
    {
        comparison::cancelled(cancel);
        if (path.empty() || path.string().find('\0') != std::string::npos || total == total_limit)
        {
            throw std::invalid_argument("invalid report path or series input budget exhausted");
        }
        const auto text =
            read_regular_file(path, std::min(file_limit, total_limit - total), cancel);
        total += text.size();
        inputs.push_back(boost::json::object{{"index", reports.size()},
                                             {"path", path.string()},
                                             {"sha256", sha256(text.data(), text.size())},
                                             {"bytes", text.size()}});
        reports.push_back(sv::parse_json(text).as_object());
    }
    auto result = compare_report_series(reports, cancel);
    result["inputs"] = std::move(inputs);
    result["input_bytes"] = total;
    return result;
}
} // namespace sv::research
