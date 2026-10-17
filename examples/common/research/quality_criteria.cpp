#include "quality_criteria.hpp"
#include <cmath>
#include <set>
#include <stdexcept>

namespace sv::research
{
QualityCriteria parse_quality_criteria(const boost::json::object &object)
{
    if (object.empty())
    {
        throw std::invalid_argument("quality criteria must not be empty");
    }
    QualityCriteria result;
    for (const auto &field : object)
    {
        const auto value = field.value().to_number<double>();
        if (!std::isfinite(value) || value < 0)
        {
            throw std::invalid_argument("quality criterion must be finite and nonnegative");
        }
        if (field.key() == "max_mae_rgb8" && value <= 255)
        {
            result.max_mae_rgb8 = value;
        }
        else if (field.key() == "max_rmse_rgb8" && value <= 255)
        {
            result.max_rmse_rgb8 = value;
        }
        else if (field.key() == "min_psnr_db" && value <= 200)
        {
            result.min_psnr_db = value;
        }
        else
        {
            throw std::invalid_argument("unknown or out-of-range quality criterion");
        }
    }
    return result;
}

boost::json::object quality_criteria_json(const QualityCriteria &criteria)
{
    boost::json::object result;
    if (criteria.max_mae_rgb8)
    {
        result["max_mae_rgb8"] = *criteria.max_mae_rgb8;
    }
    if (criteria.max_rmse_rgb8)
    {
        result["max_rmse_rgb8"] = *criteria.max_rmse_rgb8;
    }
    if (criteria.min_psnr_db)
    {
        result["min_psnr_db"] = *criteria.min_psnr_db;
    }
    return result;
}

boost::json::object check_quality_criteria(const boost::json::object &quality,
                                           const QualityCriteria &criteria)
{
    boost::json::array failed;
    if (criteria.max_mae_rgb8 &&
        quality.at("mae_rgb8").to_number<double>() > *criteria.max_mae_rgb8)
    {
        failed.push_back("max_mae_rgb8");
    }
    if (criteria.max_rmse_rgb8 &&
        quality.at("rmse_rgb8").to_number<double>() > *criteria.max_rmse_rgb8)
    {
        failed.push_back("max_rmse_rgb8");
    }
    if (criteria.min_psnr_db)
    {
        const auto &psnr = quality.at("psnr_db");
        // A perfect match represents +infinity as null in JSON.
        const bool perfect = quality.at("exact_match").as_bool();
        if ((psnr.is_null() && !perfect) ||
            (!psnr.is_null() && psnr.to_number<double>() < *criteria.min_psnr_db))
        {
            failed.push_back("min_psnr_db");
        }
    }
    return {{"passed", failed.empty()}, {"failed_criteria", failed}};
}

boost::json::object quality_acceptance(const boost::json::array &samples,
                                       const std::optional<QualityCriteria> &criteria,
                                       size_t expected_samples, bool workflow_complete)
{
    if (!criteria)
    {
        return {{"status", "not_requested"}};
    }
    size_t evaluated = 0, failed = 0;
    std::set<std::pair<unsigned, unsigned>> frames, failed_frames;
    for (const auto &value : samples)
    {
        const auto &sample = value.as_object();
        if (sample.at("warmup").as_bool())
        {
            continue;
        }
        const auto *quality = sample.if_contains("quality");
        if (!quality)
        {
            continue;
        }
        ++evaluated;
        const auto identity = std::make_pair(sample.at("frame_index").to_number<unsigned>(),
                                             sample.at("variant").to_number<unsigned>());
        frames.insert(identity);
        // Recompute from raw metrics, not from a possibly stale annotation.
        if (!check_quality_criteria(quality->as_object(), *criteria).at("passed").as_bool())
        {
            ++failed;
            failed_frames.insert(identity);
        }
    }
    const bool complete =
        workflow_complete && evaluated == expected_samples && expected_samples > 0;
    return {{"status", !complete ? "incomplete"
                       : failed  ? "failed"
                                 : "passed"},
            {"policy", "all_measured_samples_v1"},
            {"criteria", quality_criteria_json(*criteria)},
            {"expected_samples", expected_samples},
            {"evaluated_samples", evaluated},
            {"failed_samples", failed},
            {"evaluated_frame_variants", frames.size()},
            {"failed_frame_variants", failed_frames.size()}};
}
} // namespace sv::research
