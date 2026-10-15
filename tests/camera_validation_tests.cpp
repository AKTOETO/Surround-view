#include "camera_capture.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <thread>

namespace
{
struct FormatState
{
    std::atomic<bool> valid{false};
    std::atomic<unsigned> reads{0};
};

class FormatCapture final : public sv::CameraCapture
{
  public:

    FormatCapture(std::shared_ptr<FormatState> state, int invalid_kind)
        : state_(std::move(state)), invalid_kind_(invalid_kind)
    {
    }

    bool is_open() const override
    {
        return true;
    }

    bool open(const std::string &, int, int) override
    {
        return true;
    }

    bool read(cv::Mat &image) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool valid = state_->valid;
        image = cv::Mat(!valid && invalid_kind_ == 0 ? 3 : 2, 2,
                        !valid && invalid_kind_ == 1   ? CV_16UC1
                        : !valid && invalid_kind_ == 2 ? CV_8UC2
                                                       : CV_8UC3,
                        cv::Scalar(3, 5, 7));
        ++state_->reads;
        return true;
    }

    void close() override
    {
    }

  private:

    std::shared_ptr<FormatState> state_;
    int invalid_kind_;
};

class AbsentCapture final : public sv::CameraCapture
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

template <typename Predicate>
bool poll_until(sv::FrameSource &source, std::vector<sv::SourceEvent> &events, Predicate done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto next = source.poll();
        events.insert(events.end(), std::make_move_iterator(next.begin()),
                      std::make_move_iterator(next.end()));
        if (done())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

size_t status_count(const std::vector<sv::SourceEvent> &events, const std::string &prefix)
{
    return std::count_if(events.begin(), events.end(), [&](const auto &event)
                         { return event.camera_id == 0 && event.reason.find(prefix) == 0; });
}

class CameraValidation : public testing::TestWithParam<int>
{
};
} // namespace

TEST_P(CameraValidation, InvalidFormatIsPacedAndDoesNotAlternateWithReady)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    const auto state = std::make_shared<FormatState>();
    const int kind = GetParam();
    const auto started = std::chrono::steady_clock::now();
    auto source =
        sv::make_camera_source(config, {},
                               [state, kind](int id) -> std::unique_ptr<sv::CameraCapture>
                               {
                                   if (id == 0)
                                   {
                                       return std::make_unique<FormatCapture>(state, kind);
                                   }
                                   return std::make_unique<AbsentCapture>();
                               });
    const std::string reason[] = {
        "camera_resolution_mismatch:", "camera_unsupported_depth:", "camera_unsupported_channels:"};
    std::vector<sv::SourceEvent> events;
    ASSERT_TRUE(poll_until(*source, events, [&] { return source->stats().dropped >= 4; }));
    // Four rejected frames require three retry waits; generous lower bound, no upper-time claim.
    EXPECT_GE(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(250));
    EXPECT_EQ(status_count(events, reason[kind]), 1U);
    EXPECT_EQ(status_count(events, "camera_ready:"), 0U);
    EXPECT_EQ(source->stats().decoded, 0U);

    state->valid = true;
    ASSERT_TRUE(poll_until(*source, events,
                           [&]
                           {
                               return std::any_of(events.begin(), events.end(),
                                                  [](const auto &e) { return bool(e.frames[0]); });
                           }));
    EXPECT_EQ(status_count(events, "camera_ready:"), 1U);
    const auto frame =
        std::find_if(events.begin(), events.end(), [](const auto &e) { return bool(e.frames[0]); });
    ASSERT_NE(frame, events.end());
    EXPECT_EQ(frame->frames[0]->sequence, 1U);
    EXPECT_EQ(frame->frames[0]->image->pixels[0], 7);

    state->valid = false;
    ASSERT_TRUE(
        poll_until(*source, events, [&] { return status_count(events, reason[kind]) == 2; }));
    source->stop();
    EXPECT_EQ(status_count(events, "camera_ready:"), 1U);
    EXPECT_EQ(source->stats().camera_diagnostics->at(0).read_failures, 0U);
    EXPECT_EQ(source->stats().camera_diagnostics->at(0).opencv_exceptions, 0U);
}

INSTANTIATE_TEST_SUITE_P(ResolutionDepthChannels, CameraValidation, testing::Values(0, 1, 2));
