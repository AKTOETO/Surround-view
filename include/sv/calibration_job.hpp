#pragma once

#include "sv/config_store.hpp"
#include "sv/extrinsics.hpp"
#include "sv/vision.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sv
{

enum class JobState
{
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled
};

inline std::string to_string(JobState state)
{
    switch (state)
    {
    case JobState::Pending:
        return "pending";
    case JobState::Running:
        return "running";
    case JobState::Completed:
        return "completed";
    case JobState::Failed:
        return "failed";
    case JobState::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

struct CalibrationJobResult
{
    std::string job_id;
    uint64_t base_config_revision = 0;
    int camera_id = 0;
    JobState state = JobState::Pending;
    ExtrinsicCalibration calibration;
    double validation_rmse_px = 0.0;
    double validation_max_error_px = 0.0;
    bool quality_accepted = false;
    std::string error_message;
    double duration_ms = 0.0;
};

class CalibrationJobManager
{
  public:

    using Calibrator =
        std::function<ExtrinsicCalibration(const Camera &, const std::vector<Vec3> &,
                                           const std::vector<Pixel> &, const ExtrinsicOptions &)>;

    explicit CalibrationJobManager(Calibrator calibrator = calibrate_extrinsics)
        : calibrator_(std::move(calibrator)), worker_(&CalibrationJobManager::worker_loop, this)
    {
    }

    ~CalibrationJobManager()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    std::string submit_job(const std::string &owner_session_id, uint64_t base_config_revision,
                           int camera_id, const Camera &initial_camera,
                           const std::vector<Vec3> &points, const std::vector<Pixel> &pixels,
                           const std::vector<Vec3> &validation_points,
                           const std::vector<Pixel> &validation_pixels,
                           const ExtrinsicOptions &options)
    {
        if (owner_session_id.empty() || camera_id < 0 || camera_id >= 4 ||
            points.size() != pixels.size() || points.size() < 6 ||
            points.size() > max_points_per_job ||
            validation_points.size() != validation_pixels.size() || validation_points.size() < 6 ||
            validation_points.size() > max_points_per_job)
        {
            throw std::invalid_argument("calibration job input size/camera ID invalid");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= max_queued_jobs)
        {
            throw std::runtime_error("calibration_job_queue_full");
        }
        if (jobs_.size() >= max_retained_jobs)
        {
            auto old = std::find_if(jobs_.begin(), jobs_.end(),
                                    [](const auto &entry)
                                    {
                                        const auto state = entry.second.result.state;
                                        return state == JobState::Completed ||
                                               state == JobState::Failed ||
                                               state == JobState::Cancelled;
                                    });
            if (old != jobs_.end())
            {
                jobs_.erase(old);
            }
            if (jobs_.size() >= max_retained_jobs)
            {
                throw std::runtime_error("calibration_job_capacity_reached");
            }
        }
        uint64_t id = ++job_counter_;
        std::string job_id = "calib-job-" + std::to_string(id);

        JobTask task;
        task.owner_session_id = owner_session_id;
        task.result.base_config_revision = base_config_revision;
        task.camera_id = camera_id;
        task.initial_camera = initial_camera;
        task.points = points;
        task.pixels = pixels;
        task.validation_points = validation_points;
        task.validation_pixels = validation_pixels;
        task.options = options;

        task.result.job_id = job_id;
        task.result.camera_id = camera_id;
        task.result.state = JobState::Pending;

        jobs_[job_id] = task;
        queue_.push_back(job_id);
        cv_.notify_one();
        return job_id;
    }

    std::optional<CalibrationJobResult> get_job(const std::string &job_id,
                                                const std::string &owner_session_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(job_id);
        if (it != jobs_.end() && it->second.owner_session_id == owner_session_id)
        {
            return it->second.result;
        }
        return std::nullopt;
    }

    bool cancel_job(const std::string &job_id, const std::string &owner_session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(job_id);
        if (it != jobs_.end() && it->second.owner_session_id == owner_session_id)
        {
            if (it->second.result.state == JobState::Pending)
            {
                it->second.result.state = JobState::Cancelled;
                clear_observations(it->second);
                return true;
            }
            if (it->second.result.state == JobState::Running)
            {
                it->second.cancel_requested = true;
                clear_observations(it->second);
                return true;
            }
        }
        return false;
    }

    void cancel_session(const std::string &owner_session_id)
    {
        if (owner_session_id.empty())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &[job_id, task] : jobs_)
        {
            (void)job_id;
            if (task.owner_session_id != owner_session_id)
            {
                continue;
            }
            if (task.result.state == JobState::Pending)
            {
                task.result.state = JobState::Cancelled;
                clear_observations(task);
            }
            else if (task.result.state == JobState::Running)
            {
                task.cancel_requested = true;
                clear_observations(task);
            }
        }
    }

    std::optional<Config> config_for_job(const std::string &job_id,
                                         const std::string &owner_session_id,
                                         uint64_t active_config_revision, const Config &active,
                                         std::string &error) const
    {
        auto job_opt = get_job(job_id, owner_session_id);
        if (!job_opt)
        {
            error = "job_not_found";
            return std::nullopt;
        }
        if (job_opt->state != JobState::Completed)
        {
            error = "job_not_completed";
            return std::nullopt;
        }
        if (job_opt->base_config_revision != active_config_revision)
        {
            error = "stale_config_revision";
            return std::nullopt;
        }
        if (!job_opt->quality_accepted)
        {
            error = "calibration_quality_gate_failed";
            return std::nullopt;
        }
        Config new_cfg = active;
        int cam_id = job_opt->camera_id;
        if (cam_id < 0 || cam_id >= 4)
        {
            error = "invalid_camera_id";
            return std::nullopt;
        }
        new_cfg.cameras[cam_id] = job_opt->calibration.camera;
        if (new_cfg.effective.is_object())
        {
            auto &camera =
                new_cfg.effective.as_object().at("cameras").as_array().at(cam_id).as_object();
            if (camera.at("id").as_int64() != cam_id)
            {
                error = "config_camera_order_mismatch";
                return std::nullopt;
            }
            boost::json::array matrix;
            for (int row = 0; row < 4; ++row)
            {
                matrix.push_back(boost::json::array{new_cfg.cameras[cam_id].T[row * 4],
                                                    new_cfg.cameras[cam_id].T[row * 4 + 1],
                                                    new_cfg.cameras[cam_id].T[row * 4 + 2],
                                                    new_cfg.cameras[cam_id].T[row * 4 + 3]});
            }
            camera["T_camera_from_vehicle"] = std::move(matrix);
            camera["calibration_id"] = new_cfg.cameras[cam_id].calibration_id;
            try
            {
                new_cfg = parse_config(new_cfg.effective);
            }
            catch (const std::exception &exception)
            {
                error = exception.what();
                return std::nullopt;
            }
        }
        error.clear();
        return new_cfg;
    }

    bool apply_job_to_config(const std::string &job_id, const std::string &owner_session_id,
                             ConfigStore &config_store, std::string &error)
    {
        const auto [active, revision] = config_store.snapshot();
        auto candidate = config_for_job(job_id, owner_session_id, revision, *active, error);
        return candidate && config_store.update_if_revision(*candidate, revision, error);
    }

  private:

    struct JobTask
    {
        std::string owner_session_id;
        bool cancel_requested = false;
        int camera_id = 0;
        Camera initial_camera;
        std::vector<Vec3> points;
        std::vector<Pixel> pixels;
        std::vector<Vec3> validation_points;
        std::vector<Pixel> validation_pixels;
        ExtrinsicOptions options;
        CalibrationJobResult result;
    };

    static void clear_observations(JobTask &task)
    {
        task.points.clear();
        task.pixels.clear();
        task.validation_points.clear();
        task.validation_pixels.clear();
    }

    void worker_loop()
    {
        while (true)
        {
            std::string job_id;
            JobTask task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty())
                {
                    break;
                }
                job_id = queue_.front();
                queue_.pop_front();
                auto it = jobs_.find(job_id);
                if (it == jobs_.end() || it->second.result.state == JobState::Cancelled)
                {
                    continue;
                }
                it->second.result.state = JobState::Running;
                task = it->second;
            }

            auto start_time = std::chrono::steady_clock::now();
            CalibrationJobResult res;
            res.job_id = job_id;
            res.base_config_revision = task.result.base_config_revision;
            res.camera_id = task.camera_id;

            try
            {
                res.calibration =
                    calibrator_(task.initial_camera, task.points, task.pixels, task.options);
                res.calibration.camera.calibration_id =
                    task.initial_camera.calibration_id + "-server-" + job_id;
                if (cancellation_requested(job_id))
                {
                    res.state = JobState::Cancelled;
                }
                else
                {
                    const auto projected =
                        project_opencv(res.calibration.camera, task.validation_points);
                    double squared_error = 0.0;
                    double max_error = 0.0;
                    for (size_t i = 0; i < projected.size(); ++i)
                    {
                        if (!projected[i].valid)
                        {
                            throw std::runtime_error("validation_point_outside_camera_model");
                        }
                        const double error =
                            std::hypot(projected[i].u - task.validation_pixels[i].u,
                                       projected[i].v - task.validation_pixels[i].v);
                        if (!std::isfinite(error))
                        {
                            throw std::runtime_error("nonfinite_validation_error");
                        }
                        squared_error += error * error;
                        max_error = std::max(max_error, error);
                    }
                    res.validation_rmse_px = std::sqrt(squared_error / projected.size());
                    res.validation_max_error_px = max_error;
                    res.quality_accepted = res.validation_rmse_px <= max_validation_rmse_px &&
                                           res.validation_max_error_px <= max_validation_error_px;
                    res.state = JobState::Completed;
                }
            }
            catch (const std::exception &e)
            {
                res.state = JobState::Failed;
                res.error_message = e.what();
            }

            auto end_time = std::chrono::steady_clock::now();
            res.duration_ms =
                std::chrono::duration<double, std::milli>(end_time - start_time).count();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = jobs_.find(job_id);
                if (it != jobs_.end() && it->second.result.state != JobState::Cancelled)
                {
                    if (it->second.cancel_requested)
                    {
                        res = {};
                        res.job_id = job_id;
                        res.base_config_revision = task.result.base_config_revision;
                        res.camera_id = task.camera_id;
                        res.state = JobState::Cancelled;
                        res.duration_ms =
                            std::chrono::duration<double, std::milli>(end_time - start_time)
                                .count();
                    }
                    it->second.result = res;
                    it->second.points.clear();
                    it->second.pixels.clear();
                    it->second.validation_points.clear();
                    it->second.validation_pixels.clear();
                }
            }
        }
    }

    bool cancellation_requested(const std::string &job_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(job_id);
        return it == jobs_.end() || it->second.cancel_requested;
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    uint64_t job_counter_ = 0;
    std::deque<std::string> queue_;
    std::unordered_map<std::string, JobTask> jobs_;
    Calibrator calibrator_;
    std::thread worker_;
    static constexpr size_t max_points_per_job = 10000;
    // Provisional software-profile limits; validate and tune with physical camera data.
    static constexpr double max_validation_rmse_px = 3.0;
    static constexpr double max_validation_error_px = 8.0;
    static constexpr size_t max_queued_jobs = 32;
    static constexpr size_t max_retained_jobs = 128;
};

} // namespace sv
