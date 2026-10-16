#include "camera_capture.hpp"
#include "sv/client_output.hpp"
#include "sv/client_telemetry_archive.hpp"
#include "sv/telemetry.hpp"
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <thread>

namespace
{
class ReconnectingCapture final : public sv::CameraCapture
{
  public:

    bool is_open() const override
    {
        return opened_;
    }

    bool open(const std::string &, int, int) override
    {
        opened_ = true;
        ++attempt_;
        reads_ = 0;
        return true;
    }

    bool read(cv::Mat &image) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (attempt_ == 1 && ++reads_ == 2)
        {
            return false;
        }
        image = cv::Mat(2, 2, CV_8UC3, cv::Scalar(attempt_ * 10, 0, 0));
        return true;
    }

    void close() override
    {
        opened_ = false;
    }

    std::optional<sv::CameraCaptureMode> negotiated_mode() const override
    {
        return sv::CameraCaptureMode{2, 2, 0, attempt_ == 1 ? 15. : 30., "YUYV", 2};
    }

  private:

    bool opened_ = false;
    unsigned attempt_ = 0, reads_ = 0;
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

sv::Message ack(const char *key, boost::json::object fields)
{
    return {21,
            {{"accepted", true},
             {"command_id", "1"},
             {"config_revision", "0"},
             {key, std::move(fields)}},
            {}};
}

sv::Message output_message()
{
    boost::json::array inputs;
    for (int id = 0; id < 4; ++id)
    {
        inputs.emplace_back(boost::json::object{{"camera_id", id}, {"used", id == 0}});
    }
    const sv::CameraCaptureMode mode{640, 480, UINT64_MAX, 29.97, "MJPG", 2};
    inputs[0].as_object()["source_sequence_id"] = std::to_string(UINT64_MAX);
    inputs[0].as_object()["camera_capture"] = sv::camera_frame_provenance_json({UINT64_MAX, mode});
    return {11, {{"source_type", "camera"}, {"inputs", std::move(inputs)}}, {}};
}
} // namespace

TEST(CameraProvenance, OldFrameKeepsModeAndIdentityAfterReconnectAndTelemetryEpochChange)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    auto source = sv::make_camera_source(config, {},
                                         [](int id) -> std::unique_ptr<sv::CameraCapture>
                                         {
                                             if (id == 0)
                                             {
                                                 return std::make_unique<ReconnectingCapture>();
                                             }
                                             return std::make_unique<UnavailableCapture>();
                                         });
    std::optional<sv::Frame> old, fresh;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fresh && std::chrono::steady_clock::now() < deadline)
    {
        for (const auto &event : source->poll())
        {
            if (const auto &frame = event.frames[0]; frame && frame->camera_capture)
            {
                if (frame->camera_capture->open_attempt == 1)
                {
                    old = frame;
                }
                else if (!fresh)
                {
                    fresh = frame;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto current = source->stats();
    source->stop();
    ASSERT_TRUE(old);
    ASSERT_TRUE(fresh);
    EXPECT_FALSE(old->source_work); // Product provenance exists with timing disabled.
    EXPECT_EQ(old->camera_capture->open_attempt, 1U);
    EXPECT_EQ(fresh->camera_capture->open_attempt, 2U);
    EXPECT_EQ(old->source_sequence, 1U);
    EXPECT_EQ(fresh->source_sequence, 2U);
    EXPECT_EQ(old->camera_capture->negotiated_mode->fps, 15);
    EXPECT_EQ(fresh->camera_capture->negotiated_mode->fps, 30);
    EXPECT_NE(old->camera_capture, fresh->camera_capture);
    EXPECT_FALSE(source->stats().camera_diagnostics->at(0).negotiated_mode);
    ASSERT_TRUE(current.camera_diagnostics->at(0).negotiated_mode);
    EXPECT_EQ(current.camera_diagnostics->at(0).negotiated_mode->open_attempt, 2U);

    sv::TelemetryCollector collector("instance", {true, 50, 8});
    sv::FrameSet selected;
    selected.frames[0] = old;
    sv::FrameTelemetry frame;
    frame.frame_id = 1;
    frame.timestamp_ns = sv::now_ns();
    collector.frame(frame, &selected);
    collector.sample(sv::now_ns(), 0, current, false, true, false);
    collector.sample(sv::now_ns() + 50000000, 0, current, false, true, false);
    auto batch =
        sv::client::parse_telemetry_batch(ack("telemetry", collector.read("instance", 0, 0, 16)));
    ASSERT_EQ(batch.samples.size(), 2U);
    const auto &sample = batch.samples[0];
    ASSERT_EQ(sample.frame->camera_inputs.size(), 1U);
    EXPECT_EQ(sample.frame->camera_inputs[0].capture.open_attempt, 1U);
    EXPECT_EQ(sample.frame->camera_inputs[0].capture.negotiated_mode->fps, 15);
    EXPECT_EQ(sample.source.camera_diagnostics->at(0).negotiated_mode->fps, 30);
    EXPECT_FALSE(batch.samples[1].frame->fresh);
    EXPECT_EQ(batch.samples[1].frame->camera_inputs[0].capture.open_attempt, 1U);
    const auto catalog =
        sv::client::parse_telemetry_catalog(ack("telemetry_catalog", collector.catalog()));
    sv::client::TelemetryArchive archive{
        catalog, batch.samples[0].timestamp_ns, 2, 0, "reconnect", batch.samples};
    const auto restored =
        sv::client::parse_telemetry_archive(sv::client::serialize_telemetry_archive(archive));
    EXPECT_EQ(restored.samples[0].frame->camera_inputs[0].capture.open_attempt, 1U);
    collector.prepare({true, 100, 8})->commit();
    collector.frame(frame, &selected);
    collector.sample(sv::now_ns(), 0, current, false, true, false);
    EXPECT_EQ(
        sv::client::parse_telemetry_batch(ack("telemetry", collector.read("instance", 1, 0, 16)))
            .samples[0]
            .frame->camera_inputs[0]
            .capture.open_attempt,
        1U);
}

TEST(CameraProvenance, OutputParserAcceptsExactIdentityAndLegacyAbsence)
{
    auto wire = output_message();
    const auto parsed = sv::client::parse_frame_camera_inputs(wire);
    ASSERT_EQ(parsed.size(), 1U);
    EXPECT_EQ(parsed[0].source_sequence, UINT64_MAX);
    EXPECT_EQ(parsed[0].capture.open_attempt, UINT64_MAX);
    EXPECT_EQ(parsed[0].capture.negotiated_mode->fourcc, "MJPG");
    wire.header.at("inputs").as_array()[0].as_object().erase("camera_capture");
    wire.header.erase("source_type");
    EXPECT_TRUE(sv::client::parse_frame_camera_inputs(wire).empty());
    wire.header.at("inputs").as_array()[0].as_object()["camera_capture"] = nullptr;
    EXPECT_TRUE(sv::client::parse_frame_camera_inputs(wire).empty());
}

TEST(CameraProvenance, OutputParserRejectsWrongContextAndConflictingIdentity)
{
    const auto original = output_message();
    auto reject = [&](auto mutate)
    {
        auto wire = original;
        mutate(wire);
        EXPECT_ANY_THROW(sv::client::parse_frame_camera_inputs(wire));
    };
    reject([](auto &w) { w.type = 21; });
    reject([](auto &w) { w.header["source_type"] = "replay"; });
    reject([](auto &w) { w.header.at("inputs").as_array().pop_back(); });
    reject([](auto &w) { w.header.at("inputs").as_array()[1].as_object()["camera_id"] = 0; });
    reject([](auto &w) { w.header.at("inputs").as_array()[0].as_object()["used"] = false; });
    reject(
        [](auto &w)
        {
            w.header.at("inputs")
                .as_array()[0]
                .as_object()["camera_capture"]
                .as_object()["open_attempt"] = "1";
        });
    reject(
        [](auto &w)
        {
            w.header.at("inputs")
                .as_array()[0]
                .as_object()["camera_capture"]
                .as_object()["open_attempt"] = "0";
        });
}

TEST(CameraProvenance, TelemetryRejectsDuplicateOrContradictorySelectedInputsButAcceptsLegacy)
{
    sv::TelemetryCollector collector("instance", {true, 50, 8});
    const auto time = sv::now_ns();
    sv::FrameSet selected;
    selected.frames[0] = sv::Frame{};
    selected.frames[0]->source_sequence = 9;
    selected.frames[0]->camera_capture = std::make_shared<const sv::CameraFrameProvenance>(
        sv::CameraFrameProvenance{1, sv::CameraCaptureMode{2, 2, 1, 15, "YUYV", 2}});
    auto work = std::make_shared<sv::SourceWorkRecord>();
    work->session = collector.source_session();
    work->timing = {0, "camera_read", 0, 9, time, time + 1000000, .5, std::nullopt, .5, false};
    selected.frames[0]->source_work = work;
    sv::FrameTelemetry frame;
    frame.frame_id = 1;
    frame.timestamp_ns = time + 2000000;
    collector.frame(frame, &selected);
    collector.sample(time + 3000000, 0, {}, false, true, false);
    const auto original = collector.read("instance", 0, 0, 16);
    EXPECT_NO_THROW(sv::client::parse_telemetry_batch(ack("telemetry", original)));
    auto reject = [&](auto mutate)
    {
        auto wire = original;
        mutate(wire.at("samples").as_array()[0].as_object().at("frame").as_object().at(
            "camera_inputs"));
        EXPECT_ANY_THROW(sv::client::parse_telemetry_batch(ack("telemetry", wire)));
    };
    reject(
        [](auto &v)
        {
            const auto copy = v.as_array()[0];
            v.as_array().push_back(copy);
        });
    reject([](auto &v) { v.as_array()[0].as_object()["source_sequence_id"] = "10"; });
    reject([](auto &v) { v.as_array()[0].as_object()["camera_capture"] = nullptr; });
    reject([](auto &v)
           { v.as_array()[0].as_object()["camera_capture"].as_object()["open_attempt"] = "2"; });
    auto legacy = original;
    legacy.at("samples").as_array()[0].as_object().at("frame").as_object().erase("camera_inputs");
    EXPECT_TRUE(sv::client::parse_telemetry_batch(ack("telemetry", legacy))
                    .samples[0]
                    .frame->camera_inputs.empty());
}
