#include "opencv_capture.hpp"
#include "sv/vision.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <opencv2/imgproc.hpp>
#include <string_view>
#include <thread>

namespace sv
{
namespace
{
class CameraFrameSource : public FrameSource
{
  public:

    explicit CameraFrameSource(const Config &config,
                               std::shared_ptr<const SourceTimingSession> timing,
                               CameraCaptureFactory factory)
        : config_(config), stopped_(false)
    {
        timing_.set(std::move(timing));
        for (int i = 0; i < 4; ++i)
        {
            captures_[i] =
                factory ? factory(i) : make_opencv_capture(config.source.camera_capture[i]);
            if (!captures_[i])
            {
                throw std::invalid_argument("null camera capture");
            }
        }
        try
        {
            for (int i = 0; i < 4; ++i)
            {
                workers_.emplace_back(&CameraFrameSource::capture_loop, this, i);
            }
        }
        catch (...)
        {
            stop();
            throw;
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

        if (action == SourceAction::Step || pending_events_.size() >= queue_capacity)
        {
            return false;
        }
        if (action == SourceAction::Pause)
        {
            paused_.store(true);
            event.paused = true;
        }
        else if (action == SourceAction::Resume)
        {
            paused_.store(false);
            event.paused = false;
        }
        pending_events_.push_back(event);
        return true;
    }

    std::vector<SourceEvent> poll() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SourceEvent> result;
        result.reserve(pending_events_.size());
        while (!pending_events_.empty())
        {
            result.push_back(std::move(pending_events_.front()));
            pending_events_.pop_front();
        }
        return result;
    }

    void set_timing_session(std::shared_ptr<const SourceTimingSession> timing) noexcept override
    {
        timing_.set(std::move(timing));
    }

    SourceStats stats() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SourceStats s;
        s.decoded = decoded_count_.load();
        s.received = received_count_.load();
        s.dropped = dropped_count_.load();
        s.queued_batches = pending_events_.size();
        s.camera_diagnostics = diagnostics_;
        return s;
    }

    void stop() override
    {
        std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_)
            {
                return;
            }
            stopped_.store(true, std::memory_order_release);
        }
        wake_.notify_all();
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
        const auto &dev_path = config_.source.camera_devices[camera_id];
        auto &cap = *captures_[camera_id];
        cv::Mat frame_mat;
        uint64_t camera_sequence = 0;
        std::string last_status;
        bool previously_opened = false;

        while (true)
        {
            if (stopping())
            {
                break;
            }

            const char *phase = "open";
            try
            {
                if (!cap.is_open())
                {
                    if (stopping())
                    {
                        break;
                    }
                    update_camera(camera_id,
                                  [](auto &state)
                                  {
                                      ++state.open_attempts;
                                      state.negotiated_mode.reset();
                                  });
                    if (!cap.open(dev_path, cam_cfg.width, cam_cfg.height))
                    {
                        const auto failure = cap.open_failure();
                        emit_status(camera_id, last_status,
                                    failure.empty() ? "camera_unavailable:" + dev_path : failure);
                        if (!close_capture(cap, camera_id, last_status))
                        {
                            return;
                        }
                        wait_retry(std::chrono::milliseconds(250));
                        continue;
                    }
                    auto mode = cap.negotiated_mode();
                    update_camera(camera_id,
                                  [&mode](auto &state)
                                  {
                                      if (mode)
                                      {
                                          mode->open_attempt = state.open_attempts;
                                      }
                                      state.negotiated_mode = std::move(mode);
                                  });
                    if (previously_opened)
                    {
                        update_camera(camera_id, [](auto &state) { ++state.reconnects; });
                    }
                    previously_opened = true;
                }

                if (stopping())
                {
                    break;
                }
                phase = "read";
                const auto timing = timing_.get();
                const auto read_start = timing ? now_ns() : 0;
                const bool read_ok = cap.read(frame_mat);
                const auto read_done = timing ? now_ns() : 0;
                if (stopping())
                {
                    break;
                }
                if (!read_ok || frame_mat.empty())
                {
                    update_camera(camera_id, [](auto &state) { ++state.read_failures; });
                    emit_status(camera_id, last_status, "camera_read_failed:" + dev_path);
                    if (!close_capture(cap, camera_id, last_status))
                    {
                        return;
                    }
                    wait_retry(std::chrono::milliseconds(100));
                    continue;
                }

                const uint64_t ts_ns = now_ns();
                received_count_++;

                if (frame_mat.cols != cam_cfg.width || frame_mat.rows != cam_cfg.height)
                {
                    dropped_count_++;
                    emit_status(camera_id, last_status,
                                "camera_resolution_mismatch:" + std::to_string(frame_mat.cols) +
                                    "x" + std::to_string(frame_mat.rows));
                    wait_retry(std::chrono::milliseconds(100));
                    continue;
                }

                if (frame_mat.depth() != CV_8U)
                {
                    dropped_count_++;
                    emit_status(camera_id, last_status,
                                "camera_unsupported_depth:" + std::to_string(frame_mat.depth()));
                    wait_retry(std::chrono::milliseconds(100));
                    continue;
                }

                phase = "prepare";
                const auto prepare_start = timing ? now_ns() : 0;
                cv::Mat rgb;
                if (frame_mat.channels() == 3)
                {
                    cv::cvtColor(frame_mat, rgb, cv::COLOR_BGR2RGB);
                }
                else if (frame_mat.channels() == 1)
                {
                    cv::cvtColor(frame_mat, rgb, cv::COLOR_GRAY2RGB);
                }
                else if (frame_mat.channels() == 4)
                {
                    cv::cvtColor(frame_mat, rgb, cv::COLOR_BGRA2RGB);
                }
                else
                {
                    dropped_count_++;
                    emit_status(camera_id, last_status,
                                "camera_unsupported_channels:" +
                                    std::to_string(frame_mat.channels()));
                    wait_retry(std::chrono::milliseconds(100));
                    continue;
                }

                auto img = std::make_shared<Image>();
                img->width = rgb.cols;
                img->height = rgb.rows;
                img->channels = 3;
                img->pixels.assign(rgb.data, rgb.data + rgb.total() * rgb.elemSize());

                const auto prepare_done = timing ? now_ns() : 0;
                emit_status(camera_id, last_status, "camera_ready:" + dev_path);
                Frame frame;
                frame.camera_id = camera_id;
                frame.sequence = ++camera_sequence;
                frame.release_ns = ts_ns;
                frame.scenario_ns = ts_ns;
                frame.source_sequence = camera_sequence;
                frame.source_timestamp_ns = ts_ns;
                frame.source_session = "camera-hw-v1";
                frame.source_clock_domain = "host_delivery_monotonic";
                frame.image = img;
                if (timing)
                {
                    auto record = std::make_shared<SourceWorkRecord>();
                    record->session = timing;
                    record->timing = {camera_id,
                                      "camera_read",
                                      timing->epoch,
                                      camera_sequence,
                                      read_start,
                                      prepare_done,
                                      double(read_done - read_start) / 1e6,
                                      std::nullopt,
                                      double(prepare_done - prepare_start) / 1e6,
                                      false};
                    frame.source_work = std::move(record);
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (stopped_)
                    {
                        break;
                    }
                    if (paused_.load())
                    {
                        dropped_count_++;
                        continue;
                    }
                    SourceEvent ev;
                    ev.kind = SourceEvent::Kind::Frames;
                    ev.batch_id = ++batch_counter_;
                    ev.paused = false;
                    ev.frames[camera_id] = frame;
                    if (pending_events_.size() >= queue_capacity)
                    {
                        const auto oldest_frame =
                            std::find_if(pending_events_.begin(), pending_events_.end(),
                                         [](const SourceEvent &item)
                                         { return item.kind == SourceEvent::Kind::Frames; });
                        if (oldest_frame == pending_events_.end())
                        {
                            dropped_count_++;
                            continue;
                        }
                        pending_events_.erase(oldest_frame);
                        dropped_count_++;
                    }
                    pending_events_.push_back(std::move(ev));
                    decoded_count_++;
                }
            }
            catch (const cv::Exception &)
            {
                update_camera(camera_id,
                              [phase](auto &state)
                              {
                                  ++state.opencv_exceptions;
                                  if (std::string_view(phase) == "read")
                                  {
                                      ++state.read_failures;
                                  }
                              });
                // Do not publish partially prepared frames or leave a failed driver open.
                emit_status(camera_id, last_status, std::string("camera_exception:") + phase);
                if (!close_capture(cap, camera_id, last_status))
                {
                    return; // The driver state after a failed release is unknown.
                }
                wait_retry(std::chrono::milliseconds(250));
            }
        }

        close_capture(cap, camera_id, last_status);
        update_camera(camera_id, [](auto &state) { state.worker_stopped = true; });
    }

    bool stopping() const
    {
        return stopped_.load(std::memory_order_acquire);
    }

    template <typename Update> void update_camera(int camera_id, Update update)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        update(diagnostics_[camera_id]);
    }

    void wait_retry(std::chrono::milliseconds delay)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, delay, [this] { return stopping(); });
    }

    bool close_capture(CameraCapture &capture, int camera_id, std::string &last_status)
    {
        update_camera(camera_id, [](auto &state) { state.negotiated_mode.reset(); });
        try
        {
            capture.close();
            return true;
        }
        catch (const cv::Exception &)
        {
            update_camera(camera_id,
                          [](auto &state)
                          {
                              ++state.opencv_exceptions;
                              ++state.release_failures;
                              state.worker_stopped = true;
                          });
            emit_status(camera_id, last_status, "camera_exception:close_worker_stopped");
            return false;
        }
    }

    void emit_status(int camera_id, std::string &last_status, const std::string &status)
    {
        if (last_status == status)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        SourceEvent event;
        event.kind = SourceEvent::Kind::Status;
        event.camera_id = camera_id;
        event.reason = status;
        if (pending_events_.size() >= queue_capacity)
        {
            const auto oldest_frame = std::find_if(
                pending_events_.begin(), pending_events_.end(),
                [](const SourceEvent &item) { return item.kind == SourceEvent::Kind::Frames; });
            if (oldest_frame != pending_events_.end())
            {
                pending_events_.erase(oldest_frame);
                dropped_count_++;
            }
            else
            {
                return;
            }
        }
        pending_events_.push_back(std::move(event));
        last_status = status;
    }

    static constexpr size_t queue_capacity = 64;
    Config config_;
    SourceTimingGate timing_;
    CameraDiagnosticsSet diagnostics_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::mutex shutdown_mutex_;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> paused_{false};
    uint64_t batch_counter_ = 0;

    std::atomic<uint64_t> decoded_count_{0};
    std::atomic<uint64_t> received_count_{0};
    std::atomic<uint64_t> dropped_count_{0};

    std::array<std::unique_ptr<CameraCapture>, 4> captures_;
    std::vector<std::thread> workers_;
    std::deque<SourceEvent> pending_events_;
};
} // namespace

std::unique_ptr<FrameSource> make_camera_source(const Config &config,
                                                std::shared_ptr<const SourceTimingSession> timing)
{
    return make_camera_source(config, std::move(timing), {});
}

std::unique_ptr<FrameSource> make_camera_source(const Config &config,
                                                std::shared_ptr<const SourceTimingSession> timing,
                                                CameraCaptureFactory factory)
{
    return std::make_unique<CameraFrameSource>(config, std::move(timing), std::move(factory));
}
} // namespace sv
