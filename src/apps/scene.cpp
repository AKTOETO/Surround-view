#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

int main(int argc, char **argv)
{
    try
    {
        std::map<std::string, std::string> options;
        for (int i = 1; i < argc; i += 2)
        {
            std::string key = argv[i];
            if (i + 1 == argc ||
                (key != "--config" && key != "--panorama" && key != "--output" &&
                 key != "--exposure") ||
                !options.emplace(key, argv[i + 1]).second)
            {
                throw std::runtime_error("invalid scene argument");
            }
        }
        if (!options.count("--config") || !options.count("--panorama") ||
            !options.count("--output"))
        {
            throw std::runtime_error(
                "usage: sv-scene --config FILE --panorama HDR --output NEW_DIR [--exposure 1]");
        }
        const auto config = sv::load_config(options.at("--config"));
        const std::filesystem::path output = options.at("--output");
        if (std::filesystem::exists(output))
        {
            throw std::runtime_error("output directory already exists");
        }
        double exposure = options.count("--exposure") ? std::stod(options.at("--exposure")) : 1.0;
        if (!std::isfinite(exposure) || exposure <= 0 || exposure > 100)
        {
            throw std::runtime_error("exposure must be in (0,100]");
        }
        auto hdr = cv::imread(options.at("--panorama"), cv::IMREAD_ANYDEPTH | cv::IMREAD_COLOR);
        if (hdr.empty() || hdr.type() != CV_32FC3 || hdr.cols != 2 * hdr.rows)
        {
            throw std::runtime_error("expected 2:1 floating-point HDR panorama");
        }
        std::filesystem::create_directories(output);
        boost::json::array paths, calibration_ids;
        boost::json::object hashes;
        for (const auto &camera : config.cameras)
        {
            std::vector<cv::Point2d> distorted;
            for (int y = 0; y < camera.height; ++y)
            {
                for (int x = 0; x < camera.width; ++x)
                {
                    distorted.emplace_back(x, y);
                }
            }
            std::vector<cv::Point2d> normalized;
            cv::fisheye::undistortPoints(
                distorted, normalized, sv::camera_matrix(camera),
                cv::Vec4d(camera.k[0], camera.k[1], camera.k[2], camera.k[3]));
            cv::Mat map_x(camera.height, camera.width, CV_32F), map_y = map_x.clone();
            std::vector<bool> valid(normalized.size());
            for (size_t i = 0; i < normalized.size(); ++i)
            {
                const auto ray = normalized[i];
                const double length = std::sqrt(ray.x * ray.x + ray.y * ray.y + 1);
                valid[i] = std::isfinite(length) &&
                           std::atan(std::hypot(ray.x, ray.y)) <= camera.theta_max;
                const sv::Vec3 direction{camera.T[0] * ray.x + camera.T[4] * ray.y + camera.T[8],
                                         camera.T[1] * ray.x + camera.T[5] * ray.y + camera.T[9],
                                         camera.T[2] * ray.x + camera.T[6] * ray.y + camera.T[10]};
                map_x.at<float>(i / camera.width, i % camera.width) =
                    valid[i] ? (std::atan2(direction.y, direction.x) / (2 * sv::pi) + .5) * hdr.cols
                             : 0;
                map_y.at<float>(i / camera.width, i % camera.width) =
                    valid[i] ? std::clamp(std::acos(std::clamp(direction.z / length, -1., 1.)) /
                                              sv::pi * hdr.rows,
                                          0., double(hdr.rows - 1))
                             : 0;
            }
            cv::Mat sampled;
            cv::remap(hdr, sampled, map_x, map_y, cv::INTER_LINEAR, cv::BORDER_WRAP);
            sv::Image image{camera.width, camera.height, 3,
                            std::vector<unsigned char>(normalized.size() * 3)};
            for (size_t i = 0; i < normalized.size(); ++i)
            {
                auto pixel = sampled.at<cv::Vec3f>(i / camera.width, i % camera.width);
                for (int channel = 0; channel < 3; ++channel)
                {
                    double value = std::max(0., double(pixel[2 - channel])) * exposure;
                    image.pixels[i * 3 + channel] =
                        valid[i] ? std::lround(255 * sv::encode_srgb(value / (1 + value))) : 0;
                }
            }
            auto name = "camera-" + std::to_string(camera.id) + ".ppm";
            sv::write_ppm(output / name, image);
            paths.emplace_back(name);
            hashes[name] = sv::file_sha256(output / name);
            calibration_ids.emplace_back(camera.calibration_id);
        }
        boost::json::object manifest{
            {"schema_version", 1},
            {"calibration_ids", calibration_ids},
            {"origin", "photographic_panorama_virtual_rig"},
            {"sha256", hashes},
            {"frames", boost::json::array{
                           boost::json::object{{"scenario_timestamp_ns", "0"},
                                               {"paths", paths},
                                               {"offset_ns", boost::json::array{0, 0, 0, 0}}}}}};
        sv::write_json(output / "manifest.json", manifest);
        sv::write_json(
            output / "provenance.json",
            boost::json::object{
                {"kind", "photographic_panorama_virtual_rig"},
                {"panorama", options.at("--panorama")},
                {"panorama_sha256", sv::file_sha256(options.at("--panorama"))},
                {"opencv_version", sv::opencv_version()},
                {"exposure", exposure},
                {"tone_map", "per-channel Reinhard then sRGB"},
                {"limitations", "Monoscopic panorama at infinity; translation ignored; no parallax "
                                "or measured vehicle cameras; visual demonstration only"}});
        std::cout << "created photographic demo at " << output << '\n';
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
