#include "sv/client_telemetry_archive.hpp"
#include "sv/telemetry.hpp"
#include <boost/version.hpp>
#include <cmath>
#include <gtest/gtest.h>

namespace
{
sv::client::TelemetryArchive example()
{
    sv::TelemetryCollector collector("instance", {true, 50, 8},
                                     [] { return sv::ProcessObservation{100, 1048576}; });
    sv::FrameTelemetry frame;
    frame.frame_id = 1;
    frame.timestamp_ns = 1;
    collector.frame(frame);
    for (uint64_t i = 1; i <= 3; ++i)
    {
        collector.sample(i * 50000000, 0, {}, false, true, false);
    }
    auto ack = [](const char *key, boost::json::object fields)
    {
        return sv::Message{21,
                           {{"accepted", true},
                            {"command_id", "1"},
                            {"config_revision", "0"},
                            {key, std::move(fields)}},
                           {}};
    };
    const auto batch =
        sv::client::parse_telemetry_batch(ack("telemetry", collector.read("instance", 0, 0, 16)));
    return {sv::client::parse_telemetry_catalog(ack("telemetry_catalog", collector.catalog())),
            50000000,
            3,
            0,
            "host sample",
            batch.samples};
}
} // namespace

TEST(TelemetryArchive, RoundTripRetainsExactIntegersNullsAndRepeatedFrames)
{
    auto archive = example();
    archive.samples[2].source.received = UINT64_MAX;
    archive.samples[2].process.rss_bytes = UINT64_MAX;
    archive.catalog.epoch = UINT64_MAX;
    const auto text = sv::client::serialize_telemetry_archive(archive);
    const auto result = sv::client::parse_telemetry_archive(text);
    EXPECT_EQ(result.catalog.epoch, UINT64_MAX);
    EXPECT_TRUE(result.catalog.source_work_supported);
    ASSERT_EQ(result.samples.size(), 3U);
    EXPECT_EQ(result.samples[2].source.received, UINT64_MAX);
    EXPECT_EQ(result.samples[2].process.rss_bytes, UINT64_MAX);
    EXPECT_FALSE(result.samples[0].process.cpu_percent);
    ASSERT_TRUE(result.samples[1].frame);
    EXPECT_FALSE(result.samples[1].frame->fresh);
    EXPECT_EQ(result.samples[1].frame->spans_ms, archive.samples[1].frame->spans_ms);
    EXPECT_EQ(result.label, "host sample");
}

TEST(TelemetryArchive, PreciseParserPreservesFractionalMetricsAcrossRepeatedSaves)
{
#if BOOST_VERSION >= 108300
    auto archive = example();
    archive.samples[0].frame->spans_ms["fusion_cpu"] = .000001 * 23;
    archive.samples[1].process.cpu_percent = std::nextafter(.1, 1.0);
    archive.samples[2].collection_wall_ms = std::nextafter(.000001, 1.0);
    const auto original = archive;
    for (unsigned i = 0; i < 5; ++i)
    {
        archive =
            sv::client::parse_telemetry_archive(sv::client::serialize_telemetry_archive(archive));
    }
    EXPECT_DOUBLE_EQ(archive.samples[0].frame->spans_ms.at("fusion_cpu").as_double(),
                     original.samples[0].frame->spans_ms.at("fusion_cpu").as_double());
    EXPECT_EQ(archive.samples[1].process.cpu_percent, original.samples[1].process.cpu_percent);
    EXPECT_DOUBLE_EQ(archive.samples[2].collection_wall_ms, original.samples[2].collection_wall_ms);
#else
    GTEST_SKIP()
        << "Boost <1.83 lacks precise number parsing; legacy parser fidelity remains unverified";
#endif
}

TEST(TelemetryArchive, TruncatedWindowAndCursorHolesAreDifferentFromTotalLoss)
{
    auto archive = example();
    archive.samples.erase(archive.samples.begin()); // UI eviction, not server loss.
    EXPECT_EQ(sv::client::parse_telemetry_archive(sv::client::serialize_telemetry_archive(archive))
                  .lost_samples_total,
              0U);
    archive = example();
    archive.samples.erase(archive.samples.begin() + 1);
    archive.lost_samples_total = 1;
    EXPECT_EQ(sv::client::parse_telemetry_archive(sv::client::serialize_telemetry_archive(archive))
                  .samples.size(),
              2U);
    archive.lost_samples_total = 0;
    EXPECT_THROW(sv::client::serialize_telemetry_archive(archive), std::invalid_argument);
}

TEST(TelemetryArchive, RejectsWrongSchemaOversizeAndInconsistentMetadata)
{
    const auto good =
        boost::json::parse(sv::client::serialize_telemetry_archive(example())).as_object();
    for (const auto &[key, value] : std::vector<std::pair<std::string, boost::json::value>>{
             {"schema_version", 2},
             {"kind", "config"},
             {"origin_ns", "999999999"},
             {"latest_cursor", "4"},
             {"lost_samples_total", "1"},
             {"label", boost::json::value(std::string(257, 'x'))}})
    {
        auto bad = good;
        bad[key] = value;
        EXPECT_ANY_THROW(sv::client::parse_telemetry_archive(boost::json::serialize(bad)));
    }
    EXPECT_THROW(sv::client::parse_telemetry_archive(
                     std::string(sv::client::telemetry_archive_max_bytes + 1, ' ')),
                 std::length_error);
    auto archive = example();
    archive.samples.resize(121);
    EXPECT_THROW(sv::client::serialize_telemetry_archive(archive), std::length_error);
}

TEST(TelemetryArchive, RejectsMalformedSamplesAndBackwardsClock)
{
    auto archive = example();
    archive.samples[1].timestamp_ns = 2;
    EXPECT_THROW(sv::client::serialize_telemetry_archive(archive), std::invalid_argument);
    archive = example();
    archive.samples[1].cursor = 1;
    EXPECT_THROW(sv::client::serialize_telemetry_archive(archive), std::invalid_argument);
    archive = example();
    archive.samples[0].frame->spans_ms["render_wall"] = -1;
    EXPECT_THROW(sv::client::serialize_telemetry_archive(archive), std::invalid_argument);
}

TEST(TelemetryArchive, EmptyDisabledWindowIsSupported)
{
    auto archive = example();
    archive.samples.clear();
    archive.catalog.settings.enabled = false;
    EXPECT_TRUE(
        sv::client::parse_telemetry_archive(sv::client::serialize_telemetry_archive(archive))
            .samples.empty());
}

TEST(TelemetryArchive, SourceWorkSurvivesArchiveAndOldOptionalFieldIsAccepted)
{
    auto archive = example();
    archive.samples[0].frame->timestamp_ns = 3000000;
    archive.samples[0].source_work.push_back(
        {0, "camera_read", 0, UINT64_MAX, 1, 1000001, .2, std::nullopt, .3, false});
    const auto text = sv::client::serialize_telemetry_archive(archive);
    const auto result = sv::client::parse_telemetry_archive(text);
    ASSERT_EQ(result.samples[0].source_work.size(), 1U);
    EXPECT_EQ(result.samples[0].source_work[0].source_sequence, UINT64_MAX);
    EXPECT_EQ(result.samples[0].source_work[0].image_prepare_wall_ms, .3);
    auto old = boost::json::parse(text).as_object();
    old.at("samples").as_array()[0].as_object().erase("source_work");
    EXPECT_TRUE(sv::client::parse_telemetry_archive(boost::json::serialize(old))
                    .samples[0]
                    .source_work.empty());
}

TEST(TelemetryArchive, CameraHealthRoundTripRetainsNullAndExactCounters)
{
    auto archive = example();
    archive.samples[1].source.camera_diagnostics = sv::CameraDiagnosticsSet{};
    archive.samples[1].source.camera_diagnostics->at(3) = {UINT64_MAX, UINT64_MAX, UINT64_MAX,
                                                           UINT64_MAX, UINT64_MAX, true};
    const auto text = sv::client::serialize_telemetry_archive(archive);
    const auto result = sv::client::parse_telemetry_archive(text);
    EXPECT_FALSE(result.samples[0].source.camera_diagnostics);
    ASSERT_TRUE(result.samples[1].source.camera_diagnostics);
    const auto &camera = result.samples[1].source.camera_diagnostics->at(3);
    EXPECT_EQ(camera.open_attempts, UINT64_MAX);
    EXPECT_EQ(camera.read_failures, UINT64_MAX);
    EXPECT_EQ(camera.opencv_exceptions, UINT64_MAX);
    EXPECT_EQ(camera.reconnects, UINT64_MAX);
    EXPECT_EQ(camera.release_failures, UINT64_MAX);
    EXPECT_TRUE(camera.worker_stopped);
    auto legacy = boost::json::parse(text).as_object();
    for (auto &sample : legacy.at("samples").as_array())
    {
        sample.as_object().at("source").as_object().erase("camera_diagnostics");
    }
    EXPECT_FALSE(sv::client::parse_telemetry_archive(boost::json::serialize(legacy))
                     .samples[1]
                     .source.camera_diagnostics);
}
