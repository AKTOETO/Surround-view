#include "sv/extrinsics.hpp"
#include "sv/vision.hpp"
#include <cmath>
#include <opencv2/calib3d.hpp>

namespace sv
{
ExtrinsicCalibration calibrate_extrinsics(const Camera &initial, const std::vector<Vec3> &points,
                                          const std::vector<Pixel> &pixels,
                                          const ExtrinsicOptions &options)
{
    if (points.size() < 6 || points.size() > 100000 || points.size() != pixels.size() ||
        !std::isfinite(options.ransac_threshold_px) || options.ransac_threshold_px <= 0 ||
        options.ransac_threshold_px > 100 || !std::isfinite(initial.fx) ||
        !std::isfinite(initial.fy) || initial.fx <= 0 || initial.fy <= 0)
    {
        throw std::invalid_argument("invalid known-intrinsics extrinsic observations/options");
    }
    std::vector<cv::Point3d> objects;
    std::vector<cv::Point2d> observed, normalized;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto p = points[i];
        const auto uv = pixels[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            !std::isfinite(uv.u) || !std::isfinite(uv.v))
        {
            throw std::invalid_argument("nonfinite correspondence");
        }
        objects.emplace_back(p.x, p.y, p.z);
        observed.emplace_back(uv.u, uv.v);
    }
    cv::Mat centered(static_cast<int>(points.size()), 3, CV_64F);
    Vec3 mean{};
    for (auto p : points)
    {
        mean = mean + p * (1.0 / points.size());
    }
    for (size_t i = 0; i < points.size(); ++i)
    {
        centered.at<double>(i, 0) = points[i].x - mean.x;
        centered.at<double>(i, 1) = points[i].y - mean.y;
        centered.at<double>(i, 2) = points[i].z - mean.z;
    }
    cv::Mat singular;
    cv::SVD::compute(centered, singular);
    if (singular.at<double>(0) <= 1e-12 || singular.at<double>(2) < 1e-8 * singular.at<double>(0))
    {
        throw std::invalid_argument("noncoplanar aggregate XYZ required for this profile");
    }
    cv::fisheye::undistortPoints(observed, normalized, camera_matrix(initial),
                                 cv::Vec4d(initial.k.data()));
    for (const auto &p : normalized)
    {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
        {
            throw std::invalid_argument("fisheye inversion failed");
        }
    }
    cv::Matx33d rotation(initial.T[0], initial.T[1], initial.T[2], initial.T[4], initial.T[5],
                         initial.T[6], initial.T[8], initial.T[9], initial.T[10]);
    cv::Vec3d rvec, tvec(initial.T[3], initial.T[7], initial.T[11]);
    cv::Rodrigues(rotation, rvec);
    bool success = false;
    size_t inliers = points.size();
    if (options.method == "ransac_epnp_lm")
    {
        cv::Mat indices;
        // Threshold is in normalized pinhole coordinates, not fisheye pixels.
        const double threshold = options.ransac_threshold_px / std::max(initial.fx, initial.fy);
        success = cv::solvePnPRansac(objects, normalized, cv::Matx33d::eye(), cv::noArray(), rvec,
                                     tvec, false, 300, threshold, .999, indices, cv::SOLVEPNP_EPNP);
        if (success && indices.total() >= 6)
        {
            std::vector<cv::Point3d> selected_objects;
            std::vector<cv::Point2d> selected_pixels;
            for (int index : cv::Mat_<int>(indices))
            {
                selected_objects.push_back(objects.at(index));
                selected_pixels.push_back(normalized.at(index));
            }
            cv::solvePnPRefineLM(selected_objects, selected_pixels, cv::Matx33d::eye(),
                                 cv::noArray(), rvec, tvec);
            inliers = indices.total();
        }
        else
        {
            success = false;
        }
    }
    else
    {
        int flag = cv::SOLVEPNP_ITERATIVE;
        if (options.method == "epnp")
        {
            flag = cv::SOLVEPNP_EPNP;
        }
        else if (options.method == "sqpnp")
        {
            flag = cv::SOLVEPNP_SQPNP;
        }
        else if (options.method != "iterative")
        {
            throw std::invalid_argument("unsupported extrinsic calibration method");
        }
        success = cv::solvePnP(objects, normalized, cv::Matx33d::eye(), cv::noArray(), rvec, tvec,
                               options.method == "iterative", flag);
    }
    if (!success || !cv::checkRange(cv::Mat(rvec)) || !cv::checkRange(cv::Mat(tvec)))
    {
        throw std::runtime_error("extrinsic calibration failed");
    }
    cv::Rodrigues(rvec, rotation);
    Camera calibrated = initial;
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            calibrated.T[row * 4 + column] = rotation(row, column);
        }
        calibrated.T[row * 4 + 3] = tvec[row];
    }
    double squared = 0;
    auto projected = project_opencv(calibrated, points);
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (!projected[i].valid)
        {
            throw std::runtime_error("estimated pose projects training point outside validity");
        }
        squared +=
            std::pow(projected[i].u - pixels[i].u, 2) + std::pow(projected[i].v - pixels[i].v, 2);
    }
    return {calibrated, inliers, std::sqrt(squared / points.size())};
}
} // namespace sv
