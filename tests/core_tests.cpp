#include "sv/config.hpp"
#include "sv/frame.hpp"
#include "sv/protocol.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
int checks = 0;

void check(bool ok, const char *description)
{
    checks++;
    if (!ok)
    {
        throw std::runtime_error(description);
    }
}

void rejects(std::function<void()> action, const char *description)
{
    try
    {
        action();
    }
    catch (const std::exception &)
    {
        checks++;
        return;
    }
    throw std::runtime_error(description);
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
        sv::Camera cam;
        cam.width = 100;
        cam.height = 100;
        cam.fx = 20;
        cam.fy = 20;
        cam.cx = 49.5;
        cam.cy = 49.5;
        auto center = sv::project(cam, {0, 0, 1});
        check(center.valid && center.u == 49.5 && center.v == 49.5, "central ray");
        auto p = sv::project(cam, {1, 0, 1});
        check(p.valid && std::abs(p.u - 49.5 - 20 * sv::pi / 4) < 1e-12, "known 45 degree ray");
        check(!sv::project(cam, {0, 0, -1}).valid, "behind camera");
        check(!sv::project(cam, {100, 0, 1}).valid, "FOV bound");
        check(!sv::project(cam, {NAN, 0, 1}).valid, "NaN projection");
        auto T = config.cameras[0].T;
        auto point = sv::Vec3{2, 3, 4};
        auto restored = sv::transform(sv::inverse_rigid(T), sv::transform(T, point));
        check(std::sqrt(sv::dot(restored - point, restored - point)) < 1e-12, "rigid inverse");
        auto top = sv::look_at({0, 0, 10}, {0, 0, 0});
        auto origin = sv::transform(top, {0, 0, 0});
        check(std::abs(origin.z + 10) < 1e-12, "top look-at fallback");
        auto surface = config.surface;
        check(surface.point(0, 0).z == 0, "flat center");
        check(std::abs(surface.point(surface.A, surface.B).z - surface.H) < 1e-12, "corner height");
        auto mesh = sv::make_mesh(surface);
        for (size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            auto a = mesh.vertices.at(mesh.indices[i]), b = mesh.vertices.at(mesh.indices[i + 1]),
                 c = mesh.vertices.at(mesh.indices[i + 2]);
            check(sv::cross(b - a, c - a).z > 0, "positive nondegenerate triangles");
        }
        const auto floor = sv::make_floor_mesh(12, 8, 24);
        check(floor.indices.size() == (24 * 3 + 7 * 24 * 6), "disk floor triangulation");
        for (size_t i = 0; i < floor.indices.size(); i += 3)
        {
            const auto a = floor.vertices.at(floor.indices[i]);
            const auto b = floor.vertices.at(floor.indices[i + 1]);
            const auto c = floor.vertices.at(floor.indices[i + 2]);
            check(sv::cross(b - a, c - a).z > 0, "disk floor faces upward");
        }
        const auto dome = sv::make_dome_mesh(12, 8, 24);
        check(std::abs(dome.vertices.back().z - 12) < 1e-12, "dome apex");
        for (auto vertex : dome.vertices)
        {
            check(std::abs(std::sqrt(sv::dot(vertex, vertex)) - 12) < 1e-10 && vertex.z >= 0,
                  "dome vertices lie on upper hemisphere");
        }
        for (size_t i = 0; i < dome.indices.size(); i += 3)
        {
            const auto a = dome.vertices.at(dome.indices[i]);
            const auto b = dome.vertices.at(dome.indices[i + 1]);
            const auto c = dome.vertices.at(dome.indices[i + 2]);
            check(sv::dot(sv::cross(b - a, c - a), a + b + c) > 0, "dome triangles face outwards");
        }
        const auto street =
            sv::load_config(std::filesystem::path(argv[1]).parent_path() / "street-demo.json");
        check(street.surface.type == "dome_floor_v1" &&
                  street.view.distance < street.surface.enclosure_radius,
              "street demo virtual camera is inside dome");
        rejects(
            [&]
            {
                auto invalid = street.effective;
                invalid.as_object()["virtual_camera"].as_object()["distance_m"] = 12.0;
                sv::parse_config(invalid);
            },
            "camera on dome shell must be rejected");
        for (const auto &enclosure :
             {sv::make_cylinder_shell(12, 10, 8, 24, 8), sv::make_box_shell(12, 10, 8)})
        {
            for (size_t index = 0; index < enclosure.indices.size(); index += 3)
            {
                const auto a = enclosure.vertices.at(enclosure.indices[index]);
                const auto b = enclosure.vertices.at(enclosure.indices[index + 1]);
                const auto c = enclosure.vertices.at(enclosure.indices[index + 2]);
                check(sv::dot(sv::cross(b - a, c - a), a + b + c) > 0,
                      "enclosure triangles face outwards without degeneracy");
            }
        }
        rejects([] { sv::make_cylinder_shell(12, 0, 8, 24, 8); }, "zero cylinder height");
        rejects([] { sv::make_box_shell(12, 10, 0); }, "empty cube grid");
        for (const auto &surface_spec : {boost::json::object{{"type", "cylinder_floor_v1"},
                                                             {"radius_m", 12.},
                                                             {"height_m", 12.},
                                                             {"vertical_cells", 32},
                                                             {"angular_cells", 128},
                                                             {"floor_radial_cells", 32}},
                                         boost::json::object{{"type", "cube_floor_v1"},
                                                             {"half_extent_m", 12.},
                                                             {"height_m", 12.},
                                                             {"face_cells", 32}}})
        {
            auto value = street.effective;
            value.as_object()["surface"] = surface_spec;
            auto parsed = sv::parse_config(value);
            check(sv::safe_view(parsed.surface, parsed.view),
                  "enclosure config accepts inside view");
            value.as_object()["surface"].as_object()["height_m"] = 3.;
            rejects([&] { sv::parse_config(value); }, "view above enclosure roof rejected");
            auto outside = parsed.view;
            outside.azimuth = 0;
            outside.elevation = .35;
            outside.distance = 18;
            check(!sv::safe_view(parsed.surface, outside), "view outside wall rejected");
        }
        check(config.fusion.mode == "edge_feather" && config.fusion.edge_width_px == 24 &&
                  config.fusion.diagnostic == "color",
              "legacy fusion defaults preserved");
        for (const char *mode : {"edge_feather", "angular_feather", "hard_best_angle"})
        {
            auto value = config.effective;
            value.as_object()["fusion"] = boost::json::object{{"mode", mode}};
            check(sv::parse_config(value).fusion.mode == mode, "supported fusion mode");
            for (const auto &invalid :
                 {boost::json::object{{"mode", "unknown"}},
                  boost::json::object{{"mode", mode}, {"edge_width_px", 0}},
                  boost::json::object{{"mode", mode}, {"angle_power", 33}},
                  boost::json::object{{"mode", mode}, {"diagnostic", "invalid"}},
                  boost::json::object{{"mode", mode}, {"extra", true}}})
            {
                value.as_object()["fusion"] = invalid;
                rejects([&] { sv::parse_config(value); }, "invalid fusion parameters rejected");
            }
        }
        auto bad = config.effective;
        bad.as_object()["unexpected"] = 1;
        rejects([&] { sv::parse_config(bad); }, "unknown config key");
        bad = config.effective;
        bad.as_object()["schema_version"] = 2;
        rejects([&] { sv::parse_config(bad); }, "schema version");
        bad = config.effective;
        bad.as_object()["cameras"].as_array()[1].as_object()["id"] = 0;
        rejects([&] { sv::parse_config(bad); }, "duplicate camera");
        bad = config.effective;
        bad.as_object()["cameras"]
            .as_array()[0]
            .as_object()["T_camera_from_vehicle"]
            .as_array()[0]
            .as_array()[0] = 2.;
        rejects([&] { sv::parse_config(bad); }, "nonrigid transform");
        bad = config.effective;
        bad.as_object()["cameras"].as_array()[0].as_object()["projection"].as_object()["k"] =
            boost::json::array{-1, 0, 0, 0};
        rejects([&] { sv::parse_config(bad); }, "nonmonotonic fisheye");
        bad = config.effective;
        bad.as_object()["surface"].as_object()["outer_half_width_m"] = .5;
        rejects([&] { sv::parse_config(bad); }, "invalid surface");
        sv::Message m{
            20, {{"command_id", "18446744073709551615"}, {"type", "preset"}}, {0, 1, 2, 255}};
        auto wire = sv::encode(m);
        sv::Decoder decoder;
        std::vector<sv::Message> decoded;
        for (auto byte : wire)
        {
            auto part = decoder.feed(&byte, 1);
            decoded.insert(decoded.end(), part.begin(), part.end());
        }
        check(decoded.size() == 1 && decoded[0].payload == m.payload, "one-byte framing");
        check(decoded[0].header.at("command_id") == m.header.at("command_id"),
              "uint64 retained as decimal");
        check(sv::parse_decimal_u64("18446744073709551615") == UINT64_MAX,
              "maximum decimal uint64");
        check(sv::parse_decimal_u64("0") == 0, "zero decimal uint64");
        for (const char *invalid : {"", "-1", "+1", " 1", "1x", "1.5", "18446744073709551616"})
        {
            rejects([&] { sv::parse_decimal_u64(invalid); }, "invalid decimal uint64 accepted");
        }
        auto merged = wire;
        merged.insert(merged.end(), wire.begin(), wire.end());
        check(decoder.feed(merged.data(), merged.size()).size() == 2, "coalesced messages");
        wire[8] = 255;
        rejects(
            [&]
            {
                sv::Decoder d;
                d.feed(wire.data(), wire.size());
            },
            "oversized header");
        sv::Synchronizer sync(config);
        auto image = std::make_shared<sv::Image>();
        uint64_t start = 1000000000;
        for (int i = 0; i < 4; i++)
        {
            sync.push({i, 0, start, 0, image});
        }
        check(sync.select(start).health == "READY", "complete set");
        check(sync.select(start + config.age_ns + 1).health == "NO_INPUT", "expired inputs");
        check(!sync.push({0, 0, start, 0, image}) && sync.duplicate == 1, "duplicate detection");
        check(!sync.push({1, 1, start - 1, 0, image}) && sync.out_of_order == 1,
              "timestamp ordering");
        for (int n = 1; n < 10; n++)
        {
            sync.push({0, uint64_t(n), start + uint64_t(n), 0, image});
        }
        check(sync.size(0) == size_t(config.queue_size) && sync.dropped > 0, "bounded input queue");
        sv::Synchronizer skew(config);
        for (int i = 0; i < 4; i++)
        {
            skew.push({i, 0, start + uint64_t(i) * config.skew_ns, 0, image});
        }
        auto set = skew.select(start + 4 * config.skew_ns);
        check(set.health == "DEGRADED" && set.skew_ns <= config.skew_ns, "skew exclusion");
        check(std::abs(sv::encode_srgb(.5) - .7353569830524495) < 1e-12, "linear mixing");
        std::cout << checks << " checks passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
