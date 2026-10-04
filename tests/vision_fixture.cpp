#include "sv/config.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

// An image-space test fixture, separate from the production corner detector and fitter.
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        return 1;
    }
    const std::filesystem::path output = argv[1];
    std::filesystem::create_directories(output);
    for (int k = 0; k < 4; ++k)
    {
        cv::VideoWriter video((output / ("source-" + std::to_string(k) + ".avi")).string(),
                              cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30, {320, 180});
        if (!video.isOpened())
        {
            return 2;
        }
        for (int n = 0; n < 3; ++n)
        {
            video.write(cv::Mat(180, 320, CV_8UC3, cv::Scalar(30 + k * 35, 90, 170 + n * 10)));
        }
    }
    boost::json::array train, validation;
    const cv::Matx33d K{620, 0, 639.5, 0, 610, 479.5, 0, 0, 1};
    const cv::Vec4d distortion{.03, -.005, .001, -.0001};
    for (int view = 0; view < 12; ++view)
    {
        cv::Mat image(960, 1280, CV_8UC3, cv::Scalar(235, 235, 235));
        const cv::Vec3d rotation(-.35 + view * .065, .25 - view * .04, -.15 + view * .027);
        const cv::Vec3d translation(-.18 + (view % 3) * .06, -.12 + (view % 2) * .035,
                                    .45 + (view % 4) * .06);
        for (int y = -1; y <= 5; ++y)
        {
            for (int x = -1; x <= 8; ++x)
            {
                std::vector<cv::Point3d> edge;
                // Sample curved fisheye edges, so detector precision is tested on projected images.
                for (int side = 0; side < 4; ++side)
                {
                    for (int sample = 0; sample < 12; ++sample)
                    {
                        const double t = sample / 12.;
                        edge.emplace_back((x + (side == 0   ? t
                                                : side == 1 ? 1
                                                : side == 2 ? 1 - t
                                                            : 0)) *
                                              .04,
                                          (y + (side == 0   ? 0
                                                : side == 1 ? t
                                                : side == 2 ? 1
                                                            : 1 - t)) *
                                              .04,
                                          0);
                    }
                }
                std::vector<cv::Point2d> pixels;
                cv::fisheye::projectPoints(edge, pixels, rotation, translation, K, distortion);
                std::vector<cv::Point> polygon;
                for (auto p : pixels)
                {
                    polygon.emplace_back(std::lround(p.x), std::lround(p.y));
                }
                cv::fillPoly(image, std::vector<std::vector<cv::Point>>{polygon},
                             (x + y) % 2 == 0 ? cv::Scalar(20, 20, 20) : cv::Scalar(250, 250, 250),
                             cv::LINE_AA);
            }
        }
        const auto name = "board-" + std::to_string(view) + ".png";
        if (!cv::imwrite((output / name).string(), image))
        {
            return 1;
        }
        (view < 9 ? train : validation).emplace_back(name);
    }
    sv::write_json(
        output / "dataset.json",
        boost::json::object{
            {"schema_version", 1},
            {"camera_id", 0},
            {"theta_max_rad", .7},
            {"board", boost::json::object{{"columns", 9}, {"rows", 6}, {"square_size_m", .04}}},
            {"train", train},
            {"validation", validation}});
    return 0;
}
