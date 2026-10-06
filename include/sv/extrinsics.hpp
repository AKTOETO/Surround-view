#pragma once
#include "sv/math.hpp"

namespace sv
{
struct ExtrinsicOptions
{
    std::string method = "iterative";
    double ransac_threshold_px = 3;
};

struct ExtrinsicCalibration
{
    Camera camera;
    size_t inliers = 0;
    double training_rmse_px = 0;
};

// Known intrinsics and non-coplanar vehicle XYZ. Does not read evaluation truth.
ExtrinsicCalibration calibrate_extrinsics(const Camera &, const std::vector<Vec3> &,
                                          const std::vector<Pixel> &, const ExtrinsicOptions &);
} // namespace sv
