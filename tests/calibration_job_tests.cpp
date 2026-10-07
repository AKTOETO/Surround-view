#include "sv/calibration_job.hpp"
#include "sv/config_store.hpp"
#include "sv/vision.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
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

    std::vector<sv::Vec3> points;
    for (int i = 0; i < 120; ++i)
    {
        points.push_back({(i % 10 - 4.5) * .3, (i / 10 - 5.5) * .2, 4.0 + (i % 7) * .4});
    }
    std::vector<sv::Pixel> pixels = sv::project_opencv(truth, points);

    sv::ExtrinsicOptions options;
    options.method = "iterative";

    sv::CalibrationJobManager manager;
    std::string job_id = manager.submit_job(0, initial, points, pixels, options);
    check(!job_id.empty(), "job id assigned");

    int retry = 0;
    sv::CalibrationJobResult res;
    while (retry < 50)
    {
        auto opt = manager.get_job(job_id);
        check(opt.has_value(), "submitted job retained");
        res = *opt;
        if (res.state == sv::JobState::Completed || res.state == sv::JobState::Failed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        retry++;
    }

    check(res.state == sv::JobState::Completed,
          ("job did not complete: state=" + sv::to_string(res.state) +
           " error=" + res.error_message).c_str());
    check(res.calibration.training_rmse_px < 1.0, "training reprojection error bounded");

    auto cfg = sv::load_config(SV_TEST_CONFIG_PATH);
    const auto persisted_path = std::filesystem::temp_directory_path() /
        ("sv-calibration-job-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    sv::ConfigStore store(cfg, persisted_path);

    std::string err;
    check(manager.apply_job_to_config(job_id, store, err), "completed calibration applied");
    check(store.revision() == 1, "calibration update increments config revision");

    std::ifstream persisted_input(persisted_path);
    check(persisted_input.good(), "calibration config was persisted");
    const auto persisted = sv::parse_config(boost::json::parse(persisted_input));
    for (size_t i = 0; i < 16; ++i)
    {
        check(std::abs(persisted.cameras[0].T[i] - res.calibration.camera.T[i]) < 1e-9,
              "persisted calibration extrinsics match solver result");
    }
    check(persisted.cameras[0].calibration_id == initial.calibration_id,
          "persisted calibration id matches solver result");
    std::filesystem::remove(persisted_path);

    std::cout << "All CalibrationJobManager tests passed successfully.\n";
    return 0;
}
