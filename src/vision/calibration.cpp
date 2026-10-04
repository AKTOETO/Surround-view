#include "sv/vision.hpp"
#include <cmath>
#include <opencv2/calib3d.hpp>

namespace sv
{
IntrinsicCalibration calibrate_intrinsics(const std::vector<std::vector<cv::Point3d>> &objects,
                                          const std::vector<std::vector<cv::Point2d>> &pixels,
                                          cv::Size image_size, double theta_max)
{
    if (objects.size() < 6 || objects.size() != pixels.size() || image_size.area() <= 0 ||
        !std::isfinite(theta_max) || theta_max <= 0 || theta_max >= pi / 2)
    {
        throw std::invalid_argument("at least six matching training views required");
    }
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (objects[i].size() < 9 || objects[i].size() != pixels[i].size())
        {
            throw std::invalid_argument("invalid calibration correspondences");
        }
    }
    cv::Matx33d K = cv::Matx33d::eye();
    cv::Vec4d distortion{};
    std::vector<cv::Vec3d> rotations, translations;
    double rms =
        cv::fisheye::calibrate(objects, pixels, image_size, K, distortion, rotations, translations,
                               cv::fisheye::CALIB_RECOMPUTE_EXTRINSIC |
                                   cv::fisheye::CALIB_CHECK_COND | cv::fisheye::CALIB_FIX_SKEW,
                               {cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 100, 1e-10});
    if (!std::isfinite(rms) || K(0, 0) <= 0 || K(1, 1) <= 0 || !cv::checkRange(K) ||
        !cv::checkRange(distortion))
    {
        throw std::runtime_error("nonfinite calibration or nonpositive focal length");
    }
    // Acceptance also requires a monotonic angle-to-radius map over the declared domain.
    for (int i = 0; i <= 1024; ++i)
    {
        double theta2 = std::pow(theta_max * i / 1024, 2);
        double derivative =
            1 + theta2 * (3 * distortion[0] +
                          theta2 * (5 * distortion[1] +
                                    theta2 * (7 * distortion[2] + theta2 * 9 * distortion[3])));
        if (derivative <= 0)
        {
            throw std::runtime_error("nonmonotonic calibrated model on declared angle domain");
        }
    }
    return {K, distortion, rms};
}

std::vector<double> intrinsic_validation_errors(const IntrinsicCalibration &calibration,
                                                const std::vector<cv::Point3d> &objects,
                                                const std::vector<cv::Point2d> &pixels)
{
    if (objects.size() < 9 || objects.size() != pixels.size())
    {
        throw std::invalid_argument("invalid held-out correspondences");
    }
    std::vector<cv::Point2d> normalized;
    cv::fisheye::undistortPoints(pixels, normalized, calibration.K, calibration.distortion);
    cv::Vec3d rotation, translation;
    if (!cv::solvePnP(objects, normalized, cv::Matx33d::eye(), cv::noArray(), rotation,
                      translation))
    {
        throw std::runtime_error("held-out board pose estimation failed");
    }
    std::vector<cv::Point2d> projected;
    cv::fisheye::projectPoints(objects, projected, rotation, translation, calibration.K,
                               calibration.distortion);
    std::vector<double> errors;
    for (size_t i = 0; i < pixels.size(); ++i)
    {
        const double error = cv::norm(pixels[i] - projected[i]);
        if (!std::isfinite(error))
        {
            throw std::runtime_error("nonfinite validation residual");
        }
        errors.push_back(error);
    }
    return errors;
}
} // namespace sv
