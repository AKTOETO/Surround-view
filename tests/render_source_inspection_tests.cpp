#include "sv/render_inspection.hpp"
#include "sv/renderer.hpp"
#include <cmath>
#include <gtest/gtest.h>

TEST(SourceInspection, ShaderCoordinatesAndRejectionsMatchIndependentProjection)
{
    auto config = sv::load_config(SV_TEST_CONFIG);
    config.width = 96;
    config.height = 64;
    config.surface.H = 0;
    config.fusion.mode = "edge_feather";
    config.fusion.diagnostic = "color";
    // Downward-looking synthetic cameras: front of camera is negative vehicle Z.
    for (auto &camera : config.cameras)
    {
        camera.T = sv::identity();
        camera.T[5] = camera.T[10] = -1;
        camera.T[11] = 5;
        camera.fx = camera.fy = 30;
        camera.cx = camera.width / 2.0;
        camera.cy = camera.height / 2.0;
        camera.k = {.01, -.002, .0003, -.00004};
        camera.theta_max = 1.5;
    }
    config.cameras[1].T[11] = -5;      // Behind the camera.
    config.cameras[2].theta_max = .01; // Outside angular support.
    config.cameras[3].cx = -10000;     // Outside image bounds.
    sv::FrameSet frames;
    for (int camera = 0; camera < 4; ++camera)
    {
        const auto &c = config.cameras[camera];
        auto image = std::make_shared<sv::Image>();
        image->width = c.width;
        image->height = c.height;
        image->channels = 3;
        image->pixels.resize(c.width * c.height * 3, 80 + camera * 40);
        sv::Frame frame;
        frame.camera_id = camera;
        frame.image = image;
        frames.frames[camera] = frame;
    }
    sv::Renderer renderer(config);
    sv::RenderInspection inspection;
    EXPECT_THROW(renderer.render(frames, config.view, nullptr, {false, true}),
                 std::invalid_argument);
    EXPECT_THROW(renderer.render(frames, config.view, &inspection, {false, false, true}),
                 std::invalid_argument);
    EXPECT_FALSE(inspection.fusion);
    const auto plain = renderer.render(frames, config.view);
    EXPECT_EQ(plain.pixels, renderer.render(frames, config.view, &inspection, {true, true}).pixels);
    EXPECT_EQ(plain.pixels, renderer.render(frames, config.view).pixels);
    std::array<int, 7> counts{};
    for (int camera = 0; camera < 4; ++camera)
    {
        const auto &source = inspection.source_projections[camera];
        ASSERT_EQ(source.pixels.type(), CV_32FC2);
        ASSERT_EQ(source.status.type(), CV_8UC1);
        for (int y = 0; y < config.height; ++y)
        {
            for (int x = 0; x < config.width; ++x)
            {
                const auto region = inspection.carrier_regions.at<uchar>(y, x);
                const auto point = inspection.carrier_positions.at<cv::Vec3f>(y, x);
                const auto pixel = source.pixels.at<cv::Vec2f>(y, x);
                const auto status = source.status.at<uchar>(y, x);
                ASSERT_LT(status, counts.size());
                ++counts[status];
                if (region == 0 || region == 5)
                {
                    EXPECT_EQ(status, 0);
                }
                else if (region == 3)
                {
                    EXPECT_EQ(status, 6);
                }
                else
                {
                    const auto projected =
                        sv::project(config.cameras[camera], {point[0], point[1], point[2]});
                    EXPECT_EQ(status == 1, projected.valid);
                    EXPECT_EQ(status, camera == 0 ? 1 : camera == 1 ? 3 : camera == 2 ? 4 : 5);
                    if (projected.valid)
                    {
                        EXPECT_NEAR(pixel[0], projected.u, .01);
                        EXPECT_NEAR(pixel[1], projected.v, .01);
                    }
                }
                if (status != 1)
                {
                    EXPECT_TRUE(std::isnan(pixel[0]) && std::isnan(pixel[1]));
                }
            }
        }
    }
    for (const auto status : {0, 1, 3, 4, 5, 6})
    {
        EXPECT_GT(counts[status], 0) << status;
    }
    renderer.render({}, config.view, &inspection, {false, true});
    EXPECT_TRUE(inspection.carrier_positions.empty());
    int unavailable = 0;
    for (const auto &source : inspection.source_projections)
    {
        for (int y = 0; y < config.height; ++y)
        {
            for (int x = 0; x < config.width; ++x)
            {
                const auto status = source.status.at<uchar>(y, x);
                EXPECT_TRUE(status == 0 || status == 2 || status == 6);
                unavailable += status == 2;
                const auto pixel = source.pixels.at<cv::Vec2f>(y, x);
                EXPECT_TRUE(std::isnan(pixel[0]) && std::isnan(pixel[1]));
            }
        }
    }
    EXPECT_GT(unavailable, 0);
    renderer.render(frames, config.view, &inspection);
    for (const auto &source : inspection.source_projections)
    {
        EXPECT_TRUE(source.pixels.empty());
        EXPECT_TRUE(source.status.empty());
    }
}

namespace
{
// Independent oracle: intersect the segment with all six rectangle faces, not slab intervals.
bool face_occlusion(sv::Vec3 from, sv::Vec3 to, const sv::SourceOcclusionBox &box)
{
    const std::array<double, 3> origin{from.x, from.y, from.z};
    const std::array<double, 3> delta{to.x - from.x, to.y - from.y, to.z - from.z};
    const std::array<double, 3> lo{box.minimum.x, box.minimum.y, box.minimum.z};
    const std::array<double, 3> hi{box.maximum.x, box.maximum.y, box.maximum.z};
    if (from.x >= lo[0] && from.x <= hi[0] && from.y >= lo[1] && from.y <= hi[1] &&
        from.z >= lo[2] && from.z <= hi[2])
    {
        return true;
    }
    const double length = std::sqrt(sv::dot(to - from, to - from));
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(delta[axis]) < 1e-12)
        {
            continue;
        }
        for (double plane : {lo[axis], hi[axis]})
        {
            const double t = (plane - origin[axis]) / delta[axis];
            bool inside = t >= 0 && t < 1 - box.tolerance / length;
            for (int other = 0; other < 3; ++other)
            {
                if (other == axis)
                {
                    continue;
                }
                const double p = origin[other] + t * delta[other];
                inside &= p >= lo[other] - 1e-10 && p <= hi[other] + 1e-10;
            }
            if (inside)
            {
                return true;
            }
        }
    }
    return false;
}
} // namespace

TEST(SourceInspection, SuppliedBoxRejectsOnlyBlockedSourceSegmentsAndRestoresOutput)
{
    auto config = sv::load_config(SV_TEST_CONFIG);
    config.width = 160;
    config.height = 100;
    config.surface.H = 0;
    config.fusion.mode = "edge_feather";
    config.fusion.diagnostic = "color";
    sv::FrameSet frames;
    for (int index = 0; index < 4; ++index)
    {
        auto &camera = config.cameras[index];
        camera.T = sv::identity();
        camera.T[5] = camera.T[10] = -1;
        camera.T[3] = index - 2.;
        camera.T[7] = 1. - index;
        camera.T[11] = 5;
        camera.theta_max = 1.5;
        camera.fx = camera.fy = 30;
        camera.cx = camera.width / 2.;
        camera.cy = camera.height / 2.;
        auto image = std::make_shared<sv::Image>();
        image->width = camera.width;
        image->height = camera.height;
        image->channels = 3;
        image->pixels.resize(camera.width * camera.height * 3, 50 + index * 60);
        sv::Frame frame;
        frame.camera_id = index;
        frame.image = image;
        frames.frames[index] = frame;
    }
    sv::Renderer renderer(config);
    sv::RenderInspection before, after;
    const auto plain = renderer.render(frames, config.view, &before, {true, true});
    const auto uploads = renderer.uploads();
    auto surface = config.surface;
    surface.source_occlusion = {true, {2, 1.5, .6}, {4, 3.5, 2}, .02};
    renderer.set_surface(surface);
    const auto filtered = renderer.render(frames, config.view, &after, {true, true});
    EXPECT_EQ(filtered.pixels, renderer.render(frames, config.view).pixels);
    EXPECT_EQ(renderer.uploads(), uploads);
    int rejected = 0, retained = 0;
    for (int camera = 0; camera < 4; ++camera)
    {
        const auto origin = sv::transform(sv::inverse_rigid(config.cameras[camera].T), {0, 0, 0});
        for (int y = 0; y < config.height; ++y)
        {
            for (int x = 0; x < config.width; ++x)
            {
                const int original = before.source_projections[camera].status.at<uchar>(y, x);
                const int current = after.source_projections[camera].status.at<uchar>(y, x);
                if (original != 1)
                {
                    EXPECT_EQ(current, original);
                    continue;
                }
                const auto p = before.carrier_positions.at<cv::Vec3f>(y, x);
                const bool blocked =
                    face_occlusion(origin, {p[0], p[1], p[2]}, surface.source_occlusion);
                EXPECT_EQ(current, blocked ? 7 : 1);
                if (blocked)
                {
                    ++rejected;
                    const auto uv = after.source_projections[camera].pixels.at<cv::Vec2f>(y, x);
                    EXPECT_TRUE(std::isnan(uv[0]) && std::isnan(uv[1]));
                }
                else
                {
                    ++retained;
                }
            }
        }
    }
    EXPECT_GT(rejected, 0);
    EXPECT_GT(retained, 0);
    // A carrier on the front-facing box top must not self-occlude at its endpoint.
    surface.reference_height = 2;
    renderer.set_surface(surface);
    renderer.render(frames, config.view, &after, {true, true});
    for (const auto &source : after.source_projections)
    {
        EXPECT_EQ(cv::countNonZero(source.status == 7), 0);
        EXPECT_GT(cv::countNonZero(source.status == 1), 0);
    }
    surface.reference_height = 0;
    renderer.set_surface(surface);
    auto fusion = config.fusion;
    fusion.mode = "multi_band";
    renderer.set_fusion(fusion);
    renderer.render(frames, config.view, &after, {true, true});
    ASSERT_TRUE(after.samples);
    int hybrid_rejections = 0;
    for (int camera = 0; camera < 4; ++camera)
    {
        for (int y = 0; y < config.height; ++y)
        {
            for (int x = 0; x < config.width; ++x)
            {
                if (after.source_projections[camera].status.at<uchar>(y, x) == 7)
                {
                    EXPECT_EQ(after.samples->validity[camera].at<uchar>(y, x), 0);
                    ++hybrid_rejections;
                }
            }
        }
    }
    EXPECT_EQ(hybrid_rejections, rejected);
    renderer.set_fusion(config.fusion);
    renderer.set_surface(config.surface);
    EXPECT_EQ(plain.pixels, renderer.render(frames, config.view).pixels);
}
