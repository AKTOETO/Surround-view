#include "camera_capture.hpp"
#include "sv/client_telemetry.hpp"
#include "sv/client_telemetry_archive.hpp"
#include "sv/telemetry.hpp"
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <opencv2/imgcodecs.hpp>
#include <thread>

namespace
{
sv::Message reply(boost::json::object fields)
{
    return {21,
            {{"accepted", true},
             {"command_id", "1"},
             {"config_revision", "0"},
             {"telemetry", std::move(fields)}},
            {}};
}

std::vector<sv::SourceEvent> until(
    sv::FrameSource &source, const std::function<bool(const std::vector<sv::SourceEvent> &)> &done)
{
    std::vector<sv::SourceEvent> events;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto more = source.poll();
        events.insert(events.end(), std::make_move_iterator(more.begin()),
                      std::make_move_iterator(more.end()));
        if (done(events))
        {
            return events;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("source timing test timeout");
}

class FakeCapture final : public sv::CameraCapture
{
    int camera_;
    bool opened_ = false;

  public:

    explicit FakeCapture(int camera) : camera_(camera)
    {
    }

    bool is_open() const override
    {
        return opened_;
    }

    bool open(const std::string &, int, int) override
    {
        opened_ = true;
        return true;
    }

    bool read(cv::Mat &image) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const int channels[] = {3, 1, 4, 2};
        const int type = camera_ == 4 ? CV_16UC1 : CV_MAKETYPE(CV_8U, channels[camera_]);
        image = cv::Mat(2, 2, type, cv::Scalar(3, 5, 7, 9));
        return true;
    }

    void close() override
    {
        opened_ = false;
    }
};
} // namespace

TEST(SourceWork, CameraUsesRealRgbConversionAndTagsSuccessOnlyWhenEnabled)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    sv::TelemetryCollector collector("test", {true, 50, 8});
    auto source = sv::make_camera_source(config, collector.source_session(),
                                         [](int id) { return std::make_unique<FakeCapture>(id); });
    auto events = until(*source,
                        [](const auto &events)
                        {
                            std::array<bool, 3> seen{};
                            for (const auto &event : events)
                            {
                                for (int i = 0; i < 3; ++i)
                                {
                                    seen[i] = seen[i] || bool(event.frames[i]);
                                }
                            }
                            return seen[0] && seen[1] && seen[2];
                        });
    source->stop();
    sv::FrameSet set;
    for (const auto &event : events)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (event.frames[i])
            {
                set.frames[i] = event.frames[i];
            }
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(set.frames[i]);
        ASSERT_TRUE(set.frames[i]->source_work);
        EXPECT_EQ(set.frames[i]->source_work->session, collector.source_session());
    }
    EXPECT_FALSE(set.frames[3]);
    EXPECT_EQ(set.frames[0]->image->pixels[0], 7); // BGR -> RGB
    EXPECT_EQ(set.frames[1]->image->pixels[0], 3); // Gray -> RGB
    EXPECT_EQ(set.frames[2]->image->pixels[0], 7); // BGRA -> RGB
    sv::FrameTelemetry frame;
    frame.frame_id = 1;
    frame.timestamp_ns = sv::now_ns();
    collector.frame(frame, &set);
    collector.sample(sv::now_ns(), 0, source->stats(), false, true, false);
    auto batch = sv::client::parse_telemetry_batch(reply(collector.read("test", 0, 0, 16)));
    ASSERT_EQ(batch.samples[0].source_work.size(), 3U);
    EXPECT_GE(*batch.samples[0].source_work[0].camera_read_wall_ms, 0);
    EXPECT_GE(*batch.samples[0].source_work[0].image_prepare_wall_ms, 0);
    // A new collector series must discard selected inputs from the old worker session.
    collector.prepare({true, 100, 8})->commit();
    collector.frame(frame, &set);
    collector.sample(sv::now_ns(), 0, {}, false, true, false);
    EXPECT_TRUE(sv::client::parse_telemetry_batch(reply(collector.read("test", 1, 0, 16)))
                    .samples[0]
                    .source_work.empty());
}

TEST(SourceWork, DisabledCameraFramesHaveNoWorkAllocation)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    auto source = sv::make_camera_source(config, {},
                                         [](int id) { return std::make_unique<FakeCapture>(id); });
    auto events = until(*source,
                        [](const auto &events)
                        {
                            for (const auto &event : events)
                            {
                                if (event.frames[0])
                                {
                                    return true;
                                }
                            }
                            return false;
                        });
    source->stop();
    for (const auto &event : events)
    {
        for (const auto &frame : event.frames)
        {
            if (frame)
            {
                EXPECT_FALSE(frame->source_work);
            }
        }
    }
}

TEST(SourceWork, ReplayColdLoadCacheHitAndHotDisable)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    const auto directory =
        std::filesystem::temp_directory_path() / ("sv-timing-" + std::to_string(sv::now_ns()));
    std::filesystem::create_directories(directory);

    struct Cleanup
    {
        std::filesystem::path path;

        ~Cleanup()
        {
            std::filesystem::remove_all(path);
        }
    } cleanup{directory};

    boost::json::array ids, paths, rows;
    for (const auto &camera : config.cameras)
    {
        ids.emplace_back(camera.calibration_id);
        paths.emplace_back("same-" + std::to_string(camera.id));
    }
    for (int i = 0; i < 3; ++i)
    {
        rows.push_back({{"scenario_timestamp_ns", std::to_string(i * 1000000)},
                        {"paths", paths},
                        {"offset_ns", {0, 0, 0, 0}}});
    }
    const auto manifest = directory / "manifest.json";
    sv::write_json(manifest, {{"schema_version", 1}, {"calibration_ids", ids}, {"frames", rows}});
    auto session = std::make_shared<sv::SourceTimingSession>(sv::SourceTimingSession{7});
    auto source = sv::make_replay_source(
        config, manifest, false,
        [&](const auto &path)
        {
            const int id = path.string().back() - '0';
            const auto &camera = config.cameras[id];
            return sv::Image{camera.width, camera.height, 3,
                             std::vector<unsigned char>(size_t(camera.width) * camera.height * 3)};
        },
        session);
    auto first = until(*source,
                       [](const auto &events)
                       {
                           for (const auto &event : events)
                           {
                               if (event.frames[0])
                               {
                                   return true;
                               }
                           }
                           return false;
                       });
    bool cold = false;
    for (const auto &event : first)
    {
        if (event.frames[0])
        {
            ASSERT_TRUE(event.frames[0]->source_work);
            cold |= !event.frames[0]->source_work->timing.cache_hit;
        }
    }
    EXPECT_TRUE(cold);
    ASSERT_TRUE(source->request(sv::SourceAction::Step, 10));
    auto cached = until(*source,
                        [](const auto &events)
                        {
                            for (const auto &event : events)
                            {
                                if (event.request_id == 10)
                                {
                                    return true;
                                }
                            }
                            return false;
                        });
    bool hit = false;
    for (const auto &event : cached)
    {
        if (event.frames[0])
        {
            ASSERT_TRUE(event.frames[0]->source_work);
            hit |= event.frames[0]->source_work->timing.cache_hit;
            EXPECT_EQ(event.frames[0]->source_work->timing.image_load_wall_ms, 0);
        }
    }
    EXPECT_TRUE(hit);
    source->set_timing_session({});
    ASSERT_TRUE(source->request(sv::SourceAction::Step, 11));
    auto disabled = until(*source,
                          [](const auto &events)
                          {
                              for (const auto &event : events)
                              {
                                  if (event.request_id == 11)
                                  {
                                      return true;
                                  }
                              }
                              return false;
                          });
    for (const auto &event : disabled)
    {
        for (const auto &frame : event.frames)
        {
            if (frame)
            {
                EXPECT_FALSE(frame->source_work);
            }
        }
    }
    source->stop();
}

TEST(SourceWork, BoundedReadPagesBeforeHeaderLimitWithFourCameraRecords)
{
    sv::TelemetryCollector collector("test", {true, 50, 32},
                                     [] { return sv::ProcessObservation{}; });
    sv::FrameSet inputs;
    const uint64_t base = 1000000000000000000ULL;
    for (int i = 0; i < 4; ++i)
    {
        auto work = std::make_shared<sv::SourceWorkRecord>();
        work->session = collector.source_session();
        work->timing = {i,
                        "camera_read",
                        0,
                        UINT64_MAX,
                        base,
                        base + 20000000,
                        .012345678901234567,
                        std::nullopt,
                        .003123456789012345,
                        false};
        inputs.frames[i] = sv::Frame{};
        inputs.frames[i]->camera_id = i;
        inputs.frames[i]->source_sequence = UINT64_MAX;
        inputs.frames[i]->source_work = work;
        inputs.frames[i]->camera_capture =
            std::make_shared<const sv::CameraFrameProvenance>(sv::CameraFrameProvenance{
                UINT64_MAX, sv::CameraCaptureMode{1920, 1080, UINT64_MAX, 29.97, "MJPG", 32}});
    }
    sv::FrameTelemetry frame;
    frame.frame_id = 1;
    frame.timestamp_ns = base + 50000000;
    collector.frame(frame, &inputs);
    sv::SourceStats source;
    source.camera_diagnostics = sv::CameraDiagnosticsSet{};
    for (auto &camera : *source.camera_diagnostics)
    {
        camera = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, false};
        camera.negotiated_mode = sv::CameraCaptureMode{1920, 1080, UINT64_MAX, 29.97, "MJPG", 32};
    }
    for (uint64_t i = 0; i < 16; ++i)
    {
        collector.sample(base + 100000000 + i * 50000000, 0, source, false, true, false);
    }
    const auto fields = collector.read("test", 0, 0, 16);
    EXPECT_LE(boost::json::serialize(fields).size(), sv::max_header / 2);
    const auto batch = sv::client::parse_telemetry_batch(reply(fields));
    ASSERT_FALSE(batch.samples.empty());
    EXPECT_EQ(batch.samples[0].source_work.size(), 4U);
    ASSERT_TRUE(batch.samples[0].frame);
    EXPECT_EQ(batch.samples[0].frame->camera_inputs.size(), 4U);
    if (batch.samples.size() < 16)
    {
        EXPECT_TRUE(batch.has_more);
    }
    auto cursor = batch.next_cursor;
    size_t count = batch.samples.size();
    ASSERT_LT(count, 16U); // Four-camera health and work force byte-budget pagination.
    for (unsigned page = 0; cursor < 16 && page < 16; ++page)
    {
        const auto wire = collector.read("test", 0, cursor, 16);
        EXPECT_LE(boost::json::serialize(wire).size(), sv::max_header / 2);
        const auto rest = sv::client::parse_telemetry_batch(reply(wire));
        ASSERT_FALSE(rest.samples.empty());
        EXPECT_EQ(rest.samples[0].cursor, cursor + 1);
        EXPECT_EQ(rest.samples[0].source.camera_diagnostics->at(3).open_attempts, UINT64_MAX);
        count += rest.samples.size();
        cursor = rest.next_cursor;
    }
    EXPECT_EQ(cursor, 16U);
    EXPECT_EQ(count, 16U);
}

TEST(SourceWork, ReplayDefaultLoaderTimesRealOpenCvPngRead)
{
    const auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    const auto directory =
        std::filesystem::temp_directory_path() / ("sv-source-png-" + std::to_string(sv::now_ns()));
    std::filesystem::create_directories(directory);

    struct Cleanup
    {
        std::filesystem::path path;

        ~Cleanup()
        {
            std::filesystem::remove_all(path);
        }
    } cleanup{directory};

    boost::json::array ids, paths;
    for (const auto &camera : config.cameras)
    {
        const auto filename = std::to_string(camera.id) + ".png";
        ASSERT_TRUE(
            cv::imwrite((directory / filename).string(),
                        cv::Mat(camera.height, camera.width, CV_8UC3, cv::Scalar(3, 5, 7))));
        ids.emplace_back(camera.calibration_id);
        paths.emplace_back(filename);
    }
    const auto manifest = directory / "manifest.json";
    sv::write_json(
        manifest,
        {{"schema_version", 1},
         {"calibration_ids", ids},
         {"frames",
          boost::json::array{boost::json::object{
              {"scenario_timestamp_ns", "0"}, {"paths", paths}, {"offset_ns", {0, 0, 0, 0}}}}}});
    auto session = std::make_shared<sv::SourceTimingSession>(sv::SourceTimingSession{2});
    auto source = sv::make_replay_source(config, manifest, false, {}, session);
    auto events = until(*source,
                        [](const auto &events)
                        {
                            for (const auto &event : events)
                            {
                                if (event.frames[0])
                                {
                                    return true;
                                }
                            }
                            return false;
                        });
    source->stop();
    for (const auto &event : events)
    {
        if (event.frames[0])
        {
            ASSERT_TRUE(event.frames[0]->source_work);
            const auto &work = event.frames[0]->source_work->timing;
            EXPECT_EQ(work.kind, "replay_load");
            EXPECT_EQ(work.epoch, 2U);
            EXPECT_FALSE(work.cache_hit);
            EXPECT_GE(*work.image_load_wall_ms, 0);
            EXPECT_EQ(event.frames[0]->image->pixels[0], 7);
        }
    }
}

TEST(SourceWork, InFlightCameraReadKeepsOldEpochAndIsExcludedAfterCommit)
{
    struct Gate
    {
        std::mutex mutex;
        std::condition_variable ready;
        bool entered = false, released = false;
    };

    class BlockedCapture final : public sv::CameraCapture
    {
        std::shared_ptr<Gate> gate_;
        FakeCapture capture_{0};
        bool first_ = true;

      public:

        explicit BlockedCapture(std::shared_ptr<Gate> gate) : gate_(std::move(gate))
        {
        }

        bool is_open() const override
        {
            return capture_.is_open();
        }

        bool open(const std::string &path, int w, int h) override
        {
            return capture_.open(path, w, h);
        }

        void close() override
        {
            capture_.close();
        }

        bool read(cv::Mat &image) override
        {
            if (first_)
            {
                first_ = false;
                std::unique_lock<std::mutex> lock(gate_->mutex);
                gate_->entered = true;
                gate_->ready.notify_all();
                gate_->ready.wait_for(lock, std::chrono::seconds(3),
                                      [&] { return gate_->released; });
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return capture_.read(image);
        }
    };

    class UnavailableCapture final : public sv::CameraCapture
    {
      public:

        bool is_open() const override
        {
            return false;
        }

        bool open(const std::string &, int, int) override
        {
            return false;
        }

        bool read(cv::Mat &) override
        {
            return false;
        }

        void close() override
        {
        }
    };

    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    sv::TelemetryCollector collector("test", {true, 50, 8});
    const auto original = collector.source_session();
    auto gate = std::make_shared<Gate>();
    auto source = sv::make_camera_source(
        config, original,
        [gate](int id) -> std::unique_ptr<sv::CameraCapture>
        {
            if (id == 0)
            {
                return std::make_unique<BlockedCapture>(gate);
            }
            return std::make_unique<UnavailableCapture>(); // No queue/status churn.
        });
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        ASSERT_TRUE(
            gate->ready.wait_for(lock, std::chrono::seconds(2), [&] { return gate->entered; }));
    }
    collector.prepare({true, 100, 8})->commit();
    source->set_timing_session(collector.source_session());
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->released = true;
    }
    gate->ready.notify_all();
    auto events =
        until(*source,
              [original](const auto &events)
              {
                  for (const auto &event : events)
                  {
                      if (event.frames[0] && event.frames[0]->source_work->session == original)
                      {
                          return true;
                      }
                  }
                  return false;
              });
    source->stop();
    sv::FrameSet old;
    for (const auto &event : events)
    {
        if (event.frames[0] && event.frames[0]->source_work->session == original)
        {
            old.frames[0] = event.frames[0];
        }
    }
    ASSERT_TRUE(old.frames[0]);
    EXPECT_EQ(old.frames[0]->source_work->timing.epoch, 0U);
    sv::FrameTelemetry frame;
    frame.frame_id = 1;
    frame.timestamp_ns = sv::now_ns();
    collector.frame(frame, &old);
    collector.sample(sv::now_ns(), 0, {}, false, true, false);
    EXPECT_TRUE(sv::client::parse_telemetry_batch(reply(collector.read("test", 1, 0, 16)))
                    .samples[0]
                    .source_work.empty());
}

TEST(SourceWork, UnsupportedPixelDepthIsRejectedBeforeRgbImagePublication)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    auto source =
        sv::make_camera_source(config, {}, [](int) { return std::make_unique<FakeCapture>(4); });
    const auto events = until(*source,
                              [](const auto &events)
                              {
                                  for (const auto &event : events)
                                  {
                                      if (event.reason.find("camera_unsupported_depth:") == 0)
                                      {
                                          return true;
                                      }
                                  }
                                  return false;
                              });
    source->stop();
    EXPECT_GT(source->stats().dropped, 0U);
    for (const auto &event : events)
    {
        for (const auto &frame : event.frames)
        {
            EXPECT_FALSE(frame);
        }
    }
}
