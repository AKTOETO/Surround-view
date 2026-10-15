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
