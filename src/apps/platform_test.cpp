#include "sv/build_info.hpp"
#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <algorithm>
#include <boost/version.hpp>
#include <cmath>
#include <cstdlib>
#include <glm/detail/setup.hpp>
#include <iostream>
#include <map>
#include <set>
#ifdef SV_HAS_GPU
#include "sv/render_validation.hpp"
#include "sv/renderer.hpp"
#endif

namespace
{
using Object = boost::json::object;
using Array = boost::json::array;

void criterion(Array &checks, const char *id, const char *status, const std::string &detail)
{
    checks.push_back(Object{{"id", id}, {"status", status}, {"detail", detail}});
}

sv::FrameSet fixture(const sv::Config &config, const std::string &manifest, Object &provenance,
                     Array &checks)
{
    sv::FrameSet set;
    set.health = "READY";
    if (manifest.empty())
    {
        provenance["kind"] = "integer_rgb_gradient_v1";
        std::string bytes;
        for (int k = 0; k < 4; ++k)
        {
            const auto &c = config.cameras[k];
            auto image = std::make_shared<sv::Image>();
            image->width = c.width;
            image->height = c.height;
            image->channels = 3;
            image->pixels.resize(size_t(c.width) * c.height * 3);
            for (int y = 0; y < c.height; ++y)
            {
                for (int x = 0; x < c.width; ++x)
                {
                    const auto pos = (size_t(y) * c.width + x) * 3;
                    image->pixels[pos] = 70 + x * 150 / c.width;
                    image->pixels[pos + 1] = 60 + y * 150 / c.height;
                    image->pixels[pos + 2] = 55 + k * 35;
                }
            }
            bytes.append(reinterpret_cast<const char *>(image->pixels.data()),
                         image->pixels.size());
            set.frames[k] = sv::Frame{k, 0, 1000000000, 0, image};
        }
        provenance["sha256"] = sv::sha256(bytes);
        criterion(checks, "FIXTURE_HASHES", "pass",
                  "Deterministic integer-generated RGB8 inputs; combined content SHA-256 recorded");
    }
    else
    {
        provenance["kind"] = "manifest";
        provenance["sha256"] = sv::file_sha256(manifest);
        const auto rows = sv::load_manifest(manifest, config);
        const auto description = sv::read_json(manifest).as_object();
        const auto &hashes = description.at("sha256").as_object();
        std::set<std::filesystem::path> verified;
        for (const auto &row : rows)
        {
            for (const auto &file : row.paths)
            {
                if (file.empty() || !verified.insert(file).second)
                {
                    continue;
                }
                auto relative =
                    std::filesystem::relative(file, std::filesystem::path(manifest).parent_path())
                        .generic_string();
                if (hashes.at(relative).as_string() != sv::file_sha256(file))
                {
                    throw std::runtime_error("fixture SHA-256 mismatch: " + relative);
                }
            }
        }
        provenance["verified_files"] = verified.size();
        criterion(checks, "FIXTURE_HASHES", "pass",
                  "All manifest input files checked against declared SHA-256");
        for (int k = 0; k < 4; ++k)
        {
            if (rows.front().paths[k].empty())
            {
                throw std::runtime_error("benchmark requires a complete first frame set");
            }
            auto image = std::make_shared<sv::Image>(sv::read_image(rows.front().paths[k]));
            set.frames[k] = sv::Frame{k, 0, 1000000000, rows.front().scenario_ns, image};
        }
    }
    return set;
}

#ifdef SV_HAS_GPU
void output_orientation(sv::Config config)
{
    config.width = 640;
    config.height = 360;
    config.surface.H = 0;
    config.vehicle_length = 4.6;
    config.vehicle_width = 1.8;
    config.margin = .1;
    config.view = {};
    config.view.elevation = sv::pi / 2;
    config.view.distance = 10;
    auto &cam = config.cameras[0];
    cam.width = 320;
    cam.height = 180;
    cam.fx = cam.fy = 100;
    cam.cx = 159.5;
    cam.cy = 89.5;
    cam.theta_max = 1.45;
    cam.k = {};
    const double c = std::sqrt(3.) / 2;
    cam.T = {0, -1, 0, 0, -.5, 0, -c, 1.15 + 1.2 * c, c, 0, -.5, -2.3 * c + .6, 0, 0, 0, 1};
    auto image = std::make_shared<sv::Image>();
    image->width = 320;
    image->height = 180;
    image->channels = 3;
    image->pixels.resize(320 * 180 * 3);
    for (int y = 0; y < 180; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            auto pos = (y * 320 + x) * 3;
            image->pixels[pos] = y < 90 ? 255 : 0;
            image->pixels[pos + 2] = y >= 90 ? 255 : 0;
        }
    }
    sv::FrameSet set;
    set.frames[0] = sv::Frame{0, 0, 0, 0, image};
    sv::Renderer renderer(config);
    auto output = renderer.render(set, config.view);
    const auto matrix = config.view.mvp(640. / 360);
    for (auto x : {3.7, 5.7})
    {
        const double sx = matrix[0] * x + matrix[3], sy = matrix[4] * x + matrix[7],
                     sw = matrix[12] * x + matrix[15];
        const int px = int((sx / sw * .5 + .5) * 640), py = int((.5 - sy / sw * .5) * 360);
        const auto offset = (size_t(py) * 640 + px) * 4;
        bool blue = x < 4;
        if (output.pixels.at(offset + (blue ? 2 : 0)) < 250 ||
            output.pixels.at(offset + (blue ? 0 : 2)) > 5 || output.pixels.at(offset + 3) != 255)
        {
            throw std::runtime_error("asymmetric marker: wrong channel or vertical origin");
        }
    }
}

void qualify_gpu(const sv::Config &base, sv::FrameSet input, int iterations, int warmup,
                 int repeats, Object &report, const std::filesystem::path &output)
{
    auto &checks = report.at("criteria").as_array();
    {
        sv::Renderer renderer(base);
        report["graphics"] = renderer.capabilities();
        criterion(checks, "EGL_GLES_FBO", "pass",
                  "EGL context, GLES shaders and RGBA8/depth output FBO created");
        try
        {
            double max_error = 0;
            size_t mismatch = 0;
            std::vector<sv::Vec3> points;
            for (int j = 0; j < 1024; ++j)
            {
                points.push_back({-6 + 12. * ((j * 167) % 1024) / 1024,
                                  -4.5 + 9. * ((j * 317) % 1024) / 1024,
                                  1.5 * ((j * 73) % 1024) / 1024});
            }
            for (auto camera : base.cameras)
            {
                for (int distortion = 0; distortion < 2; ++distortion)
                {
                    camera.k = distortion ? std::array<double, 4>{.03, -.005, .001, .0001}
                                          : std::array<double, 4>{};
                    auto expected = sv::project_opencv(camera, points),
                         actual = renderer.project_points(camera, points);
                    for (size_t i = 0; i < points.size(); ++i)
                    {
                        mismatch += expected[i].valid != actual[i].valid;
                        if (expected[i].valid && actual[i].valid)
                        {
                            max_error =
                                std::max(max_error, std::hypot(expected[i].u - actual[i].u,
                                                               expected[i].v - actual[i].v));
                        }
                    }
                }
            }
            report["projection"] =
                Object{{"count", 8192},
                       {"max_error_px", max_error},
                       {"validity_mismatches", mismatch},
                       {"reference", "OpenCV fisheye; zero and nonzero distortion"}};
            criterion(checks, "GPU_OPENCV_PROJECTION",
                      max_error <= .1 && !mismatch ? "pass" : "fail",
                      "8192 points; max error <= 0.1 px and no validity mismatches");
        }
        catch (const std::exception &e)
        {
            criterion(checks, "GPU_OPENCV_PROJECTION", "skip", e.what());
        }
        auto first = renderer.render(input, base.view);
        auto uploads = renderer.uploads();
        auto view = base.view;
        view.azimuth += .2;
        renderer.render(input, view);
        criterion(checks, "CACHED_INPUT_UPLOAD",
                  uploads == 4 && uploads == renderer.uploads() ? "pass" : "fail",
                  "Orbit on retained image owners does not upload inputs again");
        criterion(checks, "OUTPUT_DIMENSIONS",
                  first.channels == 4 && first.pixels.size() == size_t(base.width) * base.height * 4
                      ? "pass"
                      : "fail",
                  "RGBA8 payload matches configured dimensions");
        sv::write_ppm(output / "preview.ppm", first);
    }
    try
    {
        output_orientation(base);
        criterion(
            checks, "GPU_RGBA_TOP_LEFT", "pass",
            "Canonical asymmetric red/blue fixture confirms channels and single vertical flip");
    }
    catch (const std::exception &e)
    {
        criterion(checks, "GPU_RGBA_TOP_LEFT", "fail", e.what());
    }

    for (const auto &validation :
         {std::make_pair("GPU_FUSION_MODES", sv::qualify_fusion_modes),
          std::make_pair("GPU_ENCLOSURE_COVERAGE", sv::qualify_enclosure_coverage)})
    {
        try
        {
            validation.second(base);
            criterion(checks, validation.first, "pass",
                      "Closed-form RGB/coverage oracles or 36 complete interior enclosure views");
        }
        catch (const std::exception &error)
        {
            criterion(checks, validation.first, "fail", error.what());
        }
    }

    struct Variant
    {
        const char *name;
        double height;
        int cells, width, height_px;
        bool fresh;
        int enclosure;
        const char *fusion;
    };

    const std::array<Variant, 10> variants{
        {{"plane", 0, 32, 640, 360, false, 0, "edge_feather"},
         {"bowl", 1.5, 32, 640, 360, false, 0, "edge_feather"},
         {"bowl_dense", 1.5, 64, 640, 360, false, 0, "edge_feather"},
         {"bowl_720p", 1.5, 32, 1280, 720, false, 0, "edge_feather"},
         {"bowl_upload", 1.5, 32, 640, 360, true, 0, "edge_feather"},
         {"dome_floor", 0, 32, 640, 360, false, 1, "edge_feather"},
         {"cylinder_floor", 0, 32, 640, 360, false, 2, "edge_feather"},
         {"cube_floor", 0, 32, 640, 360, false, 3, "edge_feather"},
         {"dome_angular", 0, 32, 640, 360, false, 1, "angular_feather"},
         {"dome_hard", 0, 32, 640, 360, false, 1, "hard_best_angle"}}};
    for (int repeat = 0; repeat < repeats; ++repeat)
    {
        for (size_t index = 0; index < variants.size(); ++index)
        {
            const auto &variant = variants[repeat % 2 ? variants.size() - 1 - index : index];
            auto value = base.effective;
            if (variant.enclosure == 1)
            {
                value.as_object()["surface"] = Object{{"type", "dome_floor_v1"},
                                                      {"dome_radius_m", 12.},
                                                      {"dome_latitude_cells", 64},
                                                      {"dome_longitude_cells", 128},
                                                      {"floor_radial_cells", 32}};
            }
            else if (variant.enclosure == 2)
            {
                value.as_object()["surface"] = Object{{"type", "cylinder_floor_v1"},
                                                      {"radius_m", 12.},
                                                      {"height_m", 12.},
                                                      {"vertical_cells", 32},
                                                      {"angular_cells", 128},
                                                      {"floor_radial_cells", 32}};
            }
            else if (variant.enclosure == 3)
            {
                value.as_object()["surface"] = Object{{"type", "cube_floor_v1"},
                                                      {"half_extent_m", 12.},
                                                      {"height_m", 12.},
                                                      {"face_cells", 32}};
            }
            else
            {
                value.as_object()["surface"] =
                    Object{{"type", "rectangular_bowl_v1"},
                           {"flat_half_length_m", 2.6},
                           {"flat_half_width_m", 1.2},
                           {"outer_half_length_m", 6.},
                           {"outer_half_width_m", 4.5},
                           {"corner_height_m", variant.height},
                           {"uniform_cells", Array{variant.cells, variant.cells}}};
            }
            value.as_object()["fusion"] = Object{{"mode", variant.fusion}, {"diagnostic", "color"}};
            value.as_object()["virtual_camera"].as_object()["distance_m"] = 8.5;
            value.as_object()["output"] =
                Object{{"width", variant.width}, {"height", variant.height_px}};
            auto config = sv::parse_config(value);
            sv::Renderer renderer(config);
            std::vector<double> total, upload, readback, gpu;
            std::map<std::string, int> gpu_status;
            for (int i = -warmup; i < iterations; ++i)
            {
                if (variant.fresh)
                {
                    for (auto &frame : input.frames)
                    {
                        frame->image = std::make_shared<sv::Image>(*frame->image);
                    }
                }
                auto start = sv::now_ns();
                renderer.render(input, config.view);
                double elapsed = (sv::now_ns() - start) / 1e6;
                auto timing = renderer.last_timing();
                if (i >= 0)
                {
                    total.push_back(elapsed);
                    upload.push_back(timing.upload_cpu_ms);
                    readback.push_back(timing.readback_copy_cpu_ms);
                    if (timing.gpu_draw_ms)
                    {
                        gpu.push_back(*timing.gpu_draw_ms);
                    }
                    gpu_status[timing.gpu_timer_status]++;
                }
            }
            Array raw;
            for (double t : total)
            {
                raw.push_back(t);
            }
            Object statuses;
            for (const auto &[name, count] : gpu_status)
            {
                statuses[name] = count;
            }
            report.at("render").as_array().push_back(
                Object{{"variant", variant.name},
                       {"repeat", repeat + 1},
                       {"triangles", renderer.triangles()},
                       {"width", variant.width},
                       {"height", variant.height_px},
                       {"input_mode", variant.fresh ? "new_image_owners" : "cached"},
                       {"effective_config_sha256", sv::sha256(boost::json::serialize(value))},
                       {"upload_count", renderer.uploads()},
                       {"render_readback_ms", sv::distribution(total)},
                       {"raw_render_readback_ms", raw},
                       {"upload_cpu_ms", sv::distribution(upload)},
                       {"readback_copy_cpu_ms", sv::distribution(readback)},
                       {"gpu_draw_ms", gpu.empty() ? boost::json::value(nullptr)
                                                   : boost::json::value(sv::distribution(gpu))},
                       {"gpu_timer_status", statuses}});
        }
    }
    criterion(checks, "RENDER_VARIANTS", "pass",
              "Plane, bowl, dense mesh, 720p, new input uploads and dome+floor; raw distributions "
              "recorded");
}
#endif
} // namespace

int main(int argc, char **argv)
{
    try
    {
        std::string config_path, manifest, label = "unnamed", egl = "default";
        std::filesystem::path output;
        int iterations = 60, warmup = 10, repeats = 3, opencv_threads = 1;
        bool cpu_only = false, require_gpu = false;
        for (int i = 1; i < argc; ++i)
        {
            std::string key = argv[i];
            if (key == "--cpu-only")
            {
                cpu_only = true;
            }
            else if (key == "--require-gpu")
            {
                require_gpu = true;
            }
            else
            {
                if (++i >= argc)
                {
                    throw std::runtime_error("missing argument value");
                }
                std::string value = argv[i];
                if (key == "--config")
                {
                    config_path = value;
                }
                else if (key == "--manifest")
                {
                    manifest = value;
                }
                else if (key == "--output")
                {
                    output = value;
                }
                else if (key == "--label")
                {
                    label = value;
                }
                else if (key == "--egl-platform")
                {
                    egl = value;
                }
                else if (key == "--iterations")
                {
                    iterations = std::stoi(value);
                }
                else if (key == "--warmup")
                {
                    warmup = std::stoi(value);
                }
                else if (key == "--repeats")
                {
                    repeats = std::stoi(value);
                }
                else if (key == "--opencv-threads")
                {
                    opencv_threads = std::stoi(value);
                }
                else
                {
                    throw std::runtime_error("unknown argument: " + key);
                }
            }
        }
        if (config_path.empty() || output.empty() || iterations < 2 || iterations > 10000 ||
            warmup < 0 || warmup > 1000 || repeats < 1 || repeats > 10 ||
            (egl != "default" && egl != "surfaceless" && egl != "device") ||
            (cpu_only && require_gpu) || opencv_threads < 1 || opencv_threads > 64)
        {
            throw std::runtime_error(
                "usage: sv-platform-test --config FILE --output NEW_DIR [--manifest FILE --label "
                "NAME --egl-platform default|surfaceless|device --cpu-only --require-gpu]");
        }
        if (std::filesystem::exists(output))
        {
            throw std::runtime_error("output already exists; choose a new report directory");
        }
        std::filesystem::create_directories(output);
        auto config = sv::load_config(config_path);
        cv::setNumThreads(opencv_threads);
        const auto start = sv::now_ns();
        Object report{
            {"schema_version", 1},
            {"suite_id", "surround-view-platform-v1"},
            {"label", label},
            {"status", "partial"},
            {"build", Object{{"version", sv::version},
                             {"revision", sv::source_revision},
                             {"source_fingerprint", sv::source_fingerprint},
                             {"compiler", sv::compiler},
                             {"type", sv::build_type},
                             {"opencv", sv::opencv_version()},
                             {"boost", BOOST_LIB_VERSION},
                             {"glm", GLM_VERSION},
                             {"processor", sv::target_processor},
                             {"opencv_build_information", cv::getBuildInformation()}}},
            {"profile_id", config.profile_id},
            {"config_sha256", sv::file_sha256(config_path)},
            {"settings",
             Object{
                 {"iterations", iterations},
                 {"warmup", warmup},
                 {"repeats", repeats},
                 {"opencv_threads", opencv_threads},
                 {"repeat_scope", "new EGL contexts within one process; alternating variant order"},
                 {"egl_platform", egl},
                 {"timing_scope",
                  "CPU wall render, readback/flip and optional timer polling; not end-to-end"}}},
            {"criteria", sv::qualify_cpu(config, output)},
            {"fixture", Object{}},
            {"render", Array{}},
            {"unmeasured", Object{{"sensor_to_display_ms", nullptr},
                                  {"ipc_ui_latency_ms", nullptr},
                                  {"dedicated_gpu_memory_mib", nullptr},
                                  {"reason", "No sensor timestamps or physical display "
                                             "observation; GPU memory not queried"}}}};
        report["cpu_qualification_ms"] = (sv::now_ns() - start) / 1e6;
        try
        {
            auto input = fixture(config, manifest, report.at("fixture").as_object(),
                                 report.at("criteria").as_array());
            if (!cpu_only)
            {
#ifdef SV_HAS_GPU
                setenv("SV_EGL_PLATFORM", egl.c_str(), 1);
                try
                {
                    qualify_gpu(config, input, iterations, warmup, repeats, report, output);
                }
                catch (const std::exception &e)
                {
                    criterion(report.at("criteria").as_array(), "GPU_BACKEND",
                              require_gpu ? "fail" : "skip", e.what());
                }
#else
                criterion(report.at("criteria").as_array(), "GPU_BACKEND",
                          require_gpu ? "fail" : "skip", "Compiled without SV_GPU");
#endif
            }
            else
            {
                criterion(report.at("criteria").as_array(), "GPU_BACKEND", "skip",
                          "CPU-only invocation");
            }
        }
        catch (const std::exception &e)
        {
            criterion(report.at("criteria").as_array(), "FIXTURE_OR_RUNTIME", "fail", e.what());
        }
        int passed = 0, failed = 0, skipped = 0;
        for (const auto &value : report.at("criteria").as_array())
        {
            auto status = value.as_object().at("status").as_string();
            if (status == "pass")
            {
                ++passed;
            }
            else if (status == "fail")
            {
                ++failed;
            }
            else
            {
                ++skipped;
            }
        }
        report["status"] = failed ? "failed" : skipped ? "partial" : "passed";
        report["counts"] = Object{{"passed", passed}, {"failed", failed}, {"skipped", skipped}};
        report["system"] = sv::system_info();
        sv::write_platform_report(output, report);
        std::cout << "report: " << (output / "REPORT.md") << "; " << passed << " pass, " << failed
                  << " fail, " << skipped << " skip\n";
        return failed ? 2 : 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
