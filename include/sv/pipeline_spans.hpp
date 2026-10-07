#pragma once

#include "sv/interfaces.hpp"
#include <algorithm>
#include <boost/json.hpp>
#include <cstdint>
#include <deque>
#include <mutex>
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
        return static_cast<double>(end_ns - start_ns) / 1e6;
    }
};

struct FrameTelemetry
{
    uint64_t frame_id = 0;
    uint64_t sequence_id = 0;
    uint64_t config_revision = 0;
    uint64_t timestamp_ns = 0;

    double receive_decode_ms = 0.0;
    double queues_sync_ms = 0.0;
    double projection_fusion_ms = 0.0;
    double upload_draw_readback_ms = 0.0;
    double publish_send_ms = 0.0;
    double total_pipeline_ms = 0.0;

    boost::json::object to_json() const
    {
        return {
            {"frame_id", std::to_string(frame_id)},
            {"sequence_id", std::to_string(sequence_id)},
            {"config_revision", std::to_string(config_revision)},
            {"timestamp_ns", std::to_string(timestamp_ns)},
            {"spans_ms", {
                {"receive_decode", receive_decode_ms},
                {"queues_sync", queues_sync_ms},
                {"projection_fusion", projection_fusion_ms},
                {"upload_draw_readback", upload_draw_readback_ms},
                {"publish_send", publish_send_ms}
            }},
            {"total_pipeline_ms", total_pipeline_ms}
        };
    }
};

class PipelineSpanTracker
{
public:
    explicit PipelineSpanTracker(size_t ring_capacity = 120)
        : capacity_(ring_capacity)
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
