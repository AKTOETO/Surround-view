#pragma once
#include "sv/config.hpp"
#include <opencv2/core.hpp>

namespace sv
{
struct FusionSamples
{
    std::array<cv::Mat, 4> colors;       // CV_32FC3 linear RGB, output coordinates
    std::array<cv::Mat, 4> validity;     // CV_8UC1, nonzero means observed
    std::array<cv::Mat, 4> edge_weights; // CV_32FC1, clamped edge/edge_width
};

struct FusionResult
{
    cv::Mat color;   // CV_32FC3
    cv::Mat weights; // CV_32FC4; graph-cut weights before optional smoothing
};

FusionResult fuse_research(const FusionSamples &, const Fusion &);
} // namespace sv
