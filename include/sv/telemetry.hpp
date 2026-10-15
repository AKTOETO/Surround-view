#pragma once
#include "sv/pipeline_spans.hpp"
#include "sv/source.hpp"
#include "sv/telemetry_config.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace sv
{
struct ProcessObservation
{
    std::optional<uint64_t> cpu_ns, rss_bytes;
};

ProcessObservation read_process_observation();

// Render-thread only. No background thread or process sampler calls when disabled.
class TelemetryCollector
{
    using SourceWorkSet = std::array<std::shared_ptr<const SourceWorkRecord>, 4>;

    struct Sample
    {
        uint64_t cursor, timestamp_ns, config_revision;
        std::optional<double> cpu_percent;
        std::optional<uint64_t> rss_bytes;
        std::optional<FrameTelemetry> frame;
        bool fresh_frame, paused, data_ready, data_busy;
        SourceStats source;
        double collection_ms = 0;
        std::shared_ptr<const SourceWorkSet> source_work = {};
    };

    struct Storage
    {
        std::vector<Sample> ring;
        size_t head = 0;

        explicit Storage(size_t capacity)
        {
            ring.reserve(capacity);
        }
    };

    const std::string instance_;
    TelemetryConfig config_;
    std::unique_ptr<Storage> storage_;
    std::function<ProcessObservation()> process_;
    std::optional<FrameTelemetry> last_frame_;
    std::shared_ptr<const SourceTimingSession> source_session_;
    std::shared_ptr<const SourceWorkSet> last_source_work_;
    std::optional<uint64_t> last_cpu_, last_sample_ns_;
    uint64_t epoch_ = 0, cursor_ = 0, last_emitted_frame_ = 0;

    boost::json::object sample_json(const Sample &) const;

  public:

    class Update
    {
        friend class TelemetryCollector;
        TelemetryCollector &owner_;
        TelemetryConfig config_;
        std::unique_ptr<Storage> storage_;
        bool changed_;
        std::shared_ptr<const SourceTimingSession> session_;
        Update(TelemetryCollector &, TelemetryConfig);

      public:

        void commit() noexcept;
    };

    TelemetryCollector(const std::string &instance, TelemetryConfig,
                       std::function<ProcessObservation()> process = read_process_observation);
    std::unique_ptr<Update> prepare(TelemetryConfig);

    bool enabled() const noexcept
    {
        return config_.enabled;
    }

    bool due(uint64_t now) const noexcept;

    std::shared_ptr<const SourceTimingSession> source_session() const noexcept
    {
        return source_session_;
    }

    uint64_t epoch() const noexcept
    {
        return epoch_;
    }

    void frame(const FrameTelemetry &, const FrameSet *inputs = nullptr);

    void sample(uint64_t now, uint64_t config_revision, SourceStats, bool paused, bool ready,
                bool busy);
    boost::json::object catalog() const;
    boost::json::object read(const std::string &instance, uint64_t epoch, uint64_t after,
                             unsigned limit) const;
};
} // namespace sv
