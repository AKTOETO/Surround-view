#include "sv/fusion_runtime.hpp"
#include "sv/renderer.hpp"
#include <gtest/gtest.h>

namespace
{
sv::Config config()
{
    auto c = sv::load_config(SV_TEST_CONFIG);
    c.width = 48;
    c.height = 32;
    c.surface.H = 0;
    for (auto &camera : c.cameras)
    {
        camera.T = sv::identity();
        camera.T[5] = camera.T[10] = -1;
        camera.T[11] = 5;
        camera.fx = camera.fy = 30;
        camera.cx = camera.width / 2.0;
        camera.cy = camera.height / 2.0;
        camera.k = {};
        camera.theta_max = 1.5;
    }
    return c;
}

sv::FrameSet frames(const sv::Config &c, int count, bool identical = false)
{
    sv::FrameSet result;
    for (int camera = 0; camera < count; ++camera)
    {
        auto image = std::make_shared<sv::Image>();
        image->width = c.cameras[camera].width;
        image->height = c.cameras[camera].height;
        image->channels = 3;
        image->pixels.resize(image->width * image->height * 3, identical ? 0 : camera * 64);
        sv::Frame f;
        f.camera_id = camera;
        f.image = image;
        result.frames[camera] = f;
    }
    return result;
}

class SourceDisagreement : public testing::TestWithParam<std::string>
{
};
} // namespace

TEST_P(SourceDisagreement, KnownEncodedRangesUndefinedOverlapAndParity)
{
    auto c = config();
    c.fusion.mode = GetParam();
    sv::Renderer renderer(c);
    for (const auto [count, identical] :
         {std::pair{4, false}, {2, false}, {1, false}, {0, false}, {4, true}})
    {
        SCOPED_TRACE(count);
        SCOPED_TRACE(identical);
        const auto inputs = frames(c, count, identical);
        const auto original = renderer.render(inputs, c.view);
        sv::RenderInspection inspection;
        const auto captured =
            renderer.render(inputs, c.view, &inspection, {false, true, false, false, false, true});
        EXPECT_EQ(original.pixels, captured.pixels);
        ASSERT_EQ(inspection.source_disagreement.type(), CV_8UC4);
        int checked = 0;
        for (int y = 0; y < c.height; ++y)
        {
            for (int x = 0; x < c.width; ++x)
            {
                const auto pixel = inspection.source_disagreement.at<cv::Vec4b>(y, x);
                const auto region = inspection.carrier_regions.at<uchar>(y, x);
                if (region == 0 || region == 3 || region == 5)
                {
                    EXPECT_EQ(pixel, cv::Vec4b(0, 0, 0, 0));
                    continue;
                }
                int eligible = 0;
                for (const auto &source : inspection.source_projections)
                {
                    eligible += source.status.at<uchar>(y, x) == 1;
                }
                EXPECT_EQ(pixel[1], eligible);
                EXPECT_EQ(pixel[2], eligible >= 2 ? 255 : 0);
                EXPECT_EQ(pixel[3], 255);
                if (eligible == count)
                {
                    EXPECT_EQ(pixel[0], count >= 2 && !identical ? (count - 1) * 64 : 0);
                    ++checked;
                }
            }
        }
        EXPECT_GT(checked, 0);
        renderer.render(inputs, c.view, &inspection);
        EXPECT_TRUE(inspection.source_disagreement.empty());
    }
}

TEST(SourceDisagreementContract, EligibilityIncludesZeroWeightAndRequiresColorDestination)
{
    auto c = config();
    // Internal shader boundary fixture; public calibrated intrinsics remain strictly positive.
    for (auto &camera : c.cameras)
    {
        camera.fx = camera.fy = camera.cx = camera.cy = 0;
    }
    sv::Renderer renderer(c);
    sv::RenderInspection inspection;
    sv::RenderInspectionRequest request;
    request.support = request.source_disagreement = true;
    EXPECT_THROW(renderer.render({}, c.view, nullptr, request), std::invalid_argument);
    renderer.render(frames(c, 4), c.view, &inspection, request);
    int checked = 0;
    for (int y = 0; y < c.height; ++y)
    {
        for (int x = 0; x < c.width; ++x)
        {
            const auto support = inspection.support.at<cv::Vec4b>(y, x);
            if (support[0] != 4)
            {
                continue;
            }
            EXPECT_EQ(support[1], 0);
            EXPECT_EQ(support[2], 3);
            EXPECT_EQ(inspection.source_disagreement.at<cv::Vec4b>(y, x),
                      cv::Vec4b(192, 4, 255, 255));
            ++checked;
        }
    }
    EXPECT_GT(checked, 0);
    c.fusion.diagnostic = "weights";
    renderer.set_fusion(c.fusion);
    EXPECT_THROW(renderer.render({}, c.view, &inspection, request), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(AllFusionModes, SourceDisagreement,
                         testing::Values("edge_feather", "angular_feather", "hard_best_angle",
                                         "seam_distance_feather", "graph_cut_seam", "multi_band",
                                         "graph_cut_multi_band"));
