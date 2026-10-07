#include "sv/source.hpp"
#include "sv/vision.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <thread>

namespace sv
{
namespace
{
class CameraFrameSource : public FrameSource
{
public:
    explicit CameraFrameSource(const Config &config)
        : config_(config), stopped_(false)
    {
        for (int i = 0; i < 4; ++i)
        {
            workers_.emplace_back(&CameraFrameSource::capture_loop, this, i);
        }
    }

    ~CameraFrameSource() override
    {
        stop();
    }

    bool request(SourceAction action, uint64_t request_id) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_)
        {
            return false;
        }
        SourceEvent event;
        event.kind = SourceEvent::Kind::Control;
        event.request_id = request_id;

        if (action == SourceAction::Pause)
        {
            paused_ = true;
            event.paused = true;
        }
        else if (action == SourceAction::Resume)
        {
            paused_ = false;
            event.paused = false;
        }
        else if (action == SourceAction::Step)
        {
            event.paused = paused_;
        }
        pending_events_.push_back(event);
        return true;
    }

    std::vector<SourceEvent> poll() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SourceEvent> result = std::move(pending_events_);
        pending_events_.clear();
        return result;
    }

    SourceStats stats() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SourceStats s;
        s.decoded = decoded_count_.load();
        s.received = received_count_.load();
        s.dropped = dropped_count_.load();
        s.queued_batches = pending_events_.size();
        return s;
    }

    void stop() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_)
            {
                return;
            }
            stopped_ = true;
        }
        cv_.notify_all();
        for (auto &worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

private:
    void capture_loop(int camera_id)
    {
        const auto &cam_cfg = config_.cameras[camera_id];
        std::string dev_path = "/dev/video" + std::to_string(camera_id);
        cv::VideoCapture cap;

        if (!cap.open(camera_id, cv::CAP_V4L2))
        {
            cap.open(dev_path, cv::CAP_V4L2);
        }

        if (cap.isOpened())
        {
            cap.set(cv::CAP_PROP_FRAME_WIDTH, cam_cfg.width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, cam_cfg.height);
        }

        cv::Mat frame_mat;
        uint64_t camera_sequence = 0;

        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (stopped_)
                {
                    break;
                }
            }

            if (!cap.isOpened())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            if (!cap.read(frame_mat) || frame_mat.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            const uint64_t ts_ns = now_ns();
            received_count_++;

            if (paused_)
            {
                dropped_count_++;
                continue;
            }

            cv::Mat rgb;
            if (frame_mat.channels() == 3)
            {
                cv::cvtColor(frame_mat, rgb, cv::COLOR_BGR2RGB);
            }
            else
            {
                rgb = frame_mat;
            }

            auto img = std::make_shared<Image>();
            img->width = rgb.cols;
            img->height = rgb.rows;
            img->channels = 3;
            img->pixels.assign(rgb.data, rgb.data + rgb.total() * rgb.elemSize());

            Frame frame;
            frame.camera_id = camera_id;
            frame.sequence = ++camera_sequence;
            frame.release_ns = ts_ns;
            frame.scenario_ns = ts_ns;
            frame.source_sequence = camera_sequence;
            frame.source_timestamp_ns = ts_ns;
            frame.source_session = "camera-hw-v1";
            frame.source_clock_domain = "hardware_v4l2";
            frame.image = img;

            decoded_count_++;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                SourceEvent ev;
                ev.kind = SourceEvent::Kind::Frames;
                ev.batch_id = ++batch_counter_;
                ev.paused = paused_;
                ev.frames[camera_id] = frame;
                pending_events_.push_back(ev);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }

        if (cap.isOpened())
        {
            cap.release();
        }
    }

    Config config_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
    bool paused_ = false;
    uint64_t batch_counter_ = 0;

    std::atomic<uint64_t> decoded_count_{0};
    std::atomic<uint64_t> received_count_{0};
    std::atomic<uint64_t> dropped_count_{0};

    std::vector<std::thread> workers_;
    std::vector<SourceEvent> pending_events_;
};
} // namespace

std::unique_ptr<FrameSource> make_camera_source(const Config &config)
{
    return std::make_unique<CameraFrameSource>(config);
}
} // namespace sv
