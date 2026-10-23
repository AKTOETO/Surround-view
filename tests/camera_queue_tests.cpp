#include "camera_capture.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <gtest/gtest.h>
#include <mutex>

namespace
{
struct CameraGate
{
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<bool> images;
    unsigned entered = 0;
    bool released = false, timed_out = false;

    bool read(cv::Mat &image)
    {
        std::unique_lock<std::mutex> lock(mutex);
        ++entered;
        changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(3),
                              [&] { return released || !images.empty(); }))
        {
            timed_out = true;
            return false;
        }
        if (released)
        {
            return false;
        }
        const bool valid = images.front();
        images.pop_front();
        image = cv::Mat(2, 2, valid ? CV_8UC3 : CV_16UC1, cv::Scalar(3, 5, 7));
        return true;
    }

    bool wait(unsigned count)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2), [&] { return entered >= count; });
    }

    void send(unsigned count, bool valid = true)
    {
        std::lock_guard<std::mutex> lock(mutex);
        images.insert(images.end(), count, valid);
        changed.notify_all();
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        changed.notify_all();
    }
};

class QueuedCapture final : public sv::CameraCapture
{
    std::shared_ptr<CameraGate> gate_;

  public:

    explicit QueuedCapture(std::shared_ptr<CameraGate> gate) : gate_(std::move(gate))
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
        return gate_->read(image);
    }

    void close() override
    {
    }
};

class CameraQueue : public testing::TestWithParam<int>
{
  protected:

    std::array<std::shared_ptr<CameraGate>, 4> gates;
    std::unique_ptr<sv::FrameSource> source;

    void SetUp() override
    {
        auto config = sv::load_config(SV_TEST_CONFIG_PATH);
        config.queue_size = GetParam();
        for (int id = 0; id < 4; ++id)
        {
            config.cameras[id].width = config.cameras[id].height = 2;
            config.source.camera_capture[id].recovery.invalid_frame_delay_ms = 1;
            gates[id] = std::make_shared<CameraGate>();
        }
        source = sv::make_camera_source(config, {}, [&](int id)
                                        { return std::make_unique<QueuedCapture>(gates[id]); });
        for (const auto &gate : gates)
        {
            ASSERT_TRUE(gate->wait(1));
        }
    }

    void TearDown() override
    {
        // Release blocked fake reads before stop, including on assertion failure.
        for (const auto &gate : gates)
        {
            gate->release();
        }
        source->stop();
        for (const auto &gate : gates)
        {
            EXPECT_FALSE(gate->timed_out);
        }
    }
};
} // namespace

TEST_P(CameraQueue, FastCameraKeepsOwnNewestFramesAndPreservesSlowPeerAndControls)
{
    const unsigned capacity = unsigned(GetParam());
    gates[1]->send(1);
    ASSERT_TRUE(gates[1]->wait(2)); // First frame published before the next read starts.
    gates[0]->send(capacity + 69);  // Exceeds the former global 64-event queue as well.
    ASSERT_TRUE(gates[0]->wait(capacity + 70));
    const auto stats = source->stats();
    EXPECT_EQ(stats.received, capacity + 70);
    EXPECT_EQ(stats.decoded, capacity + 70);
    EXPECT_EQ(stats.dropped, 69U);
    EXPECT_EQ(stats.queued_batches, capacity + 1);

    for (unsigned id = 0; id < 64; ++id)
    {
        EXPECT_TRUE(
            source->request(id % 2 ? sv::SourceAction::Resume : sv::SourceAction::Pause, id + 1));
    }
    EXPECT_FALSE(source->request(sv::SourceAction::Pause, 65));
    const auto events = source->poll();
    std::vector<uint64_t> sequences;
    unsigned slow = 0, controls = 0;
    for (const auto &event : events)
    {
        if (event.frames[0])
        {
            sequences.push_back(event.frames[0]->sequence);
            EXPECT_EQ(event.camera_id, 0);
        }
        if (event.frames[1])
        {
            ++slow;
            EXPECT_EQ(event.frames[1]->sequence, 1U);
            EXPECT_EQ(event.camera_id, 1);
        }
        if (event.kind == sv::SourceEvent::Kind::Control)
        {
            EXPECT_EQ(event.request_id, ++controls);
        }
    }
    ASSERT_EQ(sequences.size(), capacity);
    for (unsigned index = 0; index < capacity; ++index)
    {
        EXPECT_EQ(sequences[index], index + 70);
    }
    EXPECT_EQ(slow, 1U);
    EXPECT_EQ(controls, 64U);
    EXPECT_EQ(source->stats().queued_batches, 0U);
    gates[0]->send(1);
    ASSERT_TRUE(gates[0]->wait(capacity + 71));
    EXPECT_EQ(source->stats().queued_batches, 1U);
    EXPECT_EQ(source->stats().dropped, 69U);
    EXPECT_TRUE(source->request(sv::SourceAction::Resume, 66));
}

TEST_P(CameraQueue, FourCameraSaturationRetainsEachNewestWindow)
{
    const unsigned capacity = unsigned(GetParam());
    for (const auto &gate : gates)
    {
        gate->send(capacity + 2);
        ASSERT_TRUE(gate->wait(capacity + 3));
    }
    EXPECT_EQ(source->stats().queued_batches, 4 * capacity);
    EXPECT_EQ(source->stats().dropped, 8U);
    const auto events = source->poll();
    std::array<unsigned, 4> counts{};
    for (const auto &event : events)
    {
        if (event.kind != sv::SourceEvent::Kind::Frames)
        {
            continue;
        }
        ASSERT_GE(event.camera_id, 0);
        ASSERT_LT(event.camera_id, 4);
        const auto id = event.camera_id;
        ASSERT_TRUE(event.frames[id]);
        EXPECT_EQ(event.frames[id]->sequence, 3 + counts[id]++);
    }
    for (const auto count : counts)
    {
        EXPECT_EQ(count, capacity);
    }
    EXPECT_EQ(source->stats().queued_batches, 0U);
}

TEST_P(CameraQueue, StatusFloodDoesNotEvictFramesOrConsumeControlBudget)
{
    const unsigned capacity = unsigned(GetParam());
    gates[1]->send(1);
    ASSERT_TRUE(gates[1]->wait(2));
    for (unsigned index = 0; index < 25; ++index)
    {
        gates[0]->send(1, index % 2 == 0);
        ASSERT_TRUE(gates[0]->wait(index + 2));
    }
    const unsigned frames = std::min(13U, capacity);
    EXPECT_EQ(source->stats().queued_batches, frames + 1);
    EXPECT_EQ(source->stats().dropped, 12 + 13 - frames);
    for (unsigned id = 1; id <= 64; ++id)
    {
        EXPECT_TRUE(source->request(sv::SourceAction::Resume, id));
    }
    EXPECT_FALSE(source->request(sv::SourceAction::Resume, 65));
    const auto events = source->poll();
    EXPECT_EQ(std::count_if(events.begin(), events.end(), [](const auto &event)
                            { return event.kind == sv::SourceEvent::Kind::Status; }),
              16);
    EXPECT_EQ(std::count_if(events.begin(), events.end(),
                            [](const auto &event) { return bool(event.frames[1]); }),
              1);
    // A full drain resets status admission too. Force another transition after the drain.
    gates[0]->send(1, false);
    ASSERT_TRUE(gates[0]->wait(27));
    const auto next = source->poll();
    EXPECT_TRUE(std::any_of(next.begin(), next.end(), [](const auto &event)
                            { return event.reason.find("camera_unsupported_depth:") == 0; }));
}

INSTANTIATE_TEST_SUITE_P(PerCameraCapacity, CameraQueue, testing::Values(1, 3, 16));
