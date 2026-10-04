#include "sv/renderer.hpp"
#include "sv/vision.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>

namespace
{
double percentile(std::vector<double> v, double p)
{
    if (v.empty())
    {
        return 0;
    }
    std::sort(v.begin(), v.end());
    double n = p * (v.size() - 1);
    size_t lo = static_cast<size_t>(n);
    return v[lo] + (v[std::min(lo + 1, v.size() - 1)] - v[lo]) * (n - lo);
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        std::string cfg, manifest, out = "artifacts/bench";
        int iterations = 20, warmup = 5, cells = 0;
        double height = -1;
        int width = 0, height_px = 0;
        for (int a = 1; a < argc; a++)
        {
            std::string key = argv[a];
            if (a + 1 >= argc)
            {
                throw std::runtime_error("argument value required");
            }
            std::string v = argv[++a];
            if (key == "--egl-platform")
            {
                if (v != "device" && v != "surfaceless" && v != "default")
                {
                    throw std::runtime_error("unknown EGL platform");
                }
                setenv("SV_EGL_PLATFORM", v.c_str(), 1);
            }
            else if (key == "--config")
            {
                cfg = v;
            }
            else if (key == "--manifest")
            {
                manifest = v;
            }
            else if (key == "--output")
            {
                out = v;
            }
            else if (key == "--iterations")
            {
                iterations = std::stoi(v);
            }
            else if (key == "--warmup")
            {
                warmup = std::stoi(v);
            }
            else if (key == "--cells")
            {
                cells = std::stoi(v);
            }
            else if (key == "--height")
            {
                height = std::stod(v);
            }
            else if (key == "--width")
            {
                width = std::stoi(v);
            }
            else if (key == "--output-height")
            {
                height_px = std::stoi(v);
            }
            else
            {
                throw std::runtime_error("unknown argument " + key);
            }
        }
        if (cfg.empty() || manifest.empty() || iterations < 1 || iterations > 10000 || warmup < 0 ||
            warmup > 1000)
        {
            throw std::runtime_error("usage: sv-bench --config FILE --manifest FILE [--output DIR "
                                     "--iterations N --warmup N --height H --cells N]");
        }
        auto c = sv::load_config(cfg);
        auto effective = c.effective.as_object();
        if (cells)
        {
            effective.at("surface").as_object()["uniform_cells"] = boost::json::array{cells, cells};
        }
        if (height >= 0)
        {
            effective.at("surface").as_object()["corner_height_m"] = height;
        }
        if (width)
        {
            effective.at("output").as_object()["width"] = width;
        }
        if (height_px)
        {
            effective.at("output").as_object()["height"] = height_px;
        }
        c = sv::parse_config(effective);
        auto rows = sv::load_manifest(manifest, c);
        sv::FrameSet set;
        set.health = "READY";
        for (int k = 0; k < 4; k++)
        {
            auto image = std::make_shared<sv::Image>(sv::read_image(rows.front().paths[k]));
            set.frames[k] = sv::Frame{k, 0, sv::now_ns(), 0, image};
        }
        sv::Renderer renderer(c);
        std::filesystem::create_directories(out);
        sv::write_json(std::filesystem::path(out) / "effective_config.json", c.effective);
        // Independent projection values are emitted for a NumPy oracle as well as CPU/GPU checks.
        std::mt19937 rng(20261004);
        std::uniform_real_distribution<double> x(-6, 6), y(-4.5, 4.5), z(0, 1.5);
        boost::json::array projections;
        std::vector<double> errors;
        size_t mismatches = 0;
        for (const auto &cam : c.cameras)
        {
            std::vector<sv::Vec3> points;
            for (int j = 0; j < 1024; j++)
            {
                points.push_back({x(rng), y(rng), z(rng)});
            }
            auto gpu = renderer.project_points(cam, points);
            auto reference = sv::project_opencv(cam, points);
            for (size_t j = 0; j < points.size(); j++)
            {
                auto cpu = reference[j];
                if (cpu.valid != gpu[j].valid)
                {
                    mismatches++;
                }
                if (cpu.valid && gpu[j].valid)
                {
                    errors.push_back(std::hypot(cpu.u - gpu[j].u, cpu.v - gpu[j].v));
                }
                projections.push_back(boost::json::object{
                    {"camera_id", cam.id},
                    {"point", boost::json::array{points[j].x, points[j].y, points[j].z}},
                    {"cpu", boost::json::array{cpu.u, cpu.v, cpu.valid}},
                    {"gpu", boost::json::array{gpu[j].u, gpu[j].v, gpu[j].valid}}});
            }
        }
        sv::write_json(std::filesystem::path(out) / "projection.json", projections);
        std::vector<double> timings;
        sv::Image image;
        for (int n = -warmup; n < iterations; n++)
        {
            auto start = sv::now_ns();
            image = renderer.render(set, c.view);
            double ms = (sv::now_ns() - start) / 1e6;
            if (n >= 0)
            {
                timings.push_back(ms);
            }
        }
        sv::write_ppm(std::filesystem::path(out) / "preview.ppm", image);
        size_t seen = 0, total = 0;
        for (int yy = 0; yy < 100; yy++)
        {
            for (int xx = 0; xx < 100; xx++)
            {
                double wx = -4 + 8.0 * (xx + .5) / 100, wy = -3 + 6.0 * (yy + .5) / 100;
                if (std::abs(wx) <= c.vehicle_length / 2 + c.margin &&
                    std::abs(wy) <= c.vehicle_width / 2 + c.margin)
                {
                    continue;
                }
                total++;
                bool ok = false;
                for (const auto &cam : c.cameras)
                {
                    ok |= sv::project(cam, c.surface.point(wx, wy)).valid;
                }
                if (ok)
                {
                    seen++;
                }
            }
        }
        boost::json::array times;
        for (double ms : timings)
        {
            times.push_back(ms);
        }
        boost::json::object report{
            {"opencv_version", sv::opencv_version()},
            {"profile_id", c.profile_id},
            {"gl_vendor", renderer.vendor()},
            {"gl_renderer", renderer.device()},
            {"backend",
             std::getenv("SV_EGL_PLATFORM") ? std::getenv("SV_EGL_PLATFORM") : "surfaceless"},
            {"iterations", iterations},
            {"warmup", warmup},
            {"output_width", c.width},
            {"output_height", c.height},
            {"surface_height_m", c.surface.H},
            {"triangles", renderer.triangles()},
            {"upload_count", renderer.uploads()},
            {"timing_scope", "CPU wall time of render including synchronous final RGBA readback; "
                             "not display latency"},
            {"render_readback_ms", times},
            {"p50_ms", percentile(timings, .5)},
            {"p95_ms", percentile(timings, .95)},
            {"p99_ms", percentile(timings, .99)},
            {"projection_max_px",
             errors.empty() ? 0 : *std::max_element(errors.begin(), errors.end())},
            {"projection_p99_px", percentile(errors, .99)},
            {"validity_mismatches", mismatches},
            {"roi_coverage", double(seen) / total},
            {"roi_samples", total},
            {"gpu_time_ms", nullptr},
            {"end_to_end_latency_ms", nullptr},
            {"dataset", sv::read_json(manifest).as_object().if_contains("origin")
                            ? sv::read_json(manifest).as_object().at("origin")
                            : boost::json::value("unspecified_manifest")}};
        sv::write_json(std::filesystem::path(out) / "metrics.json", report);
        std::cout << boost::json::serialize(report) << '\n';
        if (mismatches || (!errors.empty() && *std::max_element(errors.begin(), errors.end()) > .1))
        {
            return 2;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
