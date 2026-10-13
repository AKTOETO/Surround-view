#include "sv/render_inspection.hpp"
#include "sv/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <tuple>

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
    const double length = std::sqrt(sv::dot(to - from, to - from));
    if (length <= box.tolerance)
    {
        return false;
    }
    if (from.x >= lo[0] && from.x <= hi[0] && from.y >= lo[1] && from.y <= hi[1] &&
        from.z >= lo[2] && from.z <= hi[2])
    {
        return true;
    }
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
    const sv::SourceOcclusionBox unit_box{true, {0, 0, 0}, {1, 1, 1}, .02};
    EXPECT_FALSE(face_occlusion({.5, .5, .5}, {.5, .5, .5}, unit_box));
    EXPECT_TRUE(face_occlusion({.5, .5, .5}, {2, 2, 2}, unit_box));
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
    int footprint_rejections = rejected;
    for (int guard : {0, 1})
    {
        surface.source_occlusion.mode = "bilinear_radial_v1";
        surface.source_occlusion.guard_texels = guard;
        renderer.set_surface(surface);
        renderer.render(frames, config.view, &after, {true, true});
        int current_rejections = 0;
        for (int camera = 0; camera < 4; ++camera)
        {
            const auto inverse = sv::inverse_rigid(config.cameras[camera].T);
            const auto origin = sv::transform(inverse, {0, 0, 0});
            const auto &c = config.cameras[camera];
            for (int y = 0; y < config.height; ++y)
            {
                for (int x = 0; x < config.width; ++x)
                {
                    const int original = before.source_projections[camera].status.at<uchar>(y, x);
                    const int status = after.source_projections[camera].status.at<uchar>(y, x);
                    if (original != 1)
                    {
                        EXPECT_EQ(status, original);
                        continue;
                    }
                    const auto p = before.carrier_positions.at<cv::Vec3f>(y, x);
                    const sv::Vec3 point{p[0], p[1], p[2]};
                    const double range = std::sqrt(sv::dot(point - origin, point - origin));
                    const auto uv = before.source_projections[camera].pixels.at<cv::Vec2f>(y, x);
                    const double bx = std::floor(uv[0]), by = std::floor(uv[1]);
                    const double fx = uv[0] - bx, fy = uv[1] - by;
                    bool blocked = face_occlusion(origin, point, surface.source_occlusion);
                    for (int dy = -guard; dy <= 1 + guard; ++dy)
                    {
                        for (int dx = -guard; dx <= 1 + guard; ++dx)
                        {
                            if (guard == 0 &&
                                (dx == 0 ? 1 - fx : fx) * (dy == 0 ? 1 - fy : fy) <= 0)
                            {
                                continue;
                            }
                            const double u = std::clamp(bx + dx, 0., double(c.width - 1));
                            const double v = std::clamp(by + dy, 0., double(c.height - 1));
                            const double a = (u - c.cx) / c.fx, b = (v - c.cy) / c.fy;
                            const double theta = std::hypot(a, b);
                            const double scale = theta > 1e-14 ? std::sin(theta) / theta : 1;
                            const auto ray =
                                sv::transform(inverse, {a * scale, b * scale, std::cos(theta)}) -
                                origin;
                            auto interior = surface.source_occlusion;
                            interior.minimum = interior.minimum + sv::Vec3{1e-5, 1e-5, 1e-5};
                            interior.maximum = interior.maximum - sv::Vec3{1e-5, 1e-5, 1e-5};
                            blocked |= face_occlusion(origin, origin + ray * range, interior);
                        }
                    }
                    EXPECT_EQ(status, blocked ? 7 : 1)
                        << "guard=" << guard << " camera=" << camera << " pixel=" << x << "," << y
                        << " uv=" << uv << " point=" << p;
                    current_rejections += status == 7;
                }
            }
        }
        EXPECT_GE(current_rejections, footprint_rejections);
        footprint_rejections = current_rejections;
    }
    surface.source_occlusion.mode = "center";
    surface.source_occlusion.guard_texels = 0;
    // A carrier on the front-facing box top must not self-occlude at its endpoint.
    surface.reference_height = 2;
    renderer.set_surface(surface);
    renderer.render(frames, config.view, &after, {true, true});
    for (const auto &source : after.source_projections)
    {
        EXPECT_EQ(cv::countNonZero(source.status == 7), 0);
        EXPECT_GT(cv::countNonZero(source.status == 1), 0);
    }
    // A supported box face uses the central-ray visibility result; neighbours must not
    // turn that visible surface into a hole. Outside the box, radial filtering still applies.
    const auto top_center = after;
    for (int guard : {0, 1})
    {
        surface.source_occlusion.mode = "bilinear_box_surface_v1";
        surface.source_occlusion.guard_texels = guard;
        renderer.set_surface(surface);
        renderer.render(frames, config.view, &after, {true, true});
        int supported = 0, outside_rejected = 0;
        for (int camera = 0; camera < 4; ++camera)
        {
            for (int y = 0; y < config.height; ++y)
            {
                for (int x = 0; x < config.width; ++x)
                {
                    const auto p = top_center.carrier_positions.at<cv::Vec3f>(y, x);
                    const int old = top_center.source_projections[camera].status.at<uchar>(y, x);
                    const int status = after.source_projections[camera].status.at<uchar>(y, x);
                    if (old == 1 && p[0] >= 2 && p[0] <= 4 && p[1] >= 1.5 && p[1] <= 3.5)
                    {
                        EXPECT_NEAR(p[2], 2, 1e-5);
                        EXPECT_EQ(status, 1);
                        ++supported;
                    }
                    else if (old == 1 && status == 7)
                    {
                        ++outside_rejected;
                    }
                }
            }
        }
        EXPECT_GT(supported, 0);
        EXPECT_GT(outside_rejected, 0);
        auto hybrid = config.fusion;
        hybrid.mode = "multi_band";
        renderer.set_fusion(hybrid);
        sv::RenderInspection hybrid_inspection;
        const auto hybrid_image =
            renderer.render(frames, config.view, &hybrid_inspection, {true, true});
        EXPECT_EQ(hybrid_image.pixels, renderer.render(frames, config.view).pixels);
        ASSERT_TRUE(hybrid_inspection.samples);
        for (int camera = 0; camera < 4; ++camera)
        {
            EXPECT_EQ(cv::countNonZero(hybrid_inspection.source_projections[camera].status !=
                                       after.source_projections[camera].status),
                      0);
            for (int y = 0; y < config.height; ++y)
            {
                for (int x = 0; x < config.width; ++x)
                {
                    EXPECT_EQ(hybrid_inspection.samples->validity[camera].at<uchar>(y, x) != 0,
                              after.source_projections[camera].status.at<uchar>(y, x) == 1);
                }
            }
        }
        renderer.set_fusion(config.fusion);
    }
    // A displaced support plane is outside the exact band, but can be explicitly
    // admitted without modifying central endpoint bias or the source images.
    surface.reference_height = 2.0005;
    surface.source_occlusion.support_tolerance = 0;
    renderer.set_surface(surface);
    sv::RenderInspection exact_support;
    renderer.render(frames, config.view, &exact_support, {true, true});
    surface.source_occlusion.support_tolerance = .001;
    renderer.set_surface(surface);
    renderer.render(frames, config.view, &after, {true, true});
    int recovered = 0;
    for (int camera = 0; camera < 4; ++camera)
    {
        recovered += cv::countNonZero((exact_support.source_projections[camera].status == 7) &
                                      (after.source_projections[camera].status == 1));
    }
    EXPECT_GT(recovered, 0);
    EXPECT_EQ(renderer.uploads(), uploads);
    // Hidden bottom-face endpoints must still be rejected by the central ray.
    surface.reference_height = .6;
    renderer.set_surface(surface);
    renderer.render(frames, config.view, &after, {true, true});
    int hidden = 0;
    for (int camera = 0; camera < 4; ++camera)
    {
        for (int y = 0; y < config.height; ++y)
        {
            for (int x = 0; x < config.width; ++x)
            {
                const auto p = after.carrier_positions.at<cv::Vec3f>(y, x);
                if (p[0] > 2.01 && p[0] < 3.99 && p[1] > 1.51 && p[1] < 3.49 &&
                    std::abs(p[2] - .6) < 1e-5)
                {
                    EXPECT_EQ(after.source_projections[camera].status.at<uchar>(y, x), 7);
                    ++hidden;
                }
            }
        }
    }
    EXPECT_GT(hidden, 0);
    surface.reference_height = 0;
    surface.source_occlusion.support_tolerance = 1e-5;
    surface.source_occlusion.mode = "bilinear_radial_v1";
    surface.source_occlusion.guard_texels = 1;
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
    EXPECT_EQ(hybrid_rejections, footprint_rejections);
    renderer.set_fusion(config.fusion);
    renderer.set_surface(config.surface);
    EXPECT_EQ(plain.pixels, renderer.render(frames, config.view).pixels);
}

class OcclusionUncertaintyTest
    : public ::testing::TestWithParam<std::tuple<std::string, std::string>>
{
};

TEST_P(OcclusionUncertaintyTest, MatchesFaceCertificatesAndRestoresNominalOutput)
{
    auto config = sv::load_config(SV_TEST_CONFIG);
    config.width = 160;
    config.height = 100;
    config.surface.H = 0;
    config.fusion.mode = std::get<0>(GetParam());
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
        frames.frames[index] = sv::Frame{index, 0, 0, 0, image};
    }
    sv::Renderer renderer(config);
    sv::RenderInspection before;
    const auto plain = renderer.render(frames, config.view, &before, {true, true});
    const auto uploads = renderer.uploads();
    auto surface = config.surface;
    surface.source_occlusion = {true, {2, 1.5, -.01}, {4, 3.5, .01}, 0};
    renderer.set_surface(surface);
    const auto nominal = renderer.render(frames, config.view);
    int blocked_count = 0, ambiguous_count = 0, clear_count = 0;
    for (double radius : {0., .005, .01, .05})
    {
        surface.source_occlusion.uncertainty = {radius, std::get<1>(GetParam())};
        renderer.set_surface(surface);
        sv::RenderInspection current;
        const auto output = renderer.render(frames, config.view, &current, {true, true});
        EXPECT_EQ(output.pixels, renderer.render(frames, config.view).pixels);
        EXPECT_EQ(renderer.uploads(), uploads);
        if (radius == 0)
        {
            EXPECT_EQ(output.pixels, nominal.pixels);
        }
        for (int camera = 0; camera < 4; ++camera)
        {
            const auto origin =
                sv::transform(sv::inverse_rigid(config.cameras[camera].T), {0, 0, 0});
            for (int y = 0; y < config.height; ++y)
            {
                for (int x = 0; x < config.width; ++x)
                {
                    const int old = before.source_projections[camera].status.at<uchar>(y, x);
                    const int actual = current.source_projections[camera].status.at<uchar>(y, x);
                    if (old != 1)
                    {
                        EXPECT_EQ(actual, old);
                        continue;
                    }
                    const auto p = before.carrier_positions.at<cv::Vec3f>(y, x);
                    auto inner = surface.source_occlusion, outer = inner;
                    inner.minimum = inner.minimum + sv::Vec3{radius, radius, radius};
                    inner.maximum = inner.maximum - sv::Vec3{radius, radius, radius};
                    outer.minimum = outer.minimum - sv::Vec3{radius, radius, radius};
                    outer.maximum = outer.maximum + sv::Vec3{radius, radius, radius};
                    const bool nonempty = inner.minimum.x <= inner.maximum.x &&
                                          inner.minimum.y <= inner.maximum.y &&
                                          inner.minimum.z <= inner.maximum.z;
                    const bool blocked =
                        nonempty && face_occlusion(origin, {p[0], p[1], p[2]}, inner);
                    const bool ambiguous =
                        !blocked && face_occlusion(origin, {p[0], p[1], p[2]}, outer);
                    const int expected =
                        blocked ? 7 : (ambiguous && std::get<1>(GetParam()) == "reject" ? 8 : 1);
                    EXPECT_EQ(actual, expected);
                    blocked_count += blocked;
                    ambiguous_count += ambiguous;
                    clear_count += !blocked && !ambiguous;
                    const auto uv = current.source_projections[camera].pixels.at<cv::Vec2f>(y, x);
                    if (expected == 1)
                    {
                        EXPECT_EQ(uv, before.source_projections[camera].pixels.at<cv::Vec2f>(y, x));
                    }
                    else
                    {
                        EXPECT_TRUE(std::isnan(uv[0]) && std::isnan(uv[1]));
                    }
                }
            }
        }
    }
    EXPECT_GT(blocked_count, 0);
    EXPECT_GT(ambiguous_count, 0);
    EXPECT_GT(clear_count, 0);
    renderer.set_surface(config.surface);
    EXPECT_EQ(renderer.render(frames, config.view).pixels, plain.pixels);
}

INSTANTIATE_TEST_SUITE_P(CentralPolicies, OcclusionUncertaintyTest,
                         ::testing::Combine(::testing::Values("edge_feather", "multi_band"),
                                            ::testing::Values("accept", "reject")));
