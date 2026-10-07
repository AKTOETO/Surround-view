#pragma once

#include "sv/interfaces.hpp"
#include <algorithm>
#include <boost/json.hpp>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sv
{

struct PipelineSpan
{
    std::string name;
    uint64_t start_ns = 0;
    uint64_t end_ns = 0;

    double duration_ms() const
    {
        return end_ns >= start_ns ? static_cast<double>(end_ns - start_ns) / 1e6 : 0.0;
    }
};

struct FrameTelemetry
{
    uint64_t frame_id = 0;
    uint64_t sequence_id = 0;
    uint64_t config_revision = 0;
    uint64_t timestamp_ns = 0;

    double source_poll_ms = 0.0;
    double pre_render_prepare_ms = 0.0;
    double render_wall_ms = 0.0;
    std::optional<double> gpu_draw_ms;
    double upload_cpu_ms = 0.0;
    double readback_copy_cpu_ms = 0.0;
    double publish_enqueue_ms = 0.0;
    double total_pipeline_ms = 0.0;

    boost::json::object to_json() const
    {
        boost::json::object spans{
            {"source_poll", source_poll_ms},
            {"pre_render_prepare", pre_render_prepare_ms},
            {"render_wall", render_wall_ms},
            {"gpu_draw", gpu_draw_ms ? boost::json::value(*gpu_draw_ms)
                                      : boost::json::value(nullptr)},
            {"upload_cpu", upload_cpu_ms},
            {"readback_copy_cpu", readback_copy_cpu_ms},
            {"publish_enqueue", publish_enqueue_ms}};
        return {
            {"frame_id", std::to_string(frame_id)},
            {"sequence_id", std::to_string(sequence_id)},
            {"config_revision", std::to_string(config_revision)},
            {"timestamp_ns", std::to_string(timestamp_ns)},
            {"spans_ms", std::move(spans)},
            {"server_receive_to_render_ms", total_pipeline_ms}
        };
    }
};

class PipelineSpanTracker
{
public:
    explicit PipelineSpanTracker(size_t ring_capacity = 120)
        : capacity_(std::max<size_t>(1, ring_capacity))
    {
    }

    void record_frame(const FrameTelemetry &telemetry)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (history_.size() >= capacity_)
        {
            history_.pop_front();
        }
        history_.push_back(telemetry);
    }

    std::vector<FrameTelemetry> history() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<FrameTelemetry>(history_.begin(), history_.end());
    }

    double median_total_latency_ms() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (history_.empty())
        {
            return 0.0;
        }
        std::vector<double> latencies;
        latencies.reserve(history_.size());
        for (const auto &item : history_)
        {
            latencies.push_back(item.total_pipeline_ms);
        }
        std::sort(latencies.begin(), latencies.end());
        size_t mid = latencies.size() / 2;
        if (latencies.size() % 2 == 0)
        {
            return (latencies[mid - 1] + latencies[mid]) / 2.0;
        }
        return latencies[mid];
    }

    size_t count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return history_.size();
    }

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<FrameTelemetry> history_;
};

} // namespace sv
