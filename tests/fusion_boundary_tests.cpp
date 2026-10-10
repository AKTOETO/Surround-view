#include "sv/build_info.hpp"
#include "sv/fusion.hpp"
#include <gtest/gtest.h>

namespace
{
sv::FusionSamples constant_scene(int width, int height, int layout)
{
    sv::FusionSamples samples;
    for (int c = 0; c < 4; ++c)
    {
        samples.colors[c] = cv::Mat(height, width, CV_32FC3, cv::Scalar(.6, .3, .8)).clone();
        samples.validity[c] = cv::Mat::zeros(height, width, CV_8UC1);
        samples.edge_weights[c] = cv::Mat(height, width, CV_32F, cv::Scalar(1)).clone();
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (layout == 0)
            {
                samples.validity[0].at<uchar>(y, x) = x < 2 * width / 3;
                samples.validity[1].at<uchar>(y, x) = x >= width / 3;
            }
            else if (layout == 1)
            {
                samples.validity[0].at<uchar>(y, x) = x <= y + width / 3;
                samples.validity[1].at<uchar>(y, x) = x >= y;
            }
            else if (layout == 2)
            {
                samples.validity[(x + y) % 4].at<uchar>(y, x) = 255;
            }
            else
            {
                samples.validity[0].at<uchar>(y, x) = 255;
                samples.validity[1].at<uchar>(y, x) = x == width / 2;
                samples.validity[2].at<uchar>(y, x) = y == height / 2;
            }
        }
    }
    return samples;
}
} // namespace

TEST(FusionBoundary, NormalizedPreservesSharedConstantAcrossMaskLayouts)
{
    boost::json::array cases;
    double maximum_error = 0;
    for (const auto size : {cv::Size(3, 3), cv::Size(33, 17), cv::Size(32, 16)})
    {
        for (int layout = 0; layout < 4; ++layout)
        {
            const auto samples = constant_scene(size.width, size.height, layout);
            for (const auto *mode : {"multi_band", "graph_cut_multi_band"})
            {
                for (unsigned levels : {1U, 4U, 8U})
                {
                    SCOPED_TRACE(::testing::Message() << size << " layout=" << layout
                                                      << " mode=" << mode << " levels=" << levels);
                    sv::Fusion fusion;
                    fusion.mode = mode;
                    fusion.pyramid_levels = levels;
                    fusion.pyramid_boundary = "normalized";
                    const auto result = sv::fuse_research(samples, fusion);
                    const double error = cv::norm(result.color, samples.colors[0], cv::NORM_INF);
                    maximum_error = std::max(maximum_error, error);
                    EXPECT_LE(error, 4e-5);
                    cases.emplace_back(boost::json::object{{"width", size.width},
                                                           {"height", size.height},
                                                           {"layout", layout},
                                                           {"mode", mode},
                                                           {"levels", levels},
                                                           {"max_abs_linear_error", error}});
                }
            }
        }
    }
    RecordProperty("maximum_linear_error",
                   boost::json::serialize(boost::json::value(maximum_error)));
    RecordProperty("constant_cases", boost::json::serialize(cases));
    RecordProperty("source_fingerprint", sv::source_fingerprint);
}

TEST(FusionBoundary, ZeroExtensionHasNonzeroConstantFieldError)
{
    const auto samples = constant_scene(33, 17, 0);
    boost::json::array profiles;
    for (const auto *mode : {"multi_band", "graph_cut_multi_band"})
    {
        for (const auto *boundary : {"zero", "normalized"})
        {
            sv::Fusion fusion;
            fusion.mode = mode;
            fusion.pyramid_boundary = boundary;
            const auto output = sv::fuse_research(samples, fusion).color;
            const double error = cv::norm(output, samples.colors[0], cv::NORM_INF);
            if (std::string(boundary) == "zero")
            {
                EXPECT_GT(error, .01);
            }
            boost::json::array row;
            for (int x = 0; x < output.cols; ++x)
            {
                row.emplace_back(output.at<cv::Vec3f>(output.rows / 2, x)[0]);
            }
            profiles.emplace_back(boost::json::object{{"mode", mode},
                                                      {"boundary", boundary},
                                                      {"max_abs_linear_error", error},
                                                      {"center_row_red", row}});
        }
    }
    RecordProperty("profiles", boost::json::serialize(profiles));
}

TEST(FusionBoundary, InvalidRgbCannotAffectNormalizedResultAndEmptyInputsStayFinite)
{
    auto samples = constant_scene(33, 17, 0);
    for (const auto *mode : {"multi_band", "graph_cut_multi_band"})
    {
        sv::Fusion fusion;
        fusion.mode = mode;
        fusion.pyramid_boundary = "normalized";
        const auto before = sv::fuse_research(samples, fusion).color;
        for (int c = 0; c < 4; ++c)
        {
            samples.colors[c].setTo(cv::Scalar(1, 0, 1), samples.validity[c] == 0);
        }
        EXPECT_EQ(cv::norm(before, sv::fuse_research(samples, fusion).color, cv::NORM_INF), 0);
    }
    for (auto &mask : samples.validity)
    {
        mask.setTo(0);
    }
    sv::Fusion fusion;
    fusion.mode = "multi_band";
    fusion.pyramid_boundary = "normalized";
    const auto empty = sv::fuse_research(samples, fusion).color;
    EXPECT_TRUE(cv::checkRange(empty));
    EXPECT_EQ(cv::norm(empty, cv::NORM_INF), 0);
    fusion.pyramid_boundary = "unknown";
    EXPECT_THROW(sv::fuse_research(samples, fusion), std::invalid_argument);
}

TEST(FusionBoundary, FullyObservedSingleCameraReconstructsNonconstantImage)
{
    auto samples = constant_scene(33, 17, 0);
    for (int c = 0; c < 4; ++c)
    {
        samples.validity[c].setTo(c == 0 ? 255 : 0);
    }
    for (int y = 0; y < 17; ++y)
    {
        for (int x = 0; x < 33; ++x)
        {
            samples.colors[0].at<cv::Vec3f>(y, x) = cv::Vec3f(x / 32.f, y / 16.f, .4f);
        }
    }
    for (const auto *mode : {"multi_band", "graph_cut_multi_band"})
    {
        sv::Fusion fusion;
        fusion.mode = mode;
        fusion.pyramid_boundary = "normalized";
        EXPECT_LE(
            cv::norm(sv::fuse_research(samples, fusion).color, samples.colors[0], cv::NORM_INF),
            4e-6);
    }
}
