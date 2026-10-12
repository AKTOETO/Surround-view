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

enum class SourceProjectionStatus : unsigned char
{
    no_carrier = 0,
    valid = 1,
    unavailable = 2,
    behind_camera = 3,
    outside_angle = 4,
    outside_image = 5,
    vehicle_footprint = 6,
    supplied_occluder = 7
};

struct SourceProjectionInspection
{
    cv::Mat pixels; // CV_32FC2, source pixel centres; NaN unless valid.
    cv::Mat status; // CV_8UC1, SourceProjectionStatus; optional supplied-box occlusion.
};

struct RenderInspectionRequest
{
    bool carrier_positions = false;    // Opt-in: three extra RGBA8 passes/readbacks.
    bool source_projections = false;   // Opt-in: twelve extra passes/readbacks.
    bool fusion_contributions = false; // Hybrid research modes only; no extra GLES passes.
};

// Explicit, render-thread-only capture for numerical inspection. Not a wire subscription.
// Retained matrices own their data through OpenCV reference counting.
struct RenderInspection
{
    std::optional<FusionSamples> samples;
    std::optional<FusionInspection> fusion;
    Image fallback_rgba;
    cv::Mat ego_rgba; // CV_8UC4, top-left origin; empty for diagnostic views.
    // CV_32FC3, vehicle metres, top-left. NaN for background/ego; empty unless requested.
    // Perspective-interpolated actual mesh positions, not ideal-surface intersections.
    cv::Mat carrier_positions;
    std::array<SourceProjectionInspection, 4> source_projections;
    cv::Mat carrier_regions; // CV_8UC1, top-left; CarrierRegion IDs, including ego geometry.
};
} // namespace sv
