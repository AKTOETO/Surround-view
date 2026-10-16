#include "opencv_capture.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <opencv2/videoio.hpp>
#include <thread>

namespace
{
struct DeviceState
{
    std::map<int, double> properties{
        {cv::CAP_PROP_FRAME_WIDTH, 2},
        {cv::CAP_PROP_FRAME_HEIGHT, 2},
        {cv::CAP_PROP_CONVERT_RGB, 1},
        {cv::CAP_PROP_FPS, 30},
        {cv::CAP_PROP_BUFFERSIZE, 4},
        {cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V')}};
    std::vector<int> set_order;
    std::vector<std::string> operations;
    std::atomic<unsigned> opens{0}, reads{0}, closes{0};
    int ignored_property = -1, backend = -1, throwing_get = -1;
    mutable std::vector<int> queried_properties;
    bool return_false_on_set = false, drift_after_set = false, ignore_first_open_only = false;
    bool fail_open_once = false, fail_close = false;
    std::string path;
};

class MockVideoDevice final : public sv::VideoDevice
{
  public:

    explicit MockVideoDevice(std::shared_ptr<DeviceState> state) : state_(std::move(state))
    {
    }

    bool is_open() const override
    {
        return opened_;
    }

    bool open(const std::string &path, int backend) override
    {
        const auto attempt = ++state_->opens;
        state_->operations.push_back("open");
        state_->path = path;
        state_->backend = backend;
        opened_ = true;
        // Model a partially acquired device even when open returns false.
        return !(state_->fail_open_once && attempt == 1);
    }

    bool set(int property, double value) override
    {
        state_->set_order.push_back(property);
        if (property == state_->ignored_property &&
            (!state_->ignore_first_open_only || state_->opens == 1))
        {
            return !state_->return_false_on_set;
        }
        state_->properties[property] = value;
        if (state_->drift_after_set && property == cv::CAP_PROP_CONVERT_RGB)
        {
            state_->properties[cv::CAP_PROP_FRAME_WIDTH] = 3;
        }
        return !state_->return_false_on_set;
    }

    double get(int property) const override
    {
        state_->queried_properties.push_back(property);
        if (property == state_->throwing_get)
        {
            CV_Error(cv::Error::StsError, "injected property query failure");
        }
        return state_->properties.at(property);
    }

    bool read(cv::Mat &image) override
    {
        ++state_->reads;
        state_->operations.push_back("read");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        image = cv::Mat(int(state_->properties.at(cv::CAP_PROP_FRAME_HEIGHT)),
                        int(state_->properties.at(cv::CAP_PROP_FRAME_WIDTH)), CV_8UC3,
                        cv::Scalar(3, 5, 7));
        return true;
    }

    void close() override
    {
        ++state_->closes;
        state_->operations.push_back("close");
        if (state_->fail_close)
        {
            CV_Error(cv::Error::StsError, "injected close failure");
        }
        opened_ = false;
    }

  private:

    std::shared_ptr<DeviceState> state_;
    bool opened_ = false;
};

std::unique_ptr<sv::CameraCapture> capture(std::shared_ptr<DeviceState> state,
                                           sv::CameraCaptureSettings settings = {})
{
    return sv::make_opencv_capture(settings, std::make_unique<MockVideoDevice>(state));
}

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

class CameraNegotiation : public testing::TestWithParam<int>
{
};
} // namespace

TEST(CameraNegotiation,
     DefaultProfileChecksSizeAndDecodedConversionWithoutChangingOptionalProperties)
{
    auto state = std::make_shared<DeviceState>();
    auto camera = capture(state);
    ASSERT_TRUE(camera->open("/dev/v4l/by-id/mock", 2, 2));
    EXPECT_EQ(state->path, "/dev/v4l/by-id/mock");
    EXPECT_EQ(state->backend, cv::CAP_V4L2);
    EXPECT_EQ(state->set_order,
              (std::vector<int>{cv::CAP_PROP_FRAME_WIDTH, cv::CAP_PROP_FRAME_HEIGHT,
                                cv::CAP_PROP_CONVERT_RGB}));
    EXPECT_TRUE(camera->open_failure().empty());
    ASSERT_TRUE(camera->negotiated_mode());
    EXPECT_EQ(camera->negotiated_mode()->width, 2U);
    EXPECT_FALSE(camera->negotiated_mode()->fps);
    EXPECT_FALSE(camera->negotiated_mode()->fourcc);
    EXPECT_FALSE(camera->negotiated_mode()->buffer_count);
    ASSERT_TRUE(camera->negotiated_mode()->automatic_properties);
    const auto automatic = *camera->negotiated_mode()->automatic_properties;
    EXPECT_EQ(automatic.fps, 30);
    EXPECT_EQ(automatic.fourcc, "YUYV");
    EXPECT_EQ(automatic.buffer_count, 4U);
    camera->close();
    EXPECT_FALSE(camera->negotiated_mode());
}

TEST(CameraNegotiation, ExplicitProfileAppliesFourccBeforeDimensionsAndAcceptsFractionalFps)
{
    auto state = std::make_shared<DeviceState>();
    state->ignored_property = cv::CAP_PROP_FPS;
    state->properties[cv::CAP_PROP_FPS] = 29.97;
    auto camera = capture(state, {30, "MJPG", 2});
    ASSERT_TRUE(camera->open("/dev/mock", 2, 2));
    EXPECT_EQ(
        state->set_order,
        (std::vector<int>{cv::CAP_PROP_FOURCC, cv::CAP_PROP_FRAME_WIDTH, cv::CAP_PROP_FRAME_HEIGHT,
                          cv::CAP_PROP_FPS, cv::CAP_PROP_BUFFERSIZE, cv::CAP_PROP_CONVERT_RGB}));
    camera->close();
}

TEST_P(CameraNegotiation, MismatchedOrUnsupportedPropertyRejectsBeforeRead)
{
    const int property = GetParam();
    auto state = std::make_shared<DeviceState>();
    state->ignored_property = property;
    state->properties[property] = 0; // Unsupported get or negotiated disagreement.
    auto camera = capture(state, {30, "MJPG", 2});
    EXPECT_FALSE(camera->open("/dev/mock", 2, 2));
    EXPECT_EQ(camera->open_failure().find("camera_configuration_rejected:"), 0U);
    EXPECT_EQ(state->reads, 0U);
    EXPECT_FALSE(camera->negotiated_mode());
    camera->close();
}

INSTANTIATE_TEST_SUITE_P(WidthHeightConversionFpsFourccBuffers, CameraNegotiation,
                         testing::Values(cv::CAP_PROP_FRAME_WIDTH, cv::CAP_PROP_FRAME_HEIGHT,
                                         cv::CAP_PROP_CONVERT_RGB, cv::CAP_PROP_FPS,
                                         cv::CAP_PROP_FOURCC, cv::CAP_PROP_BUFFERSIZE));

TEST(CameraNegotiation, SetFalseIsAcceptableOnlyWhenReadbackAlreadyMatches)
{
    auto state = std::make_shared<DeviceState>();
    state->return_false_on_set = true;
    auto camera = capture(state);
    EXPECT_TRUE(camera->open("/dev/mock", 2, 2));
    camera->close();
    state->ignored_property = cv::CAP_PROP_FRAME_WIDTH;
    state->properties[cv::CAP_PROP_FRAME_WIDTH] = 640;
    EXPECT_FALSE(camera->open("/dev/mock", 2, 2));
    camera->close();
}

TEST(CameraNegotiation, ReadbackAfterAllSettingsDetectsLaterSizeReset)
{
    auto state = std::make_shared<DeviceState>();
    state->drift_after_set = true;
    auto camera = capture(state);
    EXPECT_FALSE(camera->open("/dev/mock", 2, 2));
    EXPECT_NE(camera->open_failure().find(":width:"), std::string::npos);
    camera->close();
}

TEST(CameraNegotiation, NonfiniteReadbackAndInvalidDirectSettingsAreRejected)
{
    auto state = std::make_shared<DeviceState>();
    state->ignored_property = cv::CAP_PROP_FRAME_WIDTH;
    state->properties[cv::CAP_PROP_FRAME_WIDTH] = std::numeric_limits<double>::quiet_NaN();
    auto camera = capture(state);
    EXPECT_FALSE(camera->open("/dev/mock", 2, 2));
    camera->close();
    EXPECT_THROW(capture(state, {0, "", {}}), std::invalid_argument);
    EXPECT_THROW(capture(state, {std::numeric_limits<double>::infinity(), "", {}}),
                 std::invalid_argument);
    EXPECT_THROW(capture(state, {{}, "BAD", {}}), std::invalid_argument);
    EXPECT_THROW(capture(state, {{}, "", 0}), std::invalid_argument);
}

TEST(CameraNegotiation, WorkerClosesRejectedOrPartiallyOpenedDeviceBeforeRetry)
{
    for (bool partial : {false, true})
    {
        auto state = std::make_shared<DeviceState>();
        state->fail_open_once = partial;
        if (!partial)
        {
            state->ignored_property = cv::CAP_PROP_FRAME_WIDTH;
            state->properties[cv::CAP_PROP_FRAME_WIDTH] = 3;
            state->ignore_first_open_only = true;
        }
        auto config = sv::load_config(SV_TEST_CONFIG_PATH);
        for (auto &camera : config.cameras)
        {
            camera.width = camera.height = 2;
        }
        auto source = sv::make_camera_source(config, {},
                                             [state](int id) -> std::unique_ptr<sv::CameraCapture>
                                             {
                                                 if (id == 0)
                                                 {
                                                     return capture(state);
                                                 }
                                                 return std::make_unique<UnavailableCapture>();
                                             });
        std::vector<sv::SourceEvent> events;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool ready_frame = false;
        while (std::chrono::steady_clock::now() < deadline && !ready_frame)
        {
            auto next = source->poll();
            for (const auto &event : next)
            {
                ready_frame = ready_frame || bool(event.frames[0]);
            }
            events.insert(events.end(), std::make_move_iterator(next.begin()),
                          std::make_move_iterator(next.end()));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto mode = source->stats().camera_diagnostics->at(0).negotiated_mode;
        source->stop();
        EXPECT_TRUE(ready_frame);
        ASSERT_TRUE(mode);
        EXPECT_EQ(mode->open_attempt, 2U);
        EXPECT_EQ(mode->width, 2U);
        EXPECT_FALSE(source->stats().camera_diagnostics->at(0).negotiated_mode);
        ASSERT_GE(state->operations.size(), 4U);
        EXPECT_EQ(state->operations[0], "open");
        EXPECT_EQ(state->operations[1], "close");
        EXPECT_EQ(state->operations[2], "open");
        EXPECT_EQ(state->operations[3], "read");
        EXPECT_GE(state->closes, 2U);
        EXPECT_EQ(source->stats().camera_diagnostics->at(0).open_attempts, 2U);
        EXPECT_EQ(source->stats().camera_diagnostics->at(0).reconnects, 0U);
        EXPECT_TRUE(std::any_of(
            events.begin(), events.end(),
            [&](const auto &event)
            {
                return event.camera_id == 0 &&
                       event.reason.find(partial ? "camera_unavailable:"
                                                 : "camera_configuration_rejected:width:") == 0;
            }));
    }
}

TEST(CameraCaptureConfig, FourIndependentProfilesAndLegacyDefaults)
{
    auto root = sv::read_json(SV_TEST_CONFIG_PATH).as_object();
    root["source"] = boost::json::object{{"type", "camera"}};
    auto config = sv::parse_config(root);
    EXPECT_FALSE(config.source.camera_capture[0].fps);
    root.at("source").as_object()["capture"] = boost::json::array{
        boost::json::object{
            {"camera_id", 0}, {"fps", 29.97}, {"fourcc", "MJPG"}, {"buffer_count", 2}},
        boost::json::object{{"camera_id", 1}, {"fourcc", "YUYV"}},
        boost::json::object{{"camera_id", 2}, {"fps", 15}}, boost::json::object{{"camera_id", 3}}};
    config = sv::parse_config(root);
    EXPECT_EQ(config.source.camera_capture[0].fps, 29.97);
    EXPECT_EQ(config.source.camera_capture[0].buffer_count, 2U);
    EXPECT_EQ(config.source.camera_capture[1].fourcc, "YUYV");
    EXPECT_FALSE(config.source.camera_capture[1].fps);
    EXPECT_EQ(config.source.camera_capture[2].fps, 15);
    EXPECT_TRUE(config.source.camera_capture[3].fourcc.empty());
    const auto good = root;
    auto reject = [&](auto mutate)
    {
        auto invalid = good;
        mutate(invalid.at("source").as_object().at("capture"));
        EXPECT_ANY_THROW(sv::parse_config(invalid));
    };
    reject([](auto &v) { v.as_array().pop_back(); });
    reject([](auto &v) { v.as_array()[1].as_object()["camera_id"] = 0; });
    for (auto value : {0., -1., 241.})
    {
        reject([value](auto &v) { v.as_array()[0].as_object()["fps"] = value; });
    }
    for (const auto *code : {"", "MJPEG", "\tABC"})
    {
        reject([code](auto &v) { v.as_array()[0].as_object()["fourcc"] = code; });
    }
    reject([](auto &v) { v.as_array()[0].as_object()["buffer_count"] = 33; });
    reject([](auto &v) { v.as_array()[0].as_object()["width"] = 1280; });
}

TEST(CameraNegotiation, MultipleSensorProfilesReturnExpectedMockImageDimensions)
{
    struct Profile
    {
        int width, height;
        double fps;
        const char *fourcc;
        unsigned buffers;
    };

    for (const auto &profile : {Profile{640, 480, 25, "YUYV", 1},
                                {1280, 720, 30, "MJPG", 2},
                                {1920, 1080, 29.97, "MJPG", 4},
                                {320, 240, 15, "GREY", 8}})
    {
        auto state = std::make_shared<DeviceState>();
        auto camera = capture(state, {profile.fps, profile.fourcc, profile.buffers});
        ASSERT_TRUE(camera->open("/dev/mock", profile.width, profile.height));
        cv::Mat image;
        ASSERT_TRUE(camera->read(image));
        EXPECT_EQ(image.cols, profile.width);
        EXPECT_EQ(image.rows, profile.height);
        EXPECT_EQ(state->properties[cv::CAP_PROP_FPS], profile.fps);
        EXPECT_EQ(state->properties[cv::CAP_PROP_BUFFERSIZE], profile.buffers);
        camera->close();
    }
}

TEST(CameraNegotiation, ReleaseFailureAfterRejectedProfileStopsWorkerBeforeAnyRead)
{
    auto state = std::make_shared<DeviceState>();
    state->ignored_property = cv::CAP_PROP_FRAME_WIDTH;
    state->properties[cv::CAP_PROP_FRAME_WIDTH] = 3;
    state->fail_close = true;
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    for (auto &camera : config.cameras)
    {
        camera.width = camera.height = 2;
    }
    auto source = sv::make_camera_source(config, {},
                                         [state](int id) -> std::unique_ptr<sv::CameraCapture>
                                         {
                                             if (id == 0)
                                             {
                                                 return capture(state);
                                             }
                                             return std::make_unique<UnavailableCapture>();
                                         });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!source->stats().camera_diagnostics->at(0).worker_stopped &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    source->stop();
    EXPECT_EQ(state->opens, 1U);
    EXPECT_EQ(state->reads, 0U);
    EXPECT_EQ(state->closes, 1U);
    EXPECT_EQ(source->stats().camera_diagnostics->at(0).release_failures, 1U);
}

TEST(CameraNegotiation, RejectedReopenAndFailedReleaseClearPreviousMode)
{
    auto state = std::make_shared<DeviceState>();
    auto camera = capture(state);
    ASSERT_TRUE(camera->open("/dev/mock", 2, 2));
    ASSERT_TRUE(camera->negotiated_mode());
    camera->close();
    state->ignored_property = cv::CAP_PROP_FRAME_WIDTH;
    state->properties[cv::CAP_PROP_FRAME_WIDTH] = 3;
    EXPECT_FALSE(camera->open("/dev/mock", 2, 2));
    EXPECT_FALSE(camera->negotiated_mode());
    camera->close();
    state->ignored_property = -1;
    ASSERT_TRUE(camera->open("/dev/mock", 2, 2));
    state->fail_close = true;
    EXPECT_THROW(camera->close(), cv::Exception);
    EXPECT_FALSE(camera->negotiated_mode());
}

TEST(CameraNegotiation, AutomaticReadbackIsBestEffortAndDoesNotRepeatExplicitQueries)
{
    for (int property : {cv::CAP_PROP_FPS, cv::CAP_PROP_FOURCC, cv::CAP_PROP_BUFFERSIZE})
    {
        auto state = std::make_shared<DeviceState>();
        state->throwing_get = property;
        auto camera = capture(state);
        ASSERT_TRUE(camera->open("/dev/mock", 2, 2));
        ASSERT_TRUE(camera->negotiated_mode()->automatic_properties);
        const auto automatic = *camera->negotiated_mode()->automatic_properties;
        EXPECT_EQ(bool(automatic.fps), property != cv::CAP_PROP_FPS);
        EXPECT_EQ(bool(automatic.fourcc), property != cv::CAP_PROP_FOURCC);
        EXPECT_EQ(bool(automatic.buffer_count), property != cv::CAP_PROP_BUFFERSIZE);
        EXPECT_TRUE(camera->open_failure().empty());
        auto strict_state = std::make_shared<DeviceState>();
        strict_state->throwing_get = property;
        auto strict = capture(strict_state, {30, "MJPG", 2});
        EXPECT_THROW(strict->open("/dev/mock", 2, 2), cv::Exception);
        EXPECT_FALSE(strict->negotiated_mode());
    }
    auto state = std::make_shared<DeviceState>();
    auto camera = capture(state, {30, "MJPG", 2});
    ASSERT_TRUE(camera->open("/dev/mock", 2, 2));
    const auto automatic = *camera->negotiated_mode()->automatic_properties;
    EXPECT_FALSE(automatic.fps);
    EXPECT_FALSE(automatic.fourcc);
    EXPECT_FALSE(automatic.buffer_count);
    for (int property : {cv::CAP_PROP_FPS, cv::CAP_PROP_FOURCC, cv::CAP_PROP_BUFFERSIZE})
    {
        EXPECT_EQ(std::count(state->queried_properties.begin(), state->queried_properties.end(),
                             property),
                  1);
    }
}

TEST(CameraNegotiation, AutomaticReadbackRejectsNonfiniteInvalidAndFractionalValuesIndependently)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (double invalid : {0., -1., nan, infinity})
    {
        auto state = std::make_shared<DeviceState>();
        state->properties[cv::CAP_PROP_FPS] = invalid;
        state->properties[cv::CAP_PROP_FOURCC] = invalid;
        state->properties[cv::CAP_PROP_BUFFERSIZE] = invalid;
        auto camera = capture(state);
        ASSERT_TRUE(camera->open("/dev/mock", 2, 2));
        const auto automatic = *camera->negotiated_mode()->automatic_properties;
        EXPECT_FALSE(automatic.fps);
        EXPECT_FALSE(automatic.fourcc);
        EXPECT_FALSE(automatic.buffer_count);
    }
    for (double invalid :
         {1.5, 4294967295., 4294967296., double(cv::VideoWriter::fourcc('M', 'J', 'P', '\0'))})
    {
        auto state = std::make_shared<DeviceState>();
        state->properties[cv::CAP_PROP_FOURCC] = invalid;
        state->properties[cv::CAP_PROP_BUFFERSIZE] =
            invalid == 1.5 || invalid == 4294967296. ? invalid : 0;
        auto camera = capture(state);
        ASSERT_TRUE(camera->open("/dev/mock", 2, 2));
        const auto automatic = *camera->negotiated_mode()->automatic_properties;
        EXPECT_FALSE(automatic.fourcc);
        EXPECT_FALSE(automatic.buffer_count);
        EXPECT_EQ(automatic.fps, 30);
    }
    auto state = std::make_shared<DeviceState>();
    state->properties[cv::CAP_PROP_FPS] = 29.97;
    state->properties[cv::CAP_PROP_BUFFERSIZE] = 64;
    auto camera = capture(state, {{}, "MJPG", {}});
    ASSERT_TRUE(camera->open("/dev/mock", 2, 2));
    const auto automatic = *camera->negotiated_mode()->automatic_properties;
    EXPECT_EQ(automatic.fps, 29.97);
    EXPECT_FALSE(automatic.fourcc);
    EXPECT_EQ(automatic.buffer_count,
              64U); // Observed values are not bounded by config's 32-buffer limit.
}
