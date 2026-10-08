#include "sv/calibration_job.hpp"
#include "sv/config_store.hpp"
#include "sv/vision.hpp"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
} // namespace

int main()
{
    sv::Camera initial;
    initial.id = 0;
    initial.name = "front";
    initial.calibration_id = "calibration-job-test-v1";
    initial.fx = 220;
    initial.fy = 220;
    initial.cx = 319.5;
    initial.cy = 239.5;
    initial.width = 640;
    initial.height = 480;
    initial.k = {.03, -.004, 0, 0};
    initial.theta_max = 1.45;
    initial.z_epsilon = 0.01;
    auto truth = initial;
    const double c = std::cos(.05), s = std::sin(.05);
    initial.T = {c, 0, s, .1, 0, 1, 0, -.04, -s, 0, c, .07, 0, 0, 0, 1};

    std::vector<sv::Vec3> all_points;
    for (int i = 0; i < 120; ++i)
    {
        all_points.push_back({(i % 10 - 4.5) * .3, (i / 10 - 5.5) * .2, 4.0 + (i % 7) * .4});
    }
    std::vector<sv::Pixel> all_pixels = sv::project_opencv(truth, all_points);
    std::vector<sv::Vec3> points(all_points.begin(), all_points.begin() + 90);
    std::vector<sv::Pixel> pixels(all_pixels.begin(), all_pixels.begin() + 90);
    std::vector<sv::Vec3> validation_points(all_points.begin() + 90, all_points.end());
    std::vector<sv::Pixel> validation_pixels(all_pixels.begin() + 90, all_pixels.end());

    sv::ExtrinsicOptions options;
    options.method = "iterative";

    sv::CalibrationJobManager manager;
    const std::string owner = "session-owner-a";
    std::string job_id = manager.submit_job(owner, 0, 0, initial, points, pixels, validation_points,
                                            validation_pixels, options);
    check(!job_id.empty(), "job id assigned");

    int retry = 0;
    sv::CalibrationJobResult res;
    while (retry < 50)
    {
        auto opt = manager.get_job(job_id, owner);
        check(opt.has_value(), "submitted job retained");
        res = *opt;
        if (res.state == sv::JobState::Completed || res.state == sv::JobState::Failed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        retry++;
    }

    check(
        res.state == sv::JobState::Completed,
        ("job did not complete: state=" + sv::to_string(res.state) + " error=" + res.error_message)
            .c_str());
    check(res.calibration.training_rmse_px < 1.0, "training reprojection error bounded");
    check(res.quality_accepted, "independent validation quality gate accepts accurate fit");
    check(res.validation_rmse_px < 1.0, "held-out reprojection error bounded");
    check(!manager.get_job(job_id, "session-owner-b").has_value(),
          "calibration job is hidden from other sessions");
    check(!manager.cancel_job(job_id, "session-owner-b"),
          "another session cannot cancel calibration job");
    auto cfg = sv::load_config(SV_TEST_CONFIG_PATH);
    std::string err;
    const auto stale_id = manager.submit_job(owner, 0, 0, initial, points, pixels,
                                             validation_points, validation_pixels, options);
    retry = 0;
    while (retry++ < 50)
    {
        const auto current = manager.get_job(stale_id, owner);
        check(current.has_value(), "stale candidate retained");
        if (current->state == sv::JobState::Completed || current->state == sv::JobState::Failed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(!manager.config_for_job(job_id, "session-owner-b", 0, cfg, err).has_value(),
          "another session cannot apply calibration job");
    const auto persisted_path =
        std::filesystem::temp_directory_path() /
        ("sv-calibration-job-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    sv::ConfigStore store(cfg, persisted_path);

    check(manager.apply_job_to_config(job_id, owner, store, err), "completed calibration applied");
    check(store.revision() == 1, "calibration update increments config revision");
    check(!manager.config_for_job(stale_id, owner, store.revision(), *store.active(), err),
          "job based on an old config revision cannot overwrite newer calibration");
    check(err == "stale_config_revision", "stale calibration rejection is explicit");

    std::ifstream persisted_input(persisted_path);
    check(persisted_input.good(), "calibration config was persisted");
    const auto persisted = sv::parse_config(boost::json::parse(persisted_input));
    for (size_t i = 0; i < 16; ++i)
    {
        check(std::abs(persisted.cameras[0].T[i] - res.calibration.camera.T[i]) < 1e-9,
              "persisted calibration extrinsics match solver result");
    }
    check(persisted.cameras[0].calibration_id == res.calibration.camera.calibration_id,
          "persisted calibration id matches solver result");
    std::filesystem::remove(persisted_path);

    auto corrupted_validation = validation_pixels;
    for (auto &pixel : corrupted_validation)
    {
        pixel.u += 20.0;
    }
    const auto rejected_id = manager.submit_job(owner, store.revision(), 0, initial, points, pixels,
                                                validation_points, corrupted_validation, options);
    retry = 0;
    while (retry < 50)
    {
        const auto current = manager.get_job(rejected_id, owner);
        check(current.has_value(), "rejected job retained");
        res = *current;
        if (res.state == sv::JobState::Completed || res.state == sv::JobState::Failed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ++retry;
    }
    check(res.state == sv::JobState::Completed, ("poor held-out fit reports metrics: " +
                                                 sv::to_string(res.state) + " " + res.error_message)
                                                    .c_str());
    check(!res.quality_accepted, "quality gate rejects poor held-out fit");
    check(res.validation_rmse_px > 3.0, "poor validation RMSE reported");
    check(!manager.config_for_job(rejected_id, owner, store.revision(), *store.active(), err),
          "quality-rejected calibration cannot apply");
    check(err == "calibration_quality_gate_failed", "quality-gate failure is explicit");

    std::mutex calibration_mutex;
    std::condition_variable calibration_cv;
    int started_count = 0;
    int released_count = 0;
    sv::CalibrationJobManager cancellable_manager(
        [&](const sv::Camera &camera, const std::vector<sv::Vec3> &job_points,
            const std::vector<sv::Pixel> &job_pixels, const sv::ExtrinsicOptions &job_options)
        {
            std::unique_lock<std::mutex> lock(calibration_mutex);
            const int invocation = ++started_count;
            calibration_cv.notify_all();
            calibration_cv.wait(lock, [&] { return released_count >= invocation; });
            lock.unlock();
            return sv::calibrate_extrinsics(camera, job_points, job_pixels, job_options);
        });
    const auto running_id = cancellable_manager.submit_job(
        owner, 0, 0, initial, points, pixels, validation_points, validation_pixels, options);
    {
        std::unique_lock<std::mutex> lock(calibration_mutex);
        calibration_cv.wait(lock, [&] { return started_count >= 1; });
    }
    check(cancellable_manager.cancel_job(running_id, owner), "running job accepts cancellation");
    {
        std::lock_guard<std::mutex> lock(calibration_mutex);
        released_count = 1;
    }
    calibration_cv.notify_all();
    retry = 0;
    while (retry++ < 50)
    {
        const auto current = cancellable_manager.get_job(running_id, owner);
        if (current && current->state == sv::JobState::Cancelled)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(cancellable_manager.get_job(running_id, owner)->state == sv::JobState::Cancelled,
          "cancelled running job cannot complete");

    const auto disconnected_id = cancellable_manager.submit_job(
        owner, 0, 0, initial, points, pixels, validation_points, validation_pixels, options);
    {
        std::unique_lock<std::mutex> lock(calibration_mutex);
        calibration_cv.wait(lock, [&] { return started_count >= 2; });
    }
    cancellable_manager.cancel_session(owner);
    {
        std::lock_guard<std::mutex> lock(calibration_mutex);
        released_count = 2;
    }
    calibration_cv.notify_all();
    retry = 0;
    while (retry++ < 50)
    {
        const auto current = cancellable_manager.get_job(disconnected_id, owner);
        if (current && current->state == sv::JobState::Cancelled)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(cancellable_manager.get_job(disconnected_id, owner)->state == sv::JobState::Cancelled,
          "session disconnect cancels its running jobs");

    std::cout << "All CalibrationJobManager tests passed successfully.\n";
    return 0;
}
