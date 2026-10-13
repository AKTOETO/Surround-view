#include "sv/fusion_runtime.hpp"
#include "sv/render_inspection.hpp"
#include "sv/renderer.hpp"
#include <gtest/gtest.h>

namespace
{
sv::Config support_config()
{
    auto config = sv::load_config(SV_TEST_CONFIG);
    config.width = 96;
    config.height = 64;
    config.surface.H = 0;
    config.fusion.diagnostic = "color";
    for (auto &camera : config.cameras)
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
    return config;
}

sv::FrameSet black_frames(const sv::Config &config)
{
    sv::FrameSet frames;
    for (int camera = 0; camera < 4; ++camera)
    {
        const auto &c = config.cameras[camera];
        auto image = std::make_shared<sv::Image>();
        image->width = c.width;
        image->height = c.height;
        image->channels = 3;
        image->pixels.resize(c.width * c.height * 3, 0);
        sv::Frame frame;
        frame.camera_id = camera;
        frame.image = image;
        frames.frames[camera] = frame;
    }
    return frames;
}

class RenderSupport : public testing::TestWithParam<std::string>
{
};
} // namespace

TEST_P(RenderSupport, CountsMatchSourceValidityAndActualWeightsWithoutChangingOutput)
{
    auto config = support_config();
    const auto mode = GetParam();
    const bool hybrid = sv::research_fusion(mode);
    config.fusion.mode = mode;
    config.cameras[1].T[11] = -5;
    config.cameras[2].cx = -10000;
    auto frames = black_frames(config);
    frames.frames[3].reset();
    sv::Renderer renderer(config);
    const auto plain = renderer.render(frames, config.view);
    sv::RenderInspection inspection;
    const auto captured =
        renderer.render(frames, config.view, &inspection, {false, true, hybrid, true, true});
    EXPECT_EQ(captured.pixels, plain.pixels);
    EXPECT_EQ(renderer.uploads(), 3);
    EXPECT_GT(renderer.last_timing().inspection_cpu_ms, 0);
    ASSERT_EQ(inspection.support.type(), CV_8UC4);
    ASSERT_EQ(inspection.support.size(), cv::Size(config.width, config.height));
    int supported = 0, ego = 0, background = 0;
    for (int y = 0; y < config.height; ++y)
    {
        for (int x = 0; x < config.width; ++x)
        {
            const auto region = inspection.carrier_regions.at<uchar>(y, x);
            const auto pixel = inspection.support.at<cv::Vec4b>(y, x);
            for (int camera = 0; camera < 4; ++camera)
            {
                EXPECT_EQ(inspection.source_visibility.at<cv::Vec4b>(y, x)[camera],
                          inspection.source_projections[camera].status.at<uchar>(y, x));
            }
            if (region == 0 || region == 3 || region == 5)
            {
                EXPECT_EQ(pixel[0], 0);
                EXPECT_EQ(pixel[1], 0);
                EXPECT_EQ(pixel[2], region == 0 ? 0 : region == 3 ? 1 : 2);
                EXPECT_EQ(pixel[3], region == 0 ? 0 : 255);
                ego += region == 5;
                background += region == 0;
                continue;
            }
            int valid = 0;
            for (const auto &source : inspection.source_projections)
            {
                valid += source.status.at<uchar>(y, x) == 1;
            }
            EXPECT_EQ(pixel[0], valid);
            EXPECT_LE(pixel[1], pixel[0]);
            EXPECT_EQ(pixel[3], 255);
            if (hybrid)
            {
                ASSERT_TRUE(inspection.fusion);
                const auto weights = inspection.fusion->blend_weights.at<cv::Vec4f>(y, x);
                int positive = 0;
                for (const auto w : weights.val)
                {
                    positive += w > 0;
                }
                EXPECT_EQ(pixel[1], positive);
                EXPECT_EQ(pixel[2], valid ? 6 : 5);
            }
            else
            {
                // Only camera 0 can pass; black samples still provide positive support.
                EXPECT_EQ(pixel[1], valid);
                EXPECT_EQ(pixel[2], valid ? 4 : 3);
            }
            supported += pixel[1] > 0;
        }
    }
    EXPECT_GT(supported, 0);
    EXPECT_GT(ego, 0);
    EXPECT_GT(background, 0);
    EXPECT_EQ(renderer.render(frames, config.view).pixels, plain.pixels);
    EXPECT_EQ(renderer.last_timing().inspection_cpu_ms, 0);
    renderer.render({}, config.view, &inspection, {false, false, false, true});
    EXPECT_TRUE(inspection.source_visibility.empty());
    for (int y = 0; y < config.height; ++y)
    {
        for (int x = 0; x < config.width; ++x)
        {
            const auto pixel = inspection.support.at<cv::Vec4b>(y, x);
            EXPECT_EQ(pixel[0], 0);
            EXPECT_EQ(pixel[1], 0);
            EXPECT_NE(pixel[2], 4);
            EXPECT_NE(pixel[2], 6);
        }
    }
    renderer.render(frames, config.view, &inspection);
    EXPECT_TRUE(inspection.support.empty());
}

INSTANTIATE_TEST_SUITE_P(Algorithms, RenderSupport,
                         testing::Values("edge_feather", "angular_feather", "hard_best_angle",
                                         "seam_distance_feather", "graph_cut_seam", "multi_band",
                                         "graph_cut_multi_band"));

TEST(RenderSupportContract, RequiresDestinationAndColorView)
{
    auto config = support_config();
    sv::Renderer renderer(config);
    sv::RenderInspection inspection;
    EXPECT_THROW(renderer.render({}, config.view, nullptr, {false, false, false, true}),
                 std::invalid_argument);
    auto fusion = config.fusion;
    fusion.diagnostic = "coverage";
    renderer.set_fusion(fusion);
    EXPECT_THROW(renderer.render({}, config.view, &inspection, {false, false, false, true}),
                 std::invalid_argument);
    EXPECT_TRUE(inspection.support.empty());
}

TEST(RenderSupportContract, PositiveWeightDoesNotGuaranteeGpuFusionAboveThreshold)
{
    auto config = support_config();
    config.fusion.mode = "edge_feather";
    // Numerical threshold control; intentionally bypasses public config bounds.
    config.fusion.edge_width_px = 1e12;
    sv::Renderer renderer(config);
    sv::RenderInspection inspection;
    renderer.render(black_frames(config), config.view, &inspection, {false, false, false, true});
    int positive_but_fallback = 0;
    for (int y = 0; y < config.height; ++y)
    {
        for (int x = 0; x < config.width; ++x)
        {
            const auto pixel = inspection.support.at<cv::Vec4b>(y, x);
            positive_but_fallback += pixel[0] == 4 && pixel[1] == 4 && pixel[2] == 3;
        }
    }
    EXPECT_GT(positive_but_fallback, 0);
}

TEST(RenderSupportContract, ZeroWeightsAndHardSelectionHaveDifferentOutputPaths)
{
    for (const auto *mode : {"edge_feather", "hard_best_angle", "multi_band"})
    {
        SCOPED_TRACE(mode);
        auto config = support_config();
        config.fusion.mode = mode;
        // Shader-only known-answer control: map all eligible rays to the image boundary.
        // Degenerate intrinsics deliberately bypass the public calibration parser.
        for (auto &camera : config.cameras)
        {
            camera.fx = camera.fy = camera.cx = camera.cy = 0;
        }
        sv::Renderer renderer(config);
        sv::RenderInspection inspection;
        renderer.render(black_frames(config), config.view, &inspection,
                        {false, false, false, true});
        int checked = 0;
        for (int y = 0; y < config.height; ++y)
        {
            for (int x = 0; x < config.width; ++x)
            {
                const auto pixel = inspection.support.at<cv::Vec4b>(y, x);
                if (pixel[0] == 4)
                {
                    EXPECT_EQ(pixel[1], std::string(mode) == "hard_best_angle" ? 1 : 0);
                    EXPECT_EQ(pixel[2], std::string(mode) == "hard_best_angle" ? 4
                                        : std::string(mode) == "multi_band"    ? 6
                                                                               : 3);
                    ++checked;
                }
            }
        }
        EXPECT_GT(checked, 0);
    }
}

TEST(RenderSupportContract, FusionReturnsActualPostSmoothingBaseWeightsWithoutFullInspection)
{
    sv::FusionSamples samples;
    const cv::Size size(16, 8);
    for (int camera = 0; camera < 4; ++camera)
    {
        samples.colors[camera] = cv::Mat(size, CV_32FC3, cv::Scalar::all(.2));
        samples.edge_weights[camera] = cv::Mat(size, CV_32FC1, cv::Scalar::all(1));
        samples.validity[camera] = cv::Mat::zeros(size, CV_8UC1);
    }
    samples.validity[0].colRange(0, 11).setTo(255);
    samples.validity[1].colRange(5, 16).setTo(255);
    auto config = support_config();
    config.fusion.mode = "graph_cut_multi_band";
    sv::FusionInspection inspection;
    const auto plain = sv::fuse_research(samples, config.fusion);
    const auto captured = sv::fuse_research(samples, config.fusion, &inspection);
    EXPECT_EQ(cv::norm(plain.blend_weights, inspection.blend_weights, cv::NORM_INF), 0);
    EXPECT_EQ(cv::norm(plain.color, captured.color, cv::NORM_INF), 0);
    EXPECT_GT(cv::norm(plain.blend_weights, plain.weights, cv::NORM_INF), .01);
}

TEST_P(RenderSupport, FallbackHatchMarksOnlyActualFallbackAndRestoresLegacy)
{
    for (const bool dome : {false, true})
    {
        SCOPED_TRACE(dome);
        auto config = support_config();
        config.fusion.mode = GetParam();
        if (dome)
        {
            config.surface.type = "dome_floor_v1";
        }
        sv::Renderer renderer(config);
        for (const bool missing : {true, false})
        {
            SCOPED_TRACE(missing);
            const auto frames = missing ? sv::FrameSet{} : black_frames(config);
            const auto original = renderer.render(frames, config.view);
            config.fusion.fallback_display.mode = "hatch";
            renderer.set_fusion(config.fusion);
            sv::RenderInspection inspection;
            const auto marked = renderer.render(frames, config.view, &inspection,
                                                {false, false, false, true, true});
            EXPECT_EQ(marked.pixels, renderer.render(frames, config.view).pixels);
            int fallbacks = 0, fused = 0;
            for (int y = 0; y < config.height; ++y)
            {
                for (int x = 0; x < config.width; ++x)
                {
                    const auto path = static_cast<sv::RenderSupportPath>(
                        inspection.support.at<cv::Vec4b>(y, x)[2]);
                    const auto offset = (size_t(y) * config.width + x) * 4;
                    if (path == sv::RenderSupportPath::gpu_fallback ||
                        path == sv::RenderSupportPath::hybrid_fallback)
                    {
                        const bool dark = ((x / 8) + ((config.height - 1 - y) / 8)) % 2;
                        const std::array<int, 4> expected =
                            dark ? std::array<int, 4>{38, 13, 38, 255}
                                 : std::array<int, 4>{255, 0, 255, 255};
                        for (int ch = 0; ch < 4; ++ch)
                        {
                            EXPECT_NEAR(marked.pixels[offset + ch], expected[ch], 1);
                        }
                        ++fallbacks;
                    }
                    else
                    {
                        EXPECT_TRUE(std::equal(marked.pixels.begin() + offset,
                                               marked.pixels.begin() + offset + 4,
                                               original.pixels.begin() + offset));
                        fused += path == sv::RenderSupportPath::gpu_fusion ||
                                 path == sv::RenderSupportPath::hybrid_fusion;
                    }
                }
            }
            if (missing)
            {
                EXPECT_GT(fallbacks, 0);
                EXPECT_EQ(fused, 0);
            }
            else
            {
                EXPECT_GT(fused, 0); // Real black samples must not be marked as missing.
            }
            config.fusion.fallback_display.mode = "legacy";
            renderer.set_fusion(config.fusion);
            EXPECT_EQ(renderer.render(frames, config.view).pixels, original.pixels);
        }
    }
}
