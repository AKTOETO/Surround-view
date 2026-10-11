#pragma once
#include "sv/frame.hpp"
#include "sv/fusion.hpp"

namespace sv
{
// Rasterized carrier geometry, independent of source-camera validity or scene truth.
enum class CarrierRegion : unsigned char
{
    background = 0,
    floor = 1,
    shell = 2,
    vehicle_footprint = 3,
    raised_bowl = 4,
    vehicle_model = 5
};

struct RenderInspectionRequest
{
    bool carrier_positions = false; // Opt-in: three extra RGBA8 passes/readbacks.
};

// Explicit, render-thread-only capture for numerical inspection. Not a wire subscription.
// Retained matrices own their data through OpenCV reference counting.
struct RenderInspection
{
    std::optional<FusionSamples> samples;
    Image fallback_rgba;
    cv::Mat ego_rgba; // CV_8UC4, top-left origin; empty for diagnostic views.
    // CV_32FC3, vehicle metres, top-left. NaN for background/ego; empty unless requested.
    // Perspective-interpolated actual mesh positions, not ideal-surface intersections.
    cv::Mat carrier_positions;
    cv::Mat carrier_regions; // CV_8UC1, top-left; CarrierRegion IDs, including ego geometry.
};
} // namespace sv
