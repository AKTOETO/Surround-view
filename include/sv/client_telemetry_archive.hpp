#pragma once
#include "sv/client_telemetry.hpp"
#include <string_view>

namespace sv::client
{
// Bounded window snapshot, not a full trace or an experiment/configuration backup.
struct TelemetryArchive
{
    TelemetryCatalog catalog;
    uint64_t origin_ns = 0, latest_cursor = 0, lost_samples_total = 0;
    std::string label;
    std::vector<TelemetrySample> samples;
    bool first_gap = false;
};

inline constexpr size_t telemetry_archive_max_bytes = 2 * 1024 * 1024;
inline constexpr size_t telemetry_archive_max_samples = 120;
std::string serialize_telemetry_archive(const TelemetryArchive &);
TelemetryArchive parse_telemetry_archive(std::string_view);
} // namespace sv::client
