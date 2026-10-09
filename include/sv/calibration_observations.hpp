#pragma once

#include "sv/math.hpp"
#include <boost/json.hpp>
#include <string>
#include <vector>

namespace sv
{
inline constexpr const char *calibration_validation_policy =
    "client_declared_frames_and_exact_content_disjoint";

struct CalibrationObservationSplit
{
    std::vector<std::string> observation_ids;
    std::vector<std::string> frame_ids;
};

struct CalibrationProvenance
{
    std::string dataset_id;
    CalibrationObservationSplit training;
    CalibrationObservationSplit validation;
};

struct CalibrationSplitAudit
{
    std::string dataset_id;
    size_t training_observations = 0;
    size_t validation_observations = 0;
    size_t training_frames = 0;
    size_t validation_frames = 0;
};

CalibrationProvenance parse_calibration_provenance(const boost::json::value &value);
CalibrationSplitAudit validate_calibration_observations(const std::vector<Vec3> &points,
                                                        const std::vector<Pixel> &pixels,
                                                        const std::vector<Vec3> &validation_points,
                                                        const std::vector<Pixel> &validation_pixels,
                                                        const CalibrationProvenance &provenance);
} // namespace sv
