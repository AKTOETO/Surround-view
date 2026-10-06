#include "board_observations.hpp"
#include "sv/config.hpp"
#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <cmath>
#include <limits>
#include <map>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

namespace
{
double number(const boost::json::value &value)
{
    const double result = value.is_number() ? boost::json::value_to<double>(value)
                                            : std::numeric_limits<double>::quiet_NaN();
    if (!std::isfinite(result))
    {
        throw std::invalid_argument("board transform contains a nonfinite number");
    }
    return result;
}

cv::Matx44d transform(const boost::json::value &value)
{
    const auto &rows = value.as_array();
    if (rows.size() != 4)
    {
        throw std::invalid_argument("T_vehicle_from_board must be 4x4");
    }
    cv::Matx44d result;
    for (int r = 0; r < 4; ++r)
    {
        const auto &columns = rows[r].as_array();
        if (columns.size() != 4)
        {
            throw std::invalid_argument("T_vehicle_from_board must be 4x4");
        }
        for (int c = 0; c < 4; ++c)
        {
            result(r, c) = number(columns[c]);
        }
    }
    const cv::Matx33d rotation = result.get_minor<3, 3>(0, 0);
    if (std::abs(result(3, 0)) > 1e-9 || std::abs(result(3, 1)) > 1e-9 ||
        std::abs(result(3, 2)) > 1e-9 || std::abs(result(3, 3) - 1) > 1e-9 ||
        cv::norm(cv::Mat(rotation.t() * rotation - cv::Matx33d::eye())) > 1e-5 ||
        std::abs(cv::determinant(cv::Mat(rotation)) - 1.0) > 1e-5)
    {
        throw std::invalid_argument("T_vehicle_from_board rotation must be rigid and right-handed");
    }
    return result;
}

struct View
{
    cv::Mat image;
    std::vector<cv::Point2f> corners;
    std::string corner_order;
};
} // namespace

int board_observations_command(int argc, char **argv)
{
    std::map<std::string, std::string> args;
    for (int i = 2; i < argc; i += 2)
    {
        const std::string key = argv[i];
        if ((key != "--config" && key != "--input" && key != "--output") || i + 1 == argc ||
            !args.emplace(key, argv[i + 1]).second)
        {
            throw std::invalid_argument("invalid/repeated board-observations option");
        }
    }
    for (const auto *key : {"--config", "--input", "--output"})
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
    const auto config = sv::load_config(args.at("--config"));
    const auto input_path = std::filesystem::absolute(args.at("--input"));
    const auto input = sv::read_json(input_path).as_object();
    if (input.at("schema_version").as_int64() != 1 || input.at("cameras").as_array().size() != 4)
    {
        throw std::invalid_argument("four-camera board input schema 1 required");
    }
    const auto &board = input.at("board").as_object();
    const auto &shape = board.at("inner_corners").as_array();
    if (shape.size() != 2)
    {
        throw std::invalid_argument("board.inner_corners must be [columns, rows]");
    }
    const cv::Size board_size(static_cast<int>(shape[0].as_int64()),
                              static_cast<int>(shape[1].as_int64()));
    const double square = number(board.at("square_size_m"));
    if (board_size.width < 3 || board_size.height < 3 || board_size.area() > 1000 || square <= 0 ||
        square > 1)
    {
        throw std::invalid_argument("invalid metric chessboard dimensions");
    }
    std::vector<cv::Point3d> local_points;
    for (int y = 0; y < board_size.height; ++y)
    {
        for (int x = 0; x < board_size.width; ++x)
        {
            local_points.emplace_back(x * square, y * square, 0);
        }
    }

    boost::json::array observations, captures;
    std::vector<std::vector<View>> detected(4);
    for (int id = 0; id < 4; ++id)
    {
        const auto &camera_record = input.at("cameras").as_array()[id].as_object();
        if (camera_record.at("id").as_int64() != id ||
            camera_record.at("views").as_array().empty() ||
            camera_record.at("views").as_array().size() > 100 ||
            camera_record.at("views").as_array().size() * local_points.size() > 100000)
        {
            throw std::invalid_argument(
                "camera IDs must be 0..3; provide 1..100 views and at most 100000 points");
        }
        boost::json::array xyz, uv, view_report;
        for (size_t view_id = 0; view_id < camera_record.at("views").as_array().size(); ++view_id)
        {
            const auto &record = camera_record.at("views").as_array()[view_id].as_object();
            auto image_path = std::filesystem::path(std::string(record.at("image").as_string()));
            if (image_path.is_relative())
            {
                image_path = input_path.parent_path() / image_path;
            }
            View view;
            view.image = cv::imread(image_path.string(), cv::IMREAD_COLOR);
            if (view.image.empty() || view.image.cols != config.cameras[id].width ||
                view.image.rows != config.cameras[id].height)
            {
                throw std::runtime_error(
                    "image missing or resolution differs from camera config: " +
                    image_path.string());
            }
            view.corners = sv::detect_board(view.image, board_size);
            view.corner_order = std::string(record.at("corner_order").as_string());
            if (view.corner_order != "normal" && view.corner_order != "reverse_x" &&
                view.corner_order != "reverse_y" && view.corner_order != "reverse_xy")
            {
                throw std::invalid_argument(
                    "corner_order must be normal/reverse_x/reverse_y/reverse_xy");
            }
            const auto board_to_vehicle = transform(record.at("T_vehicle_from_board"));
            for (size_t i = 0; i < local_points.size(); ++i)
            {
                int x = static_cast<int>(i % board_size.width);
                int y = static_cast<int>(i / board_size.width);
                if (view.corner_order == "reverse_x" || view.corner_order == "reverse_xy")
                {
                    x = board_size.width - 1 - x;
                }
                if (view.corner_order == "reverse_y" || view.corner_order == "reverse_xy")
                {
                    y = board_size.height - 1 - y;
                }
                const cv::Point3d p(x * square, y * square, 0);
                const auto v = board_to_vehicle * cv::Vec4d(p.x, p.y, p.z, 1);
                xyz.push_back(boost::json::array{v[0], v[1], v[2]});
                uv.push_back(boost::json::array{view.corners[i].x, view.corners[i].y});
            }
            view_report.push_back(boost::json::object{
                {"image", image_path.string()},
                {"corner_order", view.corner_order},
                {"detected_corners", static_cast<uint64_t>(view.corners.size())}});
            detected[id].push_back(std::move(view));
        }
        observations.push_back(boost::json::object{{"id", id}, {"points", xyz}, {"pixels", uv}});
        captures.push_back(boost::json::object{{"camera_id", id}, {"views", view_report}});
    }
    std::filesystem::create_directories(output / "annotated");
    for (int id = 0; id < 4; ++id)
    {
        for (size_t view_id = 0; view_id < detected[id].size(); ++view_id)
        {
            auto &view = detected[id][view_id];
            cv::drawChessboardCorners(view.image, board_size, view.corners, true);
            for (size_t i = 0; i < view.corners.size(); ++i)
            {
                cv::putText(view.image, std::to_string(i), view.corners[i],
                            cv::FONT_HERSHEY_SIMPLEX, .35, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
            }
            const auto path =
                output / "annotated" /
                ("camera" + std::to_string(id) + "_view" + std::to_string(view_id) + ".png");
            if (!cv::imwrite(path.string(), view.image))
            {
                throw std::runtime_error("could not write annotated detection: " + path.string());
            }
        }
    }
    sv::write_json(output / "observations.json",
                   boost::json::object{{"schema_version", 1}, {"cameras", observations}});
    sv::write_json(
        output / "report.json",
        boost::json::object{
            {"schema_version", 1},
            {"opencv_version", sv::opencv_version()},
            {"input_sha256", sv::file_sha256(input_path)},
            {"captures", captures},
            {"observation_sha256", sv::file_sha256(output / "observations.json")},
            {"limitations",
             boost::json::array{
                 "board-to-vehicle transforms must be measured",
                 "checkerboard corner orientation must be consistent",
                 "multiple noncoplanar board poses are needed for this solver profile"}}});
    return 0;
}
