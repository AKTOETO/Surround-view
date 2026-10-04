#pragma once

#include "sv/frame.hpp"
#include <opencv2/core.hpp>

namespace sv
{
std::vector<Pixel> project_opencv(const Camera &camera, const std::vector<Vec3> &points);
Image read_image(const std::filesystem::path &path);
std::string opencv_version();
cv::Matx33d camera_matrix(const Camera &camera);
std::vector<cv::Point2f> detect_board(const cv::Mat &image, cv::Size inner_corners);

struct IntrinsicCalibration
{
    cv::Matx33d K;
    cv::Vec4d distortion;
    double training_rmse_px = 0;
};

IntrinsicCalibration calibrate_intrinsics(const std::vector<std::vector<cv::Point3d>> &objects,
                                          const std::vector<std::vector<cv::Point2d>> &pixels,
                                          cv::Size image_size, double theta_max = 1.45);
std::vector<double> intrinsic_validation_errors(const IntrinsicCalibration &calibration,
                                                const std::vector<cv::Point3d> &objects,
                                                const std::vector<cv::Point2d> &pixels);
} // namespace sv
