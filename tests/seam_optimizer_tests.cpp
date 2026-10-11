#include "sv/fusion.hpp"
#include "sv/seam_optimizer.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <random>

namespace
{
// Independent direct enumeration; does not call the production energy evaluator.
int64_t score(const sv::seam::Problem &p, const std::vector<unsigned> &labels)
{
    int64_t value = 0;
    for (size_t n = 0; n < labels.size(); ++n)
    {
        if (p.unary[n][labels[n]] < 0)
        {
            return std::numeric_limits<int64_t>::max() / 4;
        }
        value += p.unary[n][labels[n]];
    }
    for (const auto &edge : p.edges)
    {
        if (labels[edge.first] != labels[edge.second])
        {
            value += edge.weight;
        }
    }
    return value;
}

sv::seam::Problem random_problem(std::mt19937 &random, unsigned labels)
{
    sv::seam::Problem p;
    for (unsigned n = 0; n < 6; ++n)
    {
        std::array<int64_t, 4> costs{-1, -1, -1, -1};
        for (unsigned label = 0; label < labels; ++label)
        {
            costs[label] = random() % 31;
        }
        if (random() % 3 == 0)
        {
            costs[random() % labels] = -1;
        }
        p.unary.push_back(costs);
    }
    for (unsigned n = 0; n < 6; ++n)
    {
        p.edges.push_back({n, (n + 1) % 6, int64_t(random() % 21)});
    }
    p.edges.push_back({0, 3, int64_t(random() % 21)});
    return p;
}
} // namespace

TEST(SeamOptimizer, EveryExpansionMatchesExhaustiveBinaryMoves)
{
    std::mt19937 random(20261011);
    for (unsigned trial = 0; trial < 128; ++trial)
    {
        const auto p = random_problem(random, 3 + trial % 2);
        auto initial = sv::seam::optimize(p, 1).labels;
        // Start from another feasible labeling, including labels equal to alpha.
        for (size_t n = 0; n < initial.size(); ++n)
        {
            unsigned label = random() % 4;
            while (p.unary[n][label] < 0)
            {
                label = (label + 1) % 4;
            }
            initial[n] = label;
        }
        for (unsigned alpha = 0; alpha < 4; ++alpha)
        {
            auto minimum = score(p, initial);
            for (unsigned bits = 0; bits < (1U << initial.size()); ++bits)
            {
                auto candidate = initial;
                for (size_t n = 0; n < initial.size(); ++n)
                {
                    if (bits & (1U << n))
                    {
                        candidate[n] = alpha;
                    }
                }
                minimum = std::min(minimum, score(p, candidate));
            }
            const auto actual = sv::seam::expand(p, initial, alpha);
            ASSERT_EQ(score(p, actual), minimum) << "trial=" << trial << " alpha=" << alpha;
            EXPECT_EQ(sv::seam::energy(p, actual), minimum);
        }
    }
    RecordProperty("graphs", 128);
    RecordProperty("moves", 512);
}

TEST(SeamOptimizer, ConvergedEnergyAndGlobalEnumeration)
{
    std::mt19937 random(20261012);
    unsigned global_matches = 0;
    unsigned budget_converged = 0, one_sweep_incomplete = 0;
    int64_t maximum_gap = 0;
    boost::json::object worst;
    for (unsigned trial = 0; trial < 128; ++trial)
    {
        const auto p = random_problem(random, 3 + trial % 2);
        const auto result = sv::seam::optimize(p, 32);
        budget_converged += sv::seam::optimize(p).converged;
        one_sweep_incomplete += !sv::seam::optimize(p, 1).converged;
        ASSERT_TRUE(result.converged);
        const auto measured = score(p, result.labels);
        for (size_t n = 1; n < result.accepted_energies.size(); ++n)
        {
            EXPECT_LT(result.accepted_energies[n], result.accepted_energies[n - 1]);
        }
        for (unsigned alpha = 0; alpha < 4; ++alpha)
        {
            EXPECT_GE(score(p, sv::seam::expand(p, result.labels, alpha)), measured);
        }
        auto minimum = std::numeric_limits<int64_t>::max();
        std::vector<unsigned> global;
        for (unsigned bits = 0; bits < 4096; ++bits)
        {
            std::vector<unsigned> candidate(6);
            for (unsigned n = 0; n < 6; ++n)
            {
                candidate[n] = (bits >> (2 * n)) & 3;
            }
            const auto value = score(p, candidate);
            if (value < minimum)
            {
                minimum = value;
                global = candidate;
            }
        }
        EXPECT_GE(measured, minimum);
        EXPECT_LE(measured, 2 * minimum); // Converged nonnegative weighted Potts.
        global_matches += measured == minimum;
        if (measured - minimum > maximum_gap)
        {
            boost::json::array unary, edges;
            for (const auto &costs : p.unary)
            {
                unary.push_back(boost::json::value_from(costs));
            }
            for (const auto &edge : p.edges)
            {
                edges.push_back(boost::json::array{edge.first, edge.second, edge.weight});
            }
            worst = {{"trial", trial},
                     {"unary", unary},
                     {"edges", edges},
                     {"labels", boost::json::value_from(result.labels)},
                     {"global_labels", boost::json::value_from(global)},
                     {"energy", measured},
                     {"global_energy", minimum},
                     {"accepted_energies", boost::json::value_from(result.accepted_energies)}};
        }
        maximum_gap = std::max(maximum_gap, measured - minimum);
    }
    RecordProperty("graphs", 128);
    RecordProperty("global_matches", global_matches);
    RecordProperty("maximum_global_gap", maximum_gap);
    RecordProperty("eight_sweep_converged", budget_converged);
    RecordProperty("one_sweep_incomplete", one_sweep_incomplete);
    RecordProperty("worst_case", boost::json::serialize(worst));
    EXPECT_GT(one_sweep_incomplete, 0U);
}

TEST(SeamOptimizer, BinaryIsGlobalAndDisconnectedConstraintsRemainValid)
{
    sv::seam::Problem p{{{{0, 9, -1, -1}}, {{9, 0, -1, -1}}, {{-1, -1, 0, -1}}}, {{0, 1, 20}}};
    const auto result = sv::seam::optimize(p);
    EXPECT_EQ(score(p, result.labels), 9);
    EXPECT_EQ(result.labels[2], 2U);
    p.unary[0] = {-1, -1, -1, -1};
    EXPECT_THROW(sv::seam::optimize(p), std::invalid_argument);
    EXPECT_THROW(sv::seam::expand({}, {}, 4), std::invalid_argument);
    EXPECT_THROW(sv::seam::optimize({}, 0), std::invalid_argument);
    EXPECT_THROW(sv::seam::optimize({{{{0, 0, 0, 0}}}, {{0, 1, 1}}}), std::invalid_argument);
    EXPECT_TRUE(sv::seam::optimize({}).converged);
    sv::seam::Problem oversized;
    oversized.unary.resize(262145, {0, 0, 0, 0});
    EXPECT_THROW(sv::seam::optimize(oversized), std::invalid_argument);
    oversized.unary.resize(100000);
    std::fill(oversized.unary.begin(), oversized.unary.end(),
              std::array<int64_t, 4>{0, 1000000000, -1, -1});
    EXPECT_THROW(sv::seam::optimize(oversized), std::invalid_argument);
}

TEST(SeamOptimizer, EqualCostsHaveDeterministicLabelOrderRatherThanUniformTies)
{
    const sv::seam::Problem p{{{{0, 0, 0, 0}}, {{0, 0, 0, 0}}}, {{0, 1, 7}}};
    EXPECT_EQ(sv::seam::optimize(p).labels, (std::vector<unsigned>{0, 0}));
}

TEST(SeamOptimizer, ProductionFusionSelectsOnlyAvailableCameraAndPreservesEmptyPixels)
{
    sv::FusionSamples samples;
    for (int c = 0; c < 4; ++c)
    {
        samples.colors[c] = cv::Mat(4, 7, CV_32FC3, cv::Scalar(.2, .4, .6));
        samples.edge_weights[c] = cv::Mat(4, 7, CV_32FC1, cv::Scalar(1));
        samples.validity[c] = cv::Mat(4, 7, CV_8UC1, cv::Scalar(255));
        samples.validity[c].at<uchar>(0, 0) = 0;
        samples.validity[c].at<uchar>(2, c + 1) = 0;
    }
    sv::Fusion fusion;
    fusion.mode = "graph_cut_seam";
    fusion.seam_solver = "alpha_expansion";
    const auto result = sv::fuse_research(samples, fusion);
    ASSERT_TRUE(result.seam_optimization);
    EXPECT_TRUE(result.seam_optimization->converged);
    EXPECT_LE(result.seam_optimization->final_energy, result.seam_optimization->initial_energy);
    EXPECT_EQ(result.seam_optimization->nodes, 27U);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 7; ++x)
        {
            const auto w = result.weights.at<cv::Vec4f>(y, x);
            EXPECT_EQ(w[0] + w[1] + w[2] + w[3], (x == 0 && y == 0) ? 0 : 1);
            for (int c = 0; c < 4; ++c)
            {
                if (!samples.validity[c].at<uchar>(y, x))
                {
                    EXPECT_EQ(w[c], 0);
                }
            }
            const auto rgb = result.color.at<cv::Vec3f>(y, x);
            EXPECT_NEAR(rgb[0], (x == 0 && y == 0) ? 0 : .2, 1e-6);
        }
    }
    fusion.seam_solver = "unknown";
    EXPECT_THROW(sv::fuse_research(samples, fusion), std::invalid_argument);
}

TEST(SeamOptimizer, ProductionThreeAndFourCameraOverlapRespectSingleCameraAnchors)
{
    for (int cameras : {3, 4})
    {
        sv::FusionSamples samples;
        for (int c = 0; c < 4; ++c)
        {
            samples.colors[c] = cv::Mat(1, 3, CV_32FC3, cv::Scalar(.3, .3, .3));
            samples.edge_weights[c] = cv::Mat(1, 3, CV_32FC1, cv::Scalar(1));
            samples.validity[c] = cv::Mat::zeros(1, 3, CV_8UC1);
            if (c < cameras)
            {
                samples.validity[c].at<uchar>(0, 1) = 255;
            }
        }
        samples.validity[0].at<uchar>(0, 0) = 255;
        samples.validity[1].at<uchar>(0, 2) = 255;
        sv::Fusion fusion;
        fusion.mode = "graph_cut_seam";
        fusion.seam_solver = "alpha_expansion";
        fusion.smoothness_weight = 100;
        const auto result = sv::fuse_research(samples, fusion);
        ASSERT_TRUE(result.seam_optimization);
        EXPECT_TRUE(result.seam_optimization->converged);
        EXPECT_EQ(result.seam_optimization->nodes, 3U);
        EXPECT_EQ(result.seam_optimization->edges, 2U);
        // Middle unary round(1000*(1-1/N)) + exactly one 5000-cost seam.
        EXPECT_EQ(result.seam_optimization->final_energy, cameras == 3 ? 5667 : 5750);
        EXPECT_EQ(result.weights.at<cv::Vec4f>(0, 0)[0], 1);
        EXPECT_EQ(result.weights.at<cv::Vec4f>(0, 2)[1], 1);
        EXPECT_EQ(result.weights.at<cv::Vec4f>(0, 1)[2], 0);
        EXPECT_EQ(result.weights.at<cv::Vec4f>(0, 1)[3], 0);
    }
}
