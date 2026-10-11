#pragma once
#include "sv/fusion.hpp"

namespace sv::fusion_detail
{
cv::Mat gaussian(const cv::Mat &, double sigma);
cv::Mat resize_linear(const cv::Mat &, cv::Size);
std::array<cv::Mat, 4> distances(const FusionSamples &);
cv::Mat cut_weights(const FusionSamples &, const std::array<cv::Mat, 4> &, double);
cv::Mat multilabel_weights(const FusionSamples &, const std::array<cv::Mat, 4> &, double,
                           seam::Summary &);
cv::Mat multiband(const FusionSamples &, const cv::Mat &weights, unsigned levels,
                  const std::string &boundary);
} // namespace sv::fusion_detail
