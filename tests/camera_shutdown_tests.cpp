#include "camera_capture.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <thread>

namespace
{
enum class Boundary
{
    IsOpen,
    Open,
    Read
};

struct CaptureGate
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false, timed_out = false;
    std::atomic<unsigned> opens{0}, reads{0}, closes{0};

    void enter()
    {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(3), [&] { return released; }))
        {
            timed_out = true;
        }
    }

    bool wait_entered()
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2), [&] { return entered; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        changed.notify_all();
    }
};

class GatedCapture final : public sv::CameraCapture
{
  public:

    GatedCapture(std::shared_ptr<CaptureGate> gate, Boundary boundary)
        : gate_(std::move(gate)), boundary_(boundary)
    {
    }

    bool is_open() const override
    {
        if (boundary_ == Boundary::IsOpen)
        {
            gate_->enter();
        }
        return opened_;
    }

    bool open(const std::string &, int, int) override
    {
        ++gate_->opens;
        if (boundary_ == Boundary::Open)
        {
            gate_->enter();
        }
        opened_ = true;
        return true;
    }

    bool read(cv::Mat &image) override
    {
        ++gate_->reads;
        if (boundary_ == Boundary::Read)
        {
            gate_->enter();
        }
        image = cv::Mat(2, 2, CV_8UC3, cv::Scalar(1, 2, 3));
        return true;
    }

    void close() override
    {
        ++gate_->closes;
        opened_ = false;
    }

  private:

    std::shared_ptr<CaptureGate> gate_;
    Boundary boundary_;
    bool opened_ = false;
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

class CameraShutdown : public testing::TestWithParam<Boundary>
{
};
} // namespace

TEST_P(CameraShutdown, ObservedStopAfterBackendReturnSkipsNextOperationAndPublication)
{
    const auto boundary = GetParam();
    auto gate = std::make_shared<CaptureGate>();
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    auto timing = std::make_shared<sv::SourceTimingSession>(sv::SourceTimingSession{0});
    auto source =
        sv::make_camera_source(config, timing,
                               [gate, boundary](int id) -> std::unique_ptr<sv::CameraCapture>
                               {
                                   if (id == 0)
                                   {
                                       return std::make_unique<GatedCapture>(gate, boundary);
                                   }
                                   return std::make_unique<UnavailableCapture>();
                               });
    const bool entered = gate->wait_entered();
    auto shutdown = std::async(std::launch::async, [&] { source->stop(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool observed_stop = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        // Drain controls; queue saturation must not masquerade as a rejected stopped request.
        source->poll();
        if (!source->request(sv::SourceAction::Resume, 1))
        {
            observed_stop = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::promise<void> second_entered;
    auto entered_future = second_entered.get_future();
    auto concurrent_shutdown = std::async(std::launch::async,
                                          [&]
                                          {
                                              second_entered.set_value();
                                              source->stop();
                                          });
    entered_future.wait();
    const auto second_status = concurrent_shutdown.wait_for(std::chrono::milliseconds(20));
    // Release even on assertion failure, before joining the async stops/destructing the source.
    gate->release();
    shutdown.get();
    concurrent_shutdown.get();
    EXPECT_EQ(second_status, std::future_status::timeout);
    EXPECT_TRUE(entered);
    EXPECT_TRUE(observed_stop);
    EXPECT_FALSE(gate->timed_out);
    EXPECT_EQ(gate->opens.load(), boundary == Boundary::IsOpen ? 0U : 1U);
    EXPECT_EQ(gate->reads.load(), boundary == Boundary::Read ? 1U : 0U);
    EXPECT_EQ(gate->closes.load(), 1U);
    EXPECT_EQ(source->stats().received, 0U);
    EXPECT_EQ(source->stats().decoded, 0U);
    ASSERT_TRUE(source->stats().camera_diagnostics);
    EXPECT_TRUE(source->stats().camera_diagnostics->at(0).worker_stopped);
    const auto events = source->poll();
    EXPECT_FALSE(std::any_of(events.begin(), events.end(),
                             [](const auto &event)
                             {
                                 return bool(event.frames[0]) ||
                                        (event.camera_id == 0 &&
                                         event.reason.find("camera_ready:") == 0);
                             }));
    source->stop();
}

INSTANTIATE_TEST_SUITE_P(BackendBoundaries, CameraShutdown,
                         testing::Values(Boundary::IsOpen, Boundary::Open, Boundary::Read));
