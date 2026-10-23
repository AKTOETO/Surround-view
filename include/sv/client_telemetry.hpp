#pragma once
#include "sv/camera_diagnostics.hpp"
#include "sv/camera_frame_provenance.hpp"
#include "sv/protocol.hpp"
#include "sv/source_work_timing.hpp"
#include <optional>
#include <vector>

namespace sv::client
{
struct TelemetrySettings
{
    bool enabled = false;
    unsigned period_ms = 100, history_capacity = 128;
};

struct TelemetryCatalog
{
    std::string server_instance_id, clock_domain, cpu_percent_basis;
    uint64_t epoch = 0;
    TelemetrySettings settings;
    unsigned max_read_samples = 16;
    bool source_work_supported = false;
    bool camera_failure_timing_supported = false;
};

struct TelemetryProcess
{
    std::optional<double> cpu_percent;
    std::optional<uint64_t> rss_bytes;
};

struct TelemetrySource
{
    uint64_t received = 0, rejected = 0, decoded = 0, dropped = 0, queued_batches = 0;
    std::optional<sv::CameraDiagnosticsSet> camera_diagnostics = {};
};

struct TelemetryFrame
{
    uint64_t frame_id = 0, sequence_id = 0, config_revision = 0, timestamp_ns = 0;
    bool fresh = false;
    double input_age_at_render_ms = 0;
    // Wall spans overlap; gpu_draw is nullable and does not cover the complete hybrid path.
    boost::json::object spans_ms;
    std::vector<sv::CameraInputProvenance> camera_inputs = {};
};

struct TelemetrySample
{
    uint64_t cursor = 0, timestamp_ns = 0, config_revision = 0;
    TelemetryProcess process;
    TelemetrySource source;
    std::optional<TelemetryFrame> frame;
    bool paused = false, data_ready = false, data_busy = false;
    double collection_wall_ms = 0;
    std::vector<sv::SourceWorkTiming> source_work;
};

struct TelemetryBatch
{
    std::string server_instance_id;
    uint64_t epoch = 0, after_cursor = 0, next_cursor = 0, oldest_cursor = 0, latest_cursor = 0;
    uint64_t lost_samples = 0;
    bool enabled = false, has_more = false;
    std::vector<TelemetrySample> samples;
};

TelemetryCatalog parse_telemetry_catalog(const Message &);
TelemetryBatch parse_telemetry_batch(const Message &);
} // namespace sv::client
