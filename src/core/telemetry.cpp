#include "sv/telemetry.hpp"
#include "sv/protocol.hpp"
#include <fstream>
#include <limits>
#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace sv
{
ProcessObservation read_process_observation()
{
    ProcessObservation result;
#ifdef __linux__
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
    {
        result.cpu_ns =
            (uint64_t(usage.ru_utime.tv_sec) + uint64_t(usage.ru_stime.tv_sec)) * 1000000000ULL +
            (uint64_t(usage.ru_utime.tv_usec) + uint64_t(usage.ru_stime.tv_usec)) * 1000ULL;
    }
    std::ifstream statm("/proc/self/statm");
    uint64_t virtual_pages = 0, resident_pages = 0;
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size > 0 && statm >> virtual_pages >> resident_pages &&
        resident_pages <= UINT64_MAX / uint64_t(page_size))
    {
        result.rss_bytes = resident_pages * uint64_t(page_size);
    }
#endif
    return result;
}

TelemetryCollector::Update::Update(TelemetryCollector &owner, TelemetryConfig config)
    : owner_(owner), config_(config), changed_(!(config == owner.config_))
{
    if (changed_ && owner.epoch_ == UINT64_MAX)
    {
        throw std::overflow_error("telemetry_epoch_exhausted");
    }
    if (changed_ && config.enabled)
    {
        storage_ = std::make_unique<Storage>(config.history_capacity);
        session_ = std::make_shared<SourceTimingSession>(SourceTimingSession{owner.epoch_ + 1});
    }
}

void TelemetryCollector::Update::commit() noexcept
{
    if (!changed_)
    {
        return;
    }
    owner_.storage_.swap(storage_);
    owner_.config_ = config_;
    owner_.source_session_.swap(session_);
    owner_.last_source_work_.reset();
    ++owner_.epoch_;
    owner_.cursor_ = owner_.last_emitted_frame_ = 0;
    owner_.last_cpu_.reset();
    owner_.last_sample_ns_.reset();
    owner_.last_frame_.reset();
    changed_ = false;
}

TelemetryCollector::TelemetryCollector(const std::string &instance, TelemetryConfig config,
                                       std::function<ProcessObservation()> process)
    : instance_(instance), config_(config), process_(std::move(process))
{
    if (config.period_ms < 50 || config.period_ms > 5000 || config.history_capacity < 8 ||
        config.history_capacity > 256)
    {
        throw std::invalid_argument("telemetry_settings_out_of_range");
    }
    if (config.enabled)
    {
        storage_ = std::make_unique<Storage>(config.history_capacity);
        source_session_ = std::make_shared<SourceTimingSession>(SourceTimingSession{epoch_});
    }
}

std::unique_ptr<TelemetryCollector::Update> TelemetryCollector::prepare(TelemetryConfig config)
{
    if (config.period_ms < 50 || config.period_ms > 5000 || config.history_capacity < 8 ||
        config.history_capacity > 256)
    {
        throw std::invalid_argument("telemetry_settings_out_of_range");
    }
    return std::unique_ptr<Update>(new Update(*this, config));
}

bool TelemetryCollector::due(uint64_t now) const noexcept
{
    return enabled() &&
           (!last_sample_ns_ || (now >= *last_sample_ns_ &&
                                 now - *last_sample_ns_ >= uint64_t(config_.period_ms) * 1000000));
}

void TelemetryCollector::frame(const FrameTelemetry &frame, const FrameSet *inputs)
{
    if (!enabled())
    {
        return;
    }
    std::shared_ptr<SourceWorkSet> work;
    if (inputs)
    {
        for (size_t i = 0; i < inputs->frames.size(); ++i)
        {
            if (const auto &input = inputs->frames[i];
                input && input->source_work && input->source_work->session == source_session_ &&
                input->source_work->timing.camera_id == int(i) &&
                input->source_work->timing.source_sequence == input->source_sequence &&
                input->source_work->timing.epoch == epoch_)
            {
                if (!work)
                {
                    work = std::make_shared<SourceWorkSet>();
                }
                (*work)[i] = input->source_work;
            }
        }
    }
    last_frame_ = frame;
    last_source_work_ = std::move(work);
}

void TelemetryCollector::sample(uint64_t now, uint64_t revision, SourceStats source, bool paused,
                                bool ready, bool busy)
{
    if (!due(now))
    {
        return;
    }
    if (cursor_ == UINT64_MAX)
    {
        throw std::overflow_error("telemetry_cursor_exhausted");
    }
    const auto started = now_ns();
    const auto process = process_();
    std::optional<double> cpu;
    if (last_cpu_ && process.cpu_ns && *process.cpu_ns >= *last_cpu_ && last_sample_ns_ &&
        now > *last_sample_ns_)
    {
        cpu = 100.0 * double(*process.cpu_ns - *last_cpu_) / double(now - *last_sample_ns_);
    }
    Sample sample{++cursor_,
                  now,
                  revision,
                  cpu,
                  process.rss_bytes,
                  last_frame_,
                  last_frame_ && last_frame_->frame_id != last_emitted_frame_,
                  paused,
                  ready,
                  busy,
                  source};
    sample.source_work = last_source_work_;
    last_emitted_frame_ = last_frame_ ? last_frame_->frame_id : 0;
    last_cpu_ = process.cpu_ns;
    last_sample_ns_ = now;
    sample.collection_ms = double(now_ns() - started) / 1e6;
    if (storage_->ring.size() < config_.history_capacity)
    {
        storage_->ring.push_back(sample);
    }
    else
    {
        storage_->ring[storage_->head] = sample;
        storage_->head = (storage_->head + 1) % storage_->ring.size();
    }
}

boost::json::object TelemetryCollector::sample_json(const Sample &sample) const
{
    boost::json::value frame = nullptr;
    if (sample.frame)
    {
        frame = sample.frame->to_json();
        auto &object = frame.as_object();
        object["fresh"] = sample.fresh_frame;
        object["input_age_at_render_ms"] = object.at("server_receive_to_render_ms");
        object.erase("server_receive_to_render_ms");
    }
    boost::json::array source_work;
    if (sample.source_work)
    {
        for (const auto &record : *sample.source_work)
        {
            if (record)
            {
                source_work.push_back(source_work_json(record->timing));
            }
        }
    }
    return {
        {"source_work", std::move(source_work)},
        {"cursor", std::to_string(sample.cursor)},
        {"timestamp_ns", std::to_string(sample.timestamp_ns)},
        {"config_revision", std::to_string(sample.config_revision)},
        {"frame", std::move(frame)},
        {"process",
         boost::json::object{
             {"cpu_percent", sample.cpu_percent ? boost::json::value(*sample.cpu_percent)
                                                : boost::json::value(nullptr)},
             {"rss_bytes", sample.rss_bytes ? boost::json::value(std::to_string(*sample.rss_bytes))
                                            : boost::json::value(nullptr)}}},
        {"source",
         boost::json::object{
             {"camera_diagnostics", sv::camera_diagnostics_json(sample.source.camera_diagnostics)},
             {"received", std::to_string(sample.source.received)},
             {"rejected", std::to_string(sample.source.rejected)},
             {"decoded", std::to_string(sample.source.decoded)},
             {"dropped", std::to_string(sample.source.dropped)},
             {"queued_batches", std::to_string(sample.source.queued_batches)}}},
        {"paused", sample.paused},
        {"data_ready", sample.data_ready},
        {"data_busy", sample.data_busy},
        {"collection_wall_ms", sample.collection_ms}};
}

boost::json::object TelemetryCollector::catalog() const
{
    return {{"schema_version", 1},
            {"scope", "devel_telemetry"},
            {"server_instance_id", instance_},
            {"epoch", std::to_string(epoch_)},
            {"clock_domain", "server_steady_clock_ns"},
            {"settings", telemetry_settings(config_)},
            {"max_read_samples", 16},
            {"cpu_percent_basis", "one_logical_cpu_100_percent"},
            {"delivery", "bounded_control_poll"},
            {"gpu_utilization_supported", false},
            {"capture_decode_duration_supported", false},
            {"source_work_timing_v1", true},
            {"legacy_frame_timers_independent", true}};
}

boost::json::object TelemetryCollector::read(const std::string &instance, uint64_t epoch,
                                             uint64_t after, unsigned limit) const
{
    if (instance != instance_)
    {
        throw std::invalid_argument("telemetry_server_instance_mismatch");
    }
    if (epoch != epoch_)
    {
        throw std::invalid_argument("telemetry_epoch_changed");
    }
    if (after > cursor_)
    {
        throw std::invalid_argument("telemetry_cursor_ahead");
    }
    if (!limit || limit > 16)
    {
        throw std::invalid_argument("telemetry_read_limit_out_of_range");
    }
    boost::json::array samples;
    uint64_t next = after, oldest = 0, lost = 0;
    if (storage_ && !storage_->ring.empty())
    {
        oldest = storage_->ring[storage_->head].cursor;
        if (after < oldest - 1)
        {
            lost = oldest - after - 1;
        }
        for (size_t i = 0; i < storage_->ring.size() && samples.size() < limit; ++i)
        {
            const auto &sample = storage_->ring[(storage_->head + i) % storage_->ring.size()];
            if (sample.cursor > after)
            {
                samples.push_back(sample_json(sample));
                next = sample.cursor;
            }
        }
    }
    boost::json::object result{{"schema_version", 1},
                               {"scope", "devel_telemetry"},
                               {"server_instance_id", instance_},
                               {"epoch", std::to_string(epoch_)},
                               {"enabled", enabled()},
                               {"after_cursor", std::to_string(after)},
                               {"next_cursor", std::to_string(next)},
                               {"oldest_cursor", std::to_string(oldest)},
                               {"latest_cursor", std::to_string(cursor_)},
                               {"lost_samples", std::to_string(lost)},
                               {"has_more", next < cursor_},
                               {"samples", std::move(samples)}};
    // Input timing adds up to four records per sample. Honor the byte budget by paging sooner.
    auto &items = result.at("samples").as_array();
    while (items.size() > 1 && boost::json::serialize(result).size() > max_header / 2)
    {
        items.pop_back();
        const auto next =
            parse_decimal_u64(std::string(items.back().as_object().at("cursor").as_string()));
        result["next_cursor"] = std::to_string(next);
        result["has_more"] = next < cursor_;
    }
    if (boost::json::serialize(result).size() > max_header / 2)
    {
        throw std::length_error("telemetry_snapshot_too_large");
    }
    return result;
}
} // namespace sv
