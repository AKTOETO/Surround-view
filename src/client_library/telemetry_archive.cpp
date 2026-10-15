#include "sv/client_telemetry_archive.hpp"
#include <boost/version.hpp>
#include <stdexcept>

namespace sv::client
{
namespace
{
uint64_t integer(const boost::json::object &object, const char *key)
{
    return parse_decimal_u64(std::string(object.at(key).as_string()));
}

boost::json::object catalog_json(const TelemetryCatalog &catalog)
{
    return {
        {"schema_version", 1},
        {"scope", "devel_telemetry"},
        {"server_instance_id", catalog.server_instance_id},
        {"epoch", std::to_string(catalog.epoch)},
        {"clock_domain", catalog.clock_domain},
        {"cpu_percent_basis", catalog.cpu_percent_basis},
        {"delivery", "bounded_control_poll"},
        {"max_read_samples", catalog.max_read_samples},
        {"source_work_timing_v1", catalog.source_work_supported},
        {"settings", boost::json::object{{"enabled", catalog.settings.enabled},
                                         {"period_ms", catalog.settings.period_ms},
                                         {"history_capacity", catalog.settings.history_capacity}}}};
}

boost::json::object sample_json(const TelemetrySample &sample)
{
    boost::json::value frame = nullptr;
    if (sample.frame)
    {
        const auto &value = *sample.frame;
        frame = boost::json::object{{"frame_id", std::to_string(value.frame_id)},
                                    {"sequence_id", std::to_string(value.sequence_id)},
                                    {"config_revision", std::to_string(value.config_revision)},
                                    {"timestamp_ns", std::to_string(value.timestamp_ns)},
                                    {"fresh", value.fresh},
                                    {"spans_ms", value.spans_ms},
                                    {"input_age_at_render_ms", value.input_age_at_render_ms}};
    }
    boost::json::array work;
    for (const auto &timing : sample.source_work)
    {
        work.push_back(sv::source_work_json(timing));
    }
    return {{"source_work", std::move(work)},
            {"cursor", std::to_string(sample.cursor)},
            {"timestamp_ns", std::to_string(sample.timestamp_ns)},
            {"config_revision", std::to_string(sample.config_revision)},
            {"frame", std::move(frame)},
            {"process",
             boost::json::object{
                 {"cpu_percent", sample.process.cpu_percent
                                     ? boost::json::value(*sample.process.cpu_percent)
                                     : boost::json::value(nullptr)},
                 {"rss_bytes", sample.process.rss_bytes
                                   ? boost::json::value(std::to_string(*sample.process.rss_bytes))
                                   : boost::json::value(nullptr)}}},
            {"source",
             boost::json::object{{"camera_diagnostics",
                                  sv::camera_diagnostics_json(sample.source.camera_diagnostics)},
                                 {"received", std::to_string(sample.source.received)},
                                 {"rejected", std::to_string(sample.source.rejected)},
                                 {"decoded", std::to_string(sample.source.decoded)},
                                 {"dropped", std::to_string(sample.source.dropped)},
                                 {"queued_batches", std::to_string(sample.source.queued_batches)}}},
            {"paused", sample.paused},
            {"data_ready", sample.data_ready},
            {"data_busy", sample.data_busy},
            {"collection_wall_ms", sample.collection_wall_ms}};
}

Message ack(const char *key, boost::json::object fields)
{
    return {21,
            {{"accepted", true},
             {"command_id", "0"},
             {"config_revision", std::to_string(UINT64_MAX)},
             {key, std::move(fields)}},
            {}};
}
} // namespace

TelemetryArchive parse_telemetry_archive(std::string_view text)
{
    if (text.size() > telemetry_archive_max_bytes)
    {
        throw std::length_error("telemetry_archive_too_large");
    }
    boost::json::parse_options options;
#if BOOST_VERSION >= 108300
    // Local report fidelity only; do not silently change protocol/config/research parsing.
    options.numbers = boost::json::number_precision::precise;
#endif
    const auto root = boost::json::parse(text, {}, options);
    const auto &object = root.as_object();
    if (object.at("schema_version").as_int64() != 1 ||
        object.at("kind").as_string() != "sv_telemetry_window")
    {
        throw std::invalid_argument("unsupported_telemetry_archive");
    }
    TelemetryArchive result;
    result.catalog =
        parse_telemetry_catalog(ack("telemetry_catalog", object.at("catalog").as_object()));
    result.origin_ns = integer(object, "origin_ns");
    result.latest_cursor = integer(object, "latest_cursor");
    result.lost_samples_total = integer(object, "lost_samples_total");
    result.label = std::string(object.at("label").as_string());
    result.first_gap = object.at("first_gap").as_bool();
    const auto &samples = object.at("samples").as_array();
    if (samples.size() > telemetry_archive_max_samples || result.label.size() > 256 ||
        result.latest_cursor < samples.size() ||
        result.lost_samples_total > result.latest_cursor - samples.size() ||
        (!result.catalog.settings.enabled && !samples.empty()) ||
        (result.first_gap && samples.empty()))
    {
        throw std::invalid_argument("invalid_telemetry_archive_window");
    }
    uint64_t previous = 0, timestamp = result.origin_ns, observed_loss = 0;
    for (const auto &entry : samples)
    {
        const auto cursor = integer(entry.as_object(), "cursor");
        if (!cursor || (!result.samples.empty() && cursor <= previous))
        {
            throw std::invalid_argument("telemetry_archive_cursor_order");
        }
        // First retained cursor need not be 1: older received records may have left the UI window.
        const auto after = result.samples.empty() ? cursor - 1 : previous;
        const auto lost = cursor - after - 1;
        if (UINT64_MAX - observed_loss < lost)
        {
            throw std::invalid_argument("telemetry_archive_loss_overflow");
        }
        observed_loss += lost;
        boost::json::object batch{{"schema_version", 1},
                                  {"scope", "devel_telemetry"},
                                  {"server_instance_id", result.catalog.server_instance_id},
                                  {"epoch", std::to_string(result.catalog.epoch)},
                                  {"enabled", true},
                                  {"after_cursor", std::to_string(after)},
                                  {"next_cursor", std::to_string(cursor)},
                                  {"oldest_cursor", std::to_string(cursor)},
                                  {"latest_cursor", std::to_string(cursor)},
                                  {"lost_samples", std::to_string(lost)},
                                  {"has_more", false},
                                  {"samples", boost::json::array{entry}}};
        auto sample = parse_telemetry_batch(ack("telemetry", std::move(batch))).samples.front();
        if (sample.timestamp_ns < timestamp)
        {
            throw std::invalid_argument("telemetry_archive_timestamp_order");
        }
        timestamp = sample.timestamp_ns;
        previous = cursor;
        result.samples.push_back(std::move(sample));
    }
    if ((!samples.empty() && previous != result.latest_cursor) ||
        observed_loss > result.lost_samples_total)
    {
        throw std::invalid_argument("telemetry_archive_cursor_or_loss_mismatch");
    }
    return result;
}

std::string serialize_telemetry_archive(const TelemetryArchive &archive)
{
    if (archive.samples.size() > telemetry_archive_max_samples || archive.label.size() > 256)
    {
        throw std::length_error("telemetry_archive_too_large");
    }
    boost::json::array samples;
    for (const auto &sample : archive.samples)
    {
        samples.push_back(sample_json(sample));
    }
    const auto result = boost::json::serialize(
        boost::json::object{{"schema_version", 1},
                            {"kind", "sv_telemetry_window"},
                            {"catalog", catalog_json(archive.catalog)},
                            {"origin_ns", std::to_string(archive.origin_ns)},
                            {"latest_cursor", std::to_string(archive.latest_cursor)},
                            {"lost_samples_total", std::to_string(archive.lost_samples_total)},
                            {"first_gap", archive.first_gap},
                            {"label", archive.label},
                            {"samples", std::move(samples)}});
    (void)parse_telemetry_archive(result); // Same wire validators on export and import.
    return result;
}
} // namespace sv::client
