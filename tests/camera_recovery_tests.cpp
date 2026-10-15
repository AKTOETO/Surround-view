#include "camera_capture.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <thread>

namespace
{
struct CaptureState
{
    std::atomic<int> opens{0};
    std::atomic<int> reads{0};
    std::atomic<int> closes{0};
    std::atomic<bool> fail_close{false};
    bool return_false = false;
    bool return_empty = false;
};

class RecoveryCapture final : public sv::CameraCapture
{
  public:

    RecoveryCapture(std::shared_ptr<CaptureState> state, bool fail_open)
        : state_(std::move(state)), fail_open_(fail_open)
    {
    }

    bool is_open() const override
    {
        return opened_;
    }

    bool open(const std::string &, int, int) override
    {
        const int attempt = ++state_->opens;
        opened_ = true; // Even a failed open may have partially acquired resources.
        if (fail_open_ && attempt == 1)
        {
            CV_Error(cv::Error::StsError, "injected open failure");
        }
        return true;
    }

    bool read(cv::Mat &image) override
    {
        const int attempt = ++state_->reads;
        if (!fail_open_ && attempt == 1)
        {
            if (state_->return_false)
            {
                return false;
            }
            if (state_->return_empty)
            {
                image.release();
                return true;
            }
            CV_Error(cv::Error::StsError, "injected read failure");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        image = cv::Mat(2, 2, CV_8UC3, cv::Scalar(3, 5, 7));
        return true;
    }

    void close() override
    {
        ++state_->closes;
        if (state_->fail_close)
        {
            CV_Error(cv::Error::StsError, "injected release failure");
        }
        opened_ = false;
    }

  private:

    std::shared_ptr<CaptureState> state_;
    bool fail_open_;
    bool opened_ = false;
};

class HealthyCapture final : public sv::CameraCapture
{
  public:

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
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        image = cv::Mat(2, 2, CV_8UC3, cv::Scalar(1, 2, 3));
        return true;
    }

    void close() override
    {
    }
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

std::unique_ptr<sv::FrameSource> make_source(std::shared_ptr<CaptureState> state, bool fail_open,
                                             bool healthy_peer = false)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    return sv::make_camera_source(
        config, {},
        [state, fail_open, healthy_peer](int id) -> std::unique_ptr<sv::CameraCapture>
        {
            if (id == 0)
            {
                return std::make_unique<RecoveryCapture>(state, fail_open);
            }
            if (id == 1 && healthy_peer)
            {
                return std::make_unique<HealthyCapture>();
            }
            return std::make_unique<AbsentCapture>();
        });
}

template <typename Predicate>
std::vector<sv::SourceEvent> collect(sv::FrameSource &source, Predicate done)
{
    std::vector<sv::SourceEvent> events;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto next = source.poll();
        events.insert(events.end(), std::make_move_iterator(next.begin()),
                      std::make_move_iterator(next.end()));
        if (done(events))
        {
            return events;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return events;
}

bool has_status(const std::vector<sv::SourceEvent> &events, const std::string &reason)
{
    return std::any_of(events.begin(), events.end(), [&](const auto &event)
                       { return event.camera_id == 0 && event.reason == reason; });
}

class CameraRecovery : public testing::TestWithParam<int>
{
};
} // namespace

TEST_P(CameraRecovery, FailedOperationClosesAndReopensBeforePublishing)
{
    const bool fail_open = GetParam() == 0;
    auto state = std::make_shared<CaptureState>();
    state->return_false = GetParam() == 2;
    state->return_empty = GetParam() == 3;
    auto source = make_source(state, fail_open);
    const auto events = collect(*source,
                                [](const auto &items)
                                {
                                    return std::any_of(items.begin(), items.end(), [](const auto &e)
                                                       { return bool(e.frames[0]); });
                                });
    source->stop();
    if (GetParam() <= 1)
    {
        EXPECT_TRUE(
            has_status(events, fail_open ? "camera_exception:open" : "camera_exception:read"));
    }
    else
    {
        EXPECT_TRUE(std::any_of(
            events.begin(), events.end(), [](const auto &event)
            { return event.camera_id == 0 && event.reason.find("camera_read_failed:") == 0; }));
    }
    EXPECT_GE(state->opens.load(), 2);
    EXPECT_GE(state->closes.load(), 2); // Recovery and final stop.
    bool recovered_frame = false;
    for (const auto &event : events)
    {
        if (event.frames[0])
        {
            recovered_frame = true;
            EXPECT_EQ(event.frames[0]->sequence, 1U);
            EXPECT_EQ(event.frames[0]->image->pixels[0], 7);
            break;
        }
    }
    EXPECT_TRUE(recovered_frame);
}

INSTANTIATE_TEST_SUITE_P(OpenAndRead, CameraRecovery, testing::Values(0, 1, 2, 3));

TEST(CameraRecovery, ReleaseFailureStopsOnlyAffectedWorkerWithoutReopen)
{
    auto state = std::make_shared<CaptureState>();
    state->fail_close = true;
    auto source = make_source(state, false, true);
    const auto events =
        collect(*source,
                [](const auto &items)
                {
                    return has_status(items, "camera_exception:close_worker_stopped") &&
                           std::any_of(items.begin(), items.end(),
                                       [](const auto &e) { return bool(e.frames[1]); });
                });
    source->stop();
    EXPECT_TRUE(has_status(events, "camera_exception:read"));
    EXPECT_TRUE(has_status(events, "camera_exception:close_worker_stopped"));
    EXPECT_EQ(state->opens.load(), 1);
    EXPECT_EQ(state->reads.load(), 1);
    EXPECT_EQ(state->closes.load(), 1);
    EXPECT_GT(source->stats().decoded, 0U);
    EXPECT_TRUE(
        std::any_of(events.begin(), events.end(), [](const auto &e) { return bool(e.frames[1]); }));
    EXPECT_FALSE(
        std::any_of(events.begin(), events.end(), [](const auto &e) { return bool(e.frames[0]); }));
    EXPECT_FALSE(source->request(sv::SourceAction::Resume, 1));
}

TEST(CameraRecovery, ReleaseExceptionDuringStopDoesNotEscapeWorker)
{
    auto state = std::make_shared<CaptureState>();
    auto source = make_source(state, true);
    const auto events = collect(*source,
                                [](const auto &items)
                                {
                                    return std::any_of(items.begin(), items.end(), [](const auto &e)
                                                       { return bool(e.frames[0]); });
                                });
    ASSERT_TRUE(
        std::any_of(events.begin(), events.end(), [](const auto &e) { return bool(e.frames[0]); }));
    state->fail_close = true;
    source->stop();
    EXPECT_TRUE(has_status(source->poll(), "camera_exception:close_worker_stopped"));
    source->stop(); // Idempotent shutdown.
}
