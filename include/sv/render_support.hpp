#pragma once

namespace sv
{
// Actual output selection, not scene visibility or reconstruction confidence.
enum class RenderSupportPath : unsigned char
{
    background = 0,
    vehicle_footprint = 1,
    vehicle_model = 2,
    gpu_fallback = 3,
    gpu_fusion = 4,
    hybrid_fallback = 5,
    hybrid_fusion = 6
};
} // namespace sv
