#include "internal.hpp"
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <vector>

namespace sv::fusion_detail
{
cv::Mat gaussian(const cv::Mat &image, double sigma)
{
    const int radius = static_cast<int>(4 * sigma + .5);
    cv::Mat result;
    // scipy.ndimage gaussian_filter: truncate=4, half-sample symmetric reflect.
    cv::GaussianBlur(image, result, {2 * radius + 1, 2 * radius + 1}, sigma, sigma,
                     cv::BORDER_REFLECT);
    return result;
}

cv::Mat resize_linear(const cv::Mat &image, cv::Size target)
{
    // scipy.ndimage.zoom(order=1, grid_mode=False): align first and last centers.
    cv::Mat result(target, image.type());
    const int channels = image.channels();
    for (int y = 0; y < target.height; ++y)
    {
        const double sy =
            target.height > 1 ? double(y) * (image.rows - 1) / (target.height - 1) : 0;
        const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, image.rows - 1);
        const double fy = sy - y0;
        auto *out = result.ptr<float>(y);
        for (int x = 0; x < target.width; ++x)
        {
            const double sx =
                target.width > 1 ? double(x) * (image.cols - 1) / (target.width - 1) : 0;
            const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, image.cols - 1);
            const double fx = sx - x0;
            for (int c = 0; c < channels; ++c)
            {
                out[x * channels + c] =
                    (1 - fy) * ((1 - fx) * image.ptr<float>(y0)[x0 * channels + c] +
                                fx * image.ptr<float>(y0)[x1 * channels + c]) +
                    fy * ((1 - fx) * image.ptr<float>(y1)[x0 * channels + c] +
                          fx * image.ptr<float>(y1)[x1 * channels + c]);
            }
        }
    }
    return result;
}

namespace
{
std::vector<cv::Mat> pyramid(const cv::Mat &image, unsigned levels)
{
    std::vector<cv::Mat> result{image};
    while (result.size() < levels && result.back().rows >= 4 && result.back().cols >= 4)
    {
        const auto blurred = gaussian(result.back(), 1);
        cv::Mat down((blurred.rows + 1) / 2, (blurred.cols + 1) / 2, blurred.type());
        for (int y = 0; y < down.rows; ++y)
        {
            for (int x = 0; x < down.cols; ++x)
            {
                for (int c = 0; c < blurred.channels(); ++c)
                {
                    down.ptr<float>(y)[x * blurred.channels() + c] =
                        blurred.ptr<float>(2 * y)[2 * x * blurred.channels() + c];
                }
            }
        }
        result.push_back(down);
    }
    return result;
}
} // namespace

cv::Mat multiband(const FusionSamples &samples, const cv::Mat &weights, unsigned levels)
{
    std::array<std::vector<cv::Mat>, 4> colors, masks;
    std::vector<cv::Mat> planes;
    cv::split(weights, planes);
    for (int c = 0; c < 4; ++c)
    {
        auto observed = samples.colors[c].clone();
        observed.setTo(0, samples.validity[c] == 0);
        colors[c] = pyramid(observed, levels);
        masks[c] = pyramid(planes[c], colors[c].size());
        for (size_t level = 0; level + 1 < colors[c].size(); ++level)
        {
            colors[c][level] =
                colors[c][level] - resize_linear(colors[c][level + 1], colors[c][level].size());
        }
    }
    std::vector<cv::Mat> blended;
    for (size_t level = 0; level < colors[0].size(); ++level)
    {
        cv::Mat layer = cv::Mat::zeros(colors[0][level].size(), CV_32FC3);
        for (int y = 0; y < layer.rows; ++y)
        {
            for (int x = 0; x < layer.cols; ++x)
            {
                float total = 0;
                cv::Vec3f sum{};
                for (int c = 0; c < 4; ++c)
                {
                    const float w = masks[c][level].at<float>(y, x);
                    sum += w * colors[c][level].at<cv::Vec3f>(y, x);
                    total += w;
                }
                if (total > 1e-6)
                {
                    layer.at<cv::Vec3f>(y, x) = sum / total;
                }
            }
        }
        blended.push_back(layer);
    }
    auto result = blended.back();
    for (int level = static_cast<int>(blended.size()) - 2; level >= 0; --level)
    {
        result = blended[level] + resize_linear(result, blended[level].size());
    }
    cv::max(result, 0, result);
    cv::min(result, 1, result);
    return result;
}
} // namespace sv::fusion_detail
