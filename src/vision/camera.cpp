#include "sv/vision.hpp"
#include <cmath>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/objdetect.hpp>
#endif

namespace sv
{
cv::Matx33d camera_matrix(const Camera &c)
{
    return {c.fx, 0, c.cx, 0, c.fy, c.cy, 0, 0, 1};
}

std::string opencv_version()
{
    return CV_VERSION;
}

std::vector<Pixel> project_opencv(const Camera &c, const std::vector<Vec3> &points)
{
    std::vector<Pixel> result(points.size());
    std::vector<cv::Point3d> visible;
    std::vector<size_t> indices;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto p = transform(c.T, points[i]);
        if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && p.z > c.z_epsilon &&
            std::atan2(std::hypot(p.x, p.y), p.z) <= c.theta_max)
        {
            visible.emplace_back(p.x, p.y, p.z);
            indices.push_back(i);
        }
    }
    if (visible.empty())
    {
        return result;
    }
    std::vector<cv::Point2d> pixels;
    cv::fisheye::projectPoints(visible, pixels, cv::Vec3d{}, cv::Vec3d{}, camera_matrix(c),
                               cv::Vec4d(c.k[0], c.k[1], c.k[2], c.k[3]));
    for (size_t i = 0; i < pixels.size(); ++i)
    {
        const auto p = pixels[i];
        result[indices[i]] = {p.x, p.y,
                              std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 &&
                                  p.x <= c.width - 1 && p.y <= c.height - 1};
    }
    return result;
}

Image read_image(const std::filesystem::path &path)
{
    if (path.extension() == ".ppm")
    {
        return read_ppm(path);
    }
    cv::Mat bgr = cv::imread(path.string(), cv::IMREAD_COLOR);
    if (bgr.empty() || bgr.cols < 2 || bgr.rows < 2 || bgr.cols > 4096 || bgr.rows > 2160)
    {
        throw std::runtime_error("invalid image: " + path.string());
    }
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    if (!rgb.isContinuous())
    {
        rgb = rgb.clone();
    }
    return {rgb.cols, rgb.rows, 3, {rgb.data, rgb.data + rgb.total() * rgb.elemSize()}};
}

std::vector<cv::Point2f> detect_board(const cv::Mat &image, cv::Size size)
{
    if (image.empty() || size.width < 3 || size.height < 3 || size.area() > 1000)
    {
        throw std::invalid_argument("board/image dimensions");
    }
    cv::Mat gray;
    if (image.channels() == 1)
    {
        gray = image;
    }
    else
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    std::vector<cv::Point2f> points;
    if (!cv::findChessboardCornersSB(gray, size, points, cv::CALIB_CB_NORMALIZE_IMAGE))
    {
        throw std::runtime_error("chessboard not detected");
    }
    return points;
}
} // namespace sv
