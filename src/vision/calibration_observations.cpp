#include "sv/calibration_observations.hpp"
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>

namespace sv
{
namespace
{
void valid_id(const std::string &id)
{
    if (id.empty() || id.size() > 128)
    {
        throw std::invalid_argument("calibration_provenance_invalid_id");
    }
    for (const unsigned char c : id)
    {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' || c == ':' || c == '/'))
        {
            throw std::invalid_argument("calibration_provenance_invalid_id");
        }
    }
}

CalibrationObservationSplit parse_split(const boost::json::value &value)
{
    const auto &object = value.as_object();
    CalibrationObservationSplit split;
    for (const auto &id : object.at("observation_ids").as_array())
    {
        split.observation_ids.emplace_back(id.as_string());
    }
    for (const auto &id : object.at("frame_ids").as_array())
    {
        split.frame_ids.emplace_back(id.as_string());
    }
    return split;
}
} // namespace

CalibrationProvenance parse_calibration_provenance(const boost::json::value &value)
{
    const auto &object = value.as_object();
    return {std::string(object.at("dataset_id").as_string()), parse_split(object.at("training")),
            parse_split(object.at("validation"))};
}

CalibrationSplitAudit validate_calibration_observations(const std::vector<Vec3> &points,
                                                        const std::vector<Pixel> &pixels,
                                                        const std::vector<Vec3> &validation_points,
                                                        const std::vector<Pixel> &validation_pixels,
                                                        const CalibrationProvenance &provenance)
{
    valid_id(provenance.dataset_id);
    std::set<std::string> observation_ids, training_frames, validation_frames;
    std::set<std::array<double, 5>> correspondences;
    const auto check = [&](const std::vector<Vec3> &xyz, const std::vector<Pixel> &uv,
                           const CalibrationObservationSplit &split, std::set<std::string> &frames)
    {
        if (xyz.size() < 6 || xyz.size() > 10000 || xyz.size() != uv.size() ||
            split.observation_ids.size() != xyz.size() || split.frame_ids.size() != xyz.size())
        {
            throw std::invalid_argument("calibration_provenance_size_mismatch");
        }
        for (size_t i = 0; i < xyz.size(); ++i)
        {
            valid_id(split.observation_ids[i]);
            valid_id(split.frame_ids[i]);
            if (!observation_ids.insert(split.observation_ids[i]).second)
            {
                throw std::invalid_argument("calibration_duplicate_observation_id");
            }
            frames.insert(split.frame_ids[i]);
            const std::array<double, 5> key{xyz[i].x, xyz[i].y, xyz[i].z, uv[i].u, uv[i].v};
            if (!uv[i].valid)
            {
                throw std::invalid_argument("calibration_invalid_correspondence");
            }
            for (double coordinate : key)
            {
                if (!std::isfinite(coordinate))
                {
                    throw std::invalid_argument("calibration_nonfinite_correspondence");
                }
            }
            if (!correspondences.insert(key).second)
            {
                throw std::invalid_argument("calibration_duplicate_correspondence");
            }
        }
    };
    check(points, pixels, provenance.training, training_frames);
    check(validation_points, validation_pixels, provenance.validation, validation_frames);
    for (const auto &frame : validation_frames)
    {
        if (training_frames.count(frame))
        {
            throw std::invalid_argument("calibration_train_validation_frame_overlap");
        }
    }
    return {provenance.dataset_id, points.size(), validation_points.size(), training_frames.size(),
            validation_frames.size()};
}
} // namespace sv
