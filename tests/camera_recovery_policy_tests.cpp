#include "camera_capture.hpp"
#include "camera_recovery.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <thread>

namespace
{
sv::CameraRecoverySettings short_policy(unsigned budget = 3)
{
    return {2, 2, 2, 2, 10, 2, budget};
}

struct FailureState
{
    std::atomic<unsigned> opens{0}, reads{0};
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
};

class FailingCapture final : public sv::CameraCapture
{
  public:

    FailingCapture(std::shared_ptr<FailureState> state, int kind)
        : state_(std::move(state)), kind_(kind)
    {
    }

    bool is_open() const override
    {
        return opened_;
    }

    bool open(const std::string &, int, int) override
    {
        ++state_->opens;
        opened_ = true;
        if (kind_ == 3)
        {
            CV_Error(cv::Error::StsError, "injected open exception");
        }
        return kind_ != 0;
    }

    bool read(cv::Mat &image) override
    {
        const auto read = ++state_->reads;
        if (kind_ == 1 || (kind_ == 4 && read == 6))
        {
            return false;
        }
        if (kind_ == 4 && read == 1)
        {
            std::unique_lock<std::mutex> lock(state_->mutex);
            state_->entered = true;
            state_->changed.notify_all();
            state_->changed.wait_for(lock, std::chrono::seconds(3),
                                     [&] { return state_->released; });
        }
        image = cv::Mat(kind_ == 4 && read == 3 ? 2 : 3, 2, CV_8UC3, cv::Scalar(1, 2, 3));
        return true;
    }

    void close() override
    {
        opened_ = false;
    }

  private:

    std::shared_ptr<FailureState> state_;
    int kind_;
    bool opened_ = false;
};

class OtherCapture final : public sv::CameraCapture
{
  public:

    explicit OtherCapture(bool healthy) : healthy_(healthy)
    {
    }

    bool is_open() const override
    {
        return healthy_;
    }

    bool open(const std::string &, int, int) override
    {
        return false;
    }

    bool read(cv::Mat &image) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        image = cv::Mat(2, 2, CV_8UC3, cv::Scalar(1, 2, 3));
        return true;
    }

    void close() override
    {
    }

  private:

    bool healthy_;
};

std::vector<sv::SourceEvent> wait_stopped(sv::FrameSource &source, bool healthy_peer)
{
    std::vector<sv::SourceEvent> events;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto next = source.poll();
        events.insert(events.end(), std::make_move_iterator(next.begin()),
                      std::make_move_iterator(next.end()));
        const bool exhausted =
            std::any_of(events.begin(), events.end(), [](const auto &e)
                        { return e.camera_id == 0 && e.reason == "camera_retry_exhausted"; });
        const bool peer =
            !healthy_peer || std::any_of(events.begin(), events.end(),
                                         [](const auto &e) { return bool(e.frames[1]); });
        if (source.stats().camera_diagnostics->at(0).worker_stopped && exhausted && peer)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return events;
}

class CameraRecoveryBudget : public testing::TestWithParam<int>
{
};
} // namespace

TEST(CameraRecoveryPolicy, LegacyDefaultsAndCappedBackoffResetOnlyOnSuccess)
{
    sv::CameraRecoveryPolicy legacy({});
    EXPECT_EQ(legacy.failure(sv::CameraFailure::Open)->count(), 250);
    EXPECT_EQ(legacy.failure(sv::CameraFailure::Read)->count(), 100);
    EXPECT_EQ(legacy.failure(sv::CameraFailure::InvalidFrame)->count(), 100);
    EXPECT_EQ(legacy.failure(sv::CameraFailure::Exception)->count(), 250);
    auto settings = short_policy(0);
    settings.open_delay_ms = 3;
    sv::CameraRecoveryPolicy policy(settings);
    EXPECT_EQ(policy.failure(sv::CameraFailure::Open)->count(), 3);
    EXPECT_EQ(policy.failure(sv::CameraFailure::Read)->count(), 4);
    EXPECT_EQ(policy.failure(sv::CameraFailure::Open)->count(), 10);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(policy.failure(sv::CameraFailure::Read)->count(), 10);
    }
    policy.success();
    EXPECT_EQ(policy.consecutive_failures(), 0U);
    EXPECT_EQ(policy.failure(sv::CameraFailure::Read)->count(), 2);
}

TEST(CameraRecoveryPolicy, BudgetIncludesFirstFailureAndDirectInvalidSettingsAreRejected)
{
    sv::CameraRecoveryPolicy policy(short_policy(3));
    EXPECT_TRUE(policy.failure(sv::CameraFailure::Read));
    EXPECT_TRUE(policy.failure(sv::CameraFailure::Read));
    EXPECT_FALSE(policy.failure(sv::CameraFailure::Read));
    sv::CameraRecoveryPolicy once(short_policy(1));
    EXPECT_FALSE(once.failure(sv::CameraFailure::Open));
    auto invalid = short_policy();
    invalid.read_delay_ms = 0;
    EXPECT_THROW(sv::CameraRecoveryPolicy{invalid}, std::invalid_argument);
    invalid = short_policy();
    invalid.max_delay_ms = 1;
    EXPECT_THROW(sv::CameraRecoveryPolicy{invalid}, std::invalid_argument);
    invalid = short_policy();
    invalid.backoff_multiplier = 9;
    EXPECT_THROW(sv::CameraRecoveryPolicy{invalid}, std::invalid_argument);
    auto unlimited = short_policy(0);
    unlimited.backoff_multiplier = 1;
    sv::CameraRecoveryPolicy fixed(unlimited);
    for (unsigned i = 0; i < 10000; ++i)
    {
        ASSERT_EQ(fixed.failure(sv::CameraFailure::Open)->count(), 2);
    }
}

TEST_P(CameraRecoveryBudget, PersistentFailureStopsOnlyConfiguredWorkerWithoutResetOnOpen)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    config.source.camera_capture[0].recovery = short_policy();
    auto state = std::make_shared<FailureState>();
    const int kind = GetParam();
    auto source =
        sv::make_camera_source(config, {},
                               [state, kind](int id) -> std::unique_ptr<sv::CameraCapture>
                               {
                                   if (id == 0)
                                   {
                                       return std::make_unique<FailingCapture>(state, kind);
                                   }
                                   return std::make_unique<OtherCapture>(id == 1);
                               });
    const auto events = wait_stopped(*source, true);
    EXPECT_TRUE(source->stats().camera_diagnostics->at(0).worker_stopped);
    EXPECT_TRUE(source->request(sv::SourceAction::Resume,
                                1)); // Source-wide resume cannot restart a dead worker.
    source->stop();
    EXPECT_EQ(state->opens, kind == 2 ? 1U : 3U);
    EXPECT_EQ(state->reads, kind == 0 || kind == 3 ? 0U : 3U);
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto &e)
                            { return e.camera_id == 0 && e.reason == "camera_retry_exhausted"; }));
    EXPECT_TRUE(
        std::any_of(events.begin(), events.end(), [](const auto &e) { return bool(e.frames[1]); }));
    EXPECT_FALSE(
        std::any_of(events.begin(), events.end(), [](const auto &e) { return bool(e.frames[0]); }));
}

INSTANTIATE_TEST_SUITE_P(OpenReadFormatException, CameraRecoveryBudget,
                         testing::Values(0, 1, 2, 3));

TEST(CameraRecoveryPolicy, ValidRgbResetsBudgetEvenWhenPausedAndNeverPublished)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    config.source.camera_capture[0].recovery = short_policy();
    auto state = std::make_shared<FailureState>();
    auto source = sv::make_camera_source(config, {},
                                         [state](int id) -> std::unique_ptr<sv::CameraCapture>
                                         {
                                             if (id == 0)
                                             {
                                                 return std::make_unique<FailingCapture>(state, 4);
                                             }
                                             return std::make_unique<OtherCapture>(false);
                                         });
    bool entered;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        entered =
            state->changed.wait_for(lock, std::chrono::seconds(2), [&] { return state->entered; });
    }
    const bool paused = source->request(sv::SourceAction::Pause, 1);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->released = true;
    }
    state->changed.notify_all();
    const auto events = wait_stopped(*source, false);
    source->stop();
    EXPECT_TRUE(entered);
    EXPECT_TRUE(paused);
    EXPECT_EQ(state->reads, 6U);
    EXPECT_EQ(state->opens, 1U);
    EXPECT_EQ(source->stats().decoded, 0U);
    EXPECT_EQ(source->stats().camera_diagnostics->at(0).read_failures, 1U);
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto &e)
                            { return e.camera_id == 0 && e.reason == "camera_retry_exhausted"; }));
}

TEST(CameraRecoveryPolicy, StopInterruptsConfiguredSixtySecondWait)
{
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    config.source.camera_capture[0].recovery.open_delay_ms = 60000;
    config.source.camera_capture[0].recovery.max_delay_ms = 60000;
    auto source = sv::make_camera_source(config, {},
                                         [](int) { return std::make_unique<OtherCapture>(false); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool unavailable = false;
    while (!unavailable && std::chrono::steady_clock::now() < deadline)
    {
        for (const auto &event : source->poll())
        {
            unavailable = unavailable ||
                          (event.camera_id == 0 && event.reason.find("camera_unavailable:") == 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto stopped = std::async(std::launch::async, [&] { source->stop(); });
    EXPECT_EQ(stopped.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    stopped.get();
    EXPECT_TRUE(unavailable);
}

TEST(CameraRecoveryPolicy, ConfigParsesIndependentPartialPoliciesAndRejectsInvalidBounds)
{
    auto root = sv::read_json(SV_TEST_CONFIG_PATH).as_object();
    root["source"] = boost::json::object{
        {"type", "camera"},
        {"capture",
         boost::json::array{boost::json::object{
                                {"camera_id", 0},
                                {"recovery", boost::json::object{{"backoff_multiplier", 2},
                                                                 {"max_consecutive_failures", 5}}}},
                            boost::json::object{{"camera_id", 1}},
                            boost::json::object{{"camera_id", 2}},
                            boost::json::object{{"camera_id", 3}}}}};
    const auto config = sv::parse_config(root);
    EXPECT_EQ(config.source.camera_capture[0].recovery.backoff_multiplier, 2U);
    EXPECT_EQ(config.source.camera_capture[0].recovery.max_consecutive_failures, 5U);
    EXPECT_EQ(config.source.camera_capture[1].recovery.backoff_multiplier, 1U);
    const auto good = root;
    auto reject = [&](const char *key, boost::json::value value)
    {
        auto bad = good;
        bad.at("source")
            .as_object()
            .at("capture")
            .as_array()[0]
            .as_object()
            .at("recovery")
            .as_object()[key] = value;
        EXPECT_ANY_THROW(sv::parse_config(bad));
    };
    reject("read_delay_ms", 0);
    reject("open_delay_ms", 60001);
    reject("backoff_multiplier", 9);
    reject("max_consecutive_failures", -1);
    reject("max_delay_ms", 1);
    reject("read_delay_ms", 1.5);
    reject("timeout_ms", 100);
    auto direct = config;
    direct.source.camera_capture[0].recovery.read_delay_ms = 0;
    EXPECT_THROW(sv::make_camera_source(direct, {},
                                        [](int) { return std::make_unique<OtherCapture>(false); }),
                 std::invalid_argument);
}
