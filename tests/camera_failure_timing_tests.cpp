#include "camera_capture.hpp"
#include "sv/client_telemetry.hpp"
#include "sv/client_telemetry_archive.hpp"
#include "sv/telemetry.hpp"
#include <condition_variable>
#include <gtest/gtest.h>
#include <thread>

namespace
{
struct Gate
{
    std::mutex mutex;
    std::condition_variable ready;
    unsigned entered = 0;
    bool release = false;
};

class FailedCapture final : public sv::CameraCapture
{
    std::string mode_;
    std::shared_ptr<Gate> gate_;
    bool open_ = false;

  public:

    FailedCapture(std::string mode, std::shared_ptr<Gate> gate = {})
        : mode_(std::move(mode)), gate_(std::move(gate))
    {
    }

    bool is_open() const override
    {
        if (mode_ == "is_open")
        {
            CV_Error(cv::Error::StsError, "mock is_open");
        }
        return open_;
    }

    bool open(const std::string &, int, int) override
    {
        if (mode_ == "open_throw")
        {
            CV_Error(cv::Error::StsError, "mock open");
        }
        open_ = mode_ != "open";
        return open_;
    }

    bool read(cv::Mat &) override
    {
        if (gate_)
        {
            std::unique_lock<std::mutex> lock(gate_->mutex);
            ++gate_->entered;
            gate_->ready.notify_all();
            gate_->ready.wait_for(lock, std::chrono::seconds(2), [&] { return gate_->release; });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (mode_ == "read_throw")
        {
            CV_Error(cv::Error::StsError, "mock read");
        }
        return mode_ == "empty";
    }

    void close() override
    {
        if (mode_ == "close")
        {
            CV_Error(cv::Error::StsError, "mock close");
        }
        open_ = false;
    }
};

sv::Config config()
{
    auto result = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : result.source.camera_capture)
    {
        camera.recovery.max_consecutive_failures = 1;
    }
    return result;
}

sv::SourceStats stopped(sv::FrameSource &source)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto stats = source.stats();
        bool done = true;
        for (const auto &camera : *stats.camera_diagnostics)
        {
            done = done && camera.worker_stopped;
        }
        if (done)
        {
            return stats;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("mock failure worker timeout");
}

sv::Message reply(boost::json::object fields)
{
    return {21,
            {{"accepted", true},
             {"command_id", "1"},
             {"config_revision", "0"},
             {"telemetry", std::move(fields)}},
            {}};
}

boost::json::object sample()
{
    sv::TelemetryCollector collector("failures", {true, 50, 16});
    sv::SourceStats source;
    source.camera_diagnostics = sv::CameraDiagnosticsSet{};
    source.camera_diagnostics->at(0).latest_failure = {collector.epoch(), 1,      100,
                                                       3000100,           "read", "false_return"};
    collector.sample(1000000000, 0, source, false, false, false);
    return collector.read("failures", 0, 0, 16);
}
} // namespace

TEST(CameraFailureTiming, MockOpenReadEmptyExceptionAndReleaseFailuresHaveCompletedIntervals)
{
    for (const auto *mode :
         {"is_open", "open", "open_throw", "read", "empty", "read_throw", "close"})
    {
        SCOPED_TRACE(mode);
        auto session = std::make_shared<sv::SourceTimingSession>(sv::SourceTimingSession{7});
        auto source = sv::make_camera_source(config(), session, [mode](int)
                                             { return std::make_unique<FailedCapture>(mode); });
        const auto stats = stopped(*source);
        for (const auto &camera : *stats.camera_diagnostics)
        {
            ASSERT_TRUE(camera.latest_failure);
            const auto &failure = *camera.latest_failure;
            EXPECT_EQ(failure.epoch, 7U);
            EXPECT_GT(failure.event_id, 0U);
            EXPECT_GE(failure.completed_ns, failure.started_ns);
            EXPECT_GE(failure.wall_ms(), 0);
            const std::string kind(mode);
            EXPECT_EQ(failure.operation, kind == "open_throw"                      ? "open"
                                         : kind == "empty" || kind == "read_throw" ? "read"
                                                                                   : kind);
            EXPECT_EQ(failure.outcome, kind == "empty"                    ? "empty_frame"
                                       : kind == "read" || kind == "open" ? "false_return"
                                                                          : "opencv_exception");
            if (kind == "read")
            {
                EXPECT_GE(failure.wall_ms(), 1);
            }
        }
        source->set_timing_session({});
        const auto disabled_stats = source->stats();
        for (const auto &camera : *disabled_stats.camera_diagnostics)
        {
            EXPECT_FALSE(camera.latest_failure);
        }
    }
}

TEST(CameraFailureTiming, DisabledAndSameEpochReplacementNeverPublishOldInFlightFailure)
{
    auto disabled = sv::make_camera_source(config(), {}, [](int)
                                           { return std::make_unique<FailedCapture>("read"); });
    const auto disabled_stats = stopped(*disabled);
    for (const auto &camera : *disabled_stats.camera_diagnostics)
    {
        EXPECT_FALSE(camera.latest_failure);
    }
    auto gate = std::make_shared<Gate>();
    auto first = std::make_shared<sv::SourceTimingSession>(sv::SourceTimingSession{7});
    auto source = sv::make_camera_source(config(), first, [gate](int)
                                         { return std::make_unique<FailedCapture>("read", gate); });
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        ASSERT_TRUE(gate->ready.wait_for(lock, std::chrono::seconds(1),
                                         [&] { return gate->entered == 4; }));
        source->set_timing_session(
            std::make_shared<sv::SourceTimingSession>(sv::SourceTimingSession{7}));
        gate->release = true;
    }
    gate->ready.notify_all();
    const auto stats = stopped(*source);
    for (const auto &camera : *stats.camera_diagnostics)
    {
        EXPECT_EQ(camera.read_failures, 1U);
        EXPECT_FALSE(camera.latest_failure);
    }
}

TEST(CameraFailureTiming, TypedParserArchiveAndLegacyNullRetainIdentityAndRejectMalformedClaims)
{
    const auto fields = sample();
    auto parsed = sv::client::parse_telemetry_batch(reply(fields));
    ASSERT_EQ(parsed.samples.size(), 1U);
    ASSERT_TRUE(parsed.samples[0].source.camera_diagnostics->at(0).latest_failure);
    EXPECT_DOUBLE_EQ(parsed.samples[0].source.camera_diagnostics->at(0).latest_failure->wall_ms(),
                     3);
    sv::TelemetryCollector collector("failures", {true, 50, 16});
    auto catalog_reply = reply({});
    catalog_reply.header.erase("telemetry");
    catalog_reply.header["telemetry_catalog"] = collector.catalog();
    auto catalog = sv::client::parse_telemetry_catalog(catalog_reply);
    EXPECT_TRUE(catalog.camera_failure_timing_supported);
    sv::client::TelemetryArchive archive{catalog, 1000000000, 1, 0, "mock failure", parsed.samples};
    const auto restored =
        sv::client::parse_telemetry_archive(sv::client::serialize_telemetry_archive(archive));
    EXPECT_TRUE(restored.catalog.camera_failure_timing_supported);
    EXPECT_EQ(restored.samples[0].source.camera_diagnostics->at(0).latest_failure->event_id, 1U);
    for (unsigned kind = 0; kind < 9; ++kind)
    {
        auto changed = fields;
        auto &camera = changed.at("samples")
                           .as_array()[0]
                           .as_object()
                           .at("source")
                           .as_object()
                           .at("camera_diagnostics")
                           .as_array()[0]
                           .as_object();
        auto &value = camera.at("latest_failure").as_object();
        if (kind == 0)
        {
            value["policy"] = "future";
        }
        if (kind == 1)
        {
            value["epoch"] = "999";
        }
        if (kind == 2)
        {
            value["event_id"] = "0";
        }
        if (kind == 3)
        {
            value["operation"] = "decode";
        }
        if (kind == 4)
        {
            value["completed_ns"] = "1";
        }
        if (kind == 5)
        {
            value["completed_ns"] = "1000000001";
        }
        if (kind == 6)
        {
            value["wall_ms"] = 0;
        }
        if (kind == 7)
        {
            value["outcome"] = "unknown";
        }
        if (kind == 8)
        {
            value["extra"] = true;
        }
        EXPECT_ANY_THROW(sv::client::parse_telemetry_batch(reply(changed))) << kind;
    }
    auto legacy = fields;
    auto &camera = legacy.at("samples")
                       .as_array()[0]
                       .as_object()
                       .at("source")
                       .as_object()
                       .at("camera_diagnostics")
                       .as_array()[0]
                       .as_object();
    camera.erase("latest_failure");
    EXPECT_FALSE(sv::client::parse_telemetry_batch(reply(legacy))
                     .samples[0]
                     .source.camera_diagnostics->at(0)
                     .latest_failure);
}

TEST(CameraFailureTiming, CollectorFiltersStaleEpochAndFutureCompletionBeforePublication)
{
    for (const bool future : {false, true})
    {
        sv::TelemetryCollector collector("filter", {true, 50, 8});
        sv::SourceStats source;
        source.camera_diagnostics = sv::CameraDiagnosticsSet{};
        source.camera_diagnostics->at(0).latest_failure = {future ? collector.epoch()
                                                                  : collector.epoch() + 1,
                                                           1,
                                                           100,
                                                           future ? 1000000001U : 200U,
                                                           "read",
                                                           "false_return"};
        collector.sample(1000000000, 0, source, false, false, false);
        const auto parsed =
            sv::client::parse_telemetry_batch(reply(collector.read("filter", 0, 0, 16)));
        EXPECT_FALSE(parsed.samples[0].source.camera_diagnostics->at(0).latest_failure);
    }
}

TEST(CameraFailureTiming, MaximumSnapshotsRespectTelemetryReplyBudgetAndPaginateWithoutLoss)
{
    sv::TelemetryCollector collector("budget", {true, 50, 16});
    sv::SourceStats source;
    source.camera_diagnostics = sv::CameraDiagnosticsSet{};
    constexpr uint64_t base = 4000000000000000000ULL;
    for (auto &camera : *source.camera_diagnostics)
    {
        camera.open_attempts = camera.read_failures = camera.opencv_exceptions = camera.reconnects =
            camera.release_failures = UINT64_MAX;
        camera.latest_failure = {collector.epoch(), UINT64_MAX, base - 2000000,
                                 base - 1,          "is_open",  "opencv_exception"};
    }
    for (uint64_t index = 0; index < 16; ++index)
    {
        collector.sample(base + index * 50000000, 0, source, false, false, false);
    }
    unsigned count = 0;
    uint64_t cursor = 0;
    bool more;
    do
    {
        const auto fields = collector.read("budget", 0, cursor, 16);
        EXPECT_LE(boost::json::serialize(fields).size(), sv::max_header / 2);
        const auto parsed = sv::client::parse_telemetry_batch(reply(fields));
        ASSERT_FALSE(parsed.samples.empty());
        count += parsed.samples.size();
        cursor = parsed.next_cursor;
        more = parsed.has_more;
    } while (more);
    EXPECT_EQ(count, 16U);
}
