#include "internal.hpp"
#include "sv/seam_optimizer.hpp"
#include <algorithm>
#include <cmath>

namespace sv::fusion_detail
{
cv::Mat multilabel_weights(const FusionSamples &samples, const std::array<cv::Mat, 4> &distance,
                           double smoothness, seam::Summary &summary)
{
    const auto size = samples.colors[0].size();
    cv::Mat ids(size, CV_32SC1, cv::Scalar(-1));
    std::vector<cv::Point> points;
    std::vector<double> disagreement;
    seam::Problem problem;
    for (int y = 0; y < size.height; ++y)
    {
        for (int x = 0; x < size.width; ++x)
        {
            double sum = 0, delta = 0;
            for (int c = 0; c < 4; ++c)
            {
                if (!samples.validity[c].at<uchar>(y, x))
                {
                    continue;
                }
                sum += distance[c].at<float>(y, x);
                for (int d = c + 1; d < 4; ++d)
                {
                    if (samples.validity[d].at<uchar>(y, x))
                    {
                        delta = std::max(delta, cv::norm(samples.colors[c].at<cv::Vec3f>(y, x) -
                                                         samples.colors[d].at<cv::Vec3f>(y, x)));
                    }
                }
            }
            if (sum <= 0)
            {
                continue;
            }
            std::array<int64_t, 4> costs;
            for (int c = 0; c < 4; ++c)
            {
                costs[c] = samples.validity[c].at<uchar>(y, x)
                               ? std::llround(1000 * (1 - distance[c].at<float>(y, x) / sum))
                               : -1;
            }
            ids.at<int>(y, x) = points.size();
            points.emplace_back(x, y);
            disagreement.push_back(delta);
            problem.unary.push_back(costs);
        }
    }
    auto sorted = disagreement;
    std::sort(sorted.begin(), sorted.end());
    double scale = 1e-6;
    if (!sorted.empty())
    {
        const double position = .95 * (sorted.size() - 1);
        const auto lo = static_cast<size_t>(position);
        scale = std::max(scale, sorted[lo] +
                                    (position - lo) *
                                        (sorted[std::min(lo + 1, sorted.size() - 1)] - sorted[lo]));
    }
    for (unsigned n = 0; n < points.size(); ++n)
    {
        const auto p = points[n];
        for (const auto d : {cv::Point(1, 0), cv::Point(0, 1)})
        {
            const auto q = p + d;
            if (q.x >= size.width || q.y >= size.height || ids.at<int>(q) < 0)
            {
                continue;
            }
            const auto m = static_cast<unsigned>(ids.at<int>(q));
            const double difference = .5 * (std::min(1., disagreement[n] / scale) +
                                            std::min(1., disagreement[m] / scale));
            problem.edges.push_back({n, m, std::llround(1000 * smoothness * (.05 + difference))});
        }
    }
    const auto result = seam::optimize(problem);
    summary = {result.accepted_energies.front(),
               result.accepted_energies.back(),
               problem.unary.size(),
               problem.edges.size(),
               result.sweeps,
               static_cast<unsigned>(result.accepted_energies.size() - 1),
               result.converged};
    cv::Mat weights = cv::Mat::zeros(size, CV_32FC4);
    for (size_t n = 0; n < points.size(); ++n)
    {
        weights.at<cv::Vec4f>(points[n])[result.labels[n]] = 1;
    }
    return weights;
}
} // namespace sv::fusion_detail
