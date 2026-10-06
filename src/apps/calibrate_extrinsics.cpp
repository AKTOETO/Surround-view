#include "calibrate_extrinsics.hpp"
#include "sv/config.hpp"
#include "sv/extrinsics.hpp"
#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <map>
#include <opencv2/core.hpp>

int calibrate_extrinsics_command(int argc, char **argv)
{
    std::map<std::string, std::string> args;
    for (int i = 2; i < argc; i += 2)
    {
        const std::string key = argv[i];
        if ((key != "--config" && key != "--observations" && key != "--output" &&
             key != "--method") ||
            i + 1 == argc || !args.emplace(key, argv[i + 1]).second)
        {
            throw std::invalid_argument("invalid/repeated extrinsics option");
        }
    }
    for (const auto *key : {"--config", "--observations", "--output"})
    {
        if (!args.count(key))
        {
            throw std::invalid_argument(std::string(key) + " required");
        }
    }
    const std::filesystem::path output = args.at("--output");
    if (std::filesystem::exists(output))
    {
        throw std::invalid_argument("output must be a new directory");
    }
    auto config = sv::load_config(args.at("--config"));
    const auto data = sv::read_json(args.at("--observations")).as_object();
    if (data.at("schema_version").as_int64() != 1 || data.at("cameras").as_array().size() != 4)
    {
        throw std::invalid_argument("four-camera observation schema 1 required");
    }
    sv::ExtrinsicOptions options;
    if (args.count("--method"))
    {
        options.method = args.at("--method");
    }
    const auto observation_hash = sv::file_sha256(args.at("--observations"));
    boost::json::array reports;
    // One-shot CLI owns OpenCV RNG; library routine does not change global RNG policy.
    cv::setRNGSeed(20261007);
    for (int id = 0; id < 4; ++id)
    {
        const auto &record = data.at("cameras").as_array()[id].as_object();
        if (record.at("id").as_int64() != id)
        {
            throw std::invalid_argument("camera order must be 0..3");
        }
        std::vector<sv::Vec3> points;
        std::vector<sv::Pixel> pixels;
        for (const auto &item : record.at("points").as_array())
        {
            const auto &p = item.as_array();
            if (p.size() != 3)
            {
                throw std::invalid_argument("XYZ size");
            }
            points.push_back({boost::json::value_to<double>(p[0]),
                              boost::json::value_to<double>(p[1]),
                              boost::json::value_to<double>(p[2])});
        }
        for (const auto &item : record.at("pixels").as_array())
        {
            const auto &p = item.as_array();
            if (p.size() != 2)
            {
                throw std::invalid_argument("UV size");
            }
            pixels.push_back(
                {boost::json::value_to<double>(p[0]), boost::json::value_to<double>(p[1]), true});
        }
        const auto started = sv::now_ns();
        const auto fitted = sv::calibrate_extrinsics(config.cameras[id], points, pixels, options);
        boost::json::array transform;
        for (int row = 0; row < 4; ++row)
        {
            transform.push_back(
                boost::json::array{fitted.camera.T[row * 4], fitted.camera.T[row * 4 + 1],
                                   fitted.camera.T[row * 4 + 2], fitted.camera.T[row * 4 + 3]});
        }
        // Input is canonical ID order for this explicit CLI profile.
        auto &camera = config.effective.as_object().at("cameras").as_array()[id].as_object();
        if (camera.at("id").as_int64() != id)
        {
            throw std::invalid_argument("config camera order must be 0..3 for export");
        }
        camera["T_camera_from_vehicle"] = transform;
        camera["calibration_id"] = fitted.camera.calibration_id + "-" + options.method + "-" +
                                   observation_hash.substr(0, 12);
        reports.push_back(
            boost::json::object{{"camera_id", id},
                                {"training_rmse_px", fitted.training_rmse_px},
                                {"inliers", fitted.inliers},
                                {"count", points.size()},
                                {"fit_ms", (sv::now_ns() - started) / 1e6},
                                {"ransac_normalized_threshold",
                                 options.ransac_threshold_px /
                                     std::max(config.cameras[id].fx, config.cameras[id].fy)}});
    }
    sv::parse_config(config.effective);
    std::filesystem::create_directories(output);
    sv::write_json(output / "config.json", config.effective);
    sv::write_json(
        output / "report.json",
        boost::json::object{{"schema_version", 1},
                            {"method", options.method},
                            {"opencv_version", sv::opencv_version()},
                            {"config_sha256", sv::file_sha256(args.at("--config"))},
                            {"observations_sha256", sv::file_sha256(args.at("--observations"))},
                            {"scope", "known intrinsics; provided noncoplanar XYZ/UV, no detector"},
                            {"cameras", reports}});
    return 0;
}
