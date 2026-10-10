#include "internal.hpp"
#include <cmath>
#include <stdexcept>

namespace sv
{
FusionResult fuse_research(const FusionSamples &samples, const Fusion &settings)
{
    const auto size = samples.colors[0].size();
    if (size.empty() || size.area() > 262144 || settings.pyramid_levels < 1 ||
        settings.pyramid_levels > 8 || !std::isfinite(settings.smoothness_weight) ||
        settings.smoothness_weight < 0 || settings.smoothness_weight > 100)
    {
        throw std::invalid_argument("research fusion budget/parameters");
    }
    for (int c = 0; c < 4; ++c)
    {
        if (samples.colors[c].size() != size || samples.colors[c].type() != CV_32FC3 ||
            samples.validity[c].size() != size || samples.validity[c].type() != CV_8UC1 ||
            samples.edge_weights[c].size() != size || samples.edge_weights[c].type() != CV_32FC1 ||
            !cv::checkRange(samples.colors[c], true, nullptr, 0, 1.000001) ||
            !cv::checkRange(samples.edge_weights[c], true, nullptr, 0, 1.000001))
        {
            throw std::invalid_argument("research sample shape/type/range");
        }
    }
    const bool cut = settings.mode == "graph_cut_seam" || settings.mode == "graph_cut_multi_band";
    const bool band = settings.mode == "multi_band" || settings.mode == "graph_cut_multi_band";
    if (!cut && !band && settings.mode != "seam_distance_feather")
    {
        throw std::invalid_argument("unknown research fusion mode");
    }
    cv::Mat weights = cv::Mat::zeros(size, CV_32FC4);
    if (cut)
    {
        weights = fusion_detail::cut_weights(samples, fusion_detail::distances(samples),
                                             settings.smoothness_weight);
    }
    else
    {
        auto distances = band ? samples.edge_weights : fusion_detail::distances(samples);
        for (int y = 0; y < size.height; ++y)
        {
            for (int x = 0; x < size.width; ++x)
            {
                auto &w = weights.at<cv::Vec4f>(y, x);
                float total = 0;
                for (int c = 0; c < 4; ++c)
                {
                    if (samples.validity[c].at<uchar>(y, x))
                    {
                        w[c] = distances[c].at<float>(y, x);
                        total += w[c];
                    }
                }
                if (total > 1e-6)
                {
                    w /= total;
                }
                else
                {
                    w = {};
                }
            }
        }
    }
    auto blend_weights = weights.clone();
    if (settings.mode == "graph_cut_multi_band")
    {
        std::vector<cv::Mat> planes;
        cv::split(weights, planes);
        for (int c = 0; c < 4; ++c)
        {
            planes[c] = fusion_detail::gaussian(planes[c], 2);
            planes[c].setTo(0, samples.validity[c] == 0);
        }
        cv::merge(planes, blend_weights);
        for (int y = 0; y < size.height; ++y)
        {
            for (int x = 0; x < size.width; ++x)
            {
                auto &w = blend_weights.at<cv::Vec4f>(y, x);
                const float total = w[0] + w[1] + w[2] + w[3];
                if (total > 1e-6)
                {
                    w /= total;
                }
                else
                {
                    w = {};
                }
            }
        }
    }
    cv::Mat color;
    if (band)
    {
        color = fusion_detail::multiband(samples, blend_weights, settings.pyramid_levels);
    }
    else
    {
        color = cv::Mat::zeros(size, CV_32FC3);
        for (int y = 0; y < size.height; ++y)
        {
            for (int x = 0; x < size.width; ++x)
            {
                for (int c = 0; c < 4; ++c)
                {
                    color.at<cv::Vec3f>(y, x) +=
                        weights.at<cv::Vec4f>(y, x)[c] * samples.colors[c].at<cv::Vec3f>(y, x);
                }
            }
        }
    }
    return {color, weights};
}
} // namespace sv
