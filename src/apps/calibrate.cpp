#include "board_observations.hpp"
#include "calibrate_extrinsics.hpp"
#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <cmath>
#include <iostream>
#include <map>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/objdetect.hpp>
#endif
#include <set>

namespace
{
struct Board
{
    cv::Size size;
    double square;

    std::vector<cv::Point3d> objects() const
    {
        std::vector<cv::Point3d> result;
        for (int y = 0; y < size.height; ++y)
        {
            for (int x = 0; x < size.width; ++x)
            {
                result.emplace_back(x * square, y * square, 0);
            }
        }
        return result;
    }
};

std::vector<cv::Point2d> detect(const std::filesystem::path &file, Board board,
                                const std::filesystem::path &annotated, cv::Size &size)
{
    cv::Mat image = cv::imread(file.string(), cv::IMREAD_COLOR);
    auto corners = sv::detect_board(image, board.size);
    if (size.area() && image.size() != size)
    {
        throw std::runtime_error("inconsistent image resolution");
    }
    size = image.size();
    cv::drawChessboardCorners(image, board.size, corners, true);
    if (!cv::imwrite(annotated.string(), image))
    {
        throw std::runtime_error("annotation write failed");
    }
    return {corners.begin(), corners.end()};
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc < 2)
        {
            throw std::runtime_error(
                "usage: sv-calibrate detect|intrinsics|extrinsics|board-observations [options]");
        }
        const std::string mode = argv[1];
        if (mode == "extrinsics")
        {
            return calibrate_extrinsics_command(argc, argv);
        }
        if (mode == "board-observations")
        {
            return board_observations_command(argc, argv);
        }
        std::map<std::string, std::string> options;
        const std::set<std::string> allowed{
            "--image", "--dataset",       "--output",       "--columns",
            "--rows",  "--square-size-m", "--max-error-px", "--distortion-order"};
        for (int i = 2; i < argc; i += 2)
        {
            if (!allowed.count(argv[i]) || i + 1 == argc ||
                !options.emplace(argv[i], argv[i + 1]).second)
            {
                throw std::runtime_error("invalid or repeated argument");
            }
        }
        if (!options.count("--output"))
        {
            throw std::runtime_error("--output required");
        }
        const std::filesystem::path output = options.at("--output");
        if (std::filesystem::exists(output))
        {
            throw std::runtime_error("output directory already exists");
        }
        Board board{{9, 6}, .03};
        boost::json::object dataset;
        std::filesystem::path root;
        if (mode == "intrinsics")
        {
            if (!options.count("--dataset"))
            {
                throw std::runtime_error("--dataset required");
            }
            auto file = std::filesystem::path(options.at("--dataset"));
            dataset = sv::read_json(file).as_object();
            if (dataset.at("schema_version").as_int64() != 1)
            {
                throw std::runtime_error("dataset schema must be 1");
            }
            root = std::filesystem::absolute(file).parent_path();
            auto description = dataset.at("board").as_object();
            board = {
                {int(description.at("columns").as_int64()), int(description.at("rows").as_int64())},
                boost::json::value_to<double>(description.at("square_size_m"))};
        }
        else if (mode == "detect")
        {
            if (!options.count("--image"))
            {
                throw std::runtime_error("--image required");
            }
            if (options.count("--columns"))
            {
                board.size.width = std::stoi(options.at("--columns"));
            }
            if (options.count("--rows"))
            {
                board.size.height = std::stoi(options.at("--rows"));
            }
            if (options.count("--square-size-m"))
            {
                board.square = std::stod(options.at("--square-size-m"));
            }
        }
        else
        {
            throw std::runtime_error("unknown calibration mode");
        }
        if (board.size.width < 3 || board.size.height < 3 || board.size.area() > 1000 ||
            !std::isfinite(board.square) || board.square <= 0)
        {
            throw std::runtime_error("invalid board dimensions");
        }
        std::filesystem::create_directories(output);
        cv::Size image_size;
        boost::json::object report{{"schema_version", 1},
                                   {"opencv_version", sv::opencv_version()},
                                   {"mode", mode},
                                   {"square_size_m", board.square}};
        if (mode == "detect")
        {
            auto points = detect(options.at("--image"), board, output / "detected.png", image_size);
            boost::json::array correspondences;
            auto objects = board.objects();
            for (size_t i = 0; i < points.size(); ++i)
            {
                correspondences.push_back(boost::json::object{
                    {"uv_px", boost::json::array{points[i].x, points[i].y}},
                    {"board_xyz_m", boost::json::array{objects[i].x, objects[i].y, 0.0}}});
            }
            report["points"] = correspondences;
            report["image_sha256"] = sv::file_sha256(options.at("--image"));
            sv::write_json(output / "detections.json", report);
        }
        else
        {
            std::set<std::string> seen_hashes;
            std::vector<std::vector<cv::Point3d>> training_objects;
            std::vector<std::vector<cv::Point2d>> training_pixels, validation_pixels;
            boost::json::array provenance;
            for (const auto *split : {"train", "validation"})
            {
                const auto &files = dataset.at(split).as_array();
                if (files.size() < (std::string(split) == "train" ? 6u : 3u) || files.size() > 200)
                {
                    throw std::runtime_error("require 6..200 train and 3..200 validation views");
                }
                for (size_t i = 0; i < files.size(); ++i)
                {
                    auto file = root / std::string(files[i].as_string());
                    auto hash = sv::file_sha256(file);
                    if (!seen_hashes.insert(hash).second)
                    {
                        throw std::runtime_error("duplicate image content across dataset");
                    }
                    auto pixels =
                        detect(file, board,
                               output / (std::string(split) + "-" + std::to_string(i) + ".png"),
                               image_size);
                    if (std::string(split) == "train")
                    {
                        training_objects.push_back(board.objects());
                        training_pixels.push_back(pixels);
                    }
                    else
                    {
                        validation_pixels.push_back(pixels);
                    }
                    provenance.push_back(boost::json::object{
                        {"split", split}, {"file", file.string()}, {"sha256", hash}});
                }
            }
            double theta_max = dataset.if_contains("theta_max_rad")
                                   ? boost::json::value_to<double>(dataset.at("theta_max_rad"))
                                   : 1.45;
            const auto order_text =
                options.count("--distortion-order") ? options.at("--distortion-order") : "4";
            if (order_text != "2" && order_text != "4")
            {
                throw std::runtime_error("distortion order must be 2 or 4");
            }
            const auto order = order_text == "2" ? sv::FisheyeDistortionOrder::Two
                                                 : sv::FisheyeDistortionOrder::Four;
            auto calibration = sv::calibrate_intrinsics(training_objects, training_pixels,
                                                        image_size, theta_max, order);
            report["distortion_order"] = order_text == "2" ? 2 : 4;
            std::vector<double> errors;
            for (const auto &view : validation_pixels)
            {
                auto values = sv::intrinsic_validation_errors(calibration, board.objects(), view);
                errors.insert(errors.end(), values.begin(), values.end());
            }
            auto stats = sv::distribution(errors);
            double threshold =
                options.count("--max-error-px") ? std::stod(options.at("--max-error-px")) : 1.0;
            if (!std::isfinite(threshold) || threshold <= 0)
            {
                throw std::runtime_error("invalid error threshold");
            }
            const bool accepted = boost::json::value_to<double>(stats.at("p95")) <= threshold;
            report["camera_id"] = dataset.at("camera_id");
            report["status"] = accepted ? "accepted" : "rejected";
            report["training_rmse_px"] = calibration.training_rmse_px;
            report["validation_error_px"] = stats;
            report["threshold_p95_px"] = threshold;
            report["theta_max_rad"] = theta_max;
            report["validation_scope"] =
                "Held-out images; fixed intrinsics, board pose fitted separately by solvePnP; not "
                "vehicle extrinsic calibration";
            report["images"] = provenance;
            report["dataset_sha256"] = sv::file_sha256(options.at("--dataset"));
            if (accepted)
            {
                sv::write_json(
                    output / "intrinsics.json",
                    boost::json::object{
                        {"model", "opencv_fisheye"},
                        {"fx", calibration.K(0, 0)},
                        {"fy", calibration.K(1, 1)},
                        {"cx", calibration.K(0, 2)},
                        {"cy", calibration.K(1, 2)},
                        {"alpha", 0.0},
                        {"k",
                         boost::json::array{calibration.distortion[0], calibration.distortion[1],
                                            calibration.distortion[2], calibration.distortion[3]}},
                        {"theta_max_rad", theta_max},
                        {"z_epsilon_m", 1e-6},
                        {"resolution", boost::json::object{{"width", image_size.width},
                                                           {"height", image_size.height}}}});
            }
            sv::write_json(output / "report.json", report);
            std::cout << report.at("status") << "; validation p95=" << stats.at("p95") << " px\n";
            return accepted ? 0 : 2;
        }
        std::cout << "detected board; output " << output << '\n';
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
