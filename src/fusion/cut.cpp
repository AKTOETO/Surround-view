#include "internal.hpp"
#include <algorithm>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/push_relabel_max_flow.hpp>
#include <cmath>
#include <cstdint>
#include <opencv2/imgproc.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry/2d.hpp>
#endif
#include <queue>
#include <vector>

namespace sv::fusion_detail
{
std::array<cv::Mat, 4> distances(const FusionSamples &samples)
{
    std::array<cv::Mat, 4> result;
    for (int c = 0; c < 4; ++c)
    {
        if (cv::countNonZero(samples.validity[c]) ==
            samples.validity[c].rows * samples.validity[c].cols)
        {
            // scipy EDT's special no-background case: implicit zero at (-1,0).
            result[c] = cv::Mat(samples.validity[c].size(), CV_32FC1);
            for (int y = 0; y < result[c].rows; ++y)
            {
                for (int x = 0; x < result[c].cols; ++x)
                {
                    result[c].at<float>(y, x) = std::hypot(double(y + 1), double(x));
                }
            }
        }
        else
        {
            cv::distanceTransform(samples.validity[c], result[c], cv::DIST_L2,
                                  cv::DIST_MASK_PRECISE);
        }
    }
    return result;
}

using Traits = boost::adjacency_list_traits<boost::vecS, boost::vecS, boost::directedS>;
using Graph = boost::adjacency_list<
    boost::vecS, boost::vecS, boost::directedS, boost::no_property,
    boost::property<
        boost::edge_capacity_t, int64_t,
        boost::property<boost::edge_residual_capacity_t, int64_t,
                        boost::property<boost::edge_reverse_t, Traits::edge_descriptor>>>>;

cv::Mat cut_weights(const FusionSamples &samples, const std::array<cv::Mat, 4> &distance,
                    double smoothness)
{
    const auto size = samples.colors[0].size();
    cv::Mat weights = cv::Mat::zeros(size, CV_32FC4);
    cv::Mat coverage = cv::Mat::zeros(size, CV_8UC1);
    for (int y = 0; y < size.height; ++y)
    {
        for (int x = 0; x < size.width; ++x)
        {
            int winner = -1, count = 0;
            float best = -1;
            for (int c = 0; c < 4; ++c)
            {
                if (samples.validity[c].at<uchar>(y, x))
                {
                    ++count;
                    if (distance[c].at<float>(y, x) > best)
                    {
                        winner = c;
                        best = distance[c].at<float>(y, x);
                    }
                }
            }
            coverage.at<uchar>(y, x) = count;
            auto &w = weights.at<cv::Vec4f>(y, x);
            if (winner >= 0)
            {
                w[winner] = 1;
            }
            if (count >= 3)
            {
                w = {};
                float ties = 0;
                for (int c = 0; c < 4; ++c)
                {
                    if (samples.validity[c].at<uchar>(y, x) &&
                        std::abs(distance[c].at<float>(y, x) - best) <=
                            1e-6 + 1e-6 * std::abs(best))
                    {
                        w[c] = 1;
                        ++ties;
                    }
                }
                w /= ties;
            }
        }
    }
    for (int a = 0; a < 4; ++a)
    {
        for (int b = a + 1; b < 4; ++b)
        {
            cv::Mat ids(size, CV_32SC1, cv::Scalar(-1));
            std::vector<cv::Point> points;
            std::vector<double> disagreement;
            for (int y = 0; y < size.height; ++y)
            {
                for (int x = 0; x < size.width; ++x)
                {
                    if (coverage.at<uchar>(y, x) == 2 && samples.validity[a].at<uchar>(y, x) &&
                        samples.validity[b].at<uchar>(y, x))
                    {
                        ids.at<int>(y, x) = points.size();
                        points.emplace_back(x, y);
                        const auto delta = samples.colors[a].at<cv::Vec3f>(y, x) -
                                           samples.colors[b].at<cv::Vec3f>(y, x);
                        disagreement.push_back(cv::norm(delta));
                    }
                }
            }
            const auto count = points.size();
            if (!count)
            {
                continue;
            }
            auto sorted = disagreement;
            std::sort(sorted.begin(), sorted.end());
            const double quantile = .95 * (count - 1);
            const size_t lo = static_cast<size_t>(quantile);
            const double scale =
                std::max(1e-6, sorted[lo] + (quantile - lo) *
                                                (sorted[std::min(lo + 1, count - 1)] - sorted[lo]));
            Graph graph(count + 2);
            auto capacity = get(boost::edge_capacity, graph);
            auto reverse = get(boost::edge_reverse, graph);
            auto edge = [&](size_t u, size_t v, int64_t amount)
            {
                const auto e = add_edge(u, v, graph).first, r = add_edge(v, u, graph).first;
                capacity[e] = amount;
                capacity[r] = 0;
                reverse[e] = r;
                reverse[r] = e;
            };
            for (size_t n = 0; n < count; ++n)
            {
                const auto p = points[n];
                const double da = distance[a].at<float>(p), db = distance[b].at<float>(p),
                             sum = da + db + 1e-6;
                edge(count, n, std::nearbyint((1 - db / sum) * 1000));
                edge(n, count + 1, std::nearbyint((1 - da / sum) * 1000));
                for (const auto d : {cv::Point(1, 0), cv::Point(0, 1)})
                {
                    const auto q = p + d;
                    if (q.x >= size.width || q.y >= size.height)
                    {
                        continue;
                    }
                    const int m = ids.at<int>(q);
                    if (m < 0)
                    {
                        continue;
                    }
                    const double difference = .5 * (std::min(1., disagreement[n] / scale) +
                                                    std::min(1., disagreement[m] / scale));
                    const auto cost = std::max<int64_t>(
                        1, std::nearbyint(smoothness * (.05 + difference) * 1000));
                    edge(n, m, cost);
                    edge(m, n, cost);
                }
            }
            boost::push_relabel_max_flow(graph, count, count + 1);
            const auto residual = get(boost::edge_residual_capacity, graph);
            std::vector<bool> reachable(count + 2, false);
            std::queue<size_t> queue;
            queue.push(count);
            reachable[count] = true;
            while (!queue.empty())
            {
                const auto u = queue.front();
                queue.pop();
                for (auto [it, end] = out_edges(u, graph); it != end; ++it)
                {
                    const auto v = target(*it, graph);
                    if (residual[*it] > 0 && !reachable[v])
                    {
                        reachable[v] = true;
                        queue.push(v);
                    }
                }
            }
            for (size_t n = 0; n < count; ++n)
            {
                auto &w = weights.at<cv::Vec4f>(points[n]);
                w = {};
                w[reachable[n] ? a : b] = 1;
            }
        }
    }
    return weights;
}
} // namespace sv::fusion_detail
