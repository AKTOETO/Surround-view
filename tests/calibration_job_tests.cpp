#include "sv/calibration_job.hpp"
#include "sv/config_store.hpp"
#include "sv/vision.hpp"
#include <cassert>
#include <iostream>
#include <thread>

int main()
{
    sv::Camera initial;
    initial.id = 0;
    initial.fx = 300;
    initial.fy = 300;
    initial.cx = 640;
    initial.cy = 360;
    initial.width = 1280;
    initial.height = 720;
    initial.theta_max = 1.6;
    initial.z_epsilon = 0.01;
    initial.T = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1};

    std::vector<sv::Vec3> points = {
        {-1.0, 3.0, 0.0}, {1.0, 3.0, 0.0}, {-1.0, 5.0, 0.0}, {1.0, 5.0, 0.0},
        {-0.5, 4.0, 0.5}, {0.5, 4.0, 0.5}, {-1.2, 3.5, -0.2}, {1.2, 3.5, -0.2}
    };
    std::vector<sv::Pixel> pixels = sv::project_opencv(initial, points);

    sv::ExtrinsicOptions options;
    options.method = "iterative";

    sv::CalibrationJobManager manager;
    std::string job_id = manager.submit_job(0, initial, points, pixels, options);
    assert(!job_id.empty());

    int retry = 0;
    sv::CalibrationJobResult res;
    while (retry < 50)
    {
        auto opt = manager.get_job(job_id);
        assert(opt.has_value());
        res = *opt;
        if (res.state == sv::JobState::Completed || res.state == sv::JobState::Failed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        retry++;
    }

    assert(res.state == sv::JobState::Completed);
    assert(res.calibration.training_rmse_px < 1.0);

    sv::Config cfg;
    cfg.cameras[0] = initial;
    sv::ConfigStore store(cfg);

    std::string err;
    assert(manager.apply_job_to_config(job_id, store, err));
    assert(store.revision() == 1);

    std::cout << "All CalibrationJobManager tests passed successfully.\n";
    return 0;
}
