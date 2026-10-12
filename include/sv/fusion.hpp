#pragma once
#include "sv/config.hpp"
#include "sv/seam_optimizer.hpp"
#include <opencv2/core.hpp>
#include <optional>
#include <vector>

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
    std::optional<seam::Summary> seam_optimization;
    cv::Mat blend_weights; // CV_32FC4; actual local base weights after seam smoothing.
};

// Opt-in diagnostic data from the actual fusion operations, before final color clamping.
struct FusionInspection
{
    cv::Mat blend_weights; // CV_32FC4; after optional seam smoothing, before pyramid reduction.
    std::vector<cv::Mat> level_weights; // CV_32FC4 normalized coefficients, fine to coarse.
    // Signed weighted bands at native level dimensions, fine to coarse. Opt-in only.
    std::vector<std::array<cv::Mat, 4>> level_contributions;
    std::array<cv::Mat, 4> camera_contributions; // CV_32FC3 signed linear RGB at full resolution.
};

FusionResult fuse_research(const FusionSamples &, const Fusion &, FusionInspection * = nullptr);
} // namespace sv
