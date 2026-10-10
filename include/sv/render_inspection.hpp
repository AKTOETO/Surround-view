#pragma once
#include "sv/frame.hpp"
#include "sv/fusion.hpp"

namespace sv
{
// Explicit, render-thread-only capture for numerical inspection. Not a wire subscription.
// Retained matrices own their data through OpenCV reference counting.
struct RenderInspection
{
    std::optional<FusionSamples> samples;
    Image fallback_rgba;
    cv::Mat ego_rgba; // CV_8UC4, top-left origin; empty for diagnostic views.
};
} // namespace sv
