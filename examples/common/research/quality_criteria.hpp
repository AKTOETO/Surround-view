#pragma once
#include <boost/json.hpp>
#include <optional>

namespace sv::research
{
struct QualityCriteria
{
    std::optional<double> max_mae_rgb8, max_rmse_rgb8, min_psnr_db;
};

QualityCriteria parse_quality_criteria(const boost::json::object &);
boost::json::object quality_criteria_json(const QualityCriteria &);
boost::json::object check_quality_criteria(const boost::json::object &, const QualityCriteria &);
// A completed workflow and complete measured sample set are mandatory for passed/failed.
boost::json::object quality_acceptance(const boost::json::array &samples,
                                       const std::optional<QualityCriteria> &criteria,
                                       size_t expected_samples, bool workflow_complete);
} // namespace sv::research
