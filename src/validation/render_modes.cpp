#include "sv/render_validation.hpp"
#include "sv/renderer.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char *description)
{
    if (!value)
    {
        throw std::runtime_error(description);
    }
}

std::array<int, 4> sample(const sv::Image &image, const sv::View &view, sv::Vec3 point)
{
    const auto matrix = view.mvp(double(image.width) / image.height);
    const double input[4] = {point.x, point.y, point.z, 1};
    double clip[4] = {};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            clip[row] += matrix[4 * row + column] * input[column];
        }
    }
    const int x = int((clip[0] / clip[3] + 1) * image.width / 2);
    const int y = int((1 - clip[1] / clip[3]) * image.height / 2);
    require(x >= 0 && x < image.width && y >= 0 && y < image.height, "test sample outside output");
    const size_t offset = (size_t(y) * image.width + x) * 4;
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2],
            image.pixels[offset + 3]};
}

void expected(const std::array<int, 4> &actual, std::array<int, 4> wanted)
{
    for (int channel = 0; channel < 4; ++channel)
    {
        require(std::abs(actual[channel] - wanted[channel]) <= 1, "GPU color oracle mismatch");
    }
}
} // namespace

namespace sv
{
void qualify_fusion_modes(Config config)
{
    config.width = 320;
    config.height = 240;
    config.surface = Surface{};
    config.surface.H = 0;
    config.vehicle_length = 4.6;
    config.vehicle_width = 1.8;
    config.margin = .1;
    config.view = View{};
    config.view.elevation = sv::pi / 2;
    const std::array<std::array<unsigned char, 3>, 4> colors{
        {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}}};
    sv::FrameSet all;
    for (int id = 0; id < 4; ++id)
    {
        auto &camera = config.cameras[id];
        camera.width = 320;
        camera.height = 180;
        camera.cx = 159.5;
        camera.cy = 89.5;
        camera.theta_max = 1.45;
        camera.z_epsilon = 1e-6;
        camera.T = sv::identity();
        camera.T[5] = camera.T[10] = -1;
        camera.T[11] = 3;
        camera.fx = camera.fy = 30;
        camera.k = {};
        auto image = std::make_shared<sv::Image>();
        image->width = camera.width;
        image->height = camera.height;
        image->channels = 3;
        for (int pixel = 0; pixel < camera.width * camera.height; ++pixel)
        {
            image->pixels.insert(image->pixels.end(), colors[id].begin(), colors[id].end());
        }
        all.frames[id] = sv::Frame{id, 0, 0, 0, image};
    }
    for (const auto *mode :
         {"edge_feather", "angular_feather", "hard_best_angle", "seam_distance_feather",
          "graph_cut_seam", "multi_band", "graph_cut_multi_band"})
    {
        config.fusion.mode = mode;
        config.fusion.diagnostic = "color";
        {
            sv::Renderer renderer(config);
            expected(sample(renderer.render(all, config.view), config.view, {3, 0, 0}),
                     config.fusion.mode == "hard_best_angle"
                         ? std::array<int, 4>{255, 0, 0, 255}
                         : std::array<int, 4>{188, 188, 188, 255});
            if (config.fusion.mode == "hard_best_angle")
            {
                auto missing = all;
                missing.frames[0].reset();
                expected(sample(renderer.render(missing, config.view), config.view, {3, 0, 0}),
                         {0, 255, 0, 255});
            }
        }
        config.fusion.diagnostic = "weights";
        {
            sv::Renderer renderer(config);
            expected(sample(renderer.render(all, config.view), config.view, {3, 0, 0}),
                     config.fusion.mode == "hard_best_angle"
                         ? std::array<int, 4>{255, 0, 0, 255}
                         : std::array<int, 4>{128, 128, 64, 255});
        }
        config.fusion.diagnostic = "coverage";
        {
            sv::Renderer renderer(config);
            auto missing = all;
            missing.frames[0].reset();
            expected(sample(renderer.render(all, config.view), config.view, {3, 0, 0}),
                     {255, 255, 255, 255});
            expected(sample(renderer.render(missing, config.view), config.view, {3, 0, 0}),
                     {191, 191, 191, 255});
            missing.frames[1].reset();
            expected(sample(renderer.render(missing, config.view), config.view, {3, 0, 0}),
                     {128, 128, 128, 255});
            missing.frames[2].reset();
            expected(sample(renderer.render(missing, config.view), config.view, {3, 0, 0}),
                     {64, 64, 64, 255});
            expected(sample(renderer.render({}, config.view), config.view, {3, 0, 0}),
                     {0, 0, 0, 255});
        }
    }
    // Camera 0 points at the sample; the others observe it at 45 degrees.
    // cos^2 scores are 1, .5, .5, .5: RGB linear mixture is (.6,.4,.4).
    const double q = std::sqrt(.5);
    config.cameras[0].T = {q, 0, q, -3 * q, 0, -1, 0, 0, q, 0, -q, 3 * q, 0, 0, 0, 1};
    config.fusion.mode = "angular_feather";
    config.fusion.diagnostic = "color";
    config.fusion.angle_power = 2;
    sv::Renderer renderer(config);
    expected(sample(renderer.render(all, config.view), config.view, {3, 0, 0}),
             {203, 170, 170, 255});
}

void qualify_enclosure_coverage(Config config)
{
    config.width = 320;
    config.height = 240;
    config.surface = Surface{};
    config.view = View{};
    config.surface.H = 0;
    for (const auto *carrier : {"dome_floor_v1", "cylinder_floor_v1", "cube_floor_v1"})
    {
        config.surface.type = carrier;
        config.surface.A = config.surface.B = config.surface.enclosure_radius;
        config.view.distance = 8.5;
        config.fusion.diagnostic = "coverage";
        sv::Renderer renderer(config);
        for (double elevation : {.35, 1., sv::pi / 2})
        {
            for (double azimuth : {0., .8, sv::pi, -sv::pi / 2})
            {
                config.view.elevation = elevation;
                config.view.azimuth = azimuth;
                require(sv::safe_view(config.surface, config.view), "unsafe test orbit");
                const auto image = renderer.render({}, config.view);
                for (size_t pixel = 0; pixel < image.pixels.size(); pixel += 4)
                {
                    const auto *p = image.pixels.data() + pixel;
                    const bool no_input = p[0] == 0 && p[1] == 0 && p[2] == 0;
                    const bool vehicle_mask = p[0] == 255 && p[1] == 0 && p[2] == 255;
                    require((no_input || vehicle_mask) && p[3] == 255,
                            "enclosure has uncovered raster pixels");
                }
            }
        }
    }
}
} // namespace sv
