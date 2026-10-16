#include "sv/client_telemetry.hpp"
#include "sv/config_service.hpp"
#include "sv/telemetry.hpp"
#include <gtest/gtest.h>

namespace
{
sv::Message reply(boost::json::object data, const char *key = "telemetry")
{
    return {21,
            {{"accepted", true},
             {"command_id", "1"},
             {"config_revision", "10"},
             {key, std::move(data)}},
            {}};
}
} // namespace

TEST(Telemetry, DisabledNeverSamplesProcessAndValidatesInitialBounds)
{
    unsigned calls = 0;
    sv::TelemetryCollector collector("instance", {},
                                     [&]
                                     {
                                         ++calls;
                                         return sv::ProcessObservation{};
                                     });
    collector.sample(100000000, 0, {}, false, true, false);
    EXPECT_EQ(calls, 0U);
    EXPECT_FALSE(collector.due(UINT64_MAX));
    auto catalog =
        sv::client::parse_telemetry_catalog(reply(collector.catalog(), "telemetry_catalog"));
    EXPECT_FALSE(catalog.settings.enabled);
    auto batch = sv::client::parse_telemetry_batch(reply(collector.read("instance", 0, 0, 16)));
    EXPECT_TRUE(batch.samples.empty());
    EXPECT_THROW(sv::TelemetryCollector("instance", (sv::TelemetryConfig{true, 0, 8})),
                 std::invalid_argument);
}

TEST(Telemetry, PeriodicProcessAndFrameMetricsHaveIndependentFreshness)
{
    uint64_t cpu = 100000000;
    sv::TelemetryCollector collector("instance", {true, 50, 8},
                                     [&] { return sv::ProcessObservation{cpu, 4096}; });
    sv::FrameTelemetry frame;
    frame.frame_id = 1;
    frame.timestamp_ns = 1;
    frame.config_revision = 2;
    collector.frame(frame);
    collector.sample(100000000, 2, {}, true, true, true);
    EXPECT_FALSE(collector.due(149999999));
    cpu += 100000000;
    collector.sample(150000000, 2, {}, true, true, true);
    auto batch = sv::client::parse_telemetry_batch(reply(collector.read("instance", 0, 0, 16)));
    ASSERT_EQ(batch.samples.size(), 2U);
    EXPECT_FALSE(batch.samples[0].process.cpu_percent);
    EXPECT_EQ(batch.samples[1].process.cpu_percent, 200);
    EXPECT_EQ(batch.samples[1].process.rss_bytes, 4096U);
    ASSERT_TRUE(batch.samples[0].frame);
    EXPECT_TRUE(batch.samples[0].frame->fresh);
    EXPECT_FALSE(batch.samples[1].frame->fresh);
    EXPECT_TRUE(batch.samples[1].paused);
    EXPECT_TRUE(batch.samples[1].data_busy);
}

TEST(Telemetry, MissingOrResetCpuCounterStartsANewBaseline)
{
    std::optional<uint64_t> cpu = 10;
    sv::TelemetryCollector collector("instance", {true, 50, 8},
                                     [&] { return sv::ProcessObservation{cpu, std::nullopt}; });
    collector.sample(50000000, 0, {}, false, false, false);
    cpu = 5;
    collector.sample(100000000, 0, {}, false, false, false);
    cpu.reset();
    collector.sample(150000000, 0, {}, false, false, false);
    cpu = 20;
    collector.sample(200000000, 0, {}, false, false, false);
    const auto batch =
        sv::client::parse_telemetry_batch(reply(collector.read("instance", 0, 0, 16)));
    for (const auto &sample : batch.samples)
    {
        EXPECT_FALSE(sample.process.cpu_percent);
        EXPECT_FALSE(sample.process.rss_bytes);
    }
}

TEST(Telemetry, RingPaginationReportsLossAndRejectsForeignCursors)
{
    sv::TelemetryCollector collector("instance", {true, 50, 8},
                                     [] { return sv::ProcessObservation{}; });
    for (uint64_t i = 1; i <= 12; ++i)
    {
        collector.sample(i * 50000000, 0, {}, true, false, false);
    }
    auto first = sv::client::parse_telemetry_batch(reply(collector.read("instance", 0, 0, 3)));
    EXPECT_EQ(first.lost_samples, 4U);
    EXPECT_EQ(first.oldest_cursor, 5U);
    EXPECT_EQ(first.next_cursor, 7U);
    EXPECT_TRUE(first.has_more);
    auto rest = sv::client::parse_telemetry_batch(reply(collector.read("instance", 0, 7, 16)));
    EXPECT_EQ(rest.lost_samples, 0U);
    EXPECT_EQ(rest.samples.size(), 5U);
    EXPECT_FALSE(rest.has_more);
    EXPECT_TRUE(sv::client::parse_telemetry_batch(reply(collector.read("instance", 0, 12, 16)))
                    .samples.empty());
    EXPECT_THROW(collector.read("other", 0, 0, 16), std::invalid_argument);
    EXPECT_THROW(collector.read("instance", 1, 0, 16), std::invalid_argument);
    EXPECT_THROW(collector.read("instance", 0, 13, 16), std::invalid_argument);
    EXPECT_THROW(collector.read("instance", 0, 0, 0), std::invalid_argument);
    EXPECT_THROW(collector.read("instance", 0, 0, 17), std::invalid_argument);
}

TEST(Telemetry, PreparedUpdatesOnlyResetHistoryWhenCommittedAndChanged)
{
    sv::TelemetryCollector collector("instance", {true, 50, 8},
                                     [] { return sv::ProcessObservation{}; });
    collector.sample(1, 0, {}, false, true, false);
    {
        auto abandoned = collector.prepare({false, 50, 8});
    }
    EXPECT_TRUE(collector.enabled());
    collector.prepare({true, 50, 8})->commit();
    EXPECT_EQ(collector.read("instance", 0, 0, 16).at("samples").as_array().size(), 1U);
    auto disabled = collector.prepare({false, 50, 8});
    disabled->commit();
    disabled->commit();
    EXPECT_FALSE(collector.enabled());
    auto batch = sv::client::parse_telemetry_batch(reply(collector.read("instance", 1, 0, 16)));
    EXPECT_TRUE(batch.samples.empty());
    collector.prepare({true, 50, 8})->commit();
    collector.sample(2, 0, {}, false, false, false);
    EXPECT_EQ(collector.read("instance", 2, 0, 16).at("latest_cursor"), "1");
}

TEST(TelemetryConfiguration, CanonicalValidationAndStrictTypes)
{
    const auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::ConfigStore store(initial);
    sv::ConfigService service(store);
    auto validated = service.validate(0, {{"telemetry", boost::json::object{{"enabled", true}}}},
                                      initial.view, sv::OutputProduct::FinalFrame);
    EXPECT_EQ(validated.at("changes").as_object().at("telemetry"),
              sv::telemetry_settings({true, 100, 128}));
    EXPECT_EQ(store.revision(), 0U);
    for (const auto &bad :
         std::vector<boost::json::object>{{{"enabled", 1}},
                                          {{"enabled", true}, {"period_ms", 49}},
                                          {{"enabled", true}, {"period_ms", 50.0}},
                                          {{"enabled", true}, {"history_capacity", 257}},
                                          {{"enabled", true}, {"typo", false}},
                                          {}})
    {
        EXPECT_ANY_THROW(
            service.validate(0, {{"telemetry", bad}}, initial.view, sv::OutputProduct::FinalFrame));
    }
}

TEST(ClientTelemetry, RejectsMalformedEnvelopesWindowsAndMetrics)
{
    sv::TelemetryCollector collector("instance", {true, 50, 8},
                                     [] { return sv::ProcessObservation{1, 2}; });
    collector.sample(100, 0, {}, false, true, false);
    const auto good = collector.read("instance", 0, 0, 16);
    for (const auto &[key, value] :
         std::vector<std::pair<std::string, boost::json::value>>{{"next_cursor", "2"},
                                                                 {"latest_cursor", "0"},
                                                                 {"lost_samples", "1"},
                                                                 {"has_more", true},
                                                                 {"enabled", false},
                                                                 {"server_instance_id", ""},
                                                                 {"schema_version", 2}})
    {
        auto bad = good;
        bad[key] = value;
        EXPECT_ANY_THROW(sv::client::parse_telemetry_batch(reply(bad)));
    }
    auto bad = good;
    bad.at("samples").as_array()[0].as_object().at("process").as_object()["cpu_percent"] = -1;
    EXPECT_THROW(sv::client::parse_telemetry_batch(reply(bad)), std::invalid_argument);
    auto ack = reply(good);
    ack.header["accepted"] = false;
    EXPECT_THROW(sv::client::parse_telemetry_batch(ack), std::invalid_argument);
}

TEST(ClientTelemetry, SourceWorkChecksCameraKindEpochClockAndDurationBounds)
{
    sv::TelemetryCollector collector("instance", {true, 50, 8},
                                     [] { return sv::ProcessObservation{}; });
    sv::FrameSet inputs;
    inputs.frames[0] = sv::Frame{};
    auto record = std::make_shared<sv::SourceWorkRecord>();
    record->session = collector.source_session();
    record->timing = {0, "camera_read", 0, 0, 1, 1000001, .2, std::nullopt, .3, false};
    inputs.frames[0]->source_work = record;
    sv::FrameTelemetry frame;
    frame.frame_id = 1;
    frame.timestamp_ns = 2000000;
    collector.frame(frame, &inputs);
    collector.sample(3000000, 0, {}, false, true, false);
    const auto good = collector.read("instance", 0, 0, 16);
    const auto parsed = sv::client::parse_telemetry_batch(reply(good));
    ASSERT_EQ(parsed.samples[0].source_work.size(), 1U);
    EXPECT_EQ(parsed.samples[0].source_work[0].image_prepare_wall_ms, .3);
    for (const auto &[key, value] : std::vector<std::pair<std::string, boost::json::value>>{
             {"camera_id", true},
             {"camera_id", 4},
             {"kind", "unknown"},
             {"epoch", "1"},
             {"started_ns", "1000002"},
             {"completed_ns", "2000001"},
             {"camera_read_wall_ms", -1},
             {"camera_read_wall_ms", 10},
             {"cache_hit", true},
             {"image_load_wall_ms", 0},
             {"image_prepare_wall_ms", nullptr}})
    {
        auto bad = good;
        bad.at("samples")
            .as_array()[0]
            .as_object()
            .at("source_work")
            .as_array()[0]
            .as_object()[key] = value;
        EXPECT_ANY_THROW(sv::client::parse_telemetry_batch(reply(bad))) << key;
    }
    auto duplicate = good;
    auto &records = duplicate.at("samples").as_array()[0].as_object().at("source_work").as_array();
    records.push_back(records.front());
    EXPECT_THROW(sv::client::parse_telemetry_batch(reply(duplicate)), std::invalid_argument);
    auto missing_frame = good;
    missing_frame.at("samples").as_array()[0].as_object()["frame"] = nullptr;
    EXPECT_THROW(sv::client::parse_telemetry_batch(reply(missing_frame)), std::invalid_argument);
}

TEST(Telemetry, CameraHealthIsOptionalTypedAndIndependentOfFrameAndEpoch)
{
    sv::TelemetryCollector collector("instance", {true, 50, 8});
    sv::SourceStats source;
    source.camera_diagnostics = sv::CameraDiagnosticsSet{};
    source.camera_diagnostics->at(0) = {UINT64_MAX, 9, 8, 7, 6, true};
    collector.sample(50000000, 0, source, false, true, false);
    auto wire = collector.read("instance", 0, 0, 16);
    const auto batch = sv::client::parse_telemetry_batch(reply(wire));
    ASSERT_TRUE(batch.samples.at(0).source.camera_diagnostics);
    EXPECT_FALSE(batch.samples[0].frame);
    const auto &camera = batch.samples[0].source.camera_diagnostics->at(0);
    EXPECT_EQ(camera.open_attempts, UINT64_MAX);
    EXPECT_EQ(camera.read_failures, 9U);
    EXPECT_EQ(camera.opencv_exceptions, 8U);
    EXPECT_EQ(camera.reconnects, 7U);
    EXPECT_EQ(camera.release_failures, 6U);
    EXPECT_TRUE(camera.worker_stopped);
    collector.prepare({true, 100, 8})->commit();
    collector.sample(150000000, 0, source, false, true, false);
    EXPECT_EQ(sv::client::parse_telemetry_batch(reply(collector.read("instance", 1, 0, 16)))
                  .samples[0]
                  .source.camera_diagnostics->at(0)
                  .open_attempts,
              UINT64_MAX);

    auto &fields = wire.at("samples").as_array()[0].as_object().at("source").as_object();
    fields.erase("camera_diagnostics"); // Old schema-1 messages remain valid.
    EXPECT_FALSE(
        sv::client::parse_telemetry_batch(reply(wire)).samples[0].source.camera_diagnostics);
    fields["camera_diagnostics"] = nullptr;
    EXPECT_FALSE(
        sv::client::parse_telemetry_batch(reply(wire)).samples[0].source.camera_diagnostics);
}

TEST(Telemetry, CameraHealthRejectsInvalidShapeIdentityCounterAndFlag)
{
    sv::TelemetryCollector collector("instance", {true, 50, 8});
    sv::SourceStats source;
    source.camera_diagnostics = sv::CameraDiagnosticsSet{};
    collector.sample(50000000, 0, source, false, true, false);
    const auto original = collector.read("instance", 0, 0, 16);
    auto change = [&](auto mutate)
    {
        auto wire = original;
        auto &value = wire.at("samples").as_array()[0].as_object().at("source").as_object().at(
            "camera_diagnostics");
        mutate(value);
        EXPECT_ANY_THROW(sv::client::parse_telemetry_batch(reply(wire)));
    };
    change([](auto &value) { value = boost::json::object{}; });
    change([](auto &value) { value.as_array().pop_back(); });
    change([](auto &value) { value.as_array()[1].as_object()["camera_id"] = 0; });
    change([](auto &value) { value.as_array()[0].as_object()["camera_id"] = "0"; });
    change([](auto &value) { value.as_array()[0].as_object()["read_failures"] = -1; });
    change([](auto &value)
           { value.as_array()[0].as_object()["open_attempts"] = "18446744073709551616"; });
    change([](auto &value) { value.as_array()[0].as_object()["worker_stopped"] = "false"; });
}

TEST(Telemetry, NegotiatedModePreservesCheckedFieldsAndRejectsStaleOrMalformedRecords)
{
    sv::TelemetryCollector collector("instance", {true, 50, 8});
    sv::SourceStats source;
    source.camera_diagnostics = sv::CameraDiagnosticsSet{};
    auto &camera = source.camera_diagnostics->at(0);
    camera.open_attempts = UINT64_MAX;
    camera.negotiated_mode = sv::CameraCaptureMode{1280, 720, UINT64_MAX, 29.97, "MJPG", 2};
    collector.sample(50000000, 0, source, false, true, false);
    const auto wire = collector.read("instance", 0, 0, 16);
    const auto sample = sv::client::parse_telemetry_batch(reply(wire)).samples[0];
    ASSERT_TRUE(sample.source.camera_diagnostics->at(0).negotiated_mode);
    const auto &mode = *sample.source.camera_diagnostics->at(0).negotiated_mode;
    EXPECT_EQ(mode.width, 1280U);
    EXPECT_EQ(mode.open_attempt, UINT64_MAX);
    EXPECT_EQ(mode.fps, 29.97);
    EXPECT_EQ(mode.fourcc, "MJPG");
    EXPECT_EQ(mode.buffer_count, 2U);
    auto change = [&](auto mutate, bool valid = false)
    {
        auto data = wire;
        auto &camera = data.at("samples")
                           .as_array()[0]
                           .as_object()
                           .at("source")
                           .as_object()
                           .at("camera_diagnostics")
                           .as_array()[0]
                           .as_object();
        mutate(camera);
        if (valid)
        {
            EXPECT_NO_THROW(sv::client::parse_telemetry_batch(reply(data)));
        }
        else
        {
            EXPECT_ANY_THROW(sv::client::parse_telemetry_batch(reply(data)));
        }
    };
    change([](auto &c) { c.erase("negotiated_mode"); }, true);
    change([](auto &c) { c["negotiated_mode"] = nullptr; }, true);
    change([](auto &c) { c["negotiated_mode"].as_object()["fps"] = nullptr; }, true);
    change([](auto &c) { c["worker_stopped"] = true; });
    change([](auto &c) { c["open_attempts"] = "1"; });
    change([](auto &c) { c["negotiated_mode"].as_object()["open_attempt"] = "0"; });
    change([](auto &c) { c["negotiated_mode"].as_object()["open_attempt"] = "1"; });
    change([](auto &c) { c["negotiated_mode"].as_object()["width"] = 0; });
    change([](auto &c) { c["negotiated_mode"].as_object()["height"] = "720"; });
    change([](auto &c) { c["negotiated_mode"].as_object()["fps"] = 0; });
    change([](auto &c) { c["negotiated_mode"].as_object()["fourcc"] = "MJPEG"; });
    change([](auto &c) { c["negotiated_mode"].as_object()["buffer_count"] = 33; });
}

TEST(Telemetry, RecoveryStatusAcceptsLegacyAndRejectsInconsistentWaitAndTerminalStates)
{
    sv::TelemetryCollector collector("instance", {true, 50, 8});
    sv::SourceStats source;
    source.camera_diagnostics = sv::CameraDiagnosticsSet{};
    source.camera_diagnostics->at(0).recovery = sv::CameraRecoveryStatus{UINT64_MAX, 60000, {}};
    collector.sample(50000000, 0, source, false, false, false);
    const auto wire = collector.read("instance", 0, 0, 16);
    const auto parsed = sv::client::parse_telemetry_batch(reply(wire));
    const auto &status = parsed.samples[0].source.camera_diagnostics->at(0).recovery;
    ASSERT_TRUE(status);
    EXPECT_EQ(status->consecutive_failures, UINT64_MAX);
    EXPECT_EQ(status->retry_delay_ms, 60000U);
    EXPECT_FALSE(status->terminal_reason);
    auto change = [&](auto mutate, bool valid = false)
    {
        auto copy = wire;
        auto &camera = copy.at("samples")
                           .as_array()[0]
                           .as_object()
                           .at("source")
                           .as_object()
                           .at("camera_diagnostics")
                           .as_array()[0]
                           .as_object();
        mutate(camera);
        if (valid)
        {
            EXPECT_NO_THROW(sv::client::parse_telemetry_batch(reply(copy)));
        }
        else
        {
            EXPECT_ANY_THROW(sv::client::parse_telemetry_batch(reply(copy)));
        }
    };
    change([](auto &c) { c.erase("recovery"); }, true);
    change([](auto &c) { c["recovery"] = nullptr; }, true);
    change([](auto &c) { c["recovery"] = boost::json::object{}; });
    change([](auto &c) { c.at("recovery").as_object()["retry_delay_ms"] = 0; });
    change([](auto &c) { c.at("recovery").as_object()["retry_delay_ms"] = 60001; });
    change([](auto &c) { c.at("recovery").as_object()["retry_delay_ms"] = "100"; });
    change([](auto &c) { c.at("recovery").as_object()["consecutive_failures"] = "0"; });
    change([](auto &c)
           { c.at("recovery").as_object()["consecutive_failures"] = "18446744073709551616"; });
    change([](auto &c) { c["worker_stopped"] = true; });
    change([](auto &c) { c.at("recovery").as_object()["terminal_reason"] = "unexpected"; });
    change([](auto &c) { c.at("recovery").as_object()["terminal_reason"] = "stop_requested"; });
    change(
        [](auto &c)
        {
            c["worker_stopped"] = true;
            auto &r = c.at("recovery").as_object();
            r["retry_delay_ms"] = nullptr;
            r["terminal_reason"] = "retry_exhausted";
            r["consecutive_failures"] = "0";
        });
    change(
        [](auto &c)
        {
            c["worker_stopped"] = true;
            auto &r = c.at("recovery").as_object();
            r["retry_delay_ms"] = nullptr;
            r["terminal_reason"] = "retry_exhausted";
        },
        true);
}
