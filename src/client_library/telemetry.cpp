#include "sv/client_telemetry.hpp"
#include <array>
#include <cmath>
#include <stdexcept>

namespace sv::client
{
namespace
{
uint64_t integer(const boost::json::object &object, const char *key)
{
    return parse_decimal_u64(std::string(object.at(key).as_string()));
}

double duration(const boost::json::value &value)
{
    if (!value.is_number())
    {
        throw std::invalid_argument("telemetry metric requires a number");
    }
    const auto number = value.to_number<double>();
    if (!std::isfinite(number) || number < 0)
    {
        throw std::invalid_argument("invalid telemetry metric");
    }
    return number;
}

const boost::json::object &fields(const Message &message, const char *key)
{
    if (message.type != 21 || !message.payload.empty() || !message.header.at("accepted").as_bool())
    {
        throw std::invalid_argument("telemetry requires accepted ACK");
    }
    (void)integer(message.header, "command_id");
    const auto &result = message.header.at(key).as_object();
    if (result.at("schema_version").as_int64() != 1 ||
        result.at("scope").as_string() != "devel_telemetry" ||
        result.at("server_instance_id").as_string().empty())
    {
        throw std::invalid_argument("unsupported telemetry schema or identity");
    }
    return result;
}

unsigned bounded(const boost::json::value &value, unsigned min, unsigned max)
{
    if ((!value.is_int64() && !value.is_uint64()) || (value.is_int64() && value.as_int64() < 0))
    {
        throw std::invalid_argument("telemetry integer required");
    }
    const auto number = value.to_number<uint64_t>();
    if (number < min || number > max)
    {
        throw std::invalid_argument("telemetry setting out of range");
    }
    return unsigned(number);
}
} // namespace

TelemetryCatalog parse_telemetry_catalog(const Message &message)
{
    const auto &data = fields(message, "telemetry_catalog");
    TelemetryCatalog result;
    result.server_instance_id = std::string(data.at("server_instance_id").as_string());
    result.epoch = integer(data, "epoch");
    result.clock_domain = std::string(data.at("clock_domain").as_string());
    result.cpu_percent_basis = std::string(data.at("cpu_percent_basis").as_string());
    if (result.clock_domain != "server_steady_clock_ns" ||
        result.cpu_percent_basis != "one_logical_cpu_100_percent" ||
        data.at("delivery").as_string() != "bounded_control_poll")
    {
        throw std::invalid_argument("unsupported telemetry units or delivery");
    }
    const auto &settings = data.at("settings").as_object();
    result.settings.enabled = settings.at("enabled").as_bool();
    result.settings.period_ms = bounded(settings.at("period_ms"), 50, 5000);
    result.settings.history_capacity = bounded(settings.at("history_capacity"), 8, 256);
    result.max_read_samples = bounded(data.at("max_read_samples"), 1, 16);
    if (const auto *supported = data.if_contains("source_work_timing_v1"))
    {
        result.source_work_supported = supported->as_bool();
    }
    return result;
}

TelemetryBatch parse_telemetry_batch(const Message &message)
{
    const auto &data = fields(message, "telemetry");
    TelemetryBatch result;
    result.server_instance_id = std::string(data.at("server_instance_id").as_string());
    result.epoch = integer(data, "epoch");
    result.after_cursor = integer(data, "after_cursor");
    result.next_cursor = integer(data, "next_cursor");
    result.oldest_cursor = integer(data, "oldest_cursor");
    result.latest_cursor = integer(data, "latest_cursor");
    result.lost_samples = integer(data, "lost_samples");
    result.enabled = data.at("enabled").as_bool();
    result.has_more = data.at("has_more").as_bool();
    const auto &samples = data.at("samples").as_array();
    if (samples.size() > 16 || result.after_cursor > result.latest_cursor ||
        result.oldest_cursor > result.latest_cursor ||
        (result.oldest_cursor == 0) != (result.latest_cursor == 0) ||
        result.lost_samples != (result.oldest_cursor > result.after_cursor &&
                                        result.oldest_cursor - result.after_cursor > 1
                                    ? result.oldest_cursor - result.after_cursor - 1
                                    : 0) ||
        (!result.enabled && (result.latest_cursor || !samples.empty())))
    {
        throw std::invalid_argument("inconsistent telemetry window");
    }
    auto previous = result.after_cursor + result.lost_samples;
    uint64_t timestamp = 0;
    const auto revision = integer(message.header, "config_revision");
    for (const auto &entry : samples)
    {
        const auto &item = entry.as_object();
        TelemetrySample sample;
        sample.cursor = integer(item, "cursor");
        sample.timestamp_ns = integer(item, "timestamp_ns");
        sample.config_revision = integer(item, "config_revision");
        if (previous == UINT64_MAX || sample.cursor != previous + 1 ||
            sample.cursor > result.latest_cursor || sample.timestamp_ns < timestamp ||
            sample.config_revision > revision)
        {
            throw std::invalid_argument("inconsistent telemetry sample sequence");
        }
        previous = sample.cursor;
        timestamp = sample.timestamp_ns;
        const auto &process = item.at("process").as_object();
        if (!process.at("cpu_percent").is_null())
        {
            sample.process.cpu_percent = duration(process.at("cpu_percent"));
        }
        if (!process.at("rss_bytes").is_null())
        {
            sample.process.rss_bytes = integer(process, "rss_bytes");
        }
        const auto &source = item.at("source").as_object();
        sample.source = {integer(source, "received"), integer(source, "rejected"),
                         integer(source, "decoded"), integer(source, "dropped"),
                         integer(source, "queued_batches")};
        sample.paused = item.at("paused").as_bool();
        sample.data_ready = item.at("data_ready").as_bool();
        sample.data_busy = item.at("data_busy").as_bool();
        sample.collection_wall_ms = duration(item.at("collection_wall_ms"));
        if (!item.at("frame").is_null())
        {
            const auto &frame = item.at("frame").as_object();
            TelemetryFrame parsed;
            parsed.frame_id = integer(frame, "frame_id");
            parsed.sequence_id = integer(frame, "sequence_id");
            parsed.config_revision = integer(frame, "config_revision");
            parsed.timestamp_ns = integer(frame, "timestamp_ns");
            parsed.fresh = frame.at("fresh").as_bool();
            parsed.input_age_at_render_ms = duration(frame.at("input_age_at_render_ms"));
            parsed.spans_ms = frame.at("spans_ms").as_object();
            for (const auto *name :
                 {"source_poll", "pre_render_prepare", "render_wall", "gpu_draw", "fusion_cpu",
                  "layer_readback_cpu", "upload_cpu", "readback_copy_cpu", "publish_enqueue"})
            {
                const auto &span = parsed.spans_ms.at(name);
                if (std::string(name) != "gpu_draw" || !span.is_null())
                {
                    (void)duration(span);
                }
            }
            if (parsed.timestamp_ns > sample.timestamp_ns ||
                parsed.config_revision > sample.config_revision)
            {
                throw std::invalid_argument("inconsistent telemetry frame identity");
            }
            sample.frame = std::move(parsed);
        }
        if (const auto *work = item.if_contains("source_work"))
        {
            std::array<bool, 4> seen{};
            if (work->as_array().size() > 4 || (!work->as_array().empty() && !sample.frame))
            {
                throw std::invalid_argument("invalid source work count or missing frame");
            }
            for (const auto &entry : work->as_array())
            {
                const auto &value = entry.as_object();
                sv::SourceWorkTiming timing;
                timing.camera_id = int(bounded(value.at("camera_id"), 0, 3));
                if (seen[timing.camera_id])
                {
                    throw std::invalid_argument("duplicate source work camera");
                }
                seen[timing.camera_id] = true;
                timing.kind = std::string(value.at("kind").as_string());
                timing.epoch = integer(value, "epoch");
                timing.source_sequence = integer(value, "source_sequence");
                timing.started_ns = integer(value, "started_ns");
                timing.completed_ns = integer(value, "completed_ns");
                timing.cache_hit = value.at("cache_hit").as_bool();
                auto metric = [&](const char *name) -> std::optional<double>
                {
                    return value.at(name).is_null()
                               ? std::nullopt
                               : std::optional<double>(duration(value.at(name)));
                };
                timing.camera_read_wall_ms = metric("camera_read_wall_ms");
                timing.image_load_wall_ms = metric("image_load_wall_ms");
                timing.image_prepare_wall_ms = metric("image_prepare_wall_ms");
                if (timing.epoch != result.epoch || timing.started_ns > timing.completed_ns ||
                    timing.completed_ns > sample.frame->timestamp_ns)
                {
                    throw std::invalid_argument("invalid source work series or clock");
                }
                const double elapsed = double(timing.completed_ns - timing.started_ns) / 1e6;
                if (timing.kind == "camera_read")
                {
                    if (!timing.camera_read_wall_ms || !timing.image_prepare_wall_ms ||
                        timing.image_load_wall_ms || timing.cache_hit ||
                        *timing.camera_read_wall_ms + *timing.image_prepare_wall_ms >
                            elapsed + .000001)
                    {
                        throw std::invalid_argument("invalid camera work durations");
                    }
                }
                else if (timing.kind == "replay_load")
                {
                    if (!timing.image_load_wall_ms || timing.camera_read_wall_ms ||
                        timing.image_prepare_wall_ms ||
                        *timing.image_load_wall_ms > elapsed + .000001 ||
                        (timing.cache_hit && (*timing.image_load_wall_ms != 0 ||
                                              timing.started_ns != timing.completed_ns)))
                    {
                        throw std::invalid_argument("invalid replay work durations");
                    }
                }
                else
                {
                    throw std::invalid_argument("unsupported source work kind");
                }
                sample.source_work.push_back(std::move(timing));
            }
        }
        result.samples.push_back(std::move(sample));
    }
    const auto expected_next = samples.empty() ? result.after_cursor : previous;
    if (result.next_cursor != expected_next ||
        result.has_more != (result.next_cursor < result.latest_cursor) ||
        (result.has_more && samples.empty()))
    {
        throw std::invalid_argument("inconsistent telemetry pagination");
    }
    return result;
}
} // namespace sv::client
