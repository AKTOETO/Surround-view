#pragma once

namespace sv
{
// Per-camera policy result on the actual carrier; not independent scene truth.
enum class SourceVisibilityStatus : unsigned char
{
    no_carrier = 0,
    eligible_unclassified = 1,
    unavailable = 2,
    behind_camera = 3,
    outside_angle = 4,
    outside_image = 5,
    vehicle_footprint = 6,
    central_occluder = 7,
    uncertain_occluder = 8,
    backfacing_support = 9,
    accepted_ambiguous = 10,
    accepted_clear = 11,
    footprint_occluder = 12
};

constexpr bool source_visibility_eligible(SourceVisibilityStatus status) noexcept
{
    return status == SourceVisibilityStatus::eligible_unclassified ||
           status == SourceVisibilityStatus::accepted_ambiguous ||
           status == SourceVisibilityStatus::accepted_clear;
}
} // namespace sv
