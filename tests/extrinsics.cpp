#include "sv/extrinsics.hpp"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace
{
struct Fixture
{
    sv::Camera truth, initial;
    std::vector<sv::Vec3> points;
    std::vector<sv::Pixel> pixels;

    Fixture()
    {
        truth.width = 640;
        truth.height = 480;
        truth.fx = truth.fy = 220;
        truth.cx = 319.5;
        truth.cy = 239.5;
        truth.k = {.03, -.004, 0, 0};
        initial = truth;
        const double c = std::cos(.05), s = std::sin(.05);
        initial.T = {c, 0, s, .1, 0, 1, 0, -.04, -s, 0, c, .07, 0, 0, 0, 1};
        for (int i = 0; i < 120; ++i)
        {
            points.push_back({(i % 10 - 4.5) * .3, (i / 10 - 5.5) * .2, 4.0 + (i % 7) * .4});
            pixels.push_back(sv::project(truth, points.back()));
        }
    }
};
} // namespace

TEST(Extrinsics, FourSolversRecoverExactPoseWithDistortion)
{
    const Fixture fixture;
    for (const auto *method : {"iterative", "epnp", "sqpnp", "ransac_epnp_lm"})
    {
        const auto result =
            sv::calibrate_extrinsics(fixture.initial, fixture.points, fixture.pixels, {method, 3});
        for (size_t i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(result.camera.T[i], fixture.truth.T[i], 1e-5) << method;
        }
        EXPECT_LT(result.training_rmse_px, 1e-3) << method;
    }
}

TEST(Extrinsics, RansacRejectsControlledOutliers)
{
    Fixture fixture;
    for (size_t i = 0; i < fixture.pixels.size(); i += 8)
    {
        fixture.pixels[i].u += 30;
        fixture.pixels[i].v -= 25;
    }
    const auto result = sv::calibrate_extrinsics(fixture.initial, fixture.points, fixture.pixels,
                                                 {"ransac_epnp_lm", 3});
    EXPECT_LT(result.inliers, fixture.points.size());
    EXPECT_GT(result.inliers, 90u);
    for (auto point : fixture.points)
    {
        const auto estimated = sv::project(result.camera, point);
        const auto expected = sv::project(fixture.truth, point);
        EXPECT_NEAR(estimated.u, expected.u, .01);
        EXPECT_NEAR(estimated.v, expected.v, .01);
    }
}

TEST(Extrinsics, RejectsDegeneracyAndInvalidInput)
{
    Fixture fixture;
    EXPECT_ANY_THROW(sv::calibrate_extrinsics(fixture.initial, {}, {}, {}));
    EXPECT_ANY_THROW(sv::calibrate_extrinsics(fixture.initial, fixture.points, fixture.pixels,
                                              {"unsupported", 3}));
    fixture.points[0].x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_ANY_THROW(sv::calibrate_extrinsics(fixture.initial, fixture.points, fixture.pixels, {}));
    for (auto &point : fixture.points)
    {
        point.x = 0;
        point.z = 4;
    }
    EXPECT_ANY_THROW(sv::calibrate_extrinsics(fixture.initial, fixture.points, fixture.pixels, {}));
    for (auto &point : fixture.points)
    {
        point = {0, 0, 4};
    }
    EXPECT_ANY_THROW(sv::calibrate_extrinsics(fixture.initial, fixture.points, fixture.pixels, {}));
}
