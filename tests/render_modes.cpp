#include "sv/render_validation.hpp"
#include "sv/renderer.hpp"
#include <iostream>
#include <tuple>

namespace
{
void mesh_budgets(sv::Config config)
{
    sv::Surface surface;
    surface.A = surface.B = 4;
    surface.a = surface.b = 2;
    surface.nx = surface.ny = 4;
    surface.enclosure_radius = 4;
    surface.enclosure_height = 4;
    surface.enclosure_cells = 4;
    surface.floor_radial_cells = 4;
    surface.dome_latitude_cells = 8;
    surface.dome_longitude_cells = 16;
    config.surface = surface;
    sv::Renderer renderer(config);
    const auto original = renderer.mesh_resources();
    if (original.at("active") != original.at("resident"))
    {
        throw std::runtime_error("fresh renderer contains inactive carrier buffers");
    }
    // Hand-counted grids: regular 5x5 plane; disk rings/apex; five box faces.
    for (const auto &[type, vertices, triangles] :
         {std::tuple{"rectangular_bowl_v1", 25, 32}, std::tuple{"dome_floor_v1", 194, 352},
          std::tuple{"cylinder_floor_v1", 210, 352}, std::tuple{"cube_floor_v1", 150, 192}})
    {
        surface.type = type;
        renderer.set_surface(surface);
        const auto resources = renderer.mesh_resources();
        const auto &active = resources.at("active").as_object();
        auto value = [&](const char *name)
        { return boost::json::value_to<size_t>(active.at(name)); };
        if (value("vertices") != size_t(vertices) || value("triangles") != size_t(triangles) ||
            value("indices") != size_t(3 * triangles) ||
            value("vertex_buffer_bytes") != size_t(12 * vertices) ||
            value("index_buffer_bytes") != size_t(12 * triangles) ||
            value("buffer_bytes") != size_t(12 * (vertices + triangles)) ||
            renderer.triangles() != size_t(triangles) ||
            boost::json::value_to<size_t>(resources.at("resident").at("buffer_bytes")) <
                value("buffer_bytes"))
        {
            throw std::runtime_error(std::string("carrier resource oracle mismatch: ") + type);
        }
    }
    surface.type = "rectangular_bowl_v1";
    renderer.set_surface(surface);
    if (renderer.mesh_resources().at("active") != original.at("active") ||
        renderer.mesh_resources().at("resident") == original.at("resident"))
    {
        throw std::runtime_error("active/resident resources lost retained carrier accounting");
    }
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("config required");
        }
        auto config = sv::load_config(argv[1]);
        sv::qualify_fusion_modes(config);
        sv::qualify_enclosure_coverage(config);
        mesh_budgets(config);
        std::cout << "GPU fusion color/coverage oracles and 36 enclosure orbits passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
