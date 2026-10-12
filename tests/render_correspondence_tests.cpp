#include "sv/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace
{
// Independent double-precision, two-sided Moller-Trumbore oracle over actual triangles.
// No analytic carrier equations or GPU inspection data enter intersection selection.
std::optional<sv::Vec3> nearest(const std::vector<sv::Mesh> &meshes, sv::Vec3 origin,
                                sv::Vec3 direction)
{
    double best = std::numeric_limits<double>::infinity();
    for (const auto &mesh : meshes)
    {
        for (size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            const auto a = mesh.vertices[mesh.indices[i]];
            const auto e1 = mesh.vertices[mesh.indices[i + 1]] - a;
            const auto e2 = mesh.vertices[mesh.indices[i + 2]] - a;
            const auto p = sv::cross(direction, e2);
            const auto determinant = sv::dot(e1, p);
            if (std::abs(determinant) < 1e-12)
            {
                continue;
            }
            const auto t = origin - a;
            const auto u = sv::dot(t, p) / determinant;
            const auto q = sv::cross(t, e1);
            const auto v = sv::dot(direction, q) / determinant;
            const auto distance = sv::dot(e2, q) / determinant;
            if (u >= -1e-10 && v >= -1e-10 && u + v <= 1 + 1e-10 && distance > 0)
            {
                best = std::min(best, distance);
            }
        }
    }
    return std::isfinite(best) ? std::optional<sv::Vec3>(origin + direction * best) : std::nullopt;
}

std::vector<sv::Mesh> meshes(const sv::Surface &s)
{
    if (s.type == "rectangular_bowl_v1")
    {
        return {sv::make_mesh(s)};
    }
    if (s.type == "cube_floor_v1")
    {
        auto floor = s;
        floor.nx = floor.ny = s.enclosure_cells;
        return {sv::make_mesh(floor),
                sv::make_box_shell(s.enclosure_radius, s.enclosure_height, s.enclosure_cells)};
    }
    return {
        sv::make_floor_mesh(s.enclosure_radius, s.floor_radial_cells, s.dome_longitude_cells),
        s.type == "dome_floor_v1"
            ? sv::make_dome_mesh(s.enclosure_radius, s.dome_latitude_cells, s.dome_longitude_cells)
            : sv::make_cylinder_shell(s.enclosure_radius, s.enclosure_height, s.enclosure_cells,
                                      s.dome_longitude_cells, s.floor_radial_cells)};
}

TEST(CarrierIntersectionOracle, SharedEdgeAndNearestDepth)
{
    sv::Mesh mesh{{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}, {0, 1, 2, 0, 2, 3}};
    const auto hit = nearest({mesh}, {0, 0, 2}, {0, 0, -1});
    ASSERT_TRUE(hit);
    EXPECT_DOUBLE_EQ(hit->z, 0);
    EXPECT_FALSE(nearest({mesh}, {2, 2, 2}, {0, 0, -1}));
    EXPECT_FALSE(nearest({mesh}, {0, 0, 2}, {1, 0, 0}));
    EXPECT_FALSE(nearest({mesh}, {0, 0, 2}, {0, 0, 1}));
    auto raised = mesh;
    for (auto &v : raised.vertices)
    {
        v.z = .75;
    }
    EXPECT_DOUBLE_EQ(nearest({mesh, raised}, {0, 0, 2}, {0, 0, -1})->z, .75);
    EXPECT_DOUBLE_EQ(nearest({raised, mesh}, {0, 0, 2}, {0, 0, -1})->z, .75);
}

class CarrierInspection : public testing::TestWithParam<const char *>
{
};

TEST_P(CarrierInspection, PerspectivePointsMatchActualTrianglesAndPreserveOutput)
{
    auto config = sv::load_config(SV_TEST_CONFIG);
    config.width = 160;
    config.height = 100;
    config.fusion.mode = "edge_feather";
    config.fusion.diagnostic = "color";
    auto &surface = config.surface;
    const std::string type = GetParam();
    surface.type =
        type == "plane" || type == "raised_plane" || type == "bowl" ? "rectangular_bowl_v1" : type;
    surface.H = type == "bowl" ? 1.5 : 0;
    surface.reference_height = type == "raised_plane" ? .8 : 0;
    surface.A = surface.B = surface.enclosure_radius = surface.enclosure_height = 14;
    surface.nx = surface.ny = surface.enclosure_cells = 12;
    surface.dome_latitude_cells = 8;
    surface.dome_longitude_cells = 16;
    surface.floor_radial_cells = 8;
    config.view.azimuth = .8;
    config.view.elevation = .6;
    config.view.distance = 10;
    config.view.fov = 1.6;
    sv::Renderer renderer(config);
    sv::RenderInspection inspection;
    EXPECT_THROW(renderer.render({}, config.view, nullptr, {true}), std::invalid_argument);
    const auto plain = renderer.render({}, config.view);
    const auto captured = renderer.render({}, config.view, &inspection, {true});
    EXPECT_EQ(plain.pixels, captured.pixels);
    EXPECT_EQ(plain.pixels, renderer.render({}, config.view).pixels);
    ASSERT_EQ(inspection.carrier_positions.type(), CV_32FC3);
    ASSERT_EQ(inspection.carrier_positions.size(), cv::Size(config.width, config.height));
    const auto carrier_meshes = meshes(surface);
    const auto mvp = config.view.mvp(double(config.width) / config.height);
    glm::dmat4 matrix;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            matrix[col][row] = mvp[4 * row + col];
        }
    }
    const auto inverse = glm::inverse(matrix);
    int checked = 0, invalid = 0, faceted = 0, negative = 0;
    double max_world_error = 0, max_pixel_error = 0;
    for (int y = 0; y < config.height; ++y)
    {
        for (int x = 0; x < config.width; ++x)
        {
            const auto region = inspection.carrier_regions.at<uchar>(y, x);
            const auto point = inspection.carrier_positions.at<cv::Vec3f>(y, x);
            if (region == 0 || region == 5)
            {
                EXPECT_TRUE(std::isnan(point[0]) && std::isnan(point[1]) && std::isnan(point[2]));
                ++invalid;
                continue;
            }
            ASSERT_TRUE(std::isfinite(point[0]) && std::isfinite(point[1]) &&
                        std::isfinite(point[2]));
            negative += point[0] < 0 || point[1] < 0;
            const auto clip = matrix * glm::dvec4(point[0], point[1], point[2], 1);
            max_pixel_error = std::max(
                {max_pixel_error, std::abs((clip.x / clip.w + 1) * config.width / 2 - .5 - x),
                 std::abs((1 - clip.y / clip.w) * config.height / 2 - .5 - y)});
            EXPECT_NEAR((clip.x / clip.w + 1) * config.width / 2 - .5, x, .002);
            EXPECT_NEAR((1 - clip.y / clip.w) * config.height / 2 - .5, y, .002);
            if (x % 7 != 3 || y % 7 != 2)
            {
                continue;
            }
            const auto near = inverse * glm::dvec4(2 * (x + .5) / config.width - 1,
                                                   1 - 2 * (y + .5) / config.height, -1, 1);
            const auto far = inverse * glm::dvec4(2 * (x + .5) / config.width - 1,
                                                  1 - 2 * (y + .5) / config.height, 1, 1);
            const sv::Vec3 origin{near.x / near.w, near.y / near.w, near.z / near.w};
            const auto direction =
                sv::normalized(sv::Vec3{far.x / far.w, far.y / far.w, far.z / far.w} - origin);
            const auto hit = nearest(carrier_meshes, origin, direction);
            ASSERT_TRUE(hit);
            max_world_error = std::max({max_world_error, std::abs(point[0] - hit->x),
                                        std::abs(point[1] - hit->y), std::abs(point[2] - hit->z)});
            // Pilot fixture bound, not a universal accuracy guarantee. GPU vertex/raster
            // subpixel precision differs from the ideal double-precision center ray.
            EXPECT_NEAR(point[0], hit->x, .005);
            EXPECT_NEAR(point[1], hit->y, .005);
            EXPECT_NEAR(point[2], hit->z, .005);
            if (type == "plane" || type == "raised_plane")
            {
                // Nonzero constant varyings incur float32 perspective interpolation rounding.
                EXPECT_NEAR(point[2], surface.reference_height, type == "plane" ? 1e-7 : 1e-6);
                EXPECT_NE(region, 4);
            }
            if (type == "dome_floor_v1" && region == 2 &&
                std::sqrt(sv::dot(*hit, *hit)) < surface.enclosure_radius - .01)
            {
                ++faceted;
            }
            ++checked;
        }
    }
    std::cout << type << ": checked=" << checked << " max_axis_error_m=" << max_world_error
              << " max_reprojection_error_px=" << max_pixel_error << " device=" << renderer.device()
              << "\n";
    RecordProperty("carrier", type);
    RecordProperty("checked_rays", checked);
    RecordProperty("device", renderer.device());
    RecordProperty("raster_subpixel_bits",
                   boost::json::value_to<int>(renderer.capabilities().at("raster_subpixel_bits")));
    const auto record = [&](const char *name, double value)
    {
        std::ostringstream text;
        text << std::setprecision(17) << value;
        RecordProperty(name, text.str());
    };
    record("max_axis_error_m", max_world_error);
    record("max_reprojection_error_px", max_pixel_error);
    EXPECT_GT(checked, 100);
    EXPECT_GT(invalid, 0);
    EXPECT_GT(negative, 0);
    if (type == "dome_floor_v1")
    {
        EXPECT_GT(faceted, 10);
    }
    // Neither a region-only capture nor a later surface switch may retain stale positions.
    renderer.render({}, config.view, &inspection);
    EXPECT_TRUE(inspection.carrier_positions.empty());
    surface.H = 2;
    renderer.set_surface(surface);
    renderer.render({}, config.view, &inspection, {true});
    EXPECT_EQ(inspection.carrier_positions.size(), cv::Size(config.width, config.height));
    if (type == "plane")
    {
        EXPECT_GT(cv::countNonZero(inspection.carrier_regions == 4), 0);
        std::vector<cv::Mat> axes;
        cv::split(inspection.carrier_positions, axes);
        EXPECT_GT(cv::countNonZero(axes[2] > .1), 0);
    }
}

INSTANTIATE_TEST_SUITE_P(Carriers, CarrierInspection,
                         testing::Values("plane", "raised_plane", "bowl", "dome_floor_v1",
                                         "cylinder_floor_v1", "cube_floor_v1"));
} // namespace
