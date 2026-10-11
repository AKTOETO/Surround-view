#include "sv/fusion.hpp"
#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

namespace
{
sv::FusionSamples fixture()
{
    sv::FusionSamples samples;
    const cv::Size size(33, 17);
    for (int c = 0; c < 4; ++c)
    {
        samples.colors[c] = cv::Mat::zeros(size, CV_32FC3);
        samples.validity[c] = cv::Mat::zeros(size, CV_8UC1);
        samples.edge_weights[c] = cv::Mat(size, CV_32F, cv::Scalar(c + 1.f)).clone() / 4;
        for (int y = 0; y < size.height; ++y)
        {
            for (int x = 0; x < size.width; ++x)
            {
                samples.colors[c].at<cv::Vec3f>(y, x) = {float((x + c * 3) % 9) / 8,
                                                         float((y + c) % 5) / 4, c / 3.f};
                samples.validity[c].at<uchar>(y, x) = (c == 0   ? x < 23
                                                       : c == 1 ? x > 8
                                                       : c == 2 ? y > 6
                                                                : y < 10)
                                                          ? 255
                                                          : 0;
            }
        }
    }
    return samples;
}
} // namespace

TEST(FusionInspection, ContributionsReconstructAllResearchModesWithoutChangingOutput)
{
    const auto samples = fixture();
    for (const auto *mode :
         {"seam_distance_feather", "graph_cut_seam", "multi_band", "graph_cut_multi_band"})
    {
        for (const auto *boundary : {"zero", "normalized"})
        {
            for (const auto *solver : {"binary_pairs", "alpha_expansion"})
            {
                SCOPED_TRACE(::testing::Message() << mode << '/' << boundary << '/' << solver);
                sv::Fusion settings;
                settings.mode = mode;
                settings.pyramid_boundary = boundary;
                settings.seam_solver = solver;
                settings.pyramid_levels = 4;
                const auto plain = sv::fuse_research(samples, settings);
                sv::FusionInspection inspection;
                const auto captured = sv::fuse_research(samples, settings, &inspection);
                EXPECT_EQ(cv::norm(plain.color, captured.color, cv::NORM_INF), 0);
                EXPECT_EQ(cv::norm(plain.weights, captured.weights, cv::NORM_INF), 0);
                const bool band =
                    settings.mode == "multi_band" || settings.mode == "graph_cut_multi_band";
                EXPECT_EQ(inspection.level_weights.size(), band ? 4U : 1U);
                cv::Mat sum = cv::Mat::zeros(samples.colors[0].size(), CV_32FC3);
                for (const auto &camera : inspection.camera_contributions)
                {
                    ASSERT_EQ(camera.type(), CV_32FC3);
                    sum += camera;
                }
                if (band)
                {
                    cv::max(sum, 0, sum);
                    cv::min(sum, 1, sum);
                }
                EXPECT_LE(cv::norm(sum, captured.color, cv::NORM_INF), 2e-6);
                for (const auto &weights : inspection.level_weights)
                {
                    for (int y = 0; y < weights.rows; ++y)
                    {
                        for (int x = 0; x < weights.cols; ++x)
                        {
                            const auto w = weights.at<cv::Vec4f>(y, x);
                            float total = 0;
                            for (const auto value : w.val)
                            {
                                EXPECT_GE(value, 0);
                                total += value;
                            }
                            EXPECT_TRUE(total == 0 || std::abs(total - 1) < 2e-6);
                        }
                    }
                }
            }
        }
    }
}

TEST(FusionInspection, KnownConstantWeightsAndEmptyCaptureReset)
{
    auto samples = fixture();
    for (int c = 0; c < 4; ++c)
    {
        samples.validity[c].setTo(255);
        samples.colors[c].setTo(cv::Scalar::all((c + 1) / 4.f));
    }
    sv::Fusion settings;
    settings.mode = "multi_band";
    settings.pyramid_levels = 1;
    sv::FusionInspection inspection;
    const auto output = sv::fuse_research(samples, settings, &inspection);
    for (int c = 0; c < 4; ++c)
    {
        cv::Mat coefficient;
        cv::extractChannel(inspection.blend_weights, coefficient, c);
        EXPECT_LE(cv::norm(coefficient - (c + 1) / 10.f, cv::NORM_INF), 1e-7);
        EXPECT_LE(
            cv::norm(inspection.camera_contributions[c] - cv::Scalar::all((c + 1) * (c + 1) / 40.f),
                     cv::NORM_INF),
            1e-7);
    }
    EXPECT_LE(cv::norm(output.color - cv::Scalar::all(.75), cv::NORM_INF), 1e-7);
    settings.pyramid_levels = 8;
    sv::fuse_research(samples, settings, &inspection);
    EXPECT_GT(inspection.level_weights.size(), 1U);
    for (auto &validity : samples.validity)
    {
        validity.setTo(0);
    }
    settings.pyramid_levels = 1;
    const auto empty = sv::fuse_research(samples, settings, &inspection);
    ASSERT_EQ(inspection.level_weights.size(), 1U);
    EXPECT_EQ(cv::norm(empty.color, cv::NORM_INF), 0);
    EXPECT_EQ(cv::norm(inspection.blend_weights, cv::NORM_INF), 0);
    for (const auto &contribution : inspection.camera_contributions)
    {
        EXPECT_EQ(cv::norm(contribution, cv::NORM_INF), 0);
    }
}

TEST(FusionInspection, MultibandContributionCanCrossLocalValidityBoundary)
{
    auto samples = fixture();
    for (int c = 0; c < 4; ++c)
    {
        samples.validity[c].setTo(0);
        samples.colors[c].setTo(cv::Scalar::all(c == 0 ? .8 : .2));
        samples.edge_weights[c].setTo(1);
    }
    samples.validity[0](cv::Rect(0, 0, 16, 17)).setTo(255);
    samples.validity[1](cv::Rect(16, 0, 17, 17)).setTo(255);
    sv::Fusion settings;
    settings.mode = "multi_band";
    settings.pyramid_levels = 4;
    sv::FusionInspection inspection;
    sv::fuse_research(samples, settings, &inspection);
    int nonlocal = 0;
    for (int y = 0; y < 17; ++y)
    {
        for (int x = 16; x < 33; ++x)
        {
            EXPECT_EQ(inspection.blend_weights.at<cv::Vec4f>(y, x)[0], 0);
            nonlocal += std::abs(inspection.camera_contributions[0].at<cv::Vec3f>(y, x)[0]) > 1e-6;
        }
    }
    EXPECT_GT(nonlocal, 0);
    RecordProperty("nonlocal_camera0_pixels", nonlocal);
}
