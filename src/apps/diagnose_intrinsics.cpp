#include "diagnose_intrinsics.hpp"
#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <cmath>
#include <iostream>
#include <map>
#include <set>

namespace
{
int bounded_integer(const boost::json::object &value, const char *key, int minimum, int maximum)
{
    const auto number = value.at(key).as_int64();
    if (number < minimum || number > maximum)
    {
        throw std::invalid_argument(std::string("invalid ") + key);
    }
    return static_cast<int>(number);
}

std::vector<std::vector<cv::Point2d>> read_views(const boost::json::array &views, size_t minimum,
                                                 size_t corners, cv::Size size,
                                                 std::set<std::string> &ids)
{
    if (views.size() < minimum || views.size() > 200)
    {
        throw std::invalid_argument("require 6..200 train and 3..200 validation views");
    }
    std::vector<std::vector<cv::Point2d>> result;
    for (const auto &value : views)
    {
        const auto &view = value.as_object();
        const std::string id(view.at("id").as_string());
        if (id.empty() || id.size() > 128 || !ids.insert(id).second)
        {
            throw std::invalid_argument("empty or repeated view ID across dataset");
        }
        const auto &uv = view.at("uv_px").as_array();
        if (uv.size() != corners)
        {
            throw std::invalid_argument("board corner count mismatch");
        }
        std::vector<cv::Point2d> pixels;
        for (const auto &coordinate : uv)
        {
            const auto &pair = coordinate.as_array();
            if (pair.size() != 2)
            {
                throw std::invalid_argument("two pixel coordinates required");
            }
            const double x = boost::json::value_to<double>(pair[0]);
            const double y = boost::json::value_to<double>(pair[1]);
            if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= size.width ||
                y >= size.height)
            {
                throw std::invalid_argument("nonfinite or out-of-image coordinate");
            }
            pixels.emplace_back(x, y);
        }
        result.push_back(std::move(pixels));
    }
    return result;
}
} // namespace

int diagnose_intrinsics_command(int argc, char **argv)
{
    std::map<std::string, std::string> options;
    const std::set<std::string> allowed{"--dataset", "--output", "--distortion-order"};
    for (int i = 2; i < argc; i += 2)
    {
        if (!allowed.count(argv[i]) || i + 1 == argc ||
            !options.emplace(argv[i], argv[i + 1]).second)
        {
            throw std::invalid_argument("invalid or repeated diagnostic argument");
        }
    }
    if (!options.count("--dataset") || !options.count("--output"))
    {
        throw std::invalid_argument("--dataset and --output required");
    }
    const std::filesystem::path input = options.at("--dataset"), output = options.at("--output");
    if (std::filesystem::exists(output))
    {
        throw std::invalid_argument("output directory already exists");
    }
    if (std::filesystem::file_size(input) > 8 * 1024 * 1024)
    {
        throw std::invalid_argument("diagnostic dataset exceeds 8 MiB");
    }
    const auto input_hash = sv::file_sha256(input);
    const auto data = sv::read_json(input).as_object();
    if (sv::file_sha256(input) != input_hash)
    {
        throw std::runtime_error("diagnostic dataset changed during read");
    }
    if (data.at("schema_version").as_int64() != 1 ||
        data.at("purpose").as_string() != "intrinsic_solver_diagnostic")
    {
        throw std::invalid_argument("diagnostic schema/purpose required");
    }
    const std::string origin(data.at("observation_origin").as_string());
    if (origin != "analytic_truth" && origin != "analytic_truth_float32" &&
        origin != "detected_raster" && origin != "controlled_perturbation")
    {
        throw std::invalid_argument("unknown diagnostic observation origin");
    }
    const auto &resolution = data.at("resolution").as_object();
    const cv::Size size{bounded_integer(resolution, "width", 1, 16384),
                        bounded_integer(resolution, "height", 1, 16384)};
    const auto &board = data.at("board").as_object();
    const int columns = bounded_integer(board, "columns", 3, 31);
    const int rows = bounded_integer(board, "rows", 3, 31);
    const double square = boost::json::value_to<double>(board.at("square_size_m"));
    const double theta = boost::json::value_to<double>(data.at("theta_max_rad"));
    if (!std::isfinite(square) || square <= 0 || !std::isfinite(theta) || theta <= 0 ||
        theta >= sv::pi / 2)
    {
        throw std::invalid_argument("invalid board scale or angular domain");
    }
    const auto order = options.count("--distortion-order") ? options.at("--distortion-order") : "4";
    if (order != "2" && order != "4")
    {
        throw std::invalid_argument("distortion order must be 2 or 4");
    }
    std::set<std::string> ids;
    const auto train = read_views(data.at("train").as_array(), 6, columns * rows, size, ids);
    const auto validation =
        read_views(data.at("validation").as_array(), 3, columns * rows, size, ids);
    std::vector<cv::Point3d> objects;
    for (int y = 0; y < rows; ++y)
    {
        for (int x = 0; x < columns; ++x)
        {
            objects.emplace_back(x * square, y * square, 0);
        }
    }
    const auto calibration = sv::calibrate_intrinsics(
        std::vector<std::vector<cv::Point3d>>(train.size(), objects), train, size, theta,
        order == "2" ? sv::FisheyeDistortionOrder::Two : sv::FisheyeDistortionOrder::Four);
    std::vector<double> errors;
    for (const auto &pixels : validation)
    {
        const auto values = sv::intrinsic_validation_errors(calibration, objects, pixels);
        errors.insert(errors.end(), values.begin(), values.end());
    }
    boost::json::object estimate{
        {"model", "opencv_fisheye"},
        {"fx", calibration.K(0, 0)},
        {"fy", calibration.K(1, 1)},
        {"cx", calibration.K(0, 2)},
        {"cy", calibration.K(1, 2)},
        {"k", boost::json::array{calibration.distortion[0], calibration.distortion[1],
                                 calibration.distortion[2], calibration.distortion[3]}},
        {"theta_max_rad", theta}};
    const boost::json::object report{
        {"schema_version", 1},
        {"purpose", "intrinsic_solver_diagnostic"},
        {"status", "diagnostic_only"},
        {"opencv_version", sv::opencv_version()},
        {"observation_origin", origin},
        {"distortion_order", order == "2" ? 2 : 4},
        {"dataset_sha256", input_hash},
        {"training_rmse_px", calibration.training_rmse_px},
        {"validation_error_px", sv::distribution(errors)},
        {"estimate", estimate},
        {"scope",
         "No image detector or acceptance gate; declared view IDs are not authenticated "
         "provenance; validation board poses are fitted separately; not a deployment model"}};
    if (sv::file_sha256(input) != input_hash)
    {
        throw std::runtime_error("diagnostic dataset changed during fit");
    }
    std::filesystem::create_directories(output);
    sv::write_json(output / "diagnostics.json", report);
    std::cout << "diagnostic_only; no calibration accepted or applied\n";
    return 0;
}
